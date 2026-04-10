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
//   offload.memcpy         → @hipMemcpy (kind reconstructed from dst/src spaces)
//   offload.memcpy_to_symbol → llvm.mlir.addressof + func.call @hipMemcpy
//   offload.memset         → @hipMemset / @hipMemsetD16 / @hipMemsetD32
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/Offload/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/Offload/IR/OffloadDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace mlir {
namespace offload {

#define GEN_PASS_DEF_OFFLOADLOWERHOSTRUNTIMEPASS
#include "mlir/Dialect/Offload/Transforms/Passes.h.inc"

} // namespace offload
} // namespace mlir

using namespace mlir;
using namespace mlir::offload;

namespace {

/// We lower !offload.stream / !offload.event to i64.
static Type getHandleType(MLIRContext *ctx) {
  return IntegerType::get(ctx, 64);
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
  auto decl = b.create<func::FuncOp>(module.getLoc(), name, fnType);
  decl.setPrivate();
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
    Type i32Ty  = b.getI32Type();
    Type i64Ty  = b.getI64Type();
    Type handleTy = getHandleType(ctx); // i64

    // offload.stream_destroy → @hip/cudaStreamDestroy(i64) -> i32
    module.walk([&](offload::StreamDestroyOp op) {
      b.setInsertionPoint(op);
      StringRef fn = pick("hipStreamDestroy", "cudaStreamDestroy", useHip);
      auto decl = getOrInsertDecl(b, module, fn,
                                  b.getFunctionType({handleTy}, {i32Ty}));
      // Cast the !offload.stream value to i64 via unrealized_conversion_cast
      // (a clean placeholder; a real lowering would use bitcast/inttoptr).
      auto cast = b.create<UnrealizedConversionCastOp>(
          op.getLoc(), TypeRange{handleTy}, ValueRange{op.getStream()});
      b.create<func::CallOp>(op.getLoc(), decl,
                              ValueRange{cast.getResult(0)});
      op.erase();
    });

    // offload.stream_sync → @hip/cudaStreamSynchronize(i64) -> i32
    module.walk([&](offload::StreamSyncOp op) {
      b.setInsertionPoint(op);
      StringRef fn = pick("hipStreamSynchronize", "cudaStreamSynchronize",
                          useHip);
      auto decl = getOrInsertDecl(b, module, fn,
                                  b.getFunctionType({handleTy}, {i32Ty}));
      auto cast = b.create<UnrealizedConversionCastOp>(
          op.getLoc(), TypeRange{handleTy}, ValueRange{op.getStream()});
      b.create<func::CallOp>(op.getLoc(), decl,
                              ValueRange{cast.getResult(0)});
      op.erase();
    });

    // offload.device_sync → @hip/cudaDeviceSynchronize() -> i32
    module.walk([&](offload::DeviceSyncOp op) {
      b.setInsertionPoint(op);
      StringRef fn =
          pick("hipDeviceSynchronize", "cudaDeviceSynchronize", useHip);
      auto decl = getOrInsertDecl(b, module, fn,
                                  b.getFunctionType({}, {i32Ty}));
      b.create<func::CallOp>(op.getLoc(), decl, ValueRange{});
      op.erase();
    });

    // offload.stream_create → @hip/cudaStreamCreate(i64*) -> i32
    //
    // HIP/CUDA StreamCreate writes the new handle through a pointer argument:
    //   hipError_t hipStreamCreate(hipStream_t *pStream);
    // We model this with LLVM dialect ops:
    //   1. llvm.alloca 1 x i64  — stack slot for the handle
    //   2. llvm.call @hipStreamCreate(%slot)
    //   3. llvm.load %slot       — read back the written handle
    //   4. unrealized_cast to !offload.stream
    module.walk([&](offload::StreamCreateOp op) {
      b.setInsertionPoint(op);
      mlir::Location loc = op.getLoc();
      StringRef fn = pick("hipStreamCreate", "cudaStreamCreate", useHip);

      // Pointer type for the slot: LLVM ptr (opaque).
      Type llvmPtrTy = LLVM::LLVMPointerType::get(b.getContext());
      // Alloca 1 element of i64 to hold the stream handle.
      Value one = b.create<LLVM::ConstantOp>(
          loc, i64Ty, b.getIntegerAttr(i64Ty, 1));
      Value slot = b.create<LLVM::AllocaOp>(loc, llvmPtrTy, i64Ty, one,
                                             /*alignment=*/8);
      // Declare @hipStreamCreate(ptr) -> i32.
      auto decl = getOrInsertDecl(b, module, fn,
                                  b.getFunctionType({llvmPtrTy}, {i32Ty}));
      b.create<func::CallOp>(loc, decl, ValueRange{slot});
      // Load the written handle back.
      Value handle = b.create<LLVM::LoadOp>(loc, i64Ty, slot);
      // Cast i64 → !offload.stream.
      auto backcast = b.create<UnrealizedConversionCastOp>(
          loc, TypeRange{op.getType()}, ValueRange{handle});
      op.getResult().replaceAllUsesWith(backcast.getResult(0));
      op.erase();
    });

    // offload.malloc(size, alloc_type) → runtime malloc variant
    //
    // hipMalloc / hipHostMalloc / hipMallocManaged each write the allocated
    // pointer through a void** argument.  We model this with LLVM alloca/call/load:
    //   1. llvm.alloca 1 x i64  — slot for the returned pointer
    //   2. llvm.call @hip{Malloc|HostMalloc|MallocManaged}(%slot, %size[, flags])
    //   3. llvm.load %slot        — the allocated pointer (i64)
    module.walk([&](offload::MallocOp op) {
      b.setInsertionPoint(op);
      mlir::Location loc = op.getLoc();
      Type llvmPtrTy = LLVM::LLVMPointerType::get(b.getContext());
      Value sizeAsI64 = b.create<UnrealizedConversionCastOp>(
          loc, TypeRange{i64Ty}, ValueRange{op.getSize()}).getResult(0);
      Value one = b.create<LLVM::ConstantOp>(
          loc, i64Ty, b.getIntegerAttr(i64Ty, 1));
      Value slot = b.create<LLVM::AllocaOp>(loc, llvmPtrTy, i64Ty, one,
                                             /*alignment=*/8);

      offload::AllocType allocTy = op.getAllocType();
      if (!useHip || allocTy == offload::AllocType::device) {
        // hipMalloc(void**, size_t) / cudaMalloc(void**, size_t)
        StringRef fn = pick("hipMalloc", "cudaMalloc", useHip);
        auto decl = getOrInsertDecl(b, module, fn,
                                    b.getFunctionType({llvmPtrTy, i64Ty}, {i32Ty}));
        b.create<func::CallOp>(loc, decl, ValueRange{slot, sizeAsI64});
      } else if (allocTy == offload::AllocType::host) {
        // hipHostMalloc(void**, size_t, unsigned flags)
        Type u32Ty = b.getIntegerType(32, /*isSigned=*/false);
        Value flags = b.create<LLVM::ConstantOp>(
            loc, u32Ty, b.getIntegerAttr(u32Ty, 0));
        auto decl = getOrInsertDecl(
            b, module, "hipHostMalloc",
            b.getFunctionType({llvmPtrTy, i64Ty, u32Ty}, {i32Ty}));
        b.create<func::CallOp>(loc, decl, ValueRange{slot, sizeAsI64, flags});
      } else {
        // hipMallocManaged(void**, size_t, unsigned flags)
        // hipMemAttachGlobal = 1
        Type u32Ty = b.getIntegerType(32, /*isSigned=*/false);
        Value flags = b.create<LLVM::ConstantOp>(
            loc, u32Ty, b.getIntegerAttr(u32Ty, 1));
        auto decl = getOrInsertDecl(
            b, module, "hipMallocManaged",
            b.getFunctionType({llvmPtrTy, i64Ty, u32Ty}, {i32Ty}));
        b.create<func::CallOp>(loc, decl, ValueRange{slot, sizeAsI64, flags});
      }

      Value ptr = b.create<LLVM::LoadOp>(loc, i64Ty, slot);
      Value cast = b.create<UnrealizedConversionCastOp>(
          loc, TypeRange{op.getResult().getType()},
          ValueRange{ptr}).getResult(0);
      op.getResult().replaceAllUsesWith(cast);
      op.erase();
    });

    // offload.free(ptr, alloc_type) → hipFree / hipHostFree
    module.walk([&](offload::FreeOp op) {
      b.setInsertionPoint(op);
      mlir::Location loc = op.getLoc();
      Type llvmPtrTy = LLVM::LLVMPointerType::get(b.getContext());
      Value ptr = b.create<UnrealizedConversionCastOp>(
          loc, TypeRange{llvmPtrTy}, ValueRange{op.getPtr()}).getResult(0);

      // host-pinned memory uses hipHostFree; device and managed use hipFree.
      StringRef fn;
      if (useHip && op.getAllocType() == offload::AllocType::host)
        fn = "hipHostFree";
      else
        fn = pick("hipFree", "cudaFree", useHip);

      auto decl = getOrInsertDecl(b, module, fn,
                                  b.getFunctionType({llvmPtrTy}, {i32Ty}));
      b.create<func::CallOp>(loc, decl, ValueRange{ptr});
      op.erase();
    });

    // offload.memcpy(dst, src, size, dst_space, src_space)
    //   → @hip/cudaMemcpy(dst, src, size, kind_int) -> i32
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
      StringRef fn = pick("hipMemcpy", "cudaMemcpy", useHip);
      Type llvmPtrTy = LLVM::LLVMPointerType::get(b.getContext());
      Value dst = b.create<UnrealizedConversionCastOp>(
          loc, TypeRange{llvmPtrTy}, ValueRange{op.getDst()}).getResult(0);
      Value src = b.create<UnrealizedConversionCastOp>(
          loc, TypeRange{llvmPtrTy}, ValueRange{op.getSrc()}).getResult(0);
      Value sizeAsI64 = b.create<UnrealizedConversionCastOp>(
          loc, TypeRange{i64Ty}, ValueRange{op.getSize()}).getResult(0);

      // Derive hipMemcpyKind from the per-pointer allocation spaces.
      int32_t kindInt = 4; // Default
      offload::AllocType dstSp = op.getDstSpace();
      offload::AllocType srcSp = op.getSrcSpace();
      if (dstSp != offload::AllocType::managed &&
          srcSp != offload::AllocType::managed) {
        bool dstDev = (dstSp == offload::AllocType::device);
        bool srcDev = (srcSp == offload::AllocType::device);
        if (!dstDev && !srcDev)      kindInt = 0; // HostToHost
        else if (dstDev && !srcDev)  kindInt = 1; // HostToDevice
        else if (!dstDev && srcDev)  kindInt = 2; // DeviceToHost
        else                         kindInt = 3; // DeviceToDevice
      }

      Value kindVal = b.create<LLVM::ConstantOp>(
          loc, i32Ty, b.getIntegerAttr(i32Ty, kindInt));
      auto decl = getOrInsertDecl(
          b, module, fn,
          b.getFunctionType({llvmPtrTy, llvmPtrTy, i64Ty, i32Ty}, {i32Ty}));
      b.create<func::CallOp>(loc, decl,
                              ValueRange{dst, src, sizeAsI64, kindVal});
      op.erase();
    });

    // offload.memcpy_to_symbol @sym src = %src count = %count
    //   → @hip/cudaMemcpy(llvm.mlir.addressof @sym, src_ptr, count_i64,
    //                     hipMemcpyHostToDevice)
    //
    // We obtain the device symbol address via LLVM::AddressOfOp, which emits
    // an `llvm.mlir.addressof @sym : !llvm.ptr` referencing the llvm.mlir.global
    // that LowerSharedGlobalsPass placed in the gpu.module.
    module.walk([&](offload::MemcpyToSymbolOp op) {
      b.setInsertionPoint(op);
      mlir::Location loc = op.getLoc();
      Type llvmPtrTy = LLVM::LLVMPointerType::get(b.getContext());

      // Get the address of the device-side global symbol.
      Value symPtr = LLVM::AddressOfOp::create(
          b, loc, llvmPtrTy, op.getSymbol()).getResult();

      // Cast src (AnyType → !llvm.ptr).
      Value src = b.create<UnrealizedConversionCastOp>(
          loc, TypeRange{llvmPtrTy}, ValueRange{op.getSrc()}).getResult(0);

      // Cast count (Index → i64).
      Value sizeAsI64 = b.create<UnrealizedConversionCastOp>(
          loc, TypeRange{i64Ty}, ValueRange{op.getCount()}).getResult(0);

      // hipMemcpyKind::hipMemcpyHostToDevice = 1.
      Value kindVal = b.create<LLVM::ConstantOp>(
          loc, i32Ty, b.getIntegerAttr(i32Ty, 1));

      StringRef fn = pick("hipMemcpy", "cudaMemcpy", useHip);
      auto decl = getOrInsertDecl(
          b, module, fn,
          b.getFunctionType({llvmPtrTy, llvmPtrTy, i64Ty, i32Ty}, {i32Ty}));
      b.create<func::CallOp>(loc, decl,
                              ValueRange{symPtr, src, sizeAsI64, kindVal});
      op.erase();
    });

    // offload.memset(ptr, pattern, size_bytes)
    //   → @hipMemset(dst, i32, size)     when type(pattern) is i8  (byte fill)
    //   → @hipMemsetD16(dst, i16, count) when type(pattern) is i16 (element fill)
    //   → @hipMemsetD32(dst, i32, count) when type(pattern) is i32 (element fill)
    //
    // For a future liboffload target: stack-allocate $pattern, pass its address
    // and sizeof(type($pattern)) to olMemFill — no changes needed to this op.
    module.walk([&](offload::MemsetOp op) {
      b.setInsertionPoint(op);
      mlir::Location loc = op.getLoc();
      Type llvmPtrTy = LLVM::LLVMPointerType::get(b.getContext());
      Value dst = UnrealizedConversionCastOp::create(
          b, loc, TypeRange{llvmPtrTy}, ValueRange{op.getPtr()}).getResult(0);
      Value sizeAsI64 = UnrealizedConversionCastOp::create(
          b, loc, TypeRange{i64Ty}, ValueRange{op.getSize()}).getResult(0);

      // Determine pattern width from the IR type.
      unsigned patWidth = 8;
      if (auto intTy = dyn_cast<IntegerType>(op.getPattern().getType()))
        patWidth = intTy.getWidth();

      if (patWidth <= 8) {
        // hipMemset(void*, int, size_t): byte pattern, size in bytes.
        Value patI32 = UnrealizedConversionCastOp::create(
            b, loc, TypeRange{i32Ty}, ValueRange{op.getPattern()}).getResult(0);
        StringRef fn = pick("hipMemset", "cudaMemset", useHip);
        auto decl = getOrInsertDecl(b, module, fn,
            b.getFunctionType({llvmPtrTy, i32Ty, i64Ty}, {i32Ty}));
        func::CallOp::create(b, loc, decl, ValueRange{dst, patI32, sizeAsI64});
      } else if (patWidth == 16) {
        // hipMemsetD16(hipDeviceptr_t, unsigned short, size_t): count in elements.
        Value two = b.create<LLVM::ConstantOp>(
            loc, i64Ty, b.getIntegerAttr(i64Ty, 2));
        Value count = LLVM::SDivOp::create(b, loc, i64Ty, sizeAsI64, two,
                                           /*isExact=*/mlir::UnitAttr{});
        Type i16Ty = b.getIntegerType(16);
        Value patI16 = UnrealizedConversionCastOp::create(
            b, loc, TypeRange{i16Ty}, ValueRange{op.getPattern()}).getResult(0);
        StringRef fn = pick("hipMemsetD16", "cuMemsetD16", useHip);
        auto decl = getOrInsertDecl(b, module, fn,
            b.getFunctionType({llvmPtrTy, i16Ty, i64Ty}, {i32Ty}));
        func::CallOp::create(b, loc, decl, ValueRange{dst, patI16, count});
      } else {
        // hipMemsetD32(hipDeviceptr_t, unsigned int, size_t): count in elements.
        Value four = b.create<LLVM::ConstantOp>(
            loc, i64Ty, b.getIntegerAttr(i64Ty, 4));
        Value count = LLVM::SDivOp::create(b, loc, i64Ty, sizeAsI64, four,
                                           /*isExact=*/mlir::UnitAttr{});
        Value patI32 = UnrealizedConversionCastOp::create(
            b, loc, TypeRange{i32Ty}, ValueRange{op.getPattern()}).getResult(0);
        StringRef fn = pick("hipMemsetD32", "cuMemsetD32", useHip);
        auto decl = getOrInsertDecl(b, module, fn,
            b.getFunctionType({llvmPtrTy, i32Ty, i64Ty}, {i32Ty}));
        func::CallOp::create(b, loc, decl, ValueRange{dst, patI32, count});
      }
      op.erase();
    });
  }
};

} // namespace
