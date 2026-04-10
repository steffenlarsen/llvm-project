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

#include "mlir/Dialect/Func/IR/FuncOps.h"
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
    Type handleTy = getHandleType(ctx); // i64

    // offload.stream_destroy → @hip/cudaStreamDestroy(i64) -> i32
    module.walk([&](offload::StreamDestroyOp op) {
      b.setInsertionPoint(op);
      StringRef fn = pick("hipStreamDestroy", "cudaStreamDestroy", useHip);
      auto decl = getOrInsertDecl(b, module, fn,
                                  b.getFunctionType({handleTy}, {i32Ty}));
      // Cast the !offload.stream value to i64 via unrealized_conversion_cast
      // (a clean placeholder; a real lowering would use bitcast/inttoptr).
      auto cast = UnrealizedConversionCastOp::create(
          b, op.getLoc(), TypeRange{handleTy}, ValueRange{op.getStream()});
      func::CallOp::create(b, op.getLoc(), decl, ValueRange{cast.getResult(0)});
      op.erase();
    });

    // offload.stream_sync → @hip/cudaStreamSynchronize(i64) -> i32
    module.walk([&](offload::StreamSyncOp op) {
      b.setInsertionPoint(op);
      StringRef fn =
          pick("hipStreamSynchronize", "cudaStreamSynchronize", useHip);
      auto decl = getOrInsertDecl(b, module, fn,
                                  b.getFunctionType({handleTy}, {i32Ty}));
      auto cast = UnrealizedConversionCastOp::create(
          b, op.getLoc(), TypeRange{handleTy}, ValueRange{op.getStream()});
      func::CallOp::create(b, op.getLoc(), decl, ValueRange{cast.getResult(0)});
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
      Value one =
          LLVM::ConstantOp::create(b, loc, i64Ty, b.getIntegerAttr(i64Ty, 1))
              .getResult();
      Value slot = LLVM::AllocaOp::create(b, loc, llvmPtrTy, i64Ty, one,
                                          /*alignment=*/8)
                       .getResult();
      // Declare @hipStreamCreate(ptr) -> i32.
      auto decl = getOrInsertDecl(b, module, fn,
                                  b.getFunctionType({llvmPtrTy}, {i32Ty}));
      func::CallOp::create(b, loc, decl, ValueRange{slot});
      // Load the written handle back.
      Value handle = LLVM::LoadOp::create(b, loc, i64Ty, slot).getResult();
      // Cast i64 → !offload.stream.
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
      auto decl = getOrInsertDecl(
          b, module, fn,
          b.getFunctionType({llvmPtrTy, llvmPtrTy, i64Ty, i32Ty}, {i32Ty}));
      func::CallOp::create(b, loc, decl,
                           ValueRange{dst, src, sizeAsI64, kindVal});
      op.erase();
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

    // offload.memset(ptr, pattern, size_bytes)
    //   → @hipMemset(dst, i32, size)     when type(pattern) is i8  (byte fill)
    //   → @hipMemsetD16(dst, i16, count) when type(pattern) is i16 (element
    //   fill) → @hipMemsetD32(dst, i32, count) when type(pattern) is i32
    //   (element fill)
    //
    // For a future liboffload target: stack-allocate $pattern, pass its address
    // and sizeof(type($pattern)) to olMemFill — no changes needed to this op.
    module.walk([&](offload::MemsetOp op) {
      b.setInsertionPoint(op);
      mlir::Location loc = op.getLoc();
      Type llvmPtrTy = LLVM::LLVMPointerType::get(b.getContext());
      Value dst = UnrealizedConversionCastOp::create(
                      b, loc, TypeRange{llvmPtrTy}, ValueRange{op.getPtr()})
                      .getResult(0);
      Value sizeAsI64 = UnrealizedConversionCastOp::create(
                            b, loc, TypeRange{i64Ty}, ValueRange{op.getSize()})
                            .getResult(0);

      // Determine pattern width from the IR type.
      unsigned patWidth = 8;
      if (auto intTy = dyn_cast<IntegerType>(op.getPattern().getType()))
        patWidth = intTy.getWidth();

      if (patWidth <= 8) {
        // hipMemset(void*, int, size_t): byte pattern, size in bytes.
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
        // hipMemsetD16(hipDeviceptr_t, unsigned short, size_t): count in
        // elements.
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
        // hipMemsetD32(hipDeviceptr_t, unsigned int, size_t): count in
        // elements.
        Value four =
            LLVM::ConstantOp::create(b, loc, i64Ty, b.getIntegerAttr(i64Ty, 4))
                .getResult();
        Value count = LLVM::SDivOp::create(b, loc, i64Ty, sizeAsI64, four,
                                           /*isExact=*/mlir::UnitAttr{});
        Value patI32 =
            UnrealizedConversionCastOp::create(b, loc, TypeRange{i32Ty},
                                               ValueRange{op.getPattern()})
                .getResult(0);
        StringRef fn = pick("hipMemsetD32", "cuMemsetD32", useHip);
        auto decl = getOrInsertDecl(
            b, module, fn,
            b.getFunctionType({llvmPtrTy, i32Ty, i64Ty}, {i32Ty}));
        func::CallOp::create(b, loc, decl, ValueRange{dst, patI32, count});
      }
      op.erase();
    });

    // offload.event_destroy → @hip/cudaEventDestroy(i64) -> i32
    module.walk([&](offload::EventDestroyOp op) {
      b.setInsertionPoint(op);
      StringRef fn = pick("hipEventDestroy", "cudaEventDestroy", useHip);
      auto decl = getOrInsertDecl(b, module, fn,
                                  b.getFunctionType({handleTy}, {i32Ty}));
      auto cast = UnrealizedConversionCastOp::create(
          b, op.getLoc(), TypeRange{handleTy}, ValueRange{op.getEvent()});
      func::CallOp::create(b, op.getLoc(), decl, ValueRange{cast.getResult(0)});
      op.erase();
    });

    // offload.event_wait → @hip/cudaEventSynchronize(i64) -> i32
    module.walk([&](offload::EventWaitOp op) {
      b.setInsertionPoint(op);
      StringRef fn = pick("hipEventSynchronize", "cudaEventSynchronize", useHip);
      auto decl = getOrInsertDecl(b, module, fn,
                                  b.getFunctionType({handleTy}, {i32Ty}));
      auto cast = UnrealizedConversionCastOp::create(
          b, op.getLoc(), TypeRange{handleTy}, ValueRange{op.getEvent()});
      func::CallOp::create(b, op.getLoc(), decl, ValueRange{cast.getResult(0)});
      op.erase();
    });

    // offload.event_record → @hip/cudaEventRecord(i64 event, i64 stream) -> i32
    module.walk([&](offload::EventRecordOp op) {
      b.setInsertionPoint(op);
      mlir::Location loc = op.getLoc();
      StringRef fn = pick("hipEventRecord", "cudaEventRecord", useHip);
      auto decl = getOrInsertDecl(
          b, module, fn, b.getFunctionType({handleTy, handleTy}, {i32Ty}));
      Value eventI64 =
          UnrealizedConversionCastOp::create(b, loc, TypeRange{handleTy},
                                             ValueRange{op.getEvent()})
              .getResult(0);
      Value streamI64;
      if (op.getStream()) {
        streamI64 =
            UnrealizedConversionCastOp::create(b, loc, TypeRange{handleTy},
                                               ValueRange{op.getStream()})
                .getResult(0);
      } else {
        streamI64 =
            LLVM::ConstantOp::create(b, loc, i64Ty, b.getIntegerAttr(i64Ty, 0))
                .getResult();
      }
      func::CallOp::create(b, loc, decl, ValueRange{eventI64, streamI64});
      op.erase();
    });

    // offload.event_create → @hip/cudaEventCreate(ptr) -> i32
    module.walk([&](offload::EventCreateOp op) {
      b.setInsertionPoint(op);
      mlir::Location loc = op.getLoc();
      StringRef fn = pick("hipEventCreate", "cudaEventCreate", useHip);
      Type llvmPtrTy2 = LLVM::LLVMPointerType::get(ctx);
      Value one64 =
          LLVM::ConstantOp::create(b, loc, i64Ty, b.getIntegerAttr(i64Ty, 1))
              .getResult();
      Value slot = LLVM::AllocaOp::create(b, loc, llvmPtrTy2, i64Ty, one64,
                                          /*alignment=*/8)
                       .getResult();
      auto decl = getOrInsertDecl(b, module, fn,
                                  b.getFunctionType({llvmPtrTy2}, {i32Ty}));
      func::CallOp::create(b, loc, decl, ValueRange{slot});
      Value handle = LLVM::LoadOp::create(b, loc, i64Ty, slot).getResult();
      auto backcast = UnrealizedConversionCastOp::create(
          b, loc, TypeRange{op.getType()}, ValueRange{handle});
      op.getResult().replaceAllUsesWith(backcast.getResult(0));
      op.erase();
    });

    // offload.kernel_launch with stream → hipLaunchKernel
    //
    // Stream-aware launches were not converted by SplitSingleSource.
    // hipLaunchKernel signature:
    //   hipError_t hipLaunchKernel(const void* f, dim3 numBlocks, dim3 dimBlocks,
    //                              void** args, size_t sharedMem, hipStream_t s)
    // dim3 is { i32 x, i32 y, i32 z }.  We pass grid/block by pointer.
    {
      SmallVector<offload::KernelLaunchOp> streamLaunches;
      module.walk([&](offload::KernelLaunchOp op) {
        if (op.getStream())
          streamLaunches.push_back(op);
      });

      Type llvmPtrTy = LLVM::LLVMPointerType::get(ctx);
      Type dim3Ty = LLVM::LLVMStructType::getLiteral(
          ctx, {b.getI32Type(), b.getI32Type(), b.getI32Type()});
      StringRef launchFn = pick("hipLaunchKernel", "cudaLaunchKernel", useHip);

      for (offload::KernelLaunchOp launch : streamLaunches) {
        b.setInsertionPoint(launch);
        Location loc = launch.getLoc();

        Value one64 =
            LLVM::ConstantOp::create(b, loc, i64Ty, b.getIntegerAttr(i64Ty, 1))
                .getResult();

        auto makeDim3 = [&](Value idxX, Value idxY, Value idxZ) -> Value {
          Value slot =
              LLVM::AllocaOp::create(b, loc, llvmPtrTy, dim3Ty, one64,
                                     /*alignment=*/4)
                  .getResult();
          auto toI32 = [&](Value v) -> Value {
            return UnrealizedConversionCastOp::create(
                       b, loc, TypeRange{b.getI32Type()}, ValueRange{v})
                .getResult(0);
          };
          auto storeField = [&](Value val, int32_t idx) {
            Value gep = LLVM::GEPOp::create(
                            b, loc, llvmPtrTy, dim3Ty, slot,
                            ArrayRef<LLVM::GEPArg>{LLVM::GEPArg(0),
                                                   LLVM::GEPArg(idx)},
                            LLVM::GEPNoWrapFlags::inbounds)
                            .getResult();
            LLVM::StoreOp::create(b, loc, val, gep);
          };
          storeField(toI32(idxX), 0);
          storeField(toI32(idxY), 1);
          storeField(toI32(idxZ), 2);
          return slot;
        };

        Value gridSlot = makeDim3(launch.getGridX(), launch.getGridY(),
                                  launch.getGridZ());
        Value blockSlot = makeDim3(launch.getBlockX(), launch.getBlockY(),
                                   launch.getBlockZ());

        // Pack kernel arguments into void*[N].
        ValueRange kernArgs = launch.getArgs();
        unsigned nArgs = kernArgs.size();
        Value argsArraySlot;
        if (nArgs == 0) {
          argsArraySlot = LLVM::ZeroOp::create(b, loc, llvmPtrTy).getResult();
        } else {
          Value nArgs64 =
              LLVM::ConstantOp::create(b, loc, i64Ty,
                                       b.getIntegerAttr(i64Ty, nArgs))
                  .getResult();
          argsArraySlot =
              LLVM::AllocaOp::create(b, loc, llvmPtrTy, llvmPtrTy, nArgs64,
                                     /*alignment=*/8)
                  .getResult();
          for (unsigned i = 0; i < nArgs; ++i) {
            Value argSlot =
                LLVM::AllocaOp::create(b, loc, llvmPtrTy, i64Ty, one64,
                                       /*alignment=*/8)
                    .getResult();
            Value argAsI64 =
                UnrealizedConversionCastOp::create(b, loc, TypeRange{i64Ty},
                                                   ValueRange{kernArgs[i]})
                    .getResult(0);
            LLVM::StoreOp::create(b, loc, argAsI64, argSlot);
            Value gep = LLVM::GEPOp::create(
                            b, loc, llvmPtrTy, llvmPtrTy, argsArraySlot,
                            ArrayRef<LLVM::GEPArg>{
                                LLVM::GEPArg(static_cast<int32_t>(i))},
                            LLVM::GEPNoWrapFlags::inbounds)
                            .getResult();
            LLVM::StoreOp::create(b, loc, argSlot, gep);
          }
        }

        Value sharedMem =
            LLVM::ConstantOp::create(b, loc, i64Ty, b.getIntegerAttr(i64Ty, 0))
                .getResult();
        Value streamI64 =
            UnrealizedConversionCastOp::create(
                b, loc, TypeRange{i64Ty}, ValueRange{launch.getStream()})
                .getResult(0);
        // Obtain the kernel function pointer via LLVM address-of.
        // The kernel stub must exist as an llvm.func in this module (placed
        // there by the GPU compilation pipeline).
        Value fnPtr =
            LLVM::AddressOfOp::create(b, loc, llvmPtrTy, launch.getCallee())
                .getResult();

        auto decl = getOrInsertDecl(
            b, module, launchFn,
            b.getFunctionType(
                {llvmPtrTy, llvmPtrTy, llvmPtrTy, llvmPtrTy, i64Ty, i64Ty},
                {i32Ty}));
        func::CallOp::create(b, loc, decl,
                             ValueRange{fnPtr, gridSlot, blockSlot,
                                        argsArraySlot, sharedMem, streamI64});
        launch.erase();
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
      module.walk(
          [&](offload::GlobalVarOp gv) { allGlobalVars.push_back(gv); });

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

          // CIR integer/float types are not LLVM types; convert to LLVM.
          // IntegerAttr from CIR uses !cir.int<s,N> or !cir.int<u,N>;
          // we map to the underlying mlir::IntegerType for llvm.mlir.global.
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
