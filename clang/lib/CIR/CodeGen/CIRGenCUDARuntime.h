//===------ CIRGenCUDARuntime.h - Interface to CUDA Runtimes -----*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This provides an abstract class for CUDA code generation.  Concrete
// subclasses of this implement code generation for specific CUDA
// runtime libraries.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CIR_CIRGENCUDARUNTIME_H
#define LLVM_CLANG_LIB_CIR_CIRGENCUDARUNTIME_H

#include "CIRGenValue.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"

#include <optional>

namespace clang {
class CallExpr;
class CUDAKernelCallExpr;
}

namespace clang::CIRGen {

class CIRGenFunction;
class CIRGenModule;
class FunctionArgList;
class ReturnValueSlot;

class CIRGenCUDARuntime {
protected:
  CIRGenModule &cgm;

public:
  CIRGenCUDARuntime(CIRGenModule &cgm) : cgm(cgm) {}
  virtual ~CIRGenCUDARuntime();

  virtual void emitDeviceStub(CIRGenFunction &cgf, cir::FuncOp fn,
                              FunctionArgList &args) = 0;

  virtual RValue emitCUDAKernelCallExpr(CIRGenFunction &cgf,
                                        const CUDAKernelCallExpr *expr,
                                        ReturnValueSlot retValue);

  /// Intercept a HIP/CUDA runtime library call and emit offload dialect ops.
  /// Returns the RValue if the call was intercepted, or nullopt to fall through
  /// to normal CIR call emission.
  virtual std::optional<RValue> emitHIPRuntimeCall(CIRGenFunction &cgf,
                                                   const CallExpr *e) {
    return std::nullopt;
  }

  virtual mlir::Operation *getKernelHandle(cir::FuncOp fn, GlobalDecl gd) = 0;

  virtual mlir::Operation *getKernelStub(mlir::Operation *handle) = 0;
};

CIRGenCUDARuntime *createNVCUDARuntime(CIRGenModule &cgm);

} // namespace clang::CIRGen

#endif // LLVM_CLANG_LIB_CIR_CIRGENCUDARUNTIME_H
