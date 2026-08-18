//===----- CIRGenCUDARuntime.cpp - Interface to CUDA Runtimes -------------===//
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

#include "CIRGenCUDARuntime.h"
#include "CIRGenBuilder.h"
#include "CIRGenFunction.h"
#include "clang/AST/ExprCXX.h"

using namespace clang;
using namespace CIRGen;

CIRGenCUDARuntime::~CIRGenCUDARuntime() {}

RValue CIRGenCUDARuntime::emitCUDAKernelCallExpr(CIRGenFunction &cgf,
                                                 const CUDAKernelCallExpr *expr,
                                                 ReturnValueSlot retValue) {

  CIRGenBuilderTy &builder = cgm.getBuilder();
  mlir::Location loc =
      cgf.currSrcLoc ? cgf.currSrcLoc.value() : builder.getUnknownLoc();

  cgf.emitIfOnBoolExpr(
      expr->getConfig(),
      [&](mlir::OpBuilder &b, mlir::Location l) { cir::YieldOp::create(b, l); },
      loc,
      [&](mlir::OpBuilder &b, mlir::Location l) {
        const Expr *calleeExpr = expr->getCallee();
        CIRGenCallee callee;

        // A launch through a function pointer receives the kernel *handle*,
        // since that is what taking a kernel's address yields on the host. The
        // stub to call is stored in the handle, so load it back out -- which is
        // what classic CodeGen emits for the same construct. A direct
        // `kernel<<<>>>` needs none of this: emitDirectCallee has already
        // resolved the handle to the stub.
        if (!expr->getDirectCallee() && cgm.getLangOpts().HIP &&
            !cgm.getLangOpts().CUDAIsDevice) {
          mlir::Value handle = cgf.emitScalarExpr(calleeExpr);
          auto fnPtrTy = mlir::cast<cir::PointerType>(handle.getType());
          mlir::Value slot = builder.createBitcast(
              loc, handle, cir::PointerType::get(fnPtrTy));
          mlir::Value stub = cir::LoadOp::create(builder, loc, fnPtrTy, slot);
          QualType fnType = calleeExpr->getType()->getPointeeType();
          CIRGenCalleeInfo calleeInfo(fnType->getAs<FunctionProtoType>(),
                                      GlobalDecl());
          callee = CIRGenCallee(calleeInfo, stub.getDefiningOp());
        } else {
          callee = cgf.emitCallee(calleeExpr);
        }

        cgf.emitCall(calleeExpr->getType(), callee, expr, retValue);
        cir::YieldOp::create(b, l);
      },
      loc);

  return RValue::get(nullptr);
}
