//===- Passes.h - CIR pass entry points -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header file defines prototypes that expose pass constructors.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CIR_DIALECT_PASSES_H
#define CLANG_CIR_DIALECT_PASSES_H

#include "mlir/Pass/Pass.h"
#include "llvm/ABI/TargetInfo.h"

namespace cir {
/// The ABI target whose calling-convention rules drive CallConvLowering.
/// None is the unset state used when the pass runs in classification-attr
/// mode instead of selecting a target.
enum class CallConvTarget { None, Test, X86_64 };
} // namespace cir
#include "llvm/ADT/IntrusiveRefCntPtr.h"

namespace clang {
class ASTContext;
}

namespace llvm::vfs {
class FileSystem;
} // namespace llvm::vfs

namespace cir {
class LowerModule;
} // namespace cir

namespace mlir {

std::unique_ptr<Pass> createCIRCanonicalizePass();
std::unique_ptr<Pass> createCIRFlattenCFGPass();
std::unique_ptr<Pass> createCIRSimplifyPass();
std::unique_ptr<Pass> createCIREHABILoweringPass();
std::unique_ptr<Pass> createCXXABILoweringPass();
std::unique_ptr<Pass> createTargetLoweringPass();
std::unique_ptr<Pass> createCallConvLoweringPass();
std::unique_ptr<Pass>
createCallConvLoweringPass(cir::CallConvTarget target,
                           llvm::abi::X86AVXABILevel x86AvxAbiLevel,
                           const llvm::abi::ABICompatInfo &x86AbiCompat);
std::unique_ptr<Pass> createHoistAllocasPass();
std::unique_ptr<Pass> createLoweringPreparePass();
std::unique_ptr<Pass> createLoweringPreparePass(
    cir::LowerModule *lowerModule,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> vfs = nullptr);
std::unique_ptr<Pass> createCUDARegisterModulePass();
std::unique_ptr<Pass> createCUDARegisterModulePass(
    cir::LowerModule *lowerModule,
    llvm::IntrusiveRefCntPtr<llvm::vfs::FileSystem> vfs = nullptr);
std::unique_ptr<Pass> createMaterializeASTFactsPass();
std::unique_ptr<Pass> createOffloadDeadKernelEliminationPass();
std::unique_ptr<Pass> createOffloadKernelArgConstantPropagationPass();
std::unique_ptr<Pass> createOffloadSpecializeLaunchWrappersPass();
std::unique_ptr<Pass> createOffloadSpecializeConstantArgsPass();
std::unique_ptr<Pass> createOffloadPropagateBlockShapePass();
std::unique_ptr<Pass> createOffloadTightenLaunchBoundsPass();
std::unique_ptr<Pass> createOffloadEliminateCoveredGuardsPass();
std::unique_ptr<Pass> createGotoSolverPass();
std::unique_ptr<Pass> createIdiomRecognizerPass();
std::unique_ptr<Pass> createLibOptPass();
std::unique_ptr<Pass> createLibOptPass(clang::ASTContext *astCtx);

void populateCIRPreLoweringPasses(mlir::OpPassManager &pm);

//===----------------------------------------------------------------------===//
// Registration
//===----------------------------------------------------------------------===//

void registerCIRDialectTranslation(mlir::MLIRContext &context);

/// Generate the code for registering passes.
#define GEN_PASS_REGISTRATION
#include "clang/CIR/Dialect/Passes.h.inc"

} // namespace mlir

#endif // CLANG_CIR_DIALECT_PASSES_H
