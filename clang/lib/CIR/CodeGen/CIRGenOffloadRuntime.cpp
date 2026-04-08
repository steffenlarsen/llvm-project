//===-- CIRGenOffloadRuntime.cpp - GPU dialect HIP/CUDA runtime -----------===//
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
#include "mlir/Dialect/GPU/IR/GPUDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"

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

static mlir::Value emitDim3Component(CIRGenFunction &cgf, mlir::Location loc,
                                     const Expr *dim3Expr, unsigned fieldIdx) {
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
    Address tmp = cgf.createTempAlloca(
        cgf.convertTypeForMem(dim3Ty),
        cgf.getContext().getTypeAlignInChars(dim3Ty), loc, "dim3.tmp");
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
  mlir::Value asI32 =
      mlir::UnrealizedConversionCastOp::create(builder, loc, i32Ty, fieldVal)
          .getResult(0);
  return mlir::arith::IndexCastUIOp::create(builder, loc, indexTy, asI32);
}

//===----------------------------------------------------------------------===//
// Helper: declare a HIP runtime function in the parent module (if not already
// declared) and emit a cir.call to it.  Returns the call result (or null for
// void functions).
//===----------------------------------------------------------------------===//

static mlir::Value emitHIPCall(CIRGenFunction &cgf, mlir::Location loc,
                               llvm::StringRef funcName, cir::FuncType funcTy,
                               mlir::ValueRange args) {
  CIRGenModule &cgm = cgf.getCIRGenModule();
  CIRGenBuilderTy &builder = cgm.getBuilder();

  cir::FuncOp fn = cgm.createRuntimeFunction(funcTy, funcName);
  cir::CallOp call = builder.createCallOp(loc, fn, args);
  if (mlir::isa<cir::VoidType>(funcTy.getReturnType()))
    return mlir::Value{};
  return call.getResult();
}

//===----------------------------------------------------------------------===//
// CIRGenOffloadRuntime implementation
//===----------------------------------------------------------------------===//

CIRGenOffloadRuntime::CIRGenOffloadRuntime(CIRGenModule &cgm)
    : CIRGenCUDARuntime(cgm) {}

// Pack function arguments into a void** array for hipLaunchKernel.
static mlir::Value prepareKernelArgs(CIRGenFunction &cgf, CIRGenModule &cgm,
                                     mlir::Location loc,
                                     FunctionArgList &args) {
  CIRGenBuilderTy &builder = cgm.getBuilder();

  auto voidPtrArrayTy = cir::ArrayType::get(cgm.voidPtrTy, args.size());
  mlir::Value kernelArgs = builder.createAlloca(
      loc, cir::PointerType::get(voidPtrArrayTy), "kernel_args",
      CharUnits::fromQuantity(16));

  mlir::Value kernelArgsDecayed =
      builder.createCast(cir::CastKind::array_to_ptrdecay, kernelArgs,
                         cir::PointerType::get(cgm.voidPtrTy));

  for (const auto &[i, arg] : llvm::enumerate(args)) {
    mlir::Value index =
        builder.getConstInt(loc, llvm::APInt(/*numBits=*/32, i));
    mlir::Value storePos =
        builder.createPtrStride(loc, kernelArgsDecayed, index);
    mlir::Value argAddr = cgf.getAddrOfLocalVar(arg).getPointer();
    mlir::Value argAsVoid = builder.createBitcast(argAddr, cgm.voidPtrTy);

    builder.CIRBaseBuilderTy::createStore(loc, argAsVoid, storePos);
  }

  return kernelArgsDecayed;
}

