//===-- CIRGenOffloadRuntime.cpp - Offload dialect HIP/CUDA runtime -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CIRGenOffloadRuntime.h"
#include "CIRGenFunction.h"
#include "CIRGenModule.h"
#include "CIRGenValue.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/GlobalDecl.h"
#include "clang/Basic/Cuda.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Offload/IR/OffloadDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"

using namespace clang;
using namespace clang::CIRGen;
using namespace mlir;

//===----------------------------------------------------------------------===//
// Helper: extract a dim3 component (x, y, or z) from a Clang dim3 expression
// and cast it to mlir::IndexType.
//
// dim3 is defined in HIP/CUDA headers as:
//   struct dim3 { uint32_t x, y, z; };
// The component is extracted by reading the n-th field of the struct.
//===----------------------------------------------------------------------===//

static mlir::Value emitDim3Component(CIRGenFunction &cgf,
                                     mlir::Location loc,
                                     const Expr *dim3Expr,
                                     unsigned fieldIdx) {
  CIRGenModule &cgm = cgf.getCIRGenModule();
  CIRGenBuilderTy &builder = cgm.getBuilder();
  mlir::MLIRContext *ctx = builder.getContext();

  // dim3 arguments in <<<grid, block>>> may be temporaries (rvalues) or
  // lvalues. For rvalues we must materialize them into a temporary alloca
  // before field-loading; for lvalues emitLValue works directly.
  LValue base;
  QualType dim3Ty = dim3Expr->getType();
  if (dim3Expr->isLValue()) {
    base = cgf.emitLValue(dim3Expr);
  } else {
    // Materialize the rvalue into a temporary and get its LValue.
    Address tmp = cgf.createTempAlloca(cgf.convertTypeForMem(dim3Ty),
                                       cgf.getContext().getTypeAlignInChars(dim3Ty),
                                       loc, "dim3.tmp");
    AggValueSlot slot = AggValueSlot::forAddr(
        tmp, dim3Ty.getQualifiers(), AggValueSlot::IsNotDestructed,
        AggValueSlot::IsNotAliased, AggValueSlot::DoesNotOverlap);
    cgf.emitAggExpr(dim3Expr, slot);
    base = cgf.makeAddrLValue(tmp, dim3Ty);
  }

  // The dim3 struct type: find the RecordDecl.
  const RecordDecl *rd = dim3Ty->getAsRecordDecl();
  if (!rd) {
    // Fallback: just use 1.
    return mlir::arith::ConstantIndexOp::create(builder, loc, 1);
  }

  // Walk to the fieldIdx-th field.
  unsigned idx = 0;
  const FieldDecl *targetField = nullptr;
  for (const FieldDecl *fd : rd->fields()) {
    if (idx == fieldIdx) {
      targetField = fd;
      break;
    }
    ++idx;
  }
  if (!targetField) {
    return mlir::arith::ConstantIndexOp::create(builder, loc, 1);
  }

  LValue fieldLV = cgf.emitLValueForField(base, targetField);
  mlir::Value fieldVal = cgf.emitLoadOfScalar(fieldLV, SourceLocation());

  // The field is a CIR integer (e.g. !cir.int<u, 32>).  arith.index_castui
  // requires a standard MLIR integer, so first unrealize the CIR type to i32,
  // then cast to index.
  mlir::Type i32Ty = mlir::IntegerType::get(ctx, 32);
  mlir::Type indexTy = mlir::IndexType::get(ctx);
  mlir::Value asI32 = mlir::UnrealizedConversionCastOp::create(
      builder, loc, i32Ty, fieldVal).getResult(0);
  return mlir::arith::IndexCastUIOp::create(builder, loc, indexTy, asI32);
}

//===----------------------------------------------------------------------===//
// CIRGenOffloadRuntime implementation
//===----------------------------------------------------------------------===//

