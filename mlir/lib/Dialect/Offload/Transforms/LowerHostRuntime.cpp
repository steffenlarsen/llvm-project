//===- LowerHostRuntime.cpp - Lower offload host runtime ops to calls -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Lowers host-side offload runtime operations to func.call ops targeting the
// HIP or CUDA runtime API.  The !offload.stream / !offload.event opaque types
// are lowered to i64 (pointer-sized integer handles).
//
//   offload.stream_create  → func.call @hipStreamCreate (i64* alloca pattern)
//   offload.stream_destroy → func.call @hipStreamDestroy(%handle : i64)
//   offload.stream_sync    → func.call @hipStreamSynchronize(%handle : i64)
//   offload.device_sync    → func.call @hipDeviceSynchronize()
//   offload.malloc         → @hipMalloc / @hipHostMalloc / @hipMallocManaged
//   offload.free           → @hipFree / @hipHostFree
//   offload.memcpy         → @hipMemcpy (kind reconstructed from dst/src
//   spaces) offload.memcpy_to_symbol → llvm.mlir.addressof + func.call
//   @hipMemcpy offload.memset         → @hipMemset / @hipMemsetD16 /
//   @hipMemsetD32
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/Offload/Transforms/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/GPU/IR/GPUDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/Offload/IR/OffloadDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"

namespace mlir {
namespace offload {

#define GEN_PASS_DEF_OFFLOADLOWERHOSTRUNTIMEPASS
#include "mlir/Dialect/Offload/Transforms/Passes.h.inc"

} // namespace offload
} // namespace mlir

using namespace mlir;
using namespace mlir::offload;