void CIRGenOffloadRuntime::emitDeviceStub(CIRGenFunction &cgf, cir::FuncOp fn,
                                          FunctionArgList &args) {
  CIRGenBuilderTy &builder = cgm.getBuilder();
  mlir::Location loc = fn.getLoc();

  // 1. Pack arguments into void** array.
  mlir::Value kernelArgs = prepareKernelArgs(cgf, cgm, loc, args);

  // 2. Look up hipLaunchKernel declaration.
  std::string launchKernelName = "hipLaunchKernel";
  if (cgm.getLangOpts().GPUDefaultStream ==
      LangOptions::GPUDefaultStreamKind::PerThread)
    launchKernelName += "_spt";

  TranslationUnitDecl *tuDecl = cgm.getASTContext().getTranslationUnitDecl();
  DeclContext *dc = TranslationUnitDecl::castToDeclContext(tuDecl);

  const IdentifierInfo &launchII =
      cgm.getASTContext().Idents.get(launchKernelName);
  FunctionDecl *hipLaunchKernelFD = nullptr;
  for (NamedDecl *result : dc->lookup(&launchII)) {
    if (FunctionDecl *fd = dyn_cast<FunctionDecl>(result))
      hipLaunchKernelFD = fd;
  }

  if (!hipLaunchKernelFD) {
    cgm.error(cgf.curFuncDecl->getLocation(),
              "Can't find declaration for " + launchKernelName);
    return;
  }

  // 3. Allocate locals for __hipPopCallConfiguration outputs.
  mlir::Type dim3Ty = cgf.getTypes().convertType(
      hipLaunchKernelFD->getParamDecl(1)->getType());
  mlir::Type streamTy = cgf.getTypes().convertType(
      hipLaunchKernelFD->getParamDecl(5)->getType());

  mlir::Value gridDim =
      builder.createAlloca(loc, cir::PointerType::get(dim3Ty),
                           "grid_dim", CharUnits::fromQuantity(8));
  mlir::Value blockDim =
      builder.createAlloca(loc, cir::PointerType::get(dim3Ty),
                           "block_dim", CharUnits::fromQuantity(8));
  mlir::Value sharedMem =
      builder.createAlloca(loc, cir::PointerType::get(cgm.sizeTy),
                           "shared_mem", cgm.getSizeAlign());
  mlir::Value stream =
      builder.createAlloca(loc, cir::PointerType::get(streamTy),
                           "stream", cgm.getPointerAlign());

  // 4. Call __hipPopCallConfiguration to get launch dimensions.
  cir::FuncOp popConfig = cgm.createRuntimeFunction(
      cir::FuncType::get({gridDim.getType(), blockDim.getType(),
                          sharedMem.getType(), stream.getType()},
                         cgm.sInt32Ty),
      "__hipPopCallConfiguration");
  cgf.emitRuntimeCall(loc, popConfig, {gridDim, blockDim, sharedMem, stream});

  // 5. Get the stub function's own address as the kernel pointer.
  //    HIP runtime uses this address to look up the registered device kernel.
  cir::PointerType kernelTy =
      cir::PointerType::get(fn.getFunctionType());
  mlir::Value kernelVal =
      cir::GetGlobalOp::create(builder, loc, kernelTy, fn.getSymName());
  mlir::Value kernel = builder.createBitcast(kernelVal, cgm.voidPtrTy);

  // 6. Call hipLaunchKernel(func, gridDim, blockDim, args, sharedMem, stream).
  CallArgList launchArgs;
  launchArgs.add(RValue::get(kernel),
                 hipLaunchKernelFD->getParamDecl(0)->getType());
  launchArgs.add(
      RValue::getAggregate(Address(gridDim, CharUnits::fromQuantity(8))),
      hipLaunchKernelFD->getParamDecl(1)->getType());
  launchArgs.add(
      RValue::getAggregate(Address(blockDim, CharUnits::fromQuantity(8))),
      hipLaunchKernelFD->getParamDecl(2)->getType());
  launchArgs.add(RValue::get(kernelArgs),
                 hipLaunchKernelFD->getParamDecl(3)->getType());
  launchArgs.add(
      RValue::get(builder.CIRBaseBuilderTy::createLoad(loc, sharedMem)),
      hipLaunchKernelFD->getParamDecl(4)->getType());
  launchArgs.add(RValue::get(builder.CIRBaseBuilderTy::createLoad(loc, stream)),
                 hipLaunchKernelFD->getParamDecl(5)->getType());

  mlir::Type launchTy =
      cgm.getTypes().convertType(hipLaunchKernelFD->getType());
  mlir::Operation *hipKernelLauncherFn = cgm.createRuntimeFunction(
      cast<cir::FuncType>(launchTy), launchKernelName);
  const CIRGenFunctionInfo &callInfo =
      cgm.getTypes().arrangeFunctionDeclaration(hipLaunchKernelFD);
  cgf.emitCall(callInfo, CIRGenCallee::forDirect(hipKernelLauncherFn),
               ReturnValueSlot(), launchArgs);
}

