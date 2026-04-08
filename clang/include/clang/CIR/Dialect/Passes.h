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

namespace clang {
class ASTContext;
}

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
                           llvm::abi::X86AVXABILevel x86AvxAbiLevel);
std::unique_ptr<Pass> createHoistAllocasPass();
std::unique_ptr<Pass> createLoweringPreparePass();
std::unique_ptr<Pass> createLoweringPreparePass(clang::ASTContext *astCtx);
std::unique_ptr<Pass> createGotoSolverPass();
std::unique_ptr<Pass> createIdiomRecognizerPass();
std::unique_ptr<Pass> createLibOptPass();
std::unique_ptr<Pass> createLibOptPass(clang::ASTContext *astCtx);
std::unique_ptr<Pass> createIdiomRecognizerPass(clang::ASTContext *astCtx);
std::unique_ptr<Pass> createMergeOffloadModules();

// CIR offload optimization passes.
std::unique_ptr<Pass> createOffloadTightenLaunchBoundsPass(bool enabled = true);
std::unique_ptr<Pass> createOffloadDevirtualizeLaunchesPass(bool enabled = true);
std::unique_ptr<Pass> createOffloadSpecializeLaunchWrappersPass(bool enabled = true);
std::unique_ptr<Pass> createOffloadPropagateBlockShapePass(bool enabled = true);
std::unique_ptr<Pass> createOffloadPropagatePointerFactsPass(bool enabled = true);
std::unique_ptr<Pass> createOffloadSpecializeScalarArgsPass(bool enabled = true);
std::unique_ptr<Pass> createOffloadMultiversionDivisibilityPass(bool enabled = true);
std::unique_ptr<Pass> createOffloadEliminateCoveredGuardsPass(bool enabled = true);
std::unique_ptr<Pass> createOffloadSpecializeSharedMemoryPass(bool enabled = true, unsigned maxVariants = 1);
std::unique_ptr<Pass> createOffloadPropagateGridCoveragePass(bool enabled = true);
std::unique_ptr<Pass> createOffloadDeadArgEliminationPass(bool enabled = true);
std::unique_ptr<Pass> createOffloadAnnotateLoopInvariantArgsPass(bool enabled = true);
std::unique_ptr<Pass> createOffloadPromoteConstantArgsPass(bool enabled = true);
std::unique_ptr<Pass> createOffloadUnrollBarrierLoopsPass(bool enabled = true);

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
