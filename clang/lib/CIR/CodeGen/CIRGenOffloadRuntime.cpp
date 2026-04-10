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

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Offload/IR/OffloadDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"

#include "clang/AST/Expr.h"

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
  if (!targetField)
    return mlir::arith::ConstantIndexOp::create(builder, loc, 1);

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
                                                       GlobalDecl) {
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
  // 3. Extract grid, block dimensions and stream from the config call.
  //
  // The config call is __hipPushCallConfiguration(gridDim, blockDim,
  // sharedMem, stream), where gridDim and blockDim are dim3 struct values.
  // We extract the x, y, z fields and cast them to index.  The stream (arg 3)
  // is passed through as !offload.stream when it is non-null.
  // ------------------------------------------------------------------ //
  const CallExpr *config = expr->getConfig();
  mlir::Value gridX, gridY, gridZ, blockX, blockY, blockZ;
  mlir::Value streamVal; // empty = default stream (no stream operand)

  if (config && config->getNumArgs() >= 2) {
    const Expr *gridArg  = config->getArg(0);
    const Expr *blockArg = config->getArg(1);

    gridX  = emitDim3Component(cgf, loc, gridArg,  0);
    gridY  = emitDim3Component(cgf, loc, gridArg,  1);
    gridZ  = emitDim3Component(cgf, loc, gridArg,  2);
    blockX = emitDim3Component(cgf, loc, blockArg, 0);
    blockY = emitDim3Component(cgf, loc, blockArg, 1);
    blockZ = emitDim3Component(cgf, loc, blockArg, 2);

    // Extract the stream argument (arg 3) if present and non-null.
    // __hipPushCallConfiguration defaults stream to 0 (default stream).
    // We emit a stream operand only when the stream is provably non-zero.
    if (config->getNumArgs() >= 4) {
      const Expr *streamArg = config->getArg(3);
      // Skip emitting a stream operand for the default (null) stream.
      // hipStream_t is a pointer type; check both null pointer constant
      // expressions and integer-zero forms (0 cast to hipStream_t).
      bool isNullStream =
          streamArg->isNullPointerConstant(
              cgf.getContext(), Expr::NPC_ValueDependentIsNull) !=
          Expr::NPCK_NotNull;
      if (!isNullStream) {
        // Also check integer constant zero (e.g. (hipStream_t)0).
        Expr::EvalResult evalRes;
        if (streamArg->EvaluateAsInt(evalRes, cgf.getContext()))
          isNullStream = evalRes.Val.getInt().isZero();
      }
      if (!isNullStream) {
        mlir::Value raw = cgf.emitScalarExpr(streamArg);
        auto streamTy = mlir::offload::StreamType::get(builder.getContext());
        streamVal = mlir::UnrealizedConversionCastOp::create(
                        builder, loc, mlir::TypeRange{streamTy},
                        mlir::ValueRange{raw})
                        .getResult(0);
      }
    }
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
      streamVal,
      mlir::ValueRange(args));

  return RValue::get(nullptr);
}

/// Return a CIR integer zero value to use as hipSuccess (0).
static mlir::Value makeHipSuccess(CIRGenBuilderTy &builder, mlir::Location loc,
                                  mlir::Type resultTy) {
  // HIP error code 0 = hipSuccess; result type is usually !cir.int<s,32>.
  return builder.getConstantInt(loc, resultTy, 0);
}

