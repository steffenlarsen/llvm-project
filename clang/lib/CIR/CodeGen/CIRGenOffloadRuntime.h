//===-- CIRGenOffloadRuntime.h - Offload dialect HIP/CUDA runtime ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the CIRGenCUDARuntime interface by emitting offload dialect ops
// (offload.func, offload.kernel_launch, offload.global_var) instead of the
// legacy hipLaunchKernel/cudaLaunchKernel stubs.
//
// This is selected when -fclangir-offload is specified and produces a unified
// host+device MLIR module.  The SplitSingleSource MLIR pass then performs the
// host/device split as a dialect transform.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_CIR_CIRGENCUDAOFFLOADRUNTIME_H
#define LLVM_CLANG_LIB_CIR_CIRGENCUDAOFFLOADRUNTIME_H

#include "CIRGenCUDARuntime.h"

namespace clang::CIRGen {

class CIRGenOffloadRuntime : public CIRGenCUDARuntime {
public:
  explicit CIRGenOffloadRuntime(CIRGenModule &cgm);
  ~CIRGenOffloadRuntime() override = default;

  /// No host-side stub needed: the kernel is emitted as an offload.func with
  /// exec_space = global; the SplitSingleSource pass handles the split.
  void emitDeviceStub(CIRGenFunction &, cir::FuncOp,
                      FunctionArgList &) override {}

  /// Emit offload.kernel_launch in place of the legacy hipLaunchKernel call.
  RValue emitCUDAKernelCallExpr(CIRGenFunction &cgf,
                                const CUDAKernelCallExpr *expr,
                                ReturnValueSlot retValue) override;

  /// Intercept HIP runtime calls and emit offload dialect ops instead.
  std::optional<RValue> emitCUDARuntimeCall(CIRGenFunction &cgf,
                                           const CallExpr *e) override;

  /// In the offload model kernels are identified by their offload.func symbol
  /// directly; no separate handle global is needed.
  mlir::Operation *getKernelHandle(cir::FuncOp fn, GlobalDecl gd) override;
  mlir::Operation *getKernelStub(mlir::Operation *handle) override {
    return handle;
  }
};

/// Factory function — creates a CIRGenOffloadRuntime for cgm.
CIRGenCUDARuntime *createOffloadRuntime(CIRGenModule &cgm);

} // namespace clang::CIRGen

#endif // LLVM_CLANG_LIB_CIR_CIRGENCUDAOFFLOADRUNTIME_H
