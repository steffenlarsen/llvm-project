//===- LowerSharedGlobals.cpp - Lower offload device globals to gpu.module -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass lowers `offload.global_var` ops with device-side mem_spaces to
// `llvm.mlir.global` inside the `gpu.module` produced by `SplitSingleSourcePass`.
//
//   mem_space = shared   → addr_space = 3  (AMDGPU LDS / NVPTX .shared)
//   mem_space = device   → addr_space = 1  (AMDGPU / NVPTX global memory)
//   mem_space = constant → addr_space = 4  (AMDGPU / NVPTX constant memory)
//
// All three address space values are the same for both AMDGPU and NVPTX.
// The globals are always zero-initialized in the device binary; the host sets
// device/constant values via hipMemcpyToSymbol / cudaMemcpyToSymbol.
//
// Run this pass *after* SplitSingleSourcePass (so the gpu.module exists) and
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

struct LowerSharedGlobalsPass
    : offload::impl::OffloadLowerSharedGlobalsPassBase<LowerSharedGlobalsPass> {

  using OffloadLowerSharedGlobalsPassBase::OffloadLowerSharedGlobalsPassBase;

  void runOnOperation() override {
    ModuleOp module = getOperation();
    MLIRContext *ctx = module.getContext();

    // Find the gpu.module by name.
    gpu::GPUModuleOp gpuModule;
    module.walk([&](gpu::GPUModuleOp gm) {
      if (gm.getName() == gpuModuleName)
        gpuModule = gm;
    });
    if (!gpuModule)
      return; // No gpu.module found — nothing to do.

    // Collect all offload.global_var ops that map to device-side globals.
    SmallVector<offload::GlobalVarOp> deviceVars;
    module.walk([&](offload::GlobalVarOp gv) {
      if (getDeviceGlobalInfo(gv.getMemSpace()))
        deviceVars.push_back(gv);
    });
    if (deviceVars.empty())
      return;

    // Build a type converter to map CIR/standard types to LLVM types.
    // We use the standard LLVMTypeConverter which handles builtin types
    // (i1, iN, f32, f64, arrays, etc.) and LLVM-legal types natively.
    LLVMTypeConverter converter(ctx);

    OpBuilder builder(ctx);
    builder.setInsertionPointToStart(gpuModule.getBody());

    for (offload::GlobalVarOp gv : deviceVars) {
      auto info = getDeviceGlobalInfo(gv.getMemSpace());
      assert(info && "only device vars collected above");

      mlir::Type elemTy = gv.getType();

      // Convert the type using the LLVM type converter.
      mlir::Type llvmTy = converter.convertType(elemTy);
      if (!llvmTy) {
        gv.emitError("LowerSharedGlobals: cannot convert type ")
            << elemTy << " to LLVM for device variable @" << gv.getSymName();
        signalPassFailure();
        return;
      }

      // Emit llvm.mlir.global internal @name {addr_space = N} : llvmTy
      // inside the gpu.module.  All device-side globals are zero-initialized
      // at device binary load time; the host sets values via
      // hipMemcpyToSymbol / cudaMemcpyToSymbol.
      mlir::LLVM::GlobalOp::create(
          builder, gv.getLoc(), llvmTy,
          /*isConstant=*/info->isConstant, mlir::LLVM::Linkage::Internal,
          gv.getSymName(),
          /*value=*/mlir::Attribute{},
          /*alignment=*/0,
          /*addrSpace=*/info->addrSpace);

      // Erase the offload.global_var from the unified module.
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