namespace {

/// We lower !offload.stream / !offload.event to !llvm.ptr.
///
/// hipStream_t = ihipStream_t* and hipEvent_t = ihipEvent_t* are pointer
/// types in the HIP ABI.  Using !llvm.ptr makes the unrealized_conversion_cast
/// chain reconcilable by ConvertCIRToLLVMPass:
///   !cir.ptr<T> → !offload.stream → !cir.ptr<T>  becomes identity.
static Type getHandleType(MLIRContext *ctx) {
  return LLVM::LLVMPointerType::get(ctx);
}

/// Return the name of the runtime function to call, selecting HIP or CUDA.
static StringRef pick(StringRef hip, StringRef cuda, bool useHip) {
  return useHip ? hip : cuda;
}

/// Ensure a func.func declaration named `name` with type `fnType` exists in
/// `module`. Returns the (potentially newly inserted) declaration.
static func::FuncOp getOrInsertDecl(OpBuilder &b, ModuleOp module,
                                    StringRef name, FunctionType fnType) {
  if (auto f = module.lookupSymbol<func::FuncOp>(name))
    return f;
  OpBuilder::InsertionGuard guard(b);
  b.setInsertionPointToStart(module.getBody());
  auto decl = func::FuncOp::create(b, module.getLoc(), name, fnType);
  decl.setPrivate();
  return decl;
}

/// Ensure an llvm.func declaration named `name` with type `fnType` exists in
/// `module`. Returns the (potentially newly inserted) declaration.
static LLVM::LLVMFuncOp getOrInsertLLVMDecl(OpBuilder &b, ModuleOp module,
                                             StringRef name,
                                             LLVM::LLVMFunctionType fnType) {
  if (auto f = module.lookupSymbol<LLVM::LLVMFuncOp>(name))
    return f;
  OpBuilder::InsertionGuard guard(b);
  b.setInsertionPointToStart(module.getBody());
  auto decl =
      LLVM::LLVMFuncOp::create(b, module.getLoc(), name, fnType);
  decl.setLinkage(LLVM::Linkage::External);
  return decl;
}

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct LowerHostRuntimePass
    : offload::impl::OffloadLowerHostRuntimePassBase<LowerHostRuntimePass> {

  using OffloadLowerHostRuntimePassBase::OffloadLowerHostRuntimePassBase;

  void runOnOperation() override {
    ModuleOp module = getOperation();
    MLIRContext *ctx = module.getContext();
    OpBuilder b(ctx);

    const bool useHip = (runtime != "cuda");
    Type i32Ty = b.getI32Type();
    Type i64Ty = b.getI64Type();
    Type handleTy = getHandleType(ctx); // !llvm.ptr

    // offload.stream_destroy → @hip/cudaStreamDestroy(ptr) -> i32
    module.walk([&](offload::StreamDestroyOp op) {
      b.setInsertionPoint(op);
      StringRef fn = pick("hipStreamDestroy", "cudaStreamDestroy", useHip);
      auto decl = getOrInsertLLVMDecl(
          b, module, fn,
          LLVM::LLVMFunctionType::get(i32Ty, {handleTy}));
      // Cast !offload.stream → !llvm.ptr via unrealized_conversion_cast.
      auto cast = UnrealizedConversionCastOp::create(
          b, op.getLoc(), TypeRange{handleTy}, ValueRange{op.getStream()});
      LLVM::CallOp::create(b, op.getLoc(), decl, ValueRange{cast.getResult(0)});
      op.erase();
    });

    // offload.stream_sync → @hip/cudaStreamSynchronize(ptr) -> i32
    module.walk([&](offload::StreamSyncOp op) {
      b.setInsertionPoint(op);
      StringRef fn =
          pick("hipStreamSynchronize", "cudaStreamSynchronize", useHip);
      auto decl = getOrInsertLLVMDecl(
          b, module, fn,
          LLVM::LLVMFunctionType::get(i32Ty, {handleTy}));
      auto cast = UnrealizedConversionCastOp::create(
          b, op.getLoc(), TypeRange{handleTy}, ValueRange{op.getStream()});
      LLVM::CallOp::create(b, op.getLoc(), decl, ValueRange{cast.getResult(0)});
      op.erase();
    });

    // offload.device_sync → @hip/cudaDeviceSynchronize() -> i32
    module.walk([&](offload::DeviceSyncOp op) {
      b.setInsertionPoint(op);
      StringRef fn =
          pick("hipDeviceSynchronize", "cudaDeviceSynchronize", useHip);
      auto decl =
          getOrInsertDecl(b, module, fn, b.getFunctionType({}, {i32Ty}));
      func::CallOp::create(b, op.getLoc(), decl, ValueRange{});
      op.erase();
    });

    // offload.stream_create → @hip/cudaStreamCreate(ptr*) -> i32
    //
    // HIP/CUDA StreamCreate writes the new handle through a pointer argument:
    //   hipError_t hipStreamCreate(hipStream_t *pStream);
    // hipStream_t is a pointer type, so we use !llvm.ptr for the slot.
    // We model this with LLVM dialect ops:
    //   1. llvm.alloca 1 x !llvm.ptr  — stack slot for the handle
    //   2. func.call @hipStreamCreate(%slot)
    //   3. llvm.load !llvm.ptr from %slot — read back the written handle
    //   4. unrealized_cast !llvm.ptr → !offload.stream
    module.walk([&](offload::StreamCreateOp op) {
      b.setInsertionPoint(op);
      mlir::Location loc = op.getLoc();
      StringRef fn = pick("hipStreamCreate", "cudaStreamCreate", useHip);

      // Pointer type for the slot and the loaded handle.
      Type llvmPtrTy = LLVM::LLVMPointerType::get(b.getContext());
      // Alloca 1 element of !llvm.ptr to hold the stream handle.
      Value one =
          LLVM::ConstantOp::create(b, loc, i64Ty, b.getIntegerAttr(i64Ty, 1))
              .getResult();
      Value slot = LLVM::AllocaOp::create(b, loc, llvmPtrTy, llvmPtrTy, one,
                                          /*alignment=*/8)
                       .getResult();
      // Declare @hipStreamCreate(ptr) -> i32 as llvm.func (arg is !llvm.ptr).
      auto decl = getOrInsertLLVMDecl(
          b, module, fn,
          LLVM::LLVMFunctionType::get(i32Ty, {llvmPtrTy}));
      LLVM::CallOp::create(b, loc, decl, ValueRange{slot});
      // Load the written handle back as !llvm.ptr.
      Value handle = LLVM::LoadOp::create(b, loc, llvmPtrTy, slot).getResult();
      // Cast !llvm.ptr → !offload.stream (identity in ConvertCIRToLLVMPass).
      auto backcast = UnrealizedConversionCastOp::create(
          b, loc, TypeRange{op.getType()}, ValueRange{handle});
      op.getResult().replaceAllUsesWith(backcast.getResult(0));
      op.erase();
    });

    // offload.malloc(size, alloc_type) → runtime malloc variant
    //
    // hipMalloc / hipHostMalloc / hipMallocManaged each write the allocated
    // pointer through a void** argument.  We model this with LLVM
    // alloca/call/load:
    //   1. llvm.alloca 1 x i64  — slot for the returned pointer
    //   2. llvm.call @hip{Malloc|HostMalloc|MallocManaged}(%slot, %size[,
    //   flags])
    //   3. llvm.load %slot        — the allocated pointer (i64)
    module.walk([&](offload::MallocOp op) {
      b.setInsertionPoint(op);
      mlir::Location loc = op.getLoc();
      Type llvmPtrTy = LLVM::LLVMPointerType::get(b.getContext());
      Value sizeAsI64 = UnrealizedConversionCastOp::create(
                            b, loc, TypeRange{i64Ty}, ValueRange{op.getSize()})
                            .getResult(0);
      Value one =
          LLVM::ConstantOp::create(b, loc, i64Ty, b.getIntegerAttr(i64Ty, 1))
              .getResult();
      Value slot = LLVM::AllocaOp::create(b, loc, llvmPtrTy, i64Ty, one,
                                          /*alignment=*/8)
                       .getResult();

      offload::AllocType allocTy = op.getAllocType();
      if (!useHip || allocTy == offload::AllocType::device) {
        // hipMalloc(void**, size_t) / cudaMalloc(void**, size_t)
        StringRef fn = pick("hipMalloc", "cudaMalloc", useHip);
        auto decl = getOrInsertDecl(
            b, module, fn, b.getFunctionType({llvmPtrTy, i64Ty}, {i32Ty}));
        func::CallOp::create(b, loc, decl, ValueRange{slot, sizeAsI64});
      } else if (allocTy == offload::AllocType::host) {
        // hipHostMalloc(void**, size_t, unsigned flags)
        Type u32Ty = b.getIntegerType(32, /*isSigned=*/false);
        Value flags =
            LLVM::ConstantOp::create(b, loc, u32Ty, b.getIntegerAttr(u32Ty, 0))
                .getResult();
        auto decl = getOrInsertDecl(
            b, module, "hipHostMalloc",
            b.getFunctionType({llvmPtrTy, i64Ty, u32Ty}, {i32Ty}));
        func::CallOp::create(b, loc, decl, ValueRange{slot, sizeAsI64, flags});
      } else {
        // hipMallocManaged(void**, size_t, unsigned flags)
        // hipMemAttachGlobal = 1
        Type u32Ty = b.getIntegerType(32, /*isSigned=*/false);
        Value flags =
            LLVM::ConstantOp::create(b, loc, u32Ty, b.getIntegerAttr(u32Ty, 1))
                .getResult();
        auto decl = getOrInsertDecl(
            b, module, "hipMallocManaged",
            b.getFunctionType({llvmPtrTy, i64Ty, u32Ty}, {i32Ty}));
        func::CallOp::create(b, loc, decl, ValueRange{slot, sizeAsI64, flags});
      }

      Value ptr = LLVM::LoadOp::create(b, loc, i64Ty, slot).getResult();
      Value cast =
          UnrealizedConversionCastOp::create(
              b, loc, TypeRange{op.getResult().getType()}, ValueRange{ptr})
              .getResult(0);
      op.getResult().replaceAllUsesWith(cast);
      op.erase();
    });

    // offload.free(ptr, alloc_type) → hipFree / hipHostFree
    module.walk([&](offload::FreeOp op) {
      b.setInsertionPoint(op);
      mlir::Location loc = op.getLoc();
      Type llvmPtrTy = LLVM::LLVMPointerType::get(b.getContext());
      Value ptr = UnrealizedConversionCastOp::create(
                      b, loc, TypeRange{llvmPtrTy}, ValueRange{op.getPtr()})
                      .getResult(0);

      // host-pinned memory uses hipHostFree; device and managed use hipFree.
      StringRef fn;
      if (useHip && op.getAllocType() == offload::AllocType::host)
        fn = "hipHostFree";
      else
        fn = pick("hipFree", "cudaFree", useHip);

      auto decl = getOrInsertDecl(b, module, fn,
                                  b.getFunctionType({llvmPtrTy}, {i32Ty}));
      func::CallOp::create(b, loc, decl, ValueRange{ptr});
      op.erase();
    });

    // offload.memcpy(dst, src, size, dst_space, src_space[, stream])
    //   → @hipMemcpy(dst, src, size, kind)        when no stream
    //   → @hipMemcpyAsync(dst, src, size, kind, stream) when stream present
    //
    // Reconstruct the hipMemcpyKind integer from (dst_space, src_space):
    //   host←host   = 0  (HostToHost)
    //   device←host = 1  (HostToDevice)
    //   host←device = 2  (DeviceToHost)
    //   device←dev  = 3  (DeviceToDevice)
    //   managed/any = 4  (Default)
    module.walk([&](offload::MemcpyOp op) {
      b.setInsertionPoint(op);
      mlir::Location loc = op.getLoc();
      Type llvmPtrTy = LLVM::LLVMPointerType::get(b.getContext());
      Value dst = UnrealizedConversionCastOp::create(
                      b, loc, TypeRange{llvmPtrTy}, ValueRange{op.getDst()})
                      .getResult(0);
      Value src = UnrealizedConversionCastOp::create(
                      b, loc, TypeRange{llvmPtrTy}, ValueRange{op.getSrc()})
                      .getResult(0);
      Value sizeAsI64 = UnrealizedConversionCastOp::create(
                            b, loc, TypeRange{i64Ty}, ValueRange{op.getSize()})
                            .getResult(0);

      // Derive hipMemcpyKind from the per-pointer allocation spaces.
      int32_t kindInt = 4; // Default
      offload::AllocType dstSp = op.getDstSpace();
      offload::AllocType srcSp = op.getSrcSpace();
      if (dstSp != offload::AllocType::managed &&
          srcSp != offload::AllocType::managed) {
        bool dstDev = (dstSp == offload::AllocType::device);
        bool srcDev = (srcSp == offload::AllocType::device);
        if (!dstDev && !srcDev)
          kindInt = 0; // HostToHost
        else if (dstDev && !srcDev)
          kindInt = 1; // HostToDevice
        else if (!dstDev && srcDev)
          kindInt = 2; // DeviceToHost
        else
          kindInt = 3; // DeviceToDevice
      }

      Value kindVal = LLVM::ConstantOp::create(b, loc, i32Ty,
                                               b.getIntegerAttr(i32Ty, kindInt))
                          .getResult();

      Value streamVal = op.getStream();
      // Track the chain of unrealized casts producing the stream so we can
      // erase them if they become dead after the memcpy op is erased.
      //
      // The stream operand chain is typically:
      //   raw (!cir.ptr<...>) → [cast → !offload.stream] → [cast → i64]
      //
      // We want to pass the stream as an opaque !llvm.ptr to the HIP runtime
      // (hipStream_t is a pointer, not an integer).  Using !llvm.ptr avoids
      // the problematic !cir.ptr → !offload.stream → i64 chain that
      // ConvertCIRToLLVMPass cannot reconcile automatically.
      //
      // Look through the !offload.stream unrealized cast to get the raw
      // pointer value and cast it directly to !llvm.ptr.
      Type llvmPtrTyLocal = LLVM::LLVMPointerType::get(b.getContext());
      Operation *streamDefOp = streamVal ? streamVal.getDefiningOp() : nullptr;
      if (streamVal) {
        // Async path: hipMemcpyAsync(dst, src, size, kind, stream)
        StringRef fn = pick("hipMemcpyAsync", "cudaMemcpyAsync", useHip);
        // Obtain the raw value underlying the !offload.stream cast.
        Value rawStream = streamVal;
        if (auto cast = streamVal.getDefiningOp<UnrealizedConversionCastOp>())
          if (cast.getInputs().size() == 1)
            rawStream = cast.getInputs()[0];
        // Cast to !llvm.ptr: hipStream_t is a pointer type in the HIP ABI.
        Value streamPtr =
            UnrealizedConversionCastOp::create(b, loc, TypeRange{llvmPtrTyLocal},
                                               ValueRange{rawStream})
                .getResult(0);
        // Use LLVM dialect call — the signature contains !llvm.ptr which is not
        // a valid func.func argument type.
        auto decl = getOrInsertLLVMDecl(
            b, module, fn,
            LLVM::LLVMFunctionType::get(i32Ty,
                {llvmPtrTyLocal, llvmPtrTyLocal, i64Ty, i32Ty, llvmPtrTyLocal}));
        LLVM::CallOp::create(b, loc, decl,
                             ValueRange{dst, src, sizeAsI64, kindVal, streamPtr});
      } else {
        // Synchronous path: hipMemcpy(dst, src, size, kind)
        StringRef fn = pick("hipMemcpy", "cudaMemcpy", useHip);
        auto decl = getOrInsertDecl(
            b, module, fn,
            b.getFunctionType({llvmPtrTy, llvmPtrTy, i64Ty, i32Ty}, {i32Ty}));
        func::CallOp::create(b, loc, decl,
                             ValueRange{dst, src, sizeAsI64, kindVal});
      }
      op.erase();
      // Erase the dead !offload.stream unrealized_conversion_cast if it became
      // dead after the memcpy op was erased.
      if (streamDefOp && streamDefOp->use_empty())
        streamDefOp->erase();
    });

    // offload.memcpy_to_symbol @sym src = %src count = %count
    //   → @hip/cudaMemcpy(llvm.mlir.addressof @sym, src_ptr, count_i64,
    //                     hipMemcpyHostToDevice)
    //
    // We obtain the device symbol address via LLVM::AddressOfOp, which emits
    // an `llvm.mlir.addressof @sym : !llvm.ptr` referencing the
    // llvm.mlir.global that LowerSharedGlobalsPass placed in the gpu.module.
    module.walk([&](offload::MemcpyToSymbolOp op) {
      b.setInsertionPoint(op);
      mlir::Location loc = op.getLoc();
      Type llvmPtrTy = LLVM::LLVMPointerType::get(b.getContext());

      // Get the address of the device-side global symbol.
      Value symPtr =
          LLVM::AddressOfOp::create(b, loc, llvmPtrTy, op.getSymbol())
              .getResult();

      // Cast src (AnyType → !llvm.ptr).
      Value src = UnrealizedConversionCastOp::create(
                      b, loc, TypeRange{llvmPtrTy}, ValueRange{op.getSrc()})
                      .getResult(0);

      // Cast count (Index → i64).
      Value sizeAsI64 = UnrealizedConversionCastOp::create(
                            b, loc, TypeRange{i64Ty}, ValueRange{op.getCount()})
                            .getResult(0);

      // hipMemcpyKind::hipMemcpyHostToDevice = 1.
      Value kindVal =
          LLVM::ConstantOp::create(b, loc, i32Ty, b.getIntegerAttr(i32Ty, 1))
              .getResult();

      StringRef fn = pick("hipMemcpy", "cudaMemcpy", useHip);
      auto decl = getOrInsertDecl(
          b, module, fn,
          b.getFunctionType({llvmPtrTy, llvmPtrTy, i64Ty, i32Ty}, {i32Ty}));
      func::CallOp::create(b, loc, decl,
                           ValueRange{symPtr, src, sizeAsI64, kindVal});
      op.erase();
    });

    // offload.memcpy_from_symbol @sym dst = %dst count = %count
    //   → @hip/cudaMemcpy(dst_ptr, llvm.mlir.addressof @sym, count_i64,
    //                     hipMemcpyDeviceToHost)
    //
    // Symmetric counterpart of memcpy_to_symbol; direction reversed.
    module.walk([&](offload::MemcpyFromSymbolOp op) {
      b.setInsertionPoint(op);
      mlir::Location loc = op.getLoc();
      Type llvmPtrTy = LLVM::LLVMPointerType::get(b.getContext());

      // Cast dst (AnyType → !llvm.ptr).
      Value dst = UnrealizedConversionCastOp::create(
                      b, loc, TypeRange{llvmPtrTy}, ValueRange{op.getDst()})
                      .getResult(0);

      // Get the address of the device-side global symbol.
      Value symPtr =
          LLVM::AddressOfOp::create(b, loc, llvmPtrTy, op.getSymbol())
              .getResult();

      // Cast count (Index → i64).
      Value sizeAsI64 = UnrealizedConversionCastOp::create(
                            b, loc, TypeRange{i64Ty}, ValueRange{op.getCount()})
                            .getResult(0);

      // hipMemcpyKind::hipMemcpyDeviceToHost = 2.
      Value kindVal =
          LLVM::ConstantOp::create(b, loc, i32Ty, b.getIntegerAttr(i32Ty, 2))
              .getResult();

      StringRef fn = pick("hipMemcpy", "cudaMemcpy", useHip);
      auto decl = getOrInsertDecl(
          b, module, fn,
          b.getFunctionType({llvmPtrTy, llvmPtrTy, i64Ty, i32Ty}, {i32Ty}));
      func::CallOp::create(b, loc, decl,
                           ValueRange{dst, symPtr, sizeAsI64, kindVal});
      op.erase();
    });

    // offload.memset(ptr, pattern, size_bytes[, stream])
    //   → @hipMemset(dst, i32, size)          when i8 pattern, no stream
    //   → @hipMemsetD16(dst, i16, count)      when i16 pattern, no stream
    //   → @hipMemsetD32(dst, i32, count)      when i32 pattern, no stream
    //   → @hipMemsetD32Async(dst, i32, count, stream)  when i32 pattern + stream
    //
    // Note: HIP has no hipMemsetAsync (byte) or hipMemsetD16Async; those fall
    // back to the synchronous variants.  Only D32Async is widely available.
    //
    // For a future liboffload target: stack-allocate $pattern, pass its address
    // and sizeof(type($pattern)) to olMemFill — no changes needed to this op.
    module.walk([&](offload::MemsetOp op) {
      b.setInsertionPoint(op);
      mlir::Location loc = op.getLoc();
      Type llvmPtrTy = LLVM::LLVMPointerType::get(b.getContext());
      // Note: dst is intentionally NOT created here unconditionally.
      // op.getPtr() may be !u64i (ptr_to_int result) for hipDeviceptr_t, which
      // cannot be cast to !llvm.ptr via UnrealizedConversionCastOp.  Each
      // branch below creates the correct dst representation.
      Value sizeAsI64 = UnrealizedConversionCastOp::create(
                            b, loc, TypeRange{i64Ty}, ValueRange{op.getSize()})
                            .getResult(0);

      // Determine pattern width from the IR type.
      unsigned patWidth = 8;
      if (auto intTy = dyn_cast<IntegerType>(op.getPattern().getType()))
        patWidth = intTy.getWidth();

      Value stream = op.getStream();
      Operation *streamDefOp = stream ? stream.getDefiningOp() : nullptr;
      // hipStream_t is a pointer in the HIP ABI; use !llvm.ptr to avoid an
      // unresolvable !cir.ptr → !offload.stream → i64 cast chain.
      Type llvmPtrTyLocal = LLVM::LLVMPointerType::get(b.getContext());
      Value streamPtr;
      if (stream) {
        // Look through the !offload.stream unrealized cast to the raw value.
        Value rawStream = stream;
        if (auto cast = stream.getDefiningOp<UnrealizedConversionCastOp>())
          if (cast.getInputs().size() == 1)
            rawStream = cast.getInputs()[0];
        streamPtr =
            UnrealizedConversionCastOp::create(b, loc, TypeRange{llvmPtrTyLocal},
                                               ValueRange{rawStream})
                .getResult(0);
      }

      if (patWidth <= 8) {
        // hipMemset(void*, int, size_t): byte pattern, size in bytes.
        // No async variant available — always synchronous.
        Value dst = UnrealizedConversionCastOp::create(
                        b, loc, TypeRange{llvmPtrTy}, ValueRange{op.getPtr()})
                        .getResult(0);
        Value patI32 =
            UnrealizedConversionCastOp::create(b, loc, TypeRange{i32Ty},
                                               ValueRange{op.getPattern()})
                .getResult(0);
        StringRef fn = pick("hipMemset", "cudaMemset", useHip);
        auto decl = getOrInsertDecl(
            b, module, fn,
            b.getFunctionType({llvmPtrTy, i32Ty, i64Ty}, {i32Ty}));
        func::CallOp::create(b, loc, decl, ValueRange{dst, patI32, sizeAsI64});
      } else if (patWidth == 16) {
        // hipMemsetD16: count in elements. No async variant — always sync.
        Value dst = UnrealizedConversionCastOp::create(
                        b, loc, TypeRange{llvmPtrTy}, ValueRange{op.getPtr()})
                        .getResult(0);
        Value two =
            LLVM::ConstantOp::create(b, loc, i64Ty, b.getIntegerAttr(i64Ty, 2))
                .getResult();
        Value count = LLVM::SDivOp::create(b, loc, i64Ty, sizeAsI64, two,
                                           /*isExact=*/mlir::UnitAttr{});
        Type i16Ty = b.getIntegerType(16);
        Value patI16 =
            UnrealizedConversionCastOp::create(b, loc, TypeRange{i16Ty},
                                               ValueRange{op.getPattern()})
                .getResult(0);
        StringRef fn = pick("hipMemsetD16", "cuMemsetD16", useHip);
        auto decl = getOrInsertDecl(
            b, module, fn,
            b.getFunctionType({llvmPtrTy, i16Ty, i64Ty}, {i32Ty}));
        func::CallOp::create(b, loc, decl, ValueRange{dst, patI16, count});
      } else {
        // hipMemsetD32 / hipMemsetD32Async: count in elements.
        Value four =
            LLVM::ConstantOp::create(b, loc, i64Ty, b.getIntegerAttr(i64Ty, 4))
                .getResult();
        Value count = LLVM::SDivOp::create(b, loc, i64Ty, sizeAsI64, four,
                                           /*isExact=*/mlir::UnitAttr{});
        Value patI32 =
            UnrealizedConversionCastOp::create(b, loc, TypeRange{i32Ty},
                                               ValueRange{op.getPattern()})
                .getResult(0);
        if (stream && useHip) {
          // hipMemsetD32Async(hipDeviceptr_t, unsigned int, size_t, stream)
          // Use LLVM dialect call — signature contains !llvm.ptr.
          // op.getPtr() is !u64i (ptr_to_int result); convert via inttoptr.
          Value dstI64 = UnrealizedConversionCastOp::create(
                             b, loc, TypeRange{i64Ty}, ValueRange{op.getPtr()})
                             .getResult(0);
          Value dstPtr = LLVM::IntToPtrOp::create(b, loc, llvmPtrTyLocal, dstI64)
                             .getResult();
          auto decl = getOrInsertLLVMDecl(
              b, module, "hipMemsetD32Async",
              LLVM::LLVMFunctionType::get(
                  i32Ty, {llvmPtrTyLocal, i32Ty, i64Ty, llvmPtrTyLocal}));
          LLVM::CallOp::create(b, loc, decl,
                               ValueRange{dstPtr, patI32, count, streamPtr});
        } else {
          Value dst = UnrealizedConversionCastOp::create(
                          b, loc, TypeRange{llvmPtrTy}, ValueRange{op.getPtr()})
                          .getResult(0);
          StringRef fn = pick("hipMemsetD32", "cuMemsetD32", useHip);
          auto decl = getOrInsertDecl(
              b, module, fn,
              b.getFunctionType({llvmPtrTy, i32Ty, i64Ty}, {i32Ty}));
          func::CallOp::create(b, loc, decl, ValueRange{dst, patI32, count});
        }
      }
      op.erase();
      // Erase the dead !offload.stream unrealized_conversion_cast if it became
      // dead after the memset op was erased.
      if (streamDefOp && streamDefOp->use_empty())
        streamDefOp->erase();
    });

    // offload.event_destroy → @hip/cudaEventDestroy(ptr) -> i32
    module.walk([&](offload::EventDestroyOp op) {
      b.setInsertionPoint(op);
      StringRef fn = pick("hipEventDestroy", "cudaEventDestroy", useHip);
      auto decl = getOrInsertLLVMDecl(
          b, module, fn,
          LLVM::LLVMFunctionType::get(i32Ty, {handleTy}));
      auto cast = UnrealizedConversionCastOp::create(
          b, op.getLoc(), TypeRange{handleTy}, ValueRange{op.getEvent()});
      LLVM::CallOp::create(b, op.getLoc(), decl, ValueRange{cast.getResult(0)});
      op.erase();
    });

    // offload.event_wait → @hip/cudaEventSynchronize(ptr) -> i32
    module.walk([&](offload::EventWaitOp op) {
      b.setInsertionPoint(op);
      StringRef fn = pick("hipEventSynchronize", "cudaEventSynchronize", useHip);
      auto decl = getOrInsertLLVMDecl(
          b, module, fn,
          LLVM::LLVMFunctionType::get(i32Ty, {handleTy}));
      auto cast = UnrealizedConversionCastOp::create(
          b, op.getLoc(), TypeRange{handleTy}, ValueRange{op.getEvent()});
      LLVM::CallOp::create(b, op.getLoc(), decl, ValueRange{cast.getResult(0)});
      op.erase();
    });

    // offload.event_record → @hip/cudaEventRecord(ptr event, ptr stream) -> i32
    module.walk([&](offload::EventRecordOp op) {
      b.setInsertionPoint(op);
      mlir::Location loc = op.getLoc();
      StringRef fn = pick("hipEventRecord", "cudaEventRecord", useHip);
      auto decl = getOrInsertLLVMDecl(
          b, module, fn,
          LLVM::LLVMFunctionType::get(i32Ty, {handleTy, handleTy}));
      Value eventPtr =
          UnrealizedConversionCastOp::create(b, loc, TypeRange{handleTy},
                                             ValueRange{op.getEvent()})
              .getResult(0);
      Value streamPtr;
      if (op.getStream()) {
        streamPtr =
            UnrealizedConversionCastOp::create(b, loc, TypeRange{handleTy},
                                               ValueRange{op.getStream()})
                .getResult(0);
      } else {
        // Null pointer for the default stream.
        streamPtr = LLVM::ZeroOp::create(b, loc, handleTy).getResult();
      }
      LLVM::CallOp::create(b, loc, decl, ValueRange{eventPtr, streamPtr});
      op.erase();
    });

    // offload.stream_wait_event → @hip/cudaStreamWaitEvent(ptr, ptr, i32) -> i32
    module.walk([&](offload::StreamWaitEventOp op) {
      b.setInsertionPoint(op);
      mlir::Location loc = op.getLoc();
      StringRef fn =
          pick("hipStreamWaitEvent", "cudaStreamWaitEvent", useHip);
      auto decl = getOrInsertLLVMDecl(
          b, module, fn,
          LLVM::LLVMFunctionType::get(i32Ty, {handleTy, handleTy, i32Ty}));
      Value streamPtr =
          UnrealizedConversionCastOp::create(
              b, loc, TypeRange{handleTy}, ValueRange{op.getStream()})
              .getResult(0);
      Value eventPtr =
          UnrealizedConversionCastOp::create(
              b, loc, TypeRange{handleTy}, ValueRange{op.getEvent()})
              .getResult(0);
      Value flagsVal =
          LLVM::ConstantOp::create(b, loc, i32Ty,
                                   b.getIntegerAttr(i32Ty, op.getFlags()))
              .getResult();
      LLVM::CallOp::create(b, loc, decl,
                           ValueRange{streamPtr, eventPtr, flagsVal});
      op.erase();
    });

    // offload.event_create → @hip/cudaEventCreate(ptr) -> i32
    //
    // hipEvent_t is a pointer type; use !llvm.ptr for the slot and handle.
    module.walk([&](offload::EventCreateOp op) {
      b.setInsertionPoint(op);
      mlir::Location loc = op.getLoc();
      StringRef fn = pick("hipEventCreate", "cudaEventCreate", useHip);
      Type llvmPtrTy2 = LLVM::LLVMPointerType::get(ctx);
      Value one64 =
          LLVM::ConstantOp::create(b, loc, i64Ty, b.getIntegerAttr(i64Ty, 1))
              .getResult();
      Value slot = LLVM::AllocaOp::create(b, loc, llvmPtrTy2, llvmPtrTy2,
                                          one64, /*alignment=*/8)
                       .getResult();
      // Use llvm.func / llvm.call since the arg type is !llvm.ptr.
      auto decl = getOrInsertLLVMDecl(
          b, module, fn,
          LLVM::LLVMFunctionType::get(i32Ty, {llvmPtrTy2}));
      LLVM::CallOp::create(b, loc, decl, ValueRange{slot});
      // Load handle as !llvm.ptr (hipEvent_t is a pointer).
      Value handle = LLVM::LoadOp::create(b, loc, llvmPtrTy2, slot).getResult();
      auto backcast = UnrealizedConversionCastOp::create(
          b, loc, TypeRange{op.getType()}, ValueRange{handle});
      op.getResult().replaceAllUsesWith(backcast.getResult(0));
      op.erase();
    });

    // offload.kernel_launch with stream → mgpuModuleGetFunction + mgpuLaunchKernel
    //
    // Stream-aware launches remain in CIR functions (not lowered by
    // SplitSingleSource). We cannot emit LLVM dialect ops directly inside CIR
    // function bodies because ConvertCIRToLLVMPass expects pure CIR there.
    //
    // Strategy: for each stream-aware kernel launch, emit a module-level
    // llvm.func @__offload_launch_{kernelName}_stream(stream_i64, gx, gy, gz,
    //   bx, by, bz, shmem_i32, arg0_i64, ..., argN_i64) → void
    // containing the mgpu API sequence, then replace the offload.kernel_launch
    // with a func.call to that helper (func.call is handled by
    // ConvertCIRToLLVMPass correctly).
    //
    // mgpuLaunchKernel signature (from SelectObjectAttr.cpp):
    //   void mgpuLaunchKernel(ptr func, intptr gx, gy, gz, bx, by, bz,
    //                         i32 shmem, ptr stream, ptr params, ptr extra,
    //                         i64 nparams)
    {
      SmallVector<offload::KernelLaunchOp> streamLaunches;
      module.walk([&](offload::KernelLaunchOp op) {
        if (op.getStream())
          streamLaunches.push_back(op);
      });

      if (!streamLaunches.empty()) {
        Type llvmPtrTy = LLVM::LLVMPointerType::get(ctx);
        auto voidTy = LLVM::LLVMVoidType::get(ctx);

        // Declare mgpuModuleGetFunction(ptr, ptr) -> ptr
        auto getModuleFuncDecl = getOrInsertLLVMDecl(
            b, module, "mgpuModuleGetFunction",
            LLVM::LLVMFunctionType::get(llvmPtrTy, {llvmPtrTy, llvmPtrTy}));

        // Declare mgpuLaunchKernel(ptr, i64, i64, i64, i64, i64, i64, i32,
        //                          ptr, ptr, ptr, i64) -> void
        auto launchDecl = getOrInsertLLVMDecl(
            b, module, "mgpuLaunchKernel",
            LLVM::LLVMFunctionType::get(
                voidTy,
                {llvmPtrTy, i64Ty, i64Ty, i64Ty, i64Ty, i64Ty, i64Ty, i32Ty,
                 llvmPtrTy, llvmPtrTy, llvmPtrTy, i64Ty}));

        constexpr StringLiteral kGpuModName = "offload_device_module";
        std::string moduleHandleName = (kGpuModName + "_module").str();

        // Ensure the module handle global exists (defined by SelectObjectAttr).
        if (!module.lookupSymbol<LLVM::GlobalOp>(moduleHandleName)) {
          OpBuilder::InsertionGuard hGuard(b);
          b.setInsertionPointToStart(module.getBody());
          LLVM::GlobalOp::create(b, module.getLoc(), llvmPtrTy,
                                 /*isConstant=*/false, LLVM::Linkage::External,
                                 moduleHandleName, /*value=*/mlir::Attribute{});
        }

        for (offload::KernelLaunchOp launch : streamLaunches) {
          Location loc = launch.getLoc();
          std::string kernelNameStr = launch.getCallee().str();
          unsigned nArgs = launch.getArgs().size();

          // Helper function name: unique per (kernel, nArgs) pair.
          std::string helperName =
              "__offload_launch_stream_" + kernelNameStr;

          // Build the helper function type:
          //   (stream: !llvm.ptr, gx: i64, gy: i64, gz: i64,
          //    bx: i64, by: i64, bz: i64, shmem: i32,
          //    arg0: i64, ..., argN: i64) -> void
          // Stream is !llvm.ptr because hipStream_t is a pointer type.
          SmallVector<Type> helperArgTypes;
          helperArgTypes.push_back(llvmPtrTy); // stream (!llvm.ptr)
          for (unsigned d = 0; d < 6; ++d)
            helperArgTypes.push_back(i64Ty); // gx,gy,gz,bx,by,bz
          helperArgTypes.push_back(i32Ty);   // shmem
          for (unsigned a = 0; a < nArgs; ++a)
            helperArgTypes.push_back(i64Ty); // kernel args

          auto helperFnTy =
              LLVM::LLVMFunctionType::get(voidTy, helperArgTypes);

          // Emit the helper llvm.func at module scope if not yet present.
          if (!module.lookupSymbol<LLVM::LLVMFuncOp>(helperName)) {
            OpBuilder::InsertionGuard fnGuard(b);
            b.setInsertionPointToEnd(module.getBody());
            auto helperFn = LLVM::LLVMFuncOp::create(b, loc, helperName,
                                                      helperFnTy);
            helperFn.setLinkage(LLVM::Linkage::Internal);
            Block *entry = helperFn.addEntryBlock(b);
            b.setInsertionPointToStart(entry);

            // Map block arguments.
            unsigned argIdx = 0;
            Value streamArg = helperFn.getArgument(argIdx++); // !llvm.ptr
            Value gxArg = helperFn.getArgument(argIdx++);
            Value gyArg = helperFn.getArgument(argIdx++);
            Value gzArg = helperFn.getArgument(argIdx++);
            Value bxArg = helperFn.getArgument(argIdx++);
            Value byArg = helperFn.getArgument(argIdx++);
            Value bzArg = helperFn.getArgument(argIdx++);
            Value shmemArg = helperFn.getArgument(argIdx++); // i32

            // 1. Load module handle.
            Value modHandle =
                LLVM::LoadOp::create(
                    b, loc, llvmPtrTy,
                    LLVM::AddressOfOp::create(b, loc, llvmPtrTy,
                                              moduleHandleName)
                        .getResult())
                    .getResult();

            // 2. Get or create the kernel name C string global.
            std::string kernelNameGlobal =
                (kGpuModName + "_" + kernelNameStr + "_name").str();
            if (!module.lookupSymbol<LLVM::GlobalOp>(kernelNameGlobal)) {
              OpBuilder::InsertionGuard nGuard(b);
              b.setInsertionPointToStart(module.getBody());
              auto i8Ty = b.getIntegerType(8);
              auto strTy =
                  LLVM::LLVMArrayType::get(i8Ty, kernelNameStr.size() + 1);
              SmallVector<int8_t> chars(kernelNameStr.begin(),
                                        kernelNameStr.end());
              chars.push_back(0);
              auto strAttr = DenseIntElementsAttr::get(
                  RankedTensorType::get({static_cast<int64_t>(chars.size())},
                                        i8Ty),
                  ArrayRef<int8_t>(chars));
              auto nameGlobal = LLVM::GlobalOp::create(
                  b, module.getLoc(), strTy, /*isConstant=*/true,
                  LLVM::Linkage::Private, kernelNameGlobal, strAttr);
              nameGlobal.setUnnamedAddr(LLVM::UnnamedAddr::Global);
            }
            b.setInsertionPointToEnd(entry); // restore after potential guard
            Value namePtr =
                LLVM::AddressOfOp::create(b, loc, llvmPtrTy, kernelNameGlobal)
                    .getResult();

            // 3. Look up the kernel function handle.
            Value funcHandle =
                LLVM::CallOp::create(b, loc, getModuleFuncDecl,
                                     ValueRange{modHandle, namePtr})
                    .getResult();

            // 4. Stream arg is already !llvm.ptr (hipStream_t is a pointer).
            Value streamPtr = streamArg;

            // 5. Pack kernel arguments into void*[N].
            Value argsArraySlot;
            if (nArgs == 0) {
              argsArraySlot =
                  LLVM::ZeroOp::create(b, loc, llvmPtrTy).getResult();
            } else {
              Value one64 =
                  LLVM::ConstantOp::create(b, loc, i64Ty,
                                           b.getIntegerAttr(i64Ty, 1))
                      .getResult();
              Value nArgs64 =
                  LLVM::ConstantOp::create(b, loc, i64Ty,
                                           b.getIntegerAttr(i64Ty, nArgs))
                      .getResult();
              argsArraySlot =
                  LLVM::AllocaOp::create(b, loc, llvmPtrTy, llvmPtrTy, nArgs64,
                                         /*alignment=*/8)
                      .getResult();
              for (unsigned i = 0; i < nArgs; ++i) {
                Value argVal = helperFn.getArgument(argIdx + i); // already i64
                Value argSlot =
                    LLVM::AllocaOp::create(b, loc, llvmPtrTy, i64Ty, one64,
                                           /*alignment=*/8)
                        .getResult();
                LLVM::StoreOp::create(b, loc, argVal, argSlot);
                Value gep =
                    LLVM::GEPOp::create(
                        b, loc, llvmPtrTy, llvmPtrTy, argsArraySlot,
                        ArrayRef<LLVM::GEPArg>{
                            LLVM::GEPArg(static_cast<int32_t>(i))},
                        LLVM::GEPNoWrapFlags::inbounds)
                        .getResult();
                LLVM::StoreOp::create(b, loc, argSlot, gep);
              }
            }

            Value nullPtr =
                LLVM::ZeroOp::create(b, loc, llvmPtrTy).getResult();
            Value nArgsFinal =
                LLVM::ConstantOp::create(b, loc, i64Ty,
                                         b.getIntegerAttr(i64Ty, nArgs))
                    .getResult();

            // 6. Launch.
            LLVM::CallOp::create(
                b, loc, launchDecl,
                ValueRange{funcHandle, gxArg, gyArg, gzArg, bxArg, byArg, bzArg,
                           shmemArg, streamPtr, argsArraySlot, nullPtr,
                           nArgsFinal});
            LLVM::ReturnOp::create(b, loc, ValueRange{});
          }

          // Replace offload.kernel_launch with func.call to the helper.
          // The helper signature: (stream: i64, gx..bz: i64x6, shmem: i32,
          //                        args: i64*).
          // Replace offload.kernel_launch with LLVM::CallOp to the helper.
          // CIR→LLVM conversion accepts LLVM dialect ops inside cir.func bodies
          // (the same approach used by stream_create, stream_sync, etc.).
          b.setInsertionPoint(launch);

          auto toI64 = [&](Value v) -> Value {
            return UnrealizedConversionCastOp::create(b, loc, TypeRange{i64Ty},
                                                      ValueRange{v})
                .getResult(0);
          };

          // Look up the just-created llvm.func helper.
          auto helperFn =
              module.lookupSymbol<LLVM::LLVMFuncOp>(helperName);

          SmallVector<Value> callArgs;
          // stream: cast !offload.stream → !llvm.ptr (hipStream_t is a pointer)
          Value streamVal = launch.getStream();
          Operation *streamDefOp =
              streamVal ? streamVal.getDefiningOp() : nullptr;
          callArgs.push_back(
              UnrealizedConversionCastOp::create(b, loc, TypeRange{llvmPtrTy},
                                                 ValueRange{streamVal})
                  .getResult(0));
          // grid/block dims (Index → i64)
          callArgs.push_back(toI64(launch.getGridX()));
          callArgs.push_back(toI64(launch.getGridY()));
          callArgs.push_back(toI64(launch.getGridZ()));
          callArgs.push_back(toI64(launch.getBlockX()));
          callArgs.push_back(toI64(launch.getBlockY()));
          callArgs.push_back(toI64(launch.getBlockZ()));
          // shmem (i32): cast from Index shmem operand, or zero constant.
          Value shmemI32;
          if (Value shmem = launch.getSharedMem()) {
            Value shmemI64 = toI64(shmem);
            shmemI32 =
                LLVM::TruncOp::create(b, loc, i32Ty, shmemI64).getResult();
          } else {
            shmemI32 =
                LLVM::ConstantOp::create(b, loc, i32Ty,
                                         b.getIntegerAttr(i32Ty, 0))
                    .getResult();
          }
          callArgs.push_back(shmemI32);
          // kernel args (cast each to i64)
          for (Value arg : launch.getArgs())
            callArgs.push_back(toI64(arg));

          LLVM::CallOp::create(b, loc, helperFn, callArgs);

          launch.erase();

          // Erase the dead !offload.stream unrealized_conversion_cast.
          // Because we used streamToI64() above, which bypasses the
          // !offload.stream hop, the cast producing the !offload.stream value
          // should now have no uses left.
          if (streamDefOp && streamDefOp->use_empty())
            streamDefOp->erase();
        }
      }
    }

    // ------------------------------------------------------------------ //
    // Device global initialization: emit __offload_init_globals()
    //
    // For each offload.global_var with a non-null initial_value, emit a
    // hipMemcpy(HostToDevice) call that writes the constant to the device
    // symbol at module load time.  The constant data is stored in an
    // llvm.mlir.global (host-side read-only); its address is the src ptr.
    //
    // The device symbol address (dst ptr) is obtained via hipGetSymbolAddress,
    // which requires a call at runtime.  For simplicity we embed this inside
    // the generated __offload_init_globals() function.
    //
    // The generated function is registered as a module constructor via
    // llvm.mlir.global_ctors so that it runs before any user code.
    //
    // After processing, all offload.global_var ops are erased (they are not
    // representable in the LLVM dialect).
    // ------------------------------------------------------------------ //
    {
      SmallVector<offload::GlobalVarOp> allGlobalVars;
      module.walk([&](offload::GlobalVarOp gv) {
        // Skip globals that have been moved into a gpu.module by
        // SplitSingleSourcePass (Step 1b).  Those will be converted to
        // llvm.mlir.global by ConvertCIRInGpuModulePass.
        if (gv->getParentOfType<mlir::gpu::GPUModuleOp>())
          return;
        allGlobalVars.push_back(gv);
      });

      SmallVector<offload::GlobalVarOp> initGlobals;
      for (auto gv : allGlobalVars)
        if (gv.getInitialValue())
          initGlobals.push_back(gv);

      if (!initGlobals.empty()) {
        Location modLoc = module.getLoc();
        Type llvmPtrTy = LLVM::LLVMPointerType::get(ctx);
        StringRef initFnName = "__offload_init_globals";

        OpBuilder::InsertionGuard guard(b);
        b.setInsertionPointToEnd(module.getBody());
        auto voidTy = LLVM::LLVMVoidType::get(ctx);
        auto initFnTy = LLVM::LLVMFunctionType::get(voidTy, {});
        auto initFn = LLVM::LLVMFuncOp::create(b, modLoc, initFnName,
                                                initFnTy);
        initFn.setLinkage(LLVM::Linkage::Internal);
        Block *entry = initFn.addEntryBlock(b);
        b.setInsertionPointToStart(entry);

        StringRef memcpyFn = pick("hipMemcpy", "cudaMemcpy", useHip);
        Value kindH2D =
            LLVM::ConstantOp::create(b, modLoc, i32Ty,
                                     b.getIntegerAttr(i32Ty, 1))
                .getResult();
        auto memcpyDecl = getOrInsertLLVMDecl(
            b, module, memcpyFn,
            LLVM::LLVMFunctionType::get(i32Ty,
                                        {llvmPtrTy, llvmPtrTy, i64Ty, i32Ty}));

        // mgpuModuleGetGlobal(devPtr, bytes, module, name) looks up a device
        // global by name in the loaded binary module object.
        // void mgpuModuleGetGlobal(void** devPtr, size_t* bytes,
        //                          hipModule_t module, const char* name)
        auto getGlobalDecl = getOrInsertLLVMDecl(
            b, module, "mgpuModuleGetGlobal",
            LLVM::LLVMFunctionType::get(
                LLVM::LLVMVoidType::get(ctx),
                {llvmPtrTy, llvmPtrTy, llvmPtrTy, llvmPtrTy}));

        // Load the device binary module handle stored by the binary-load ctor.
        // The handle global is named "{gpuModuleName}_module" by SelectObjectAttr.
        // gpuModuleName is always "offload_device_module" in this pipeline.
        constexpr StringLiteral kGpuModuleName = "offload_device_module";
        std::string moduleHandleGlobalName =
            (kGpuModuleName + "_module").str();
        Value modHandle = [&]() -> Value {
          // Look up the global or create a forward reference (will be defined
          // by SelectObjectAttr during LLVM translation of gpu.BinaryOp).
          LLVM::GlobalOp handleGlobal =
              module.lookupSymbol<LLVM::GlobalOp>(moduleHandleGlobalName);
          if (!handleGlobal) {
            OpBuilder::InsertionGuard hGuard(b);
            b.setInsertionPointToStart(module.getBody());
            handleGlobal = LLVM::GlobalOp::create(
                b, modLoc, llvmPtrTy, /*isConstant=*/false,
                LLVM::Linkage::External, moduleHandleGlobalName,
                /*value=*/mlir::Attribute{});
          }
          return LLVM::LoadOp::create(
                     b, modLoc, llvmPtrTy,
                     LLVM::AddressOfOp::create(b, modLoc, llvmPtrTy,
                                               moduleHandleGlobalName)
                         .getResult())
              .getResult();
        }();

        // Allocate slots for the device pointer and size outputs.
        Value one64 =
            LLVM::ConstantOp::create(b, modLoc, i64Ty,
                                     b.getIntegerAttr(i64Ty, 1))
                .getResult();
        Value dstSlot =
            LLVM::AllocaOp::create(b, modLoc, llvmPtrTy, llvmPtrTy, one64,
                                   /*alignment=*/8)
                .getResult();
        Value bytesSlot =
            LLVM::AllocaOp::create(b, modLoc, llvmPtrTy, i64Ty, one64,
                                   /*alignment=*/8)
                .getResult();

        unsigned constIdx = 0;
        for (offload::GlobalVarOp gv : initGlobals) {
          mlir::Attribute initAttr = *gv.getInitialValue();
          (void)gv.getType(); // used only for aggregate types (not yet supported)

          std::string constName =
              ("__offload_init_const." + gv.getSymName() + "." +
               llvm::Twine(constIdx++))
                  .str();

          // Emit the host-side constant into an llvm.mlir.global.
          mlir::Type constElemTy;
          mlir::Attribute constInitAttr = initAttr;
          if (auto intAttr = mlir::dyn_cast<mlir::IntegerAttr>(initAttr)) {
            constElemTy = intAttr.getType();
          } else if (auto fpAttr = mlir::dyn_cast<mlir::FloatAttr>(initAttr)) {
            constElemTy = fpAttr.getType();
          } else if (auto denseAttr =
                         mlir::dyn_cast<mlir::DenseElementsAttr>(initAttr)) {
            constElemTy = denseAttr.getType();
            constInitAttr = denseAttr;
          } else {
            continue; // Unknown attribute type; skip.
          }

          // Handle aggregate (array) initializers: DenseElementsAttr with a
          // RankedTensorType element type.  These are lowered to an LLVM array
          // global and the full array is copied to the device with hipMemcpy.
          if (auto tensorTy =
                  mlir::dyn_cast<mlir::RankedTensorType>(constElemTy)) {
            mlir::Type elemTy = tensorTy.getElementType();
            int64_t numElems = tensorTy.getNumElements();

            // Map MLIR element type to LLVM element type.
            mlir::Type llvmElem;
            int64_t elemBytes = 0;
            if (auto intTy = mlir::dyn_cast<mlir::IntegerType>(elemTy)) {
              llvmElem = intTy;
              elemBytes = (intTy.getWidth() + 7) / 8;
            } else if (elemTy.isF16()) {
              llvmElem = b.getF16Type();
              elemBytes = 2;
            } else if (elemTy.isF32()) {
              llvmElem = b.getF32Type();
              elemBytes = 4;
            } else if (elemTy.isF64()) {
              llvmElem = b.getF64Type();
              elemBytes = 8;
            }
            if (!llvmElem || elemBytes == 0 || numElems == 0)
              continue;

            int64_t totalBytes = numElems * elemBytes;
            auto llvmArrTy = LLVM::LLVMArrayType::get(llvmElem, numElems);

            OpBuilder::InsertionGuard gGuard(b);
            b.setInsertionPoint(initFn);
            auto constGlobal = LLVM::GlobalOp::create(
                b, modLoc, llvmArrTy, /*isConstant=*/true,
                LLVM::Linkage::Private, constName, constInitAttr);
            constGlobal.setUnnamedAddr(LLVM::UnnamedAddr::Global);

            b.setInsertionPointToEnd(entry);
            // Emit symbol name string and call mgpuModuleGetGlobal + hipMemcpy.
            std::string symNameStr = gv.getSymName().str();
            std::string symNameGlobalName =
                ("__offload_sym_name." + gv.getSymName()).str();
            {
              OpBuilder::InsertionGuard nGuard(b);
              b.setInsertionPoint(initFn);
              if (!module.lookupSymbol<LLVM::GlobalOp>(symNameGlobalName)) {
                auto i8Ty = b.getIntegerType(8);
                auto strTy =
                    LLVM::LLVMArrayType::get(i8Ty, symNameStr.size() + 1);
                SmallVector<int8_t> chars(symNameStr.begin(),
                                         symNameStr.end());
                chars.push_back(0);
                auto strAttr = DenseIntElementsAttr::get(
                    RankedTensorType::get(
                        {static_cast<int64_t>(chars.size())}, i8Ty),
                    ArrayRef<int8_t>(chars));
                auto symNameGlobal = LLVM::GlobalOp::create(
                    b, modLoc, strTy, /*isConstant=*/true,
                    LLVM::Linkage::Private, symNameGlobalName, strAttr);
                symNameGlobal.setUnnamedAddr(LLVM::UnnamedAddr::Global);
              }
            }
            Value symNamePtr =
                LLVM::AddressOfOp::create(b, modLoc, llvmPtrTy,
                                          symNameGlobalName)
                    .getResult();
            LLVM::CallOp::create(b, modLoc, getGlobalDecl,
                                 ValueRange{dstSlot, bytesSlot, modHandle,
                                            symNamePtr});
            Value dstPtr =
                LLVM::LoadOp::create(b, modLoc, llvmPtrTy, dstSlot)
                    .getResult();
            Value srcPtr =
                LLVM::AddressOfOp::create(b, modLoc, llvmPtrTy, constName)
                    .getResult();
            Value sizeVal =
                LLVM::ConstantOp::create(b, modLoc, i64Ty,
                                         b.getIntegerAttr(i64Ty, totalBytes))
                    .getResult();
            LLVM::CallOp::create(b, modLoc, memcpyDecl,
                                 ValueRange{dstPtr, srcPtr, sizeVal, kindH2D});
            continue; // Array case handled; skip the scalar path below.
          }

          // Scalar path: CIR integer/float types → standard LLVM type.
          mlir::Type llvmElemTy;
          if (auto intTy = mlir::dyn_cast<mlir::IntegerType>(constElemTy)) {
            llvmElemTy = intTy;
          } else if (constElemTy.isF16()) {
            llvmElemTy = b.getF16Type();
          } else if (constElemTy.isF32()) {
            llvmElemTy = b.getF32Type();
          } else if (constElemTy.isF64()) {
            llvmElemTy = b.getF64Type();
          } else {
            // For CIR-typed attributes (cir.int, cir.float), try to extract
            // the raw APInt/APFloat and repackage as an LLVM-compatible attr.
            // Fall back by skipping if we can't determine the LLVM type.
            if (auto intAttr = mlir::dyn_cast<mlir::IntegerAttr>(initAttr)) {
              // CIR integer attr: width is in the CIR type.
              unsigned width = 0;
              if (auto cirIntTy = llvm::dyn_cast_if_present<
                      mlir::IntegerType>(constElemTy))
                width = cirIntTy.getWidth();
              if (width == 0)
                continue;
              llvmElemTy = b.getIntegerType(width);
              constInitAttr = b.getIntegerAttr(llvmElemTy,
                                               intAttr.getValue());
            } else if (auto fpAttr =
                           mlir::dyn_cast<mlir::FloatAttr>(initAttr)) {
              APFloat val = fpAttr.getValue();
              if (&val.getSemantics() == &APFloat::IEEEhalf())
                llvmElemTy = b.getF16Type();
              else if (&val.getSemantics() == &APFloat::IEEEsingle())
                llvmElemTy = b.getF32Type();
              else if (&val.getSemantics() == &APFloat::IEEEdouble())
                llvmElemTy = b.getF64Type();
              else
                continue;
              constInitAttr = b.getFloatAttr(llvmElemTy, val);
            } else {
              continue;
            }
          }

          // Compute size in bytes.
          int64_t sizeBytes = 0;
          if (auto intTy = mlir::dyn_cast<mlir::IntegerType>(llvmElemTy))
            sizeBytes = (intTy.getWidth() + 7) / 8;
          else if (llvmElemTy.isF16())
            sizeBytes = 2;
          else if (llvmElemTy.isF32())
            sizeBytes = 4;
          else if (llvmElemTy.isF64())
            sizeBytes = 8;
          if (sizeBytes <= 0)
            continue;

          // Place the constant global before the init function.
          OpBuilder::InsertionGuard gGuard(b);
          b.setInsertionPoint(initFn);
          auto constGlobal = LLVM::GlobalOp::create(
              b, modLoc, llvmElemTy, /*isConstant=*/true,
              LLVM::Linkage::Private, constName, constInitAttr);
          constGlobal.setUnnamedAddr(LLVM::UnnamedAddr::Global);

          b.setInsertionPointToEnd(entry);

          // Create a null-terminated C string constant for the device symbol
          // name so mgpuModuleGetGlobal can look it up by name in the binary.
          std::string symNameStr = gv.getSymName().str();
          std::string symNameGlobalName =
              ("__offload_sym_name." + gv.getSymName()).str();
          {
            OpBuilder::InsertionGuard nGuard(b);
            b.setInsertionPoint(initFn);
            if (!module.lookupSymbol<LLVM::GlobalOp>(symNameGlobalName)) {
              // Store as a [N x i8] array with explicit null terminator.
              auto i8Ty = b.getIntegerType(8);
              auto strTy =
                  LLVM::LLVMArrayType::get(i8Ty, symNameStr.size() + 1);
              // Build a DenseIntElementsAttr for the char data.
              SmallVector<int8_t> chars(symNameStr.begin(), symNameStr.end());
              chars.push_back(0);
              auto strAttr = DenseIntElementsAttr::get(
                  RankedTensorType::get(
                      {static_cast<int64_t>(chars.size())}, i8Ty),
                  ArrayRef<int8_t>(chars));
              auto symNameGlobal = LLVM::GlobalOp::create(
                  b, modLoc, strTy, /*isConstant=*/true,
                  LLVM::Linkage::Private, symNameGlobalName, strAttr);
              symNameGlobal.setUnnamedAddr(LLVM::UnnamedAddr::Global);
            }
          }
          Value symNamePtr =
              LLVM::AddressOfOp::create(b, modLoc, llvmPtrTy, symNameGlobalName)
                  .getResult();

          // Look up the device global address in the loaded binary module.
          LLVM::CallOp::create(b, modLoc, getGlobalDecl,
                               ValueRange{dstSlot, bytesSlot, modHandle,
                                          symNamePtr});
          Value dstPtr =
              LLVM::LoadOp::create(b, modLoc, llvmPtrTy, dstSlot).getResult();

          // Get the host constant address.
          Value srcPtr =
              LLVM::AddressOfOp::create(b, modLoc, llvmPtrTy, constName)
                  .getResult();

          Value sizeVal =
              LLVM::ConstantOp::create(b, modLoc, i64Ty,
                                       b.getIntegerAttr(i64Ty, sizeBytes))
                  .getResult();

          LLVM::CallOp::create(b, modLoc, memcpyDecl,
                               ValueRange{dstPtr, srcPtr, sizeVal, kindH2D});
        }

        LLVM::ReturnOp::create(b, modLoc, ValueRange{});

        // Register as a module constructor.
        b.setInsertionPointToEnd(module.getBody());
        Attribute ctorFn = FlatSymbolRefAttr::get(ctx, initFnName);
        Attribute ctorDataAttr = LLVM::ZeroAttr::get(ctx);
        LLVM::GlobalCtorsOp::create(
            b, modLoc,
            b.getArrayAttr(ArrayRef<Attribute>{ctorFn}),
            b.getI32ArrayAttr(ArrayRef<int32_t>{200}),
            b.getArrayAttr(ArrayRef<Attribute>{ctorDataAttr}));
      }

      // Erase all offload.global_var ops — they are not LLVM-translatable.
      for (auto gv : allGlobalVars)
        gv.erase();
    }
  }
};

} // namespace