mlir::Operation *CIRGenOffloadRuntime::getKernelHandle(cir::FuncOp fn,
                                                       GlobalDecl) {
  // In the offload model the kernel IS the function — no separate handle.
  return fn.getOperation();
}

RValue
CIRGenOffloadRuntime::emitCUDAKernelCallExpr(CIRGenFunction &cgf,
                                             const CUDAKernelCallExpr *expr,
                                             ReturnValueSlot retValue) {

  CIRGenBuilderTy &builder = cgm.getBuilder();
  mlir::Location loc =
      cgf.currSrcLoc ? cgf.currSrcLoc.value() : builder.getUnknownLoc();
  mlir::MLIRContext *ctx = builder.getContext();

  // Ensure gpu.container_module is set on the parent ModuleOp before any
  // gpu.launch_func is created; that op's verifier requires the attribute.
  cgm.getModule()->setAttr(mlir::gpu::GPUDialect::getContainerModuleAttrName(),
                           builder.getUnitAttr());

  // ------------------------------------------------------------------ //
  // 1. Extract the callee symbol name.
  // ------------------------------------------------------------------ //
  const FunctionDecl *kernelDecl =
      dyn_cast_or_null<FunctionDecl>(expr->getCalleeDecl());
  if (!kernelDecl) {
    // Try to resolve kernel from a function pointer variable's initializer.
    // Handles: const auto k = kernel_fn<T>; k<<<grid, block>>>(args);
    const Expr *callee = expr->getCallee()->IgnoreParenImpCasts();
    if (const auto *dre = dyn_cast<DeclRefExpr>(callee)) {
      if (const auto *vd = dyn_cast<VarDecl>(dre->getDecl())) {
        if (const Expr *init = vd->getInit()) {
          init = init->IgnoreParenImpCasts();
          if (const auto *initDRE = dyn_cast<DeclRefExpr>(init)) {
            kernelDecl =
                dyn_cast<FunctionDecl>(initDRE->getDecl());
          }
        }
      }
    }
  }
  if (!kernelDecl) {
    // The callee is not resolvable to a FunctionDecl.  Fall back to
    // the runtime-call path which can handle indirect callees.
    return CIRGenCUDARuntime::emitCUDAKernelCallExpr(cgf, expr, retValue);
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
    mlir::Value v;
    if (cgf.hasAggregateEvaluationKind(arg->getType())) {
      AggValueSlot slot = cgf.createAggTemp(arg->getType(), loc,
                                            "kernel.arg");
      cgf.emitAggExpr(arg, slot);
      v = builder.createLoad(loc, slot.getAddress());
    } else {
      v = cgf.emitScalarExpr(arg);
    }
    args.push_back(v);
    argTypes.push_back(v.getType());
  }

  // ------------------------------------------------------------------ //
  // 3. Extract grid, block dimensions and shared memory from the config call.
  //
  // The config call is __hipPushCallConfiguration(gridDim, blockDim,
  // sharedMem, stream), where gridDim and blockDim are dim3 struct values.
  // We extract the x, y, z fields and cast them to index.  The stream arg
  // is dropped for now (gpu.launch_func has no stream operand).
  // ------------------------------------------------------------------ //
  const CallExpr *config = expr->getConfig();
  mlir::Value gridX, gridY, gridZ, blockX, blockY, blockZ;
  mlir::Value shmemVal; // i32; empty = zero shmem

  if (config && config->getNumArgs() >= 2) {
    const Expr *gridArg = config->getArg(0);
    const Expr *blockArg = config->getArg(1);

    gridX = emitDim3Component(cgf, loc, gridArg, 0);
    gridY = emitDim3Component(cgf, loc, gridArg, 1);
    gridZ = emitDim3Component(cgf, loc, gridArg, 2);
    blockX = emitDim3Component(cgf, loc, blockArg, 0);
    blockY = emitDim3Component(cgf, loc, blockArg, 1);
    blockZ = emitDim3Component(cgf, loc, blockArg, 2);

    // Extract the shared memory argument (arg 2) if non-zero.
    if (config->getNumArgs() >= 3) {
      const Expr *shmemArg = config->getArg(2);
      Expr::EvalResult evalRes;
      bool isZeroShmem = false;
      if (shmemArg->EvaluateAsInt(evalRes, cgf.getContext()))
        isZeroShmem = evalRes.Val.getInt().isZero();
      if (!isZeroShmem) {
        // gpu.launch_func takes dynamicSharedMemorySize as i32.
        mlir::Value raw = cgf.emitScalarExpr(shmemArg);
        mlir::Type i32Ty = mlir::IntegerType::get(ctx, 32);
        shmemVal =
            mlir::UnrealizedConversionCastOp::create(
                builder, loc, mlir::TypeRange{i32Ty}, mlir::ValueRange{raw})
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
  // 4. Emit gpu.launch_func.
  //
  // Callee is a nested symbol ref: @offload_device_module::@kernelName.
  // ------------------------------------------------------------------ //
  mlir::SymbolRefAttr callee =
      mlir::SymbolRefAttr::get(ctx, "offload_device_module",
                               {mlir::FlatSymbolRefAttr::get(ctx, kernelName)});

  mlir::gpu::KernelDim3 gridDims{gridX, gridY, gridZ};
  mlir::gpu::KernelDim3 blockDims{blockX, blockY, blockZ};

  mlir::gpu::LaunchFuncOp::create(builder, loc, callee, gridDims, blockDims,
                                  shmemVal, mlir::ValueRange(args));

  return RValue::get(nullptr);
}

/// Return a CIR integer zero value to use as hipSuccess (0).
static mlir::Value makeHipSuccess(CIRGenBuilderTy &builder, mlir::Location loc,
                                  mlir::Type resultTy) {
  return builder.getConstantInt(loc, resultTy, 0);
}

std::optional<RValue>
CIRGenOffloadRuntime::emitCUDARuntimeCall(CIRGenFunction &cgf,
                                          const CallExpr *e) {
  // Only intercept plain function calls with a named callee.
  const FunctionDecl *fd = dyn_cast_or_null<FunctionDecl>(e->getCalleeDecl());
  if (!fd || !fd->getDeclName().isIdentifier())
    return std::nullopt;

  llvm::StringRef name = fd->getName();

  CIRGenBuilderTy &builder = cgm.getBuilder();
  mlir::Location loc =
      cgf.currSrcLoc ? cgf.currSrcLoc.value() : builder.getUnknownLoc();
  mlir::MLIRContext *ctx = builder.getContext();

  // ------------------------------------------------------------------ //
  // Host runtime calls — replaced with GPU dialect ops or direct HIP calls.
  //
  // Key helpers:
  //   gpu.alloc / gpu.dealloc / gpu.memcpy / gpu.memset — GPU memory ops.
  //   gpu.wait (non-async, no deps) — synchronize device.
  //   emitHIPCall() — for cases with no direct GPU dialect equivalent.
  // ------------------------------------------------------------------ //

  // hipDeviceSynchronize() — emit a direct cir.call; gpu.wait cannot be
  // lowered to LLVM IR on the host path without gpu-to-llvm pass.
  if (name == "hipDeviceSynchronize") {
    auto hipErrTy = cir::IntType::get(ctx, 32, /*isSigned=*/true);
    auto fnTy = cir::FuncType::get({}, hipErrTy);
    mlir::Value ret = emitHIPCall(cgf, loc, "hipDeviceSynchronize", fnTy, {});
    mlir::Type retTy = cgf.convertType(e->getType());
    if (ret && ret.getType() == retTy)
      return RValue::get(ret);
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // ------------------------------------------------------------------ //
  // Allocation: hipMalloc / hipHostMalloc / hipMallocManaged
  // Emit direct cir.call to the HIP runtime — the lowering pipeline handles
  // these the same way as any other runtime call.  Using gpu.alloc would
  // require a gpu-to-llvm lowering pass that we don't run on the host path.
  // ------------------------------------------------------------------ //
  if (name == "hipMalloc" || name == "hipHostMalloc" ||
      name == "hipMallocManaged") {
    mlir::Value outPtr = cgf.emitScalarExpr(e->getArg(0));
    mlir::Value sizeVal = cgf.emitScalarExpr(e->getArg(1));
    auto hipErrTy = cir::IntType::get(ctx, 32, /*isSigned=*/true);
    auto fnTy =
        cir::FuncType::get({outPtr.getType(), sizeVal.getType()}, hipErrTy);
    mlir::Value ret = emitHIPCall(cgf, loc, name, fnTy, {outPtr, sizeVal});
    mlir::Type retTy = cgf.convertType(e->getType());
    if (ret && ret.getType() == retTy)
      return RValue::get(ret);
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // ------------------------------------------------------------------ //
  // Deallocation / memory copy / memset operations.
  //
  // gpu.dealloc / gpu.memcpy / gpu.memset take MLIR memref types, but at
  // CIRGen time we only have CIR pointer types.  Reconstructing a typed
  // memref from an opaque CIR void* would fail MLIR verification.  Emit
  // direct cir.call to the HIP runtime for these operations instead — the
  // lowering pipeline (ConvertCIRToLLVMPass) will handle them just like any
  // other runtime call.
  // ------------------------------------------------------------------ //

  auto emitHIPRuntimeCall = [&](llvm::StringRef fn,
                                llvm::ArrayRef<mlir::Value> args) -> RValue {
    llvm::SmallVector<mlir::Type> argTys;
    for (auto a : args)
      argTys.push_back(a.getType());
    auto hipI32Ty = cir::IntType::get(ctx, 32, /*isSigned=*/true);
    auto fnTy = cir::FuncType::get(argTys, hipI32Ty);
    mlir::Value ret = emitHIPCall(cgf, loc, fn, fnTy, args);
    mlir::Type retTy = cgf.convertType(e->getType());
    // If the call result is i32 and the expected type matches, return it
    // directly; otherwise return hipSuccess (0).
    if (ret && ret.getType() == retTy)
      return RValue::get(ret);
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  };

  if (name == "hipFree" || name == "hipHostFree") {
    mlir::Value ptr = cgf.emitScalarExpr(e->getArg(0));
    return emitHIPRuntimeCall(name, {ptr});
  }

  if (name == "hipMemcpy" || name == "hipMemcpyAsync") {
    SmallVector<mlir::Value> args;
    for (unsigned i = 0; i < e->getNumArgs(); ++i)
      args.push_back(cgf.emitScalarExpr(e->getArg(i)));
    return emitHIPRuntimeCall(name, args);
  }

  if (name == "hipMemcpyToSymbol" || name == "hipMemcpyFromSymbol") {
    SmallVector<mlir::Value> args;
    for (unsigned i = 0; i < e->getNumArgs(); ++i)
      args.push_back(cgf.emitScalarExpr(e->getArg(i)));
    return emitHIPRuntimeCall(name, args);
  }

  if (name == "hipMemset" || name == "hipMemsetD16Async" ||
      name == "hipMemsetD32Async") {
    SmallVector<mlir::Value> args;
    for (unsigned i = 0; i < e->getNumArgs(); ++i)
      args.push_back(cgf.emitScalarExpr(e->getArg(i)));
    return emitHIPRuntimeCall(name, args);
  }

  // ------------------------------------------------------------------ //
  // Stream / event operations — emit direct calls to the HIP runtime API.
  // Stream and event types are represented as opaque CIR pointers on the
  // host side; no GPU dialect equivalent is available yet.
  // ------------------------------------------------------------------ //
  auto hipI32Ty = cir::IntType::get(ctx, 32, /*isSigned=*/true);

  // hipStreamCreate(hipStream_t *pStream) — no GPU dialect equivalent
  if (name == "hipStreamCreate") {
    mlir::Value pStream = cgf.emitScalarExpr(e->getArg(0));
    auto fnTy = cir::FuncType::get({pStream.getType()}, hipI32Ty);
    emitHIPCall(cgf, loc, "hipStreamCreate", fnTy, {pStream});
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipStreamDestroy(hipStream_t stream)
  if (name == "hipStreamDestroy") {
    mlir::Value stream = cgf.emitScalarExpr(e->getArg(0));
    auto fnTy = cir::FuncType::get({stream.getType()}, hipI32Ty);
    emitHIPCall(cgf, loc, "hipStreamDestroy", fnTy, {stream});
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipStreamSynchronize(hipStream_t stream)
  if (name == "hipStreamSynchronize") {
    mlir::Value stream = cgf.emitScalarExpr(e->getArg(0));
    auto fnTy = cir::FuncType::get({stream.getType()}, hipI32Ty);
    emitHIPCall(cgf, loc, "hipStreamSynchronize", fnTy, {stream});
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipEventCreate(hipEvent_t *pEvent)
  if (name == "hipEventCreate") {
    mlir::Value pEvent = cgf.emitScalarExpr(e->getArg(0));
    auto fnTy = cir::FuncType::get({pEvent.getType()}, hipI32Ty);
    emitHIPCall(cgf, loc, "hipEventCreate", fnTy, {pEvent});
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipEventDestroy(hipEvent_t event)
  if (name == "hipEventDestroy") {
    mlir::Value event = cgf.emitScalarExpr(e->getArg(0));
    auto fnTy = cir::FuncType::get({event.getType()}, hipI32Ty);
    emitHIPCall(cgf, loc, "hipEventDestroy", fnTy, {event});
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipEventRecord(hipEvent_t event, hipStream_t stream)
  if (name == "hipEventRecord") {
    mlir::Value event = cgf.emitScalarExpr(e->getArg(0));
    mlir::Value stream = cgf.emitScalarExpr(e->getArg(1));
    auto fnTy =
        cir::FuncType::get({event.getType(), stream.getType()}, hipI32Ty);
    emitHIPCall(cgf, loc, "hipEventRecord", fnTy, {event, stream});
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipEventSynchronize(hipEvent_t event)
  if (name == "hipEventSynchronize") {
    mlir::Value event = cgf.emitScalarExpr(e->getArg(0));
    auto fnTy = cir::FuncType::get({event.getType()}, hipI32Ty);
    emitHIPCall(cgf, loc, "hipEventSynchronize", fnTy, {event});
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  // hipStreamWaitEvent(hipStream_t stream, hipEvent_t event, unsigned flags)
  if (name == "hipStreamWaitEvent") {
    mlir::Value stream = cgf.emitScalarExpr(e->getArg(0));
    mlir::Value event = cgf.emitScalarExpr(e->getArg(1));
    mlir::Value flags = cgf.emitScalarExpr(e->getArg(2));
    auto fnTy = cir::FuncType::get(
        {stream.getType(), event.getType(), flags.getType()}, hipI32Ty);
    emitHIPCall(cgf, loc, "hipStreamWaitEvent", fnTy, {stream, event, flags});
    mlir::Type retTy = cgf.convertType(e->getType());
    return RValue::get(makeHipSuccess(builder, loc, retTy));
  }

  return std::nullopt;
}

void CIRGenOffloadRuntime::internalizeDeviceSideVar(
    const VarDecl *d, cir::GlobalLinkageKind &linkage) {
  if (cgm.getLangOpts().GPURelocatableDeviceCode)
    return;
  if (d->hasAttr<CUDADeviceAttr>() || d->hasAttr<CUDAConstantAttr>() ||
      d->hasAttr<CUDASharedAttr>()) {
    linkage = cir::GlobalLinkageKind::InternalLinkage;
  }
}

std::string CIRGenOffloadRuntime::getDeviceSideName(const NamedDecl *nd) {
  GlobalDecl gd;
  if (auto *fd = dyn_cast<FunctionDecl>(nd))
    gd = GlobalDecl(fd, KernelReferenceKind::Kernel);
  else
    gd = GlobalDecl(nd);
  return cgm.getMangledName(gd).str();
}

CIRGenCUDARuntime *clang::CIRGen::createOffloadRuntime(CIRGenModule &cgm) {
  return new CIRGenOffloadRuntime(cgm);
}
