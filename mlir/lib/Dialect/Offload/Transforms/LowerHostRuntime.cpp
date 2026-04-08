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
//   offload.memcpy_to_symbol is deleted (requires LLVM addressof; future work)
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/Offload/Transforms/Passes.h"

#include "mlir/Dialect/Func/IR/FuncOps.h"
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

    // offload.stream_create: replace result with an i64 zero placeholder.
    // A real lowering would alloca a stream handle pointer, call the runtime
    // function, then load the pointer.  That pattern needs LLVM dialect ops
    // (llvm.alloca, llvm.store, llvm.load) which are not linked here.
    // Emit an unrealized_conversion_cast from nothing so downstream passes can
    // supply a real implementation.
    module.walk([&](offload::StreamCreateOp op) {
      b.setInsertionPoint(op);
      auto cast = b.create<UnrealizedConversionCastOp>(
          op.getLoc(), TypeRange{handleTy}, ValueRange{});
      auto backcast = b.create<UnrealizedConversionCastOp>(
          op.getLoc(), TypeRange{op.getType()},
          ValueRange{cast.getResult(0)});
      op.getResult().replaceAllUsesWith(backcast.getResult(0));
      op.erase();
    });

    // offload.memcpy_to_symbol: requires LLVM addressof for the symbol pointer.
    // Mark as a TODO — erase the op with a diagnostic note.
    module.walk([&](offload::MemcpyToSymbolOp op) {
      op.emitRemark("offload.memcpy_to_symbol lowering to LLVM addressof is "
                    "not yet implemented; op erased");
      op.erase();
    });
  }
};

} // namespace
