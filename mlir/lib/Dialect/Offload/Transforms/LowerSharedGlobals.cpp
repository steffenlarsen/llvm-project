//===- LowerSharedGlobals.cpp - Lower offload device globals to gpu.module -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass lowers `offload.global_var` ops with device-side mem_spaces to
// `llvm.mlir.global` inside the `gpu.module`(s) produced by
// `SplitSingleSourcePass`.
//
//   mem_space = shared   → addr_space = 3  (AMDGPU LDS / NVPTX .shared)
//   mem_space = device   → addr_space = 1  (AMDGPU / NVPTX global memory)
//   mem_space = constant → addr_space = 4  (AMDGPU / NVPTX constant memory)
//
// All three address space values are the same for both AMDGPU and NVPTX.
// The globals are always zero-initialized in the device binary; the host sets
// device/constant values via hipMemcpyToSymbol / cudaMemcpyToSymbol.
//
// Placement rules:
//   - device / constant / managed globals: primary module only.  These globals
//     cannot be safely replicated (two copies = two distinct allocations).
//   - shared globals: placed in every gpu.module that contains a gpu.func
//     referencing the global's symbol.  This supports the two-module split
//     where __shared__ static arrays may appear in both the primary and the
//     deferred module.
//
// Run this pass *after* SplitSingleSourcePass (so the gpu.module(s) exist) and
// *before* ConvertCIRInGpuModulePass (which requires LLVM dialect in gpu.func
// bodies).
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/Offload/Transforms/Passes.h"

#include "mlir/Conversion/LLVMCommon/TypeConverter.h"
#include "mlir/Dialect/GPU/IR/GPUDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/Offload/IR/OffloadDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace mlir {
namespace offload {

#define GEN_PASS_DEF_OFFLOADLOWERSHAREDGLOBALSPASS
#include "mlir/Dialect/Offload/Transforms/Passes.h.inc"

} // namespace offload
} // namespace mlir

using namespace mlir;
using namespace mlir::offload;