CIRGenOffloadRuntime::CIRGenOffloadRuntime(CIRGenModule &cgm)
    : CIRGenCUDARuntime(cgm) {}

mlir::Operation *CIRGenOffloadRuntime::getKernelHandle(cir::FuncOp fn,
                                                       GlobalDecl gd) {
  // In the offload model the kernel IS the function — no separate handle.
  return fn.getOperation();
}

RValue CIRGenOffloadRuntime::emitCUDAKernelCallExpr(
    CIRGenFunction &cgf, const CUDAKernelCallExpr *expr,
    ReturnValueSlot /*retValue*/) {

  CIRGenBuilderTy &builder = cgm.getBuilder();
  mlir::Location loc =
      cgf.currSrcLoc ? cgf.currSrcLoc.value() : builder.getUnknownLoc();

  // ------------------------------------------------------------------ //
  // 1. Extract the callee symbol name.
  // ------------------------------------------------------------------ //
  const FunctionDecl *kernelDecl =
      dyn_cast<FunctionDecl>(expr->getCalleeDecl());
  if (!kernelDecl) {
    cgm.errorNYI(expr->getSourceRange(),
                 "offload kernel launch: callee is not a FunctionDecl");
    return RValue::get(nullptr);
  }
  // Use KernelReferenceKind::Kernel to get the canonical kernel name (without
  // the __device_stub__ prefix that the host-side Stub kind produces).
  llvm::StringRef kernelName =
      cgm.getMangledName(GlobalDecl(kernelDecl, KernelReferenceKind::Kernel));

  // ------------------------------------------------------------------ //
  // 2. Emit kernel arguments.
  // ------------------------------------------------------------------ //
  SmallVector<mlir::Value> args;
  SmallVector<mlir::Type> argTypes;
  for (const Expr *arg : expr->arguments()) {
    mlir::Value v = cgf.emitScalarExpr(arg);
    args.push_back(v);
    argTypes.push_back(v.getType());
  }

  // ------------------------------------------------------------------ //
  // 3. Extract grid and block dimensions from the config call.
  //
  // The config call is __cudaPushCallConfiguration(gridDim, blockDim, ...),
  // where gridDim and blockDim are dim3 struct values.  We extract the x, y, z
  // fields and cast them to index.
  // ------------------------------------------------------------------ //
  const CallExpr *config = expr->getConfig();
  mlir::Value gridX, gridY, gridZ, blockX, blockY, blockZ;

  if (config && config->getNumArgs() >= 2) {
    const Expr *gridArg  = config->getArg(0);
    const Expr *blockArg = config->getArg(1);

    gridX  = emitDim3Component(cgf, loc, gridArg,  0);
    gridY  = emitDim3Component(cgf, loc, gridArg,  1);
    gridZ  = emitDim3Component(cgf, loc, gridArg,  2);
    blockX = emitDim3Component(cgf, loc, blockArg, 0);
    blockY = emitDim3Component(cgf, loc, blockArg, 1);
    blockZ = emitDim3Component(cgf, loc, blockArg, 2);
  } else {
    // Fallback: 1-D launch with unit dimensions.
    auto one = [&]() {
      return mlir::Value(mlir::arith::ConstantIndexOp::create(builder, loc, 1));
    };
    gridX = gridY = gridZ = blockX = blockY = blockZ = one();
  }

  // ------------------------------------------------------------------ //
  // 4. Emit offload.kernel_launch.
  // ------------------------------------------------------------------ //
  mlir::offload::KernelLaunchOp::create(
      builder, loc,
      kernelName,  // callee as StringRef (use the StringRef overload)
      gridX, gridY, gridZ,
      blockX, blockY, blockZ,
      /*stream=*/mlir::Value{},
      mlir::ValueRange(args));

  return RValue::get(nullptr);
}

CIRGenCUDARuntime *clang::CIRGen::createOffloadRuntime(CIRGenModule &cgm) {
  return new CIRGenOffloadRuntime(cgm);
}
