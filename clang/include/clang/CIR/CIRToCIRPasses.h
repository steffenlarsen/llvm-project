//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares an interface for running CIR-to-CIR passes.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CIR_CIRTOCIRPASSES_H
#define CLANG_CIR_CIRTOCIRPASSES_H

#include "mlir/Pass/Pass.h"

#include <memory>

namespace clang {
class ASTContext;
class TargetInfo;
} // namespace clang

namespace mlir {
class MLIRContext;
class ModuleOp;
} // namespace mlir

namespace cir {

// Run set of cleanup/prepare/etc passes CIR <-> CIR.
mlir::LogicalResult
runCIRToCIRPasses(mlir::ModuleOp theModule, mlir::MLIRContext &mlirCtx,
                  clang::ASTContext &astCtx, bool enableVerifier,
                  bool enableIdiomRecognizer, bool enableCIRSimplify,
                  bool enableLibOpt, llvm::StringRef libOptOptions,
                  bool enableCallConvLowering);

// Run the calling-convention lowering a module's emission deferred, for
// `targetInfo`'s target. A no-op for targets without one.
mlir::LogicalResult runCallConvLowering(mlir::ModuleOp theModule,
                                        mlir::MLIRContext &mlirCtx,
                                        const clang::TargetInfo &targetInfo);

} // namespace cir

#endif // CLANG_CIR_CIRTOCIRPASSES_H_
