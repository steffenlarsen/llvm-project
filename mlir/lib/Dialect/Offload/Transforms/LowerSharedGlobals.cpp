//===- LowerSharedGlobals.cpp - Lower offload shared globals to gpu.module -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass lowers `offload.global_var` ops with `mem_space = shared` to
// `llvm.mlir.global` ops with `addr_space = 3` (AMDGPU LDS / NVPTX shared
// memory) inside the `gpu.module` produced by `SplitSingleSourcePass`.
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

    // Collect all offload.global_var ops with mem_space = shared.
    SmallVector<offload::GlobalVarOp> sharedVars;
    module.walk([&](offload::GlobalVarOp gv) {
      if (gv.getMemSpace() == MemSpace::shared)
        sharedVars.push_back(gv);
    });
    if (sharedVars.empty())
      return;

    // Build a type converter to map CIR/standard types to LLVM types.
    // We use the standard LLVMTypeConverter which handles builtin types
    // (i1, iN, f32, f64, memref, etc.) and LLVM-legal types natively.
    LLVMTypeConverter converter(ctx);

    OpBuilder builder(ctx);
    builder.setInsertionPointToStart(gpuModule.getBody());

    for (offload::GlobalVarOp gv : sharedVars) {
      mlir::Type elemTy = gv.getType();

      // Convert the type using the LLVM type converter.
      mlir::Type llvmTy = converter.convertType(elemTy);
      if (!llvmTy) {
        gv.emitError("LowerSharedGlobals: cannot convert type ")
            << elemTy << " to LLVM for shared variable @" << gv.getSymName();
        signalPassFailure();
        return;
      }

      // Emit llvm.mlir.global internal @name {addr_space = 3} : llvmTy
      // inside the gpu.module.  Shared memory cannot have initializers in
      // CUDA/HIP — use the default zero-initialized (no value attribute).
      mlir::LLVM::GlobalOp::create(
          builder, gv.getLoc(), llvmTy,
          /*isConstant=*/false, mlir::LLVM::Linkage::Internal,
          gv.getSymName(),
          /*value=*/mlir::Attribute{},
          /*alignment=*/0,
          /*addrSpace=*/3);

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
