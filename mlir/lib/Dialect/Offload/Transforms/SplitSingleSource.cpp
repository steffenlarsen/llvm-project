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
//                  wrapped inside one or two gpu.modules
//
// Kernels with at least one observable launch site go into the primary
// gpu.module.  Kernels with no launch site go into a secondary ("deferred")
// gpu.module, which the HIP runtime only loads when one of its kernels is
// actually needed, avoiding unnecessary compile/load cost for unused kernels.
//
// Device helper functions (exec_space=device) and the device-side clone of
// host_device functions are replicated into every gpu.module that needs them,
// unless they transitively reference a non-replicable global (__device__,
// __constant__, __managed__).  Such helpers are placed only in the primary
// module; any unreferenced kernel calling them is also kept in the primary.
//
// The secondary module is omitted entirely when all kernels are referenced.
//
// After this pass, all offload.* ops have been eliminated.  The resulting
// gpu.module(s) can be compiled by any existing GPU backend (NVVM/ROCDL).
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/Offload/Transforms/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/GPU/IR/GPUDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/Offload/IR/OffloadDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/RegionUtils.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
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
// Lowering helpers
//===----------------------------------------------------------------------===//

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

  // Propagate launch_bounds as rocdl.flat_work_group_size if present.
  //
  // __launch_bounds__(N) means "at most N threads per block" — it constrains
  // the *flat* (total) thread count without implying a 1-D block shape.
  // The gpu.known_block_size attribute WOULD encode the exact block shape
  // (e.g. [256, 1, 1]), which causes the AMDGPU back end to fold
  // workitem.id.y to 0 when the Y dimension is 1.  For a launch configuration
  // like dim3(16, 16, 1) with launch_bounds(256) that would be wrong.
  //
  // Instead, set rocdl.flat_work_group_size = "1,N" directly on the gpu.func.
  // GPUFuncOpLowering forwards unknown discardable attributes to the llvm.func,
  // and the ROCDL LLVM-IR translator converts rocdl.flat_work_group_size to the
  // "amdgpu-flat-work-group-size" function attribute — exactly what clang emits
  // for __launch_bounds__(N) in standard HIP compilation.
  if (auto lb = offloadFunc.getLaunchBoundsAttr()) {
    auto maxN = lb.getMaxThreadsPerBlock();
    // "1,N" matches the range clang emits for __launch_bounds__(N):
    //   amdgpu-flat-work-group-size="1,N"
    std::string flatWGSVal = "1," + std::to_string(maxN);
    gpuFunc->setAttr("rocdl.flat_work_group_size",
                     builder.getStringAttr(flatWGSVal));
    // Two-arg __launch_bounds__(maxThreads, minBlocks) additionally emits
    // amdgpu-waves-per-eu=minBlocks which instructs the VGPR allocator to
    // target at least minBlocks waves/SIMD, unlocking a larger VGPR budget
    // and eliminating spilling on high-register-pressure kernels.
    if (auto minBlocks = lb.getMinBlocksPerSM()) {
      auto i32Ty = builder.getIntegerType(32);
      gpuFunc->setAttr("rocdl.waves_per_eu",
                       builder.getIntegerAttr(i32Ty, *minBlocks));
    }
  } else {
    // No launch_bounds specified (neither by the user nor by
    // TightenLaunchBoundsPass).  Mirror the HIP frontend's conservative
    // default: clang's AMDGPUTargetCodeGenInfo::setFunctionDeclAttributes
    // unconditionally emits amdgpu-flat-work-group-size="1,1024" for every
    // HIP __global__ kernel that lacks an explicit bound, using the value of
    // --gpu-max-threads-per-block (default 1024).  Without this, the CIR path
    // leaves the attribute absent, and the AMDGPU backend is free to allocate
    // registers without occupancy pressure — producing different (better for
    // that kernel, but inconsistent) codegen compared to standard HIP.
    if (offloadFunc.getExecSpace() == ExecSpace::global)
      gpuFunc->setAttr("rocdl.flat_work_group_size",
                       builder.getStringAttr("1,1024"));
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
  // offload.kernel_launch carries shared_mem as `index`; gpu.launch_func
  // requires `i32`, so insert an index_cast when the value is present.
  Value dynamicSharedMem = launch.getSharedMem();
  if (dynamicSharedMem)
    dynamicSharedMem = builder.create<arith::IndexCastOp>(
        loc, builder.getIntegerType(32), dynamicSharedMem);

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
// Dependency analysis
//===----------------------------------------------------------------------===//

/// Return true if this global mem_space cannot be safely replicated across
/// gpu.modules.  Replicating __device__, __constant__, or __managed__ globals
/// would create distinct device-side allocations with the same symbolic name,
/// breaking host-side symbol lookups (hipMemcpyToSymbol, etc.).
static bool isNonReplicableMemSpace(MemSpace ms) {
  return ms == MemSpace::device || ms == MemSpace::constant ||
         ms == MemSpace::managed;
}

/// Build direct-dependency and reverse-dependency maps across all offload.func
/// bodies in \p module.
///
///  deps[fn]     = symbols (offload.func / offload.global_var) that fn's body
///                 directly references.
///  revDeps[sym] = offload.func names that directly reference sym.
static void
buildDepMap(ModuleOp module,
            DenseMap<StringAttr, SmallVector<StringAttr>> &deps,
            DenseMap<StringAttr, SmallVector<StringAttr>> &revDeps) {
  module.walk([&](offload::FuncOp fn) {
    auto uses = SymbolTable::getSymbolUses(fn.getOperation(), module);
    if (!uses)
      return;
    SmallVector<StringAttr> &fnDeps = deps[fn.getNameAttr()];
    for (SymbolTable::SymbolUse use : *uses) {
      StringAttr ref = use.getSymbolRef().getRootReference();
      if (module.lookupSymbol<offload::FuncOp>(ref) ||
          module.lookupSymbol<offload::GlobalVarOp>(ref)) {
        fnDeps.push_back(ref);
        revDeps[ref].push_back(fn.getNameAttr());
      }
    }
  });
}

/// Starting from the seed set \p seeds, walk forward through \p deps and
/// collect every symbol transitively reachable.  Seeds are included in
/// \p reachable.
static void
computeReachableFrom(const DenseMap<StringAttr, SmallVector<StringAttr>> &deps,
                     const DenseSet<StringAttr> &seeds,
                     DenseSet<StringAttr> &reachable) {
  SmallVector<StringAttr> worklist(seeds.begin(), seeds.end());
  reachable.insert(seeds.begin(), seeds.end());
  while (!worklist.empty()) {
    StringAttr sym = worklist.pop_back_val();
    auto it = deps.find(sym);
    if (it == deps.end())
      continue;
    for (StringAttr dep : it->second) {
      if (reachable.insert(dep).second)
        worklist.push_back(dep);
    }
  }
}

/// Compute two sets:
///
///  \p primaryKernels  — kernels (exec_space=global) that must reside in the
///                       primary gpu.module, either because they have an
///                       observable launch site, or because they transitively
///                       depend on a resource that cannot be replicated.
///
///  \p primaryOnlyHelpers — device helpers / host_device functions whose
///                          device-side clone must reside only in the primary
///                          module (they transitively reference a non-replicable
///                          global).  Replicable helpers are NOT in this set
///                          and will be cloned into both modules.
static void computePrimarySet(ModuleOp module,
                               DenseSet<StringAttr> &primaryKernels,
                               DenseSet<StringAttr> &primaryOnlyHelpers) {
  // ------------------------------------------------------------------ //
  // Step A: Build direct-dependency and reverse-dependency maps.
  // ------------------------------------------------------------------ //
  DenseMap<StringAttr, SmallVector<StringAttr>> deps;
  DenseMap<StringAttr, SmallVector<StringAttr>> revDeps;
  buildDepMap(module, deps, revDeps);

  // ------------------------------------------------------------------ //
  // Step B: Mark non-replicable globals.
  // ------------------------------------------------------------------ //
  DenseSet<StringAttr> nonReplicableGlobals;
  module.walk([&](offload::GlobalVarOp gv) {
    if (isNonReplicableMemSpace(gv.getMemSpace()))
      nonReplicableGlobals.insert(gv.getSymNameAttr());
  });

  // ------------------------------------------------------------------ //
  // Step C: Classify device helpers and host_device functions.
  //
  // A helper is "primary-only" if it (transitively) references a
  // non-replicable global.  Start with helpers that directly reference one,
  // then propagate backwards through the call graph.
  // ------------------------------------------------------------------ //
  SmallVector<StringAttr> worklist;
  module.walk([&](offload::FuncOp fn) {
    ExecSpace es = fn.getExecSpace();
    if (es != ExecSpace::device && es != ExecSpace::host_device)
      return;
    for (StringAttr dep : deps[fn.getNameAttr()])
      if (nonReplicableGlobals.count(dep)) {
        worklist.push_back(fn.getNameAttr());
        break;
      }
  });

  while (!worklist.empty()) {
    StringAttr item = worklist.pop_back_val();
    if (!primaryOnlyHelpers.insert(item).second)
      continue; // already processed
    // Propagate to other helpers that call this one.
    for (StringAttr caller : revDeps[item]) {
      offload::FuncOp callerFn =
          module.lookupSymbol<offload::FuncOp>(caller);
      if (!callerFn)
        continue;
      ExecSpace es = callerFn.getExecSpace();
      if (es == ExecSpace::device || es == ExecSpace::host_device)
        worklist.push_back(caller);
    }
  }

  // ------------------------------------------------------------------ //
  // Step D: Seed primary kernels with all launched kernels.
  // ------------------------------------------------------------------ //
  module.walk([&](offload::KernelLaunchOp l) {
    primaryKernels.insert(l.getCalleeAttr().getAttr());
  });

  // ------------------------------------------------------------------ //
  // Step E: Pull unreferenced kernels into primary if they depend on a
  // resource that cannot be replicated.
  //
  // A kernel is pulled into primary if it directly references a non-replicable
  // global or calls a primary-only helper.  Repeat until convergence.
  // ------------------------------------------------------------------ //
  bool changed = true;
  while (changed) {
    changed = false;
    module.walk([&](offload::FuncOp fn) {
      if (fn.getExecSpace() != ExecSpace::global)
        return;
      if (primaryKernels.count(fn.getNameAttr()))
        return;
      for (StringAttr dep : deps[fn.getNameAttr()]) {
        if (nonReplicableGlobals.count(dep) ||
            primaryOnlyHelpers.count(dep)) {
          primaryKernels.insert(fn.getNameAttr());
          changed = true;
          return;
        }
      }
    });
  }
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
    // Determine dead-kernel handling mode.
    // ------------------------------------------------------------------ //
    bool doNone    = (deadKernelAction == "none");
    bool doDiscard = (deadKernelAction == "discard");
    bool doSplit   = !doNone && !doDiscard; // "split" is the default

    // ------------------------------------------------------------------ //
    // Dependency analysis: compute which kernels and helpers go where.
    // ------------------------------------------------------------------ //
    DenseSet<StringAttr> primaryKernels;
    DenseSet<StringAttr> primaryOnlyHelpers; // Split mode only
    DenseSet<StringAttr> reachable;          // Discard mode only

    if (doNone) {
      // All kernels → primary. No analysis needed.
      module.walk([&](offload::FuncOp fn) {
        if (fn.getExecSpace() == ExecSpace::global)
          primaryKernels.insert(fn.getNameAttr());
      });
    } else if (doDiscard) {
      // Seed from launch ops, then forward-reachability to find live helpers.
      DenseMap<StringAttr, SmallVector<StringAttr>> deps, revDeps;
      buildDepMap(module, deps, revDeps);
      module.walk([&](offload::KernelLaunchOp l) {
        primaryKernels.insert(l.getCalleeAttr().getAttr());
      });
      computeReachableFrom(deps, primaryKernels, reachable);
    } else {
      // Split mode (default): full analysis placing dead kernels in deferred.
      computePrimarySet(module, primaryKernels, primaryOnlyHelpers);
    }

    // ------------------------------------------------------------------ //
    // Step 0: Collect offload.global_var ops.
    //
    // offload.global_var represents a device-qualified variable (__device__,
    // __constant__, __shared__, __managed__).
    //
    // __shared__ (mem_space = shared): Left in the outer module for now —
    // LowerSharedGlobalsPass places it into the appropriate gpu.module(s) and
    // erases the offload.global_var.
    //
    // device/constant/managed: moved into the primary gpu.module in Step 1b.
    //
    // All other mem_spaces: erase; the host-side shadow cir.global (emitted
    // by CIRGenModule::emitGlobalVarDefinition) is intentionally preserved.
    // ------------------------------------------------------------------ //
    SmallVector<offload::GlobalVarOp> globalVars;
    module.walk([&](offload::GlobalVarOp gv) { globalVars.push_back(gv); });
    for (auto gv : globalVars) {
      if (gv.getMemSpace() == MemSpace::shared ||
          gv.getMemSpace() == MemSpace::device ||
          gv.getMemSpace() == MemSpace::constant ||
          gv.getMemSpace() == MemSpace::managed)
        continue; // handled below (Step 1b) or by LowerSharedGlobalsPass
      gv.erase();
    }

    // ------------------------------------------------------------------ //
    // Step 1: Create the gpu.module(s) to hold device functions.
    //
    // The primary module always exists.  The deferred module is created only
    // in Split mode and erased at the end if it ends up with no kernels.
    // ------------------------------------------------------------------ //
    builder.setInsertionPointToEnd(module.getBody());
    auto gpuModulePrimary = gpu::GPUModuleOp::create(
        builder, module.getLoc(), gpuModuleName);
    gpu::GPUModuleOp gpuModuleDeferred;
    if (doSplit)
      gpuModuleDeferred = gpu::GPUModuleOp::create(
          builder, module.getLoc(), deferredGpuModuleName);

    // ------------------------------------------------------------------ //
    // Step 1b: Move device offload.global_var ops into the gpu.module(s).
    //
    // gpu.module is IsolatedFromAbove (a nested SymbolTable).  Once device
    // function bodies are cloned into gpu.func ops inside the gpu.module,
    // any cir.get_global / llvm.mlir.addressof inside those bodies uses
    // lookupNearestSymbolFrom which stops at the gpu.module boundary.  The
    // offload.global_var must therefore be visible inside the gpu.module
    // before the clone happens.
    //
    // We move the offload.global_var as-is (keeping its CIR type) rather
    // than converting it here, because this pass has no access to CIR-dialect
    // type converters.  ConvertCIRInGpuModulePass (in LowerToLLVM.cpp)
    // runs after this pass and converts the offload.global_var ops inside
    // each gpu.module to llvm.mlir.global using prepareTypeConverter.
    //
    // Placement rules:
    //   - Non-replicable globals (device/constant/managed): primary only.
    //     These globals represent single device-wide allocations; two copies
    //     would create distinct allocations and break host-side symbol lookup.
    //   - Shared globals (__shared__): replicated into every gpu.module
    //     (primary always; deferred if it exists).  Each gpu.module's kernels
    //     get their own per-block LDS allocation — replication is safe and
    //     necessary for correct symbol resolution inside each module.
    // ------------------------------------------------------------------ //
    {
      for (auto gv : globalVars) {
        auto ms = gv.getMemSpace();
        if (ms == MemSpace::device || ms == MemSpace::constant ||
            ms == MemSpace::managed) {
          // Non-replicable: move into primary only.
          gv->moveBefore(gpuModulePrimary.getBody(),
                         gpuModulePrimary.getBody()->begin());
        } else if (ms == MemSpace::shared) {
          // Replicable: clone into deferred (if it exists), move into primary.
          if (doSplit && gpuModuleDeferred) {
            OpBuilder::InsertionGuard guard(builder);
            builder.setInsertionPointToStart(gpuModuleDeferred.getBody());
            builder.clone(*gv.getOperation());
          }
          gv->moveBefore(gpuModulePrimary.getBody(),
                         gpuModulePrimary.getBody()->begin());
        }
      }
    }

    // ------------------------------------------------------------------ //
    // Step 2: Lower each offload.func according to its exec_space and mode.
    // ------------------------------------------------------------------ //
    for (offload::FuncOp fn : offloadFuncs) {
      ExecSpace es = fn.getExecSpace();

      if (es == ExecSpace::global) {
        if (primaryKernels.count(fn.getNameAttr())) {
          // Launched kernel (or "none" mode where all are primary).
          lowerToGpuFunc(builder, fn, gpuModulePrimary);
        } else if (doSplit) {
          // Dead kernel in Split mode → deferred module.
          lowerToGpuFunc(builder, fn, gpuModuleDeferred);
        }
        // Discard mode: dead kernel is simply dropped (no lowering).

      } else if (es == ExecSpace::device) {
        if (doDiscard && !reachable.count(fn.getNameAttr())) {
          // Exclusive helper (not reachable from any primary kernel): drop it.
          fn.erase();
          continue;
        }
        // Helper always goes to primary.
        lowerToGpuFunc(builder, fn, gpuModulePrimary);
        // In Split mode, replicate replicable helpers to deferred.
        if (doSplit && !primaryOnlyHelpers.count(fn.getNameAttr()))
          lowerToGpuFunc(builder, fn, gpuModuleDeferred);

      } else if (es == ExecSpace::host_device) {
        // Device-side clone: drop in Discard mode if not reachable.
        bool deviceReachable =
            !doDiscard || reachable.count(fn.getNameAttr());
        if (deviceReachable) {
          lowerToGpuFunc(builder, fn, gpuModulePrimary);
          if (doSplit && !primaryOnlyHelpers.count(fn.getNameAttr()))
            lowerToGpuFunc(builder, fn, gpuModuleDeferred);
        }
        // Host-side clone is always kept.
        lowerToFuncFunc(builder, fn, module);

      } else {
        // Host-only function.
        lowerToFuncFunc(builder, fn, module);
      }

      // Remove the original offload.func.
      fn.erase();
    }

    // Erase the deferred module if it contains no kernels.  Replicable helpers
    // may have been cloned there even when all kernels were referenced; in that
    // case the helpers serve no purpose in an otherwise-empty binary.
    if (doSplit) {
      bool deferredHasKernel = false;
      gpuModuleDeferred.walk([&](gpu::GPUFuncOp f) {
        if (f->hasAttr(gpu::GPUDialect::getKernelFuncAttrName()))
          deferredHasKernel = true;
      });
      if (!deferredHasKernel)
        gpuModuleDeferred.erase();
    }

    // ------------------------------------------------------------------ //
    // Step 3: Lower offload.kernel_launch → gpu.launch_func.
    //
    // All launch sites reference kernels in the primary module.
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
