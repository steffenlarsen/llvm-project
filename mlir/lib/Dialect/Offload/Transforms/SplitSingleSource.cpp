//===- SplitSingleSource.cpp - Split offload module into host+device ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass splits a unified host+device offload module into:
//   - Host side: offload.func (host/host_device) → func.func
//                offload.kernel_launch → gpu.launch_func
//   - Device side: offload.func (global/device/host_device) → gpu.func
//                  wrapped inside a gpu.module
//
// After this pass, all offload.* ops have been eliminated.  The resulting
// gpu.module can be compiled by any existing GPU backend (NVVM/ROCDL).
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/Offload/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/GPU/IR/GPUDialect.h"
#include "mlir/Dialect/Offload/IR/OffloadDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/RegionUtils.h"
#include "llvm/ADT/SmallVector.h"

namespace mlir {
namespace offload {

#define GEN_PASS_DEF_OFFLOADSPLITSINGLESOURCEPASS
#include "mlir/Dialect/Offload/Transforms/Passes.h.inc"

} // namespace offload
} // namespace mlir

using namespace mlir;
using namespace mlir::offload;

namespace {

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

/// Return true if this exec_space implies device-side code.
static bool isDeviceExecSpace(ExecSpace es) {
  return es == ExecSpace::device || es == ExecSpace::global ||
         es == ExecSpace::host_device;
}

/// Return true if this exec_space implies host-side code.
static bool isHostExecSpace(ExecSpace es) {
  return es == ExecSpace::host || es == ExecSpace::host_device;
}

/// Lower a single offload.func to a gpu.func and insert it into gpuModule.
/// Returns the created gpu.func, or nullptr on failure.
static gpu::GPUFuncOp lowerToGpuFunc(OpBuilder &builder,
                                     offload::FuncOp offloadFunc,
                                     gpu::GPUModuleOp gpuModule) {
  MLIRContext *ctx = offloadFunc.getContext();
  Location loc = offloadFunc.getLoc();
  FunctionType fnType = offloadFunc.getFunctionType();

  // Create the gpu.func with the same name and signature.
  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToEnd(gpuModule.getBody());

  auto gpuFunc = builder.create<gpu::GPUFuncOp>(
      loc, offloadFunc.getName(), fnType,
      /*workgroupAttributions=*/TypeRange{},
      /*privateAttributions=*/TypeRange{});

  // Mark kernels (__global__ functions) with the gpu.kernel attribute.
  if (offloadFunc.getExecSpace() == ExecSpace::global)
    gpuFunc->setAttr(gpu::GPUDialect::getKernelFuncAttrName(),
                     builder.getUnitAttr());

  // Propagate launch_bounds as known_block_size if present.
  if (auto lb = offloadFunc.getLaunchBoundsAttr()) {
    auto val = static_cast<int32_t>(lb.getMaxThreadsPerBlock());
    gpuFunc->setAttr("known_block_size",
                     builder.getDenseI32ArrayAttr({val, 1, 1}));
  }

  // Clone the entire function body (all blocks) into the gpu.func.
  IRMapping mapping;
  Block &offloadEntry = offloadFunc.getBody().front();
  Block &gpuEntry = gpuFunc.getBody().front();

  // Map offload.func entry block args to gpu.func entry block args.
  for (auto [offloadArg, gpuArg] :
       llvm::zip(offloadEntry.getArguments(), gpuEntry.getArguments()))
    mapping.map(offloadArg, gpuArg);

  // Clone all blocks (including successors).
  offloadFunc.getBody().cloneInto(&gpuFunc.getBody(), mapping);

  // Replace all offload.return ops with gpu.return.
  gpuFunc.walk([](offload::ReturnOp op) {
    OpBuilder b(op);
    auto gpuRet = b.create<gpu::ReturnOp>(op.getLoc(), op.getOperands());
    (void)gpuRet;
    op.erase();
  });

  // cloneInto duplicated the entry block — splice its ops into the pre-existing
  // gpu.func entry block and erase the duplicate.
  Block *clonedEntry = mapping.lookup(&offloadEntry);
  gpuEntry.getOperations().splice(gpuEntry.getOperations().end(),
                                   clonedEntry->getOperations());
  clonedEntry->erase();

  // Lower offload.shared_mem_alloc → gpu.dynamic_shared_memory.
  //
  // offload.shared_mem_alloc %size -> !T models `extern __shared__ T arr[]`.
  // gpu.dynamic_shared_memory returns a memref<?xi8, workgroup> that covers
  // the entire dynamic shared memory window.  We bridge the type difference
  // with an unrealized_conversion_cast so the subsequent CIR-to-LLVM pass
  // can lower the pointer correctly.
  gpuFunc.walk([&](offload::SharedMemAllocOp op) {
    OpBuilder b(op);
    Location loc = op.getLoc();
    auto workgroupAS =
        gpu::AddressSpaceAttr::get(ctx, gpu::AddressSpace::Workgroup);
    auto rawMemrefTy =
        MemRefType::get({ShapedType::kDynamic}, b.getI8Type(),
                        MemRefLayoutAttrInterface{}, workgroupAS);
    Value rawMem =
        gpu::DynamicSharedMemoryOp::create(b, loc, rawMemrefTy);
    Value cast =
        UnrealizedConversionCastOp::create(b, loc, op.getResult().getType(),
                                           rawMem)
            .getResult(0);
    op.getResult().replaceAllUsesWith(cast);
    op.erase();
  });

  return gpuFunc;
}

/// Lower a single offload.func to a func.func (host side) in the parent module.
static func::FuncOp lowerToFuncFunc(OpBuilder &builder,
                                    offload::FuncOp offloadFunc,
                                    ModuleOp parentModule) {
  Location loc = offloadFunc.getLoc();
  FunctionType fnType = offloadFunc.getFunctionType();

  OpBuilder::InsertionGuard guard(builder);
  // Insert immediately after the offload.func so symbol order is preserved.
  builder.setInsertionPointAfter(offloadFunc);

  auto funcFunc = builder.create<func::FuncOp>(
      loc, offloadFunc.getName(), fnType);

  // Clone the entire function body (all blocks).
  IRMapping mapping;
  Block *funcEntry = funcFunc.addEntryBlock();
  Block &offloadEntry = offloadFunc.getBody().front();

  // Map offload.func entry block args to func.func entry block args.
  for (auto [offloadArg, funcArg] :
       llvm::zip(offloadEntry.getArguments(), funcEntry->getArguments()))
    mapping.map(offloadArg, funcArg);

  // Clone all blocks (including successors).
  offloadFunc.getBody().cloneInto(&funcFunc.getBody(), mapping);

  // Replace all offload.return ops with func.return.
  funcFunc.walk([](offload::ReturnOp op) {
    OpBuilder b(op);
    b.create<func::ReturnOp>(op.getLoc(), op.getOperands());
    op.erase();
  });

  // Splice cloned entry block ops into the pre-existing entry block.
  Block *clonedEntry = mapping.lookup(&offloadEntry);
  funcEntry->getOperations().splice(funcEntry->getOperations().end(),
                                    clonedEntry->getOperations());
  clonedEntry->erase();

  return funcFunc;
}

/// Lower offload.kernel_launch → gpu.launch_func.
///
/// The generated gpu.launch_func references @gpuModuleName::@kernelName.
static void lowerKernelLaunch(OpBuilder &builder,
                               offload::KernelLaunchOp launch,
                               StringRef gpuModuleName) {
  Location loc = launch.getLoc();
  builder.setInsertionPoint(launch);

  // Build SymbolRefAttr: @gpuModuleName::@kernelName
  auto moduleRef =
      FlatSymbolRefAttr::get(builder.getContext(), gpuModuleName);
  auto kernelRef = SymbolRefAttr::get(
      builder.getContext(), gpuModuleName,
      {FlatSymbolRefAttr::get(builder.getContext(), launch.getCallee())});

  // Collect grid and block dimensions as KernelDim3.
  gpu::KernelDim3 gridSize{launch.getGridX(), launch.getGridY(),
                            launch.getGridZ()};
  gpu::KernelDim3 blockSize{launch.getBlockX(), launch.getBlockY(),
                             launch.getBlockZ()};

  // Forward dynamic shared memory from the launch op (null = no shmem).
  Value dynamicSharedMem = launch.getSharedMem();

  // Collect the stream-producing op before erasing the launch; an
  // unrealized_conversion_cast to !offload.stream is illegal in
  // ConvertCIRToLLVMPass, so erase it if it becomes dead after the launch is
  // gone.  (Streams on kernel launches are not yet forwarded to
  // gpu.launch_func — the default-stream path handles synchronization.)
  Value streamVal = launch.getStream();
  Operation *streamDefOp = streamVal ? streamVal.getDefiningOp() : nullptr;

  builder.create<gpu::LaunchFuncOp>(
      loc, kernelRef, gridSize, blockSize,
      /*dynamicSharedMemorySize=*/dynamicSharedMem,
      /*kernelOperands=*/launch.getArgs(),
      /*asyncTokenType=*/Type{},
      /*asyncDependencies=*/ValueRange{},
      /*clusterSize=*/std::nullopt);

  launch.erase();

  if (streamDefOp && streamDefOp->use_empty())
    streamDefOp->erase();
}

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct SplitSingleSourcePass
    : offload::impl::OffloadSplitSingleSourcePassBase<SplitSingleSourcePass> {

  using OffloadSplitSingleSourcePassBase::OffloadSplitSingleSourcePassBase;

  void runOnOperation() override {
    ModuleOp module = getOperation();
    MLIRContext *ctx = module.getContext();
    OpBuilder builder(ctx);

    // Collect all offload.func ops up front to avoid iterator invalidation.
    SmallVector<offload::FuncOp> offloadFuncs;
    module.walk([&](offload::FuncOp fn) { offloadFuncs.push_back(fn); });

    if (offloadFuncs.empty())
      return; // Nothing to do.

    // Mark the module as a GPU container early so that gpu.launch_func ops
    // created in step 3 pass verification (which checks this attr).
    module->setAttr(gpu::GPUDialect::getContainerModuleAttrName(),
                    builder.getUnitAttr());

    // ------------------------------------------------------------------ //
    // Step 0: Handle offload.global_var ops.
    //
    // offload.global_var represents a device-qualified variable (__device__,
    // __constant__, __shared__, __managed__).
    //
    // __shared__ (mem_space = shared): Leave in place — the subsequent
    // LowerOffloadSharedGlobalsPass emits an llvm.mlir.global with addr_space=3
    // into the gpu.module and then erases the offload.global_var.
    //
    // All other mem_spaces: full device lowering is deferred; erase now so that
    // the CIR-to-LLVM pass does not see unknown ops.  The host-side shadow
    // cir.global (emitted by CIRGenModule::emitGlobalVarDefinition before
    // the offload.global_var conversion) is intentionally preserved so that
    // host code that takes the address of a device symbol still compiles.
    // ------------------------------------------------------------------ //
    SmallVector<offload::GlobalVarOp> globalVars;
    module.walk([&](offload::GlobalVarOp gv) { globalVars.push_back(gv); });
    for (auto gv : globalVars) {
      if (gv.getMemSpace() == MemSpace::shared ||
          gv.getMemSpace() == MemSpace::device ||
          gv.getMemSpace() == MemSpace::constant ||
          gv.getMemSpace() == MemSpace::managed)
        continue; // handled by LowerSharedGlobalsPass
      gv.erase();
    }

    // ------------------------------------------------------------------ //
    // Step 1: Create the gpu.module to hold device functions.
    // ------------------------------------------------------------------ //
    builder.setInsertionPointToEnd(module.getBody());
    auto gpuModule = builder.create<gpu::GPUModuleOp>(
        module.getLoc(), gpuModuleName);

    // ------------------------------------------------------------------ //
    // Step 2: Lower each offload.func according to its exec_space.
    // ------------------------------------------------------------------ //
    for (offload::FuncOp fn : offloadFuncs) {
      ExecSpace es = fn.getExecSpace();

      // Device side.
      if (isDeviceExecSpace(es))
        lowerToGpuFunc(builder, fn, gpuModule);

      // Host side.
      if (isHostExecSpace(es))
        lowerToFuncFunc(builder, fn, module);

      // Remove the original offload.func.
      fn.erase();
    }

    // ------------------------------------------------------------------ //
    // Step 3: Lower offload.kernel_launch → gpu.launch_func.
    // ------------------------------------------------------------------ //
    SmallVector<offload::KernelLaunchOp> launches;
    module.walk(
        [&](offload::KernelLaunchOp l) { launches.push_back(l); });
    for (auto l : launches) {
      if (l.getStream())
        continue; // stream-aware launches handled by LowerHostRuntimePass
      lowerKernelLaunch(builder, l, gpuModuleName);
    }

    // (gpu.container_module was already set before step 3)
  }
};

} // namespace

namespace mlir {
namespace offload {

void registerOffloadPasses() {
  registerPass([]() -> std::unique_ptr<Pass> {
    return std::make_unique<SplitSingleSourcePass>();
  });
  registerPass([]() -> std::unique_ptr<Pass> {
    return createOffloadLowerHostRuntimePass();
  });
  registerPass([]() -> std::unique_ptr<Pass> {
    return createOffloadLowerSharedGlobalsPass();
  });
}

} // namespace offload
} // namespace mlir