namespace {

/// Maps an offload MemSpace to the LLVM address space integer and isConstant
/// flag for the gpu.module global.  Returns nullopt for mem_spaces that are not
/// lowered by this pass (e.g. managed, generic).
struct DeviceGlobalInfo {
  unsigned addrSpace;
  bool isConstant;
};

static std::optional<DeviceGlobalInfo> getDeviceGlobalInfo(MemSpace ms) {
  switch (ms) {
  case MemSpace::shared:   return DeviceGlobalInfo{3, false};
  case MemSpace::device:   return DeviceGlobalInfo{1, false};
  case MemSpace::constant: return DeviceGlobalInfo{4, true};
  case MemSpace::managed:  return DeviceGlobalInfo{0, false};
  default:                 return std::nullopt;
  }
}

/// Return true if this global mem_space cannot be safely replicated across
/// gpu.modules.  Only __shared__ (per-block, statically allocated) is safe.
static bool isNonReplicableMemSpace(MemSpace ms) {
  return ms == MemSpace::device || ms == MemSpace::constant ||
         ms == MemSpace::managed;
}

/// Emit an llvm.mlir.global for \p gv into \p gpuModule.
static void emitGlobalInModule(OpBuilder &builder,
                                LLVMTypeConverter &converter,
                                offload::GlobalVarOp gv,
                                gpu::GPUModuleOp gpuModule,
                                const DeviceGlobalInfo &info) {
  OpBuilder::InsertionGuard guard(builder);
  builder.setInsertionPointToStart(gpuModule.getBody());

  mlir::Type elemTy = gv.getType();
  mlir::Type llvmTy = converter.convertType(elemTy);
  if (!llvmTy) {
    gv.emitError("LowerSharedGlobals: cannot convert type ")
        << elemTy << " to LLVM for device variable @" << gv.getSymName();
    return;
  }

  mlir::LLVM::GlobalOp::create(
      builder, gv.getLoc(), llvmTy,
      /*isConstant=*/info.isConstant, mlir::LLVM::Linkage::Internal,
      gv.getSymName(),
      /*value=*/mlir::Attribute{},
      /*alignment=*/0,
      /*addrSpace=*/info.addrSpace);
}

struct LowerSharedGlobalsPass
    : offload::impl::OffloadLowerSharedGlobalsPassBase<LowerSharedGlobalsPass> {

  using OffloadLowerSharedGlobalsPassBase::OffloadLowerSharedGlobalsPassBase;

  void runOnOperation() override {
    ModuleOp module = getOperation();
    MLIRContext *ctx = module.getContext();

    // Find the primary gpu.module and optionally the deferred gpu.module.
    gpu::GPUModuleOp gpuModulePrimary;
    gpu::GPUModuleOp gpuModuleDeferred;
    module.walk([&](gpu::GPUModuleOp gm) {
      if (gm.getName() == gpuModuleName)
        gpuModulePrimary = gm;
      else if (gm.getName() == deferredGpuModuleName)
        gpuModuleDeferred = gm;
    });
    if (!gpuModulePrimary)
      return; // No primary gpu.module — nothing to do.

    // Collect all offload.global_var ops that map to device-side globals.
    // SplitSingleSourcePass (Step 1b) moves ALL device globals (shared,
    // device, constant, managed) into the gpu.module(s):
    //   - Non-shared (device/constant/managed): primary module only.
    //   - Shared: primary module always, deferred module if it exists.
    // Collect globals from both gpu.modules; also fall back to collecting
    // from the outer module for any shared globals that were left there
    // (e.g. when running this pass standalone in tests without Step 1b).
    SmallVector<offload::GlobalVarOp> deviceVars;
    // Globals inside the primary gpu.module (all mem_spaces after Step 1b).
    gpuModulePrimary.walk([&](offload::GlobalVarOp gv) {
      if (getDeviceGlobalInfo(gv.getMemSpace()))
        deviceVars.push_back(gv);
    });
    // Shared globals that may still be in the deferred gpu.module.
    if (gpuModuleDeferred) {
      gpuModuleDeferred.walk([&](offload::GlobalVarOp gv) {
        if (gv.getMemSpace() == MemSpace::shared &&
            getDeviceGlobalInfo(gv.getMemSpace()))
          deviceVars.push_back(gv);
      });
    }
    // Fallback: shared globals still in the outer module (standalone/test use).
    for (auto &op : *module.getBody()) {
      if (auto gv = dyn_cast<offload::GlobalVarOp>(&op))
        if (gv.getMemSpace() == MemSpace::shared &&
            getDeviceGlobalInfo(gv.getMemSpace()))
          deviceVars.push_back(gv);
    }
    if (deviceVars.empty())
      return;

    LLVMTypeConverter converter(ctx);
    OpBuilder builder(ctx);

    for (offload::GlobalVarOp gv : deviceVars) {
      auto info = getDeviceGlobalInfo(gv.getMemSpace());
      assert(info && "only device vars collected above");

      // Determine which gpu.module owns this global.
      gpu::GPUModuleOp ownerModule;
      if (gv->getParentOp() == gpuModulePrimary.getOperation())
        ownerModule = gpuModulePrimary;
      else if (gpuModuleDeferred &&
               gv->getParentOp() == gpuModuleDeferred.getOperation())
        ownerModule = gpuModuleDeferred;

      if (isNonReplicableMemSpace(gv.getMemSpace())) {
        // Non-replicable globals: emit into the module that owns them (primary).
        gpu::GPUModuleOp target = ownerModule ? ownerModule : gpuModulePrimary;
        emitGlobalInModule(builder, converter, gv, target, *info);
      } else {
        // Shared globals: emit into the owning module.
        // If the global is still in the outer module (fallback/test path),
        // use the symbol-use check to decide placement.
        if (ownerModule) {
          emitGlobalInModule(builder, converter, gv, ownerModule, *info);
        } else {
          // Fallback: outer-module shared global — check symbol uses.
          bool placed = false;
          auto primaryUses = SymbolTable::getSymbolUses(
              gv.getSymNameAttr(), gpuModulePrimary.getOperation());
          if (primaryUses && !primaryUses->empty()) {
            emitGlobalInModule(builder, converter, gv, gpuModulePrimary, *info);
            placed = true;
          }
          if (gpuModuleDeferred) {
            auto deferredUses = SymbolTable::getSymbolUses(
                gv.getSymNameAttr(), gpuModuleDeferred.getOperation());
            if (deferredUses && !deferredUses->empty()) {
              emitGlobalInModule(builder, converter, gv, gpuModuleDeferred,
                                 *info);
              placed = true;
            }
          }
          if (!placed)
            emitGlobalInModule(builder, converter, gv, gpuModulePrimary, *info);
        }
      }

      // Erase the offload.global_var from wherever it lives.
      gv.erase();
    }
  }
};

} // namespace

namespace mlir {
namespace offload {

void registerOffloadLowerSharedGlobalsPasses() {
  registerPass([]() -> std::unique_ptr<Pass> {
    return std::make_unique<LowerSharedGlobalsPass>();
  });
}

} // namespace offload
} // namespace mlir
