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

  /// Generate the host-side __device_stub__ body so that code taking a
  /// pointer to a __global__ function (e.g. hipLaunchKernelGGL) resolves
  /// at link time.  The stub packs arguments, pops the call configuration,
  /// and calls hipLaunchKernel.
  ///
  /// TODO: The only reason stubs are needed in the offload path is that
  /// __hipRegisterFunction uses the stub address as the unique identifier
  /// for the device kernel.  If we can register kernels with a different
  /// identifier (e.g. a string name or a synthetic global), we could drop
  /// stub generation entirely and let all launches go through gpu.launch_func.
  void emitDeviceStub(CIRGenFunction &cgf, cir::FuncOp fn,
                      FunctionArgList &args) override;

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

  void handleVarRegistration(const VarDecl *, cir::GlobalOp) override {}
  void internalizeDeviceSideVar(const VarDecl *,
                                cir::GlobalLinkageKind &) override;
  std::string getDeviceSideName(const NamedDecl *nd) override;
};

/// Factory function — creates a CIRGenOffloadRuntime for cgm.
CIRGenCUDARuntime *createOffloadRuntime(CIRGenModule &cgm);

} // namespace clang::CIRGen

#endif // LLVM_CLANG_LIB_CIR_CIRGENCUDAOFFLOADRUNTIME_H