std::optional<RValue>
CIRGenOffloadRuntime::emitCUDARuntimeCall(CIRGenFunction &cgf,
                                         const CallExpr *e) {
  // Only intercept plain function calls with a named callee.
  const FunctionDecl *fd =
      dyn_cast_or_null<FunctionDecl>(e->getCalleeDecl());
  if (!fd)
    return std::nullopt;

  llvm::StringRef name = fd->getName();

  CIRGenBuilderTy &builder = cgm.getBuilder();
  mlir::Location loc =
      cgf.currSrcLoc ? cgf.currSrcLoc.value() : builder.getUnknownLoc();
  mlir::MLIRContext *ctx = builder.getContext();

  // hipDeviceSynchronize() → offload.device_sync
  if (name == "hipDeviceSynchronize") {
    mlir::offload::DeviceSyncOp::create(builder, loc);
    // Return hipSuccess (0) as !cir.int<s,32>.
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipStreamCreate(hipStream_t *pStream) → offload.stream_create
  if (name == "hipStreamCreate") {
    mlir::offload::StreamType streamTy =
        mlir::offload::StreamType::get(ctx);
    mlir::Value stream =
        mlir::offload::StreamCreateOp::create(builder, loc, streamTy)
            .getResult();
    // Emit a store of the stream handle into the output pointer argument.
    // hipStream_t is an opaque pointer; bridge via unrealized_conversion_cast.
    mlir::Value pStream = cgf.emitScalarExpr(e->getArg(0));
    // Cast !offload.stream to the CIR pointer's pointee type (opaque ptr).
    mlir::Type ptrTy = pStream.getType();
    auto ptrCirTy = mlir::dyn_cast<cir::PointerType>(ptrTy);
    if (ptrCirTy) {
      mlir::Value cast =
          mlir::UnrealizedConversionCastOp::create(
              builder, loc, TypeRange{ptrCirTy.getPointee()},
              ValueRange{stream})
              .getResult(0);
      // CIRBaseBuilderTy::createStore(loc, val, ptr_val) handles type mismatches
      // by inserting a bitcast; use it to store into *pStream.
      builder.CIRBaseBuilderTy::createStore(loc, cast, pStream);
    }
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipStreamDestroy(hipStream_t stream) → offload.stream_destroy
  if (name == "hipStreamDestroy") {
    mlir::Value handle = cgf.emitScalarExpr(e->getArg(0));
    mlir::offload::StreamType streamTy =
        mlir::offload::StreamType::get(ctx);
    mlir::Value stream =
        mlir::UnrealizedConversionCastOp::create(
            builder, loc, TypeRange{streamTy}, ValueRange{handle})
            .getResult(0);
    mlir::offload::StreamDestroyOp::create(builder, loc, stream);
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipStreamSynchronize(hipStream_t stream) → offload.stream_sync
  if (name == "hipStreamSynchronize") {
    mlir::Value handle = cgf.emitScalarExpr(e->getArg(0));
    mlir::offload::StreamType streamTy =
        mlir::offload::StreamType::get(ctx);
    mlir::Value stream =
        mlir::UnrealizedConversionCastOp::create(
            builder, loc, TypeRange{streamTy}, ValueRange{handle})
            .getResult(0);
    mlir::offload::StreamSyncOp::create(builder, loc, stream);
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipMalloc(void **ptr, size_t size) → offload.malloc %size alloc_type=device
  if (name == "hipMalloc") {
    mlir::Value sizeVal = cgf.emitScalarExpr(e->getArg(1));
    mlir::Type indexTy = mlir::IndexType::get(ctx);
    mlir::Value sizeIdx =
        mlir::UnrealizedConversionCastOp::create(
            builder, loc, TypeRange{indexTy}, ValueRange{sizeVal})
            .getResult(0);
    mlir::Type voidPtrTy = builder.getVoidPtrTy();
    mlir::Value devPtr =
        mlir::offload::MallocOp::create(builder, loc, voidPtrTy, sizeIdx,
                                        mlir::offload::AllocType::device,
                                        mlir::Value{})
            .getResult();
    mlir::Value ptrArg = cgf.emitScalarExpr(e->getArg(0));
    auto ptrCirTy = mlir::dyn_cast<cir::PointerType>(ptrArg.getType());
    if (ptrCirTy) {
      mlir::Value cast =
          mlir::UnrealizedConversionCastOp::create(
              builder, loc, TypeRange{ptrCirTy.getPointee()},
              ValueRange{devPtr})
              .getResult(0);
      builder.CIRBaseBuilderTy::createStore(loc, cast, ptrArg);
    }
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipHostMalloc(void **ptr, size_t size, unsigned flags)
  //   → offload.malloc %size alloc_type=host
  if (name == "hipHostMalloc") {
    mlir::Value sizeVal = cgf.emitScalarExpr(e->getArg(1));
    mlir::Type indexTy = mlir::IndexType::get(ctx);
    mlir::Value sizeIdx =
        mlir::UnrealizedConversionCastOp::create(
            builder, loc, TypeRange{indexTy}, ValueRange{sizeVal})
            .getResult(0);
    mlir::Type voidPtrTy = builder.getVoidPtrTy();
    mlir::Value pinnedPtr =
        mlir::offload::MallocOp::create(builder, loc, voidPtrTy, sizeIdx,
                                        mlir::offload::AllocType::host,
                                        mlir::Value{})
            .getResult();
    mlir::Value ptrArg = cgf.emitScalarExpr(e->getArg(0));
    auto ptrCirTy = mlir::dyn_cast<cir::PointerType>(ptrArg.getType());
    if (ptrCirTy) {
      mlir::Value cast =
          mlir::UnrealizedConversionCastOp::create(
              builder, loc, TypeRange{ptrCirTy.getPointee()},
              ValueRange{pinnedPtr})
              .getResult(0);
      builder.CIRBaseBuilderTy::createStore(loc, cast, ptrArg);
    }
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipMallocManaged(void **ptr, size_t size, unsigned flags)
  //   → offload.malloc %size alloc_type=managed
  if (name == "hipMallocManaged") {
    mlir::Value sizeVal = cgf.emitScalarExpr(e->getArg(1));
    mlir::Type indexTy = mlir::IndexType::get(ctx);
    mlir::Value sizeIdx =
        mlir::UnrealizedConversionCastOp::create(
            builder, loc, TypeRange{indexTy}, ValueRange{sizeVal})
            .getResult(0);
    mlir::Type voidPtrTy = builder.getVoidPtrTy();
    mlir::Value managedPtr =
        mlir::offload::MallocOp::create(builder, loc, voidPtrTy, sizeIdx,
                                        mlir::offload::AllocType::managed,
                                        mlir::Value{})
            .getResult();
    mlir::Value ptrArg = cgf.emitScalarExpr(e->getArg(0));
    auto ptrCirTy = mlir::dyn_cast<cir::PointerType>(ptrArg.getType());
    if (ptrCirTy) {
      mlir::Value cast =
          mlir::UnrealizedConversionCastOp::create(
              builder, loc, TypeRange{ptrCirTy.getPointee()},
              ValueRange{managedPtr})
              .getResult(0);
      builder.CIRBaseBuilderTy::createStore(loc, cast, ptrArg);
    }
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipFree(void *ptr) → offload.free %ptr alloc_type=device
  if (name == "hipFree") {
    mlir::Value ptr = cgf.emitScalarExpr(e->getArg(0));
    mlir::offload::FreeOp::create(builder, loc, ptr,
                                   mlir::offload::AllocType::device,
                                   mlir::Value{});
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipHostFree(void *ptr) → offload.free %ptr alloc_type=host
  if (name == "hipHostFree") {
    mlir::Value ptr = cgf.emitScalarExpr(e->getArg(0));
    mlir::offload::FreeOp::create(builder, loc, ptr,
                                   mlir::offload::AllocType::host,
                                   mlir::Value{});
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipMemcpy(dst, src, size, kind)
  //   → offload.memcpy %dst, %src, %size dst_space=X src_space=Y
  //
  // Map hipMemcpyKind enum to (dst_space, src_space) AllocType pairs:
  //   0=H2H: host←host   1=H2D: device←host
  //   2=D2H: host←device 3=D2D: device←device  4=Default: managed (→Default)
  if (name == "hipMemcpy") {
    mlir::Value dst  = cgf.emitScalarExpr(e->getArg(0));
    mlir::Value src  = cgf.emitScalarExpr(e->getArg(1));
    mlir::Value size = cgf.emitScalarExpr(e->getArg(2));
    mlir::Type indexTy = mlir::IndexType::get(ctx);
    mlir::Value sizeIdx =
        mlir::UnrealizedConversionCastOp::create(
            builder, loc, TypeRange{indexTy}, ValueRange{size})
            .getResult(0);
    // Default to managed (→ hipMemcpyDefault when lowered).
    mlir::offload::AllocType dstSpace = mlir::offload::AllocType::managed;
    mlir::offload::AllocType srcSpace = mlir::offload::AllocType::managed;
    Expr::EvalResult kindResult;
    if (e->getArg(3)->EvaluateAsInt(kindResult, cgf.getContext())) {
      switch (kindResult.Val.getInt().getZExtValue()) {
      case 0: // hipMemcpyHostToHost
        dstSpace = mlir::offload::AllocType::host;
        srcSpace = mlir::offload::AllocType::host;
        break;
      case 1: // hipMemcpyHostToDevice
        dstSpace = mlir::offload::AllocType::device;
        srcSpace = mlir::offload::AllocType::host;
        break;
      case 2: // hipMemcpyDeviceToHost
        dstSpace = mlir::offload::AllocType::host;
        srcSpace = mlir::offload::AllocType::device;
        break;
      case 3: // hipMemcpyDeviceToDevice
        dstSpace = mlir::offload::AllocType::device;
        srcSpace = mlir::offload::AllocType::device;
        break;
      default: // hipMemcpyDefault (4) or unknown
        break;
      }
    }
    mlir::offload::MemcpyOp::create(builder, loc, dst, src, sizeIdx,
                                    dstSpace, srcSpace, mlir::Value{});
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipMemcpyToSymbol(symbol, src, count, offset, kind)
  //   → offload.memcpy_to_symbol @sym src = %src count = %count_idx
  //
  // The symbol argument is a pointer to the device global; we extract the
  // referenced decl name from the DeclRefExpr so we can emit a flat symbol ref.
  // Only offset==0 is supported (the overwhelming common case).
  if (name == "hipMemcpyToSymbol") {
    const Expr *symArg = e->getArg(0)->IgnoreParenImpCasts();
    // Peel off the implicit `&` that takes the address of the global.
    if (const auto *unary = dyn_cast<UnaryOperator>(symArg))
      if (unary->getOpcode() == UO_AddrOf)
        symArg = unary->getSubExpr()->IgnoreParenImpCasts();
    const auto *dre = dyn_cast<DeclRefExpr>(symArg);
    if (!dre) {
      cgm.getDiags().Report(e->getExprLoc(),
          cgm.getDiags().getCustomDiagID(
              DiagnosticsEngine::Error,
              "hipMemcpyToSymbol: first argument must be a device global "
              "reference"));
      return std::nullopt;
    }
    llvm::StringRef symName = dre->getDecl()->getName();
    mlir::Value src   = cgf.emitScalarExpr(e->getArg(1));
    mlir::Value count = cgf.emitScalarExpr(e->getArg(2));
    mlir::Type indexTy = mlir::IndexType::get(ctx);
    mlir::Value countIdx =
        mlir::UnrealizedConversionCastOp::create(
            builder, loc, TypeRange{indexTy}, ValueRange{count})
            .getResult(0);

    mlir::offload::MemcpyToSymbolOp::create(builder, loc, symName, src,
                                             countIdx);
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipEventCreate(hipEvent_t *event) → offload.event_create + store
  if (name == "hipEventCreate") {
    auto eventTy = mlir::offload::EventType::get(ctx);
    mlir::Value event =
        mlir::offload::EventCreateOp::create(builder, loc, eventTy).getResult();
    mlir::Value pEvent = cgf.emitScalarExpr(e->getArg(0));
    auto ptrCirTy = mlir::dyn_cast<cir::PointerType>(pEvent.getType());
    if (ptrCirTy) {
      mlir::Value cast =
          mlir::UnrealizedConversionCastOp::create(
              builder, loc, TypeRange{ptrCirTy.getPointee()},
              ValueRange{event})
              .getResult(0);
      builder.CIRBaseBuilderTy::createStore(loc, cast, pEvent);
    }
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipEventDestroy(hipEvent_t event) → offload.event_destroy
  if (name == "hipEventDestroy") {
    mlir::Value handle = cgf.emitScalarExpr(e->getArg(0));
    auto eventTy = mlir::offload::EventType::get(ctx);
    mlir::Value event =
        mlir::UnrealizedConversionCastOp::create(
            builder, loc, TypeRange{eventTy}, ValueRange{handle})
            .getResult(0);
    mlir::offload::EventDestroyOp::create(builder, loc, event);
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipEventRecord(hipEvent_t event, hipStream_t stream)
  //   → offload.event_record %event (, stream = %stream)?
  if (name == "hipEventRecord") {
    mlir::Value handle = cgf.emitScalarExpr(e->getArg(0));
    auto eventTy = mlir::offload::EventType::get(ctx);
    mlir::Value event =
        mlir::UnrealizedConversionCastOp::create(
            builder, loc, TypeRange{eventTy}, ValueRange{handle})
            .getResult(0);
    // Stream argument (may be null/default stream).
    mlir::Value streamVal;
    if (e->getNumArgs() >= 2) {
      const Expr *streamArg = e->getArg(1);
      Expr::EvalResult evalRes;
      bool isNullStream = false;
      if (streamArg->EvaluateAsInt(evalRes, cgf.getContext()))
        isNullStream = evalRes.Val.getInt().isZero();
      if (!isNullStream) {
        mlir::Value raw = cgf.emitScalarExpr(streamArg);
        auto streamTy = mlir::offload::StreamType::get(ctx);
        streamVal = mlir::UnrealizedConversionCastOp::create(
                        builder, loc, TypeRange{streamTy}, ValueRange{raw})
                        .getResult(0);
      }
    }
    mlir::offload::EventRecordOp::create(builder, loc, event, streamVal);
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipEventSynchronize(hipEvent_t event) → offload.event_wait
  if (name == "hipEventSynchronize") {
    mlir::Value handle = cgf.emitScalarExpr(e->getArg(0));
    auto eventTy = mlir::offload::EventType::get(ctx);
    mlir::Value event =
        mlir::UnrealizedConversionCastOp::create(
            builder, loc, TypeRange{eventTy}, ValueRange{handle})
            .getResult(0);
    mlir::offload::EventWaitOp::create(builder, loc, event);
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipMemset(void *dst, int value, size_t size)
  //   → offload.memset %dst, %u8_pattern, %size_idx
  //
  // hipMemset fills each byte with the low byte of `value`, so the pattern is
  // semantically 1 byte wide.  We truncate the CIR int to i8 (unsigned char)
  // so the lowering pass can dispatch on bit-width → hipMemset (i8 → 1 byte).
  if (name == "hipMemset") {
    mlir::Value dst  = cgf.emitScalarExpr(e->getArg(0));
    mlir::Value val  = cgf.emitScalarExpr(e->getArg(1));
    mlir::Value size = cgf.emitScalarExpr(e->getArg(2));
    mlir::Type indexTy = mlir::IndexType::get(ctx);
    mlir::Value sizeIdx =
        mlir::UnrealizedConversionCastOp::create(
            builder, loc, TypeRange{indexTy}, ValueRange{size})
            .getResult(0);
    // Truncate to i8 so the lowering pass knows pattern_size = 1.
    mlir::Type i8Ty = cir::IntType::get(ctx, 8, /*isSigned=*/false);
    mlir::Value pattern =
        mlir::UnrealizedConversionCastOp::create(
            builder, loc, TypeRange{i8Ty}, ValueRange{val})
            .getResult(0);
    mlir::offload::MemsetOp::create(builder, loc, dst, pattern, sizeIdx,
                                    mlir::Value{});
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  return std::nullopt;
}

CIRGenCUDARuntime *clang::CIRGen::createOffloadRuntime(CIRGenModule &cgm) {
  return new CIRGenOffloadRuntime(cgm);
}
