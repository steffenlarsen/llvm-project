//===---- CIRGenBuiltinAMDGPU.cpp - Emit CIR for AMDGPU builtins ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This contains code to emit AMDGPU Builtin calls.
//
//===----------------------------------------------------------------------===//

#include "CIRGenFunction.h"

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/GPU/IR/GPUDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/LLVMIR/ROCDLDialect.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Value.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/Support/ErrorHandling.h"

using namespace clang;
using namespace clang::CIRGen;

/// Emit a cir.atomic.fetch op for a simple (ptr, val) → old_val atomic.
///
/// This uses the existing CIR atomic infrastructure and goes through the normal
/// CIR-to-LLVM lowering path, which handles GPU address spaces correctly.
/// The AMDGPU-specific builtins for fadd/fmin/fmax map directly to the CIR
/// AtomicFetchKind enum (Add, Min, Max) with fetch_first semantics.
static mlir::Value emitCIRAtomicFetch(CIRGenFunction &cgf, const CallExpr *expr,
                                       cir::AtomicFetchKind binOp) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());
  mlir::Value ptr = cgf.emitScalarExpr(expr->getArg(0));
  mlir::Value val = cgf.emitScalarExpr(expr->getArg(1));
  return cir::AtomicFetchOp::create(
             builder, loc,
             ptr, val, binOp,
             cir::MemOrder::SequentiallyConsistent,
             cir::SyncScopeKind::System,
             /*is_volatile=*/false,
             /*fetch_first=*/true)
      ->getResult(0);
}

/// Emit a wrapping unsigned atomic increment/decrement via cir.atomic.fetch.
///
/// The AMDGPU wrapping inc/dec builtins (atomic_inc32/dec32) take
/// (ptr, max_val, ordering, scope) and return the old value.  We use the
/// UIncWrap / UDecWrap AtomicFetchKind which lowers to the same AMDGPU
/// hardware instruction (llvm.amdgcn.atomic.inc / .dec) via the CIR-to-LLVM
/// lowering path.
static mlir::Value emitCIRAtomicIncDec(CIRGenFunction &cgf, const CallExpr *expr,
                                        cir::AtomicFetchKind binOp) {
  // Same as emitCIRAtomicFetch — arg(0)=ptr, arg(1)=val (the wrap-around max).
  // Args(2) and (3) are the ordering and syncscope constants; ignore them here
  // and always emit seq_cst (the conservative choice for a builtin that has
  // no corresponding standard ordering semantic).
  return emitCIRAtomicFetch(cgf, expr, binOp);
}

/// Emit a gpu.subgroup_reduce op for __builtin_amdgcn_wave_reduce_*.
///
/// CIR integer types (e.g. !cir.int<u,32>) are bridged to standard MLIR
/// integers (i32/i64) via unrealized_conversion_cast, which the CIR-to-LLVM
/// lowering eliminates.
static mlir::Value emitSubgroupReduce(CIRGenFunction &cgf, const CallExpr *expr,
                                       mlir::gpu::AllReduceOperation op) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());
  mlir::MLIRContext *ctx = builder.getContext();
  mlir::Value val = cgf.emitScalarExpr(expr->getArg(0));
  mlir::Type cirTy = val.getType();
  unsigned width = mlir::cast<cir::IntType>(cirTy).getWidth();
  mlir::Type mlirIntTy = mlir::IntegerType::get(ctx, width);
  mlir::Value asMLIR =
      mlir::UnrealizedConversionCastOp::create(
          builder, loc, mlir::TypeRange{mlirIntTy}, mlir::ValueRange{val})
          .getResult(0);
  mlir::Value result =
      mlir::gpu::SubgroupReduceOp::create(builder, loc, asMLIR, op,
                                          /*uniform=*/false)
          .getResult();
  return mlir::UnrealizedConversionCastOp::create(
             builder, loc, mlir::TypeRange{cirTy}, mlir::ValueRange{result})
      .getResult(0);
}

/// Emit a gpu.subgroup_broadcast for __builtin_amdgcn_readlane /
/// __builtin_amdgcn_readfirstlane.
///
/// SubgroupBroadcastOp takes: (src: AnyType, lane: Optional<i32>,
///   broadcast_type: GPU_BroadcastTypeAttr).
/// For readlane, lane is arg(1) cast to i32; for readfirstlane, lane is absent.
static mlir::Value emitReadlane(CIRGenFunction &cgf, const CallExpr *expr,
                                 bool firstLane) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());
  mlir::MLIRContext *ctx = builder.getContext();
  mlir::Value src = cgf.emitScalarExpr(expr->getArg(0));
  mlir::Type cirTy = src.getType();
  unsigned width = mlir::cast<cir::IntType>(cirTy).getWidth();
  mlir::Type mlirTy = mlir::IntegerType::get(ctx, width);
  mlir::Value asSrc =
      mlir::UnrealizedConversionCastOp::create(
          builder, loc, mlir::TypeRange{mlirTy}, mlir::ValueRange{src})
          .getResult(0);
  mlir::Value laneVal;
  if (!firstLane) {
    mlir::Value lane = cgf.emitScalarExpr(expr->getArg(1));
    // SubgroupBroadcastOp expects the lane as i32.
    mlir::Type i32Ty = mlir::IntegerType::get(ctx, 32);
    laneVal =
        mlir::UnrealizedConversionCastOp::create(
            builder, loc, mlir::TypeRange{i32Ty}, mlir::ValueRange{lane})
            .getResult(0);
  }
  auto bcastTy = firstLane ? mlir::gpu::BroadcastType::first_active_lane
                            : mlir::gpu::BroadcastType::specific_lane;
  auto bcast = mlir::gpu::SubgroupBroadcastOp::create(builder, loc, mlirTy,
                                                       asSrc, laneVal, bcastTy);
  return mlir::UnrealizedConversionCastOp::create(
             builder, loc, mlir::TypeRange{cirTy},
             mlir::ValueRange{bcast.getResult()})
      .getResult(0);
}

/// Emit a gpu.ballot for __builtin_amdgcn_ballot_w32 / _w64.
/// `width` is 32 or 64 (the wavefront size encoded in the builtin name).
static mlir::Value emitBallot(CIRGenFunction &cgf, const CallExpr *expr,
                               unsigned width) {
  CIRGenBuilderTy &builder = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());
  mlir::MLIRContext *ctx = builder.getContext();
  mlir::Value pred = cgf.emitScalarExpr(expr->getArg(0));
  // gpu.ballot requires an i1 predicate.
  mlir::Type i1Ty = mlir::IntegerType::get(ctx, 1);
  mlir::Value predI1 =
      mlir::UnrealizedConversionCastOp::create(
          builder, loc, mlir::TypeRange{i1Ty}, mlir::ValueRange{pred})
          .getResult(0);
  mlir::Type resultMLIRTy = mlir::IntegerType::get(ctx, width);
  mlir::Value result =
      mlir::gpu::BallotOp::create(builder, loc, resultMLIRTy, predI1)
          .getResult();
  mlir::Type cirResultTy = cgf.convertType(expr->getType());
  return mlir::UnrealizedConversionCastOp::create(
             builder, loc, mlir::TypeRange{cirResultTy}, mlir::ValueRange{result})
      .getResult(0);
}

/// Emit gpu.block_dim or gpu.block_dim * gpu.grid_dim for workgroup/grid size.
///
/// `__builtin_amdgcn_workgroup_size_{x,y,z}` = blockDim.{x,y,z}
/// `__builtin_amdgcn_grid_size_{x,y,z}`      = blockDim * gridDim (total threads)
static mlir::Value emitGPUDimension(CIRGenFunction &cgf, const CallExpr *expr,
                                     mlir::gpu::Dimension dim, bool gridTotal) {
  CIRGenBuilderTy &b = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());
  mlir::Value blockDim =
      mlir::gpu::BlockDimOp::create(b, loc, dim).getResult();
  mlir::Value result = blockDim;
  if (gridTotal) {
    mlir::Value gridDim =
        mlir::gpu::GridDimOp::create(b, loc, dim).getResult();
    result = mlir::arith::MulIOp::create(b, loc, blockDim, gridDim).getResult();
  }
  mlir::Type cirTy = cgf.convertType(expr->getType());
  return mlir::UnrealizedConversionCastOp::create(
             b, loc, mlir::TypeRange{cirTy}, mlir::ValueRange{result})
      .getResult(0);
}

/// Map a CIR float type to the corresponding MLIR builtin float type.
static mlir::Type getCIRFloatAsMLIR(mlir::Type cirTy, mlir::MLIRContext *ctx) {
  if (mlir::isa<cir::SingleType>(cirTy))     return mlir::Float32Type::get(ctx);
  if (mlir::isa<cir::DoubleType>(cirTy))     return mlir::Float64Type::get(ctx);
  if (mlir::isa<cir::FP16Type>(cirTy))       return mlir::Float16Type::get(ctx);
  if (mlir::isa<cir::BF16Type>(cirTy))       return mlir::BFloat16Type::get(ctx);
  if (mlir::isa<cir::FP80Type>(cirTy))       return mlir::Float80Type::get(ctx);
  if (mlir::isa<cir::LongDoubleType>(cirTy)) return mlir::Float128Type::get(ctx);
  return {};
}

/// Emit a ROCDL unary floating-point op, bridging CIR↔MLIR float types.
template <typename ROCDLOp>
static mlir::Value emitROCDLUnaryFP(CIRGenFunction &cgf, const CallExpr *expr) {
  CIRGenBuilderTy &b = cgf.getBuilder();
  mlir::Location loc = cgf.getLoc(expr->getExprLoc());
  mlir::MLIRContext *ctx = b.getContext();
  mlir::Value arg = cgf.emitScalarExpr(expr->getArg(0));
  mlir::Type cirTy = arg.getType();
  mlir::Type mlirTy = getCIRFloatAsMLIR(cirTy, ctx);
  mlir::Value asMLIR =
      mlir::UnrealizedConversionCastOp::create(
          b, loc, mlir::TypeRange{mlirTy}, mlir::ValueRange{arg})
          .getResult(0);
  mlir::Value result = ROCDLOp::create(b, loc, mlirTy, asMLIR).getRes();
  return mlir::UnrealizedConversionCastOp::create(
             b, loc, mlir::TypeRange{cirTy}, mlir::ValueRange{result})
      .getResult(0);
}

std::optional<mlir::Value>
CIRGenFunction::emitAMDGPUBuiltinExpr(unsigned builtinId,
                                      const CallExpr *expr) {
  switch (builtinId) {
  case AMDGPU::BI__builtin_amdgcn_s_barrier:
    // Workgroup barrier: synchronises all threads in the block and makes all
    // memory accesses visible.  Emit gpu.barrier which the GPU-to-ROCDL
    // conversion lowers to rocdl.s_barrier (s_barrier instruction).
    mlir::gpu::BarrierOp::create(builder, getLoc(expr->getExprLoc()));
    return mlir::Value{};

  case AMDGPU::BI__builtin_amdgcn_wave_reduce_add_u32:
  case AMDGPU::BI__builtin_amdgcn_wave_reduce_add_u64:
    return emitSubgroupReduce(*this, expr,
                              mlir::gpu::AllReduceOperation::ADD);
  case AMDGPU::BI__builtin_amdgcn_wave_reduce_sub_u32:
  case AMDGPU::BI__builtin_amdgcn_wave_reduce_sub_u64: {
    // gpu.subgroup_reduce has no subtraction; negate the value and reduce ADD.
    CIRGenBuilderTy &b = getBuilder();
    mlir::Location loc = getLoc(expr->getExprLoc());
    mlir::MLIRContext *ctx = b.getContext();
    mlir::Value val = emitScalarExpr(expr->getArg(0));
    mlir::Type cirTy = val.getType();
    unsigned width = mlir::cast<cir::IntType>(cirTy).getWidth();
    mlir::Type mlirTy = mlir::IntegerType::get(ctx, width);
    mlir::Value asMLIR =
        mlir::UnrealizedConversionCastOp::create(
            b, loc, mlir::TypeRange{mlirTy}, mlir::ValueRange{val})
            .getResult(0);
    mlir::Value zero =
        mlir::arith::ConstantIntOp::create(b, loc, mlirTy, 0);
    mlir::Value neg = mlir::arith::SubIOp::create(b, loc, zero, asMLIR);
    mlir::Value result =
        mlir::gpu::SubgroupReduceOp::create(
            b, loc, neg, mlir::gpu::AllReduceOperation::ADD, /*uniform=*/false)
            .getResult();
    return mlir::UnrealizedConversionCastOp::create(
               b, loc, mlir::TypeRange{cirTy}, mlir::ValueRange{result})
        .getResult(0);
  }
  case AMDGPU::BI__builtin_amdgcn_wave_reduce_min_i32:
  case AMDGPU::BI__builtin_amdgcn_wave_reduce_min_i64:
    return emitSubgroupReduce(*this, expr,
                              mlir::gpu::AllReduceOperation::MINSI);
  case AMDGPU::BI__builtin_amdgcn_wave_reduce_min_u32:
  case AMDGPU::BI__builtin_amdgcn_wave_reduce_min_u64:
    return emitSubgroupReduce(*this, expr,
                              mlir::gpu::AllReduceOperation::MINUI);
  case AMDGPU::BI__builtin_amdgcn_wave_reduce_max_i32:
  case AMDGPU::BI__builtin_amdgcn_wave_reduce_max_i64:
    return emitSubgroupReduce(*this, expr,
                              mlir::gpu::AllReduceOperation::MAXSI);
  case AMDGPU::BI__builtin_amdgcn_wave_reduce_max_u32:
  case AMDGPU::BI__builtin_amdgcn_wave_reduce_max_u64:
    return emitSubgroupReduce(*this, expr,
                              mlir::gpu::AllReduceOperation::MAXUI);
  case AMDGPU::BI__builtin_amdgcn_wave_reduce_and_b32:
  case AMDGPU::BI__builtin_amdgcn_wave_reduce_and_b64:
    return emitSubgroupReduce(*this, expr,
                              mlir::gpu::AllReduceOperation::AND);
  case AMDGPU::BI__builtin_amdgcn_wave_reduce_or_b32:
  case AMDGPU::BI__builtin_amdgcn_wave_reduce_or_b64:
    return emitSubgroupReduce(*this, expr,
                              mlir::gpu::AllReduceOperation::OR);
  case AMDGPU::BI__builtin_amdgcn_wave_reduce_xor_b32:
  case AMDGPU::BI__builtin_amdgcn_wave_reduce_xor_b64:
    return emitSubgroupReduce(*this, expr,
                              mlir::gpu::AllReduceOperation::XOR);
  case AMDGPU::BI__builtin_amdgcn_div_scale:
  case AMDGPU::BI__builtin_amdgcn_div_scalef: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_div_fmas:
  case AMDGPU::BI__builtin_amdgcn_div_fmasf: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_ds_swizzle:
  case AMDGPU::BI__builtin_amdgcn_mov_dpp8:
  case AMDGPU::BI__builtin_amdgcn_mov_dpp:
  case AMDGPU::BI__builtin_amdgcn_update_dpp: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_permlane16:
  case AMDGPU::BI__builtin_amdgcn_permlanex16:
  case AMDGPU::BI__builtin_amdgcn_permlane64: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_readlane:
    return emitReadlane(*this, expr, /*firstLane=*/false);
  case AMDGPU::BI__builtin_amdgcn_readfirstlane:
    return emitReadlane(*this, expr, /*firstLane=*/true);
  case AMDGPU::BI__builtin_amdgcn_div_fixup:
  case AMDGPU::BI__builtin_amdgcn_div_fixupf:
  case AMDGPU::BI__builtin_amdgcn_div_fixuph: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_trig_preop:
  case AMDGPU::BI__builtin_amdgcn_trig_preopf: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_rcp:
  case AMDGPU::BI__builtin_amdgcn_rcpf:
  case AMDGPU::BI__builtin_amdgcn_rcph:
  case AMDGPU::BI__builtin_amdgcn_rcp_bf16:
    return emitROCDLUnaryFP<mlir::ROCDL::ROCDLRcp>(*this, expr);
  case AMDGPU::BI__builtin_amdgcn_sqrt:
  case AMDGPU::BI__builtin_amdgcn_sqrtf:
  case AMDGPU::BI__builtin_amdgcn_sqrth:
  case AMDGPU::BI__builtin_amdgcn_sqrt_bf16:
    return emitROCDLUnaryFP<mlir::ROCDL::ROCDLSqrt>(*this, expr);
  case AMDGPU::BI__builtin_amdgcn_rsq:
  case AMDGPU::BI__builtin_amdgcn_rsqf:
  case AMDGPU::BI__builtin_amdgcn_rsqh:
  case AMDGPU::BI__builtin_amdgcn_rsq_bf16:
    return emitROCDLUnaryFP<mlir::ROCDL::ROCDLRsq>(*this, expr);
  case AMDGPU::BI__builtin_amdgcn_rsq_clamp:
  case AMDGPU::BI__builtin_amdgcn_rsq_clampf: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_sinf:
  case AMDGPU::BI__builtin_amdgcn_sinh:
  case AMDGPU::BI__builtin_amdgcn_sin_bf16:
    return emitROCDLUnaryFP<mlir::ROCDL::ROCDLSin>(*this, expr);
  case AMDGPU::BI__builtin_amdgcn_cosf:
  case AMDGPU::BI__builtin_amdgcn_cosh:
  case AMDGPU::BI__builtin_amdgcn_cos_bf16:
    return emitROCDLUnaryFP<mlir::ROCDL::ROCDLCos>(*this, expr);
  case AMDGPU::BI__builtin_amdgcn_dispatch_ptr: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_logf:
  case AMDGPU::BI__builtin_amdgcn_log_bf16:
    return emitROCDLUnaryFP<mlir::ROCDL::ROCDLLog>(*this, expr);
  case AMDGPU::BI__builtin_amdgcn_exp2f:
  case AMDGPU::BI__builtin_amdgcn_exp2_bf16:
    return emitROCDLUnaryFP<mlir::ROCDL::ROCDLExp2>(*this, expr);
  case AMDGPU::BI__builtin_amdgcn_log_clampf: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_ldexp:
  case AMDGPU::BI__builtin_amdgcn_ldexpf:
  case AMDGPU::BI__builtin_amdgcn_ldexph: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_frexp_mant:
  case AMDGPU::BI__builtin_amdgcn_frexp_mantf:
  case AMDGPU::BI__builtin_amdgcn_frexp_manth: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_frexp_exp:
  case AMDGPU::BI__builtin_amdgcn_frexp_expf:
  case AMDGPU::BI__builtin_amdgcn_frexp_exph: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_fract:
  case AMDGPU::BI__builtin_amdgcn_fractf:
  case AMDGPU::BI__builtin_amdgcn_fracth: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_lerp: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_ubfe: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_sbfe: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_ballot_w32:
    return emitBallot(*this, expr, 32);
  case AMDGPU::BI__builtin_amdgcn_ballot_w64:
    return emitBallot(*this, expr, 64);
  case AMDGPU::BI__builtin_amdgcn_inverse_ballot_w32:
  case AMDGPU::BI__builtin_amdgcn_inverse_ballot_w64: {
    CIRGenBuilderTy &b = getBuilder();
    mlir::Location loc = getLoc(expr->getExprLoc());
    mlir::MLIRContext *ctx = b.getContext();
    mlir::Value mask = emitScalarExpr(expr->getArg(0));
    unsigned width = mlir::cast<cir::IntType>(mask.getType()).getWidth();
    mlir::Type mlirIntTy = mlir::IntegerType::get(ctx, width);
    mlir::Value asMask =
        mlir::UnrealizedConversionCastOp::create(
            b, loc, mlir::TypeRange{mlirIntTy}, mlir::ValueRange{mask})
            .getResult(0);
    mlir::Type i1Ty = mlir::IntegerType::get(ctx, 1);
    auto intrOp = mlir::LLVM::CallIntrinsicOp::create(
        b, loc, i1Ty,
        mlir::StringAttr::get(ctx, "llvm.amdgcn.inverse.ballot"),
        mlir::ValueRange{asMask});
    mlir::Value resultI1 = intrOp.getResult(0);
    mlir::Type cirResultTy = convertType(expr->getType());
    return mlir::UnrealizedConversionCastOp::create(
               b, loc, mlir::TypeRange{cirResultTy}, mlir::ValueRange{resultI1})
        .getResult(0);
  }
  case AMDGPU::BI__builtin_amdgcn_tanhf:
  case AMDGPU::BI__builtin_amdgcn_tanhh:
  case AMDGPU::BI__builtin_amdgcn_tanh_bf16:
    return emitROCDLUnaryFP<mlir::ROCDL::ROCDLTanh>(*this, expr);
  case AMDGPU::BI__builtin_amdgcn_uicmp:
  case AMDGPU::BI__builtin_amdgcn_uicmpl:
  case AMDGPU::BI__builtin_amdgcn_sicmp:
  case AMDGPU::BI__builtin_amdgcn_sicmpl: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_fcmp:
  case AMDGPU::BI__builtin_amdgcn_fcmpf: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_class:
  case AMDGPU::BI__builtin_amdgcn_classf:
  case AMDGPU::BI__builtin_amdgcn_classh: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_fmed3f:
  case AMDGPU::BI__builtin_amdgcn_fmed3h: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_ds_append:
  case AMDGPU::BI__builtin_amdgcn_ds_consume: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_global_load_tr_b64_i32:
  case AMDGPU::BI__builtin_amdgcn_global_load_tr_b64_v2i32:
  case AMDGPU::BI__builtin_amdgcn_global_load_tr_b128_v4i16:
  case AMDGPU::BI__builtin_amdgcn_global_load_tr_b128_v4f16:
  case AMDGPU::BI__builtin_amdgcn_global_load_tr_b128_v4bf16:
  case AMDGPU::BI__builtin_amdgcn_global_load_tr_b128_v8i16:
  case AMDGPU::BI__builtin_amdgcn_global_load_tr_b128_v8f16:
  case AMDGPU::BI__builtin_amdgcn_global_load_tr_b128_v8bf16:
  case AMDGPU::BI__builtin_amdgcn_global_load_tr4_b64_v2i32:
  case AMDGPU::BI__builtin_amdgcn_global_load_tr8_b64_v2i32:
  case AMDGPU::BI__builtin_amdgcn_global_load_tr6_b96_v3i32:
  case AMDGPU::BI__builtin_amdgcn_global_load_tr16_b128_v8i16:
  case AMDGPU::BI__builtin_amdgcn_global_load_tr16_b128_v8f16:
  case AMDGPU::BI__builtin_amdgcn_global_load_tr16_b128_v8bf16: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_ds_load_tr4_b64_v2i32:
  case AMDGPU::BI__builtin_amdgcn_ds_load_tr8_b64_v2i32:
  case AMDGPU::BI__builtin_amdgcn_ds_load_tr6_b96_v3i32:
  case AMDGPU::BI__builtin_amdgcn_ds_load_tr16_b128_v8i16:
  case AMDGPU::BI__builtin_amdgcn_ds_load_tr16_b128_v8f16:
  case AMDGPU::BI__builtin_amdgcn_ds_load_tr16_b128_v8bf16: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_ds_read_tr4_b64_v2i32:
  case AMDGPU::BI__builtin_amdgcn_ds_read_tr8_b64_v2i32:
  case AMDGPU::BI__builtin_amdgcn_ds_read_tr6_b96_v3i32:
  case AMDGPU::BI__builtin_amdgcn_ds_read_tr16_b64_v4f16:
  case AMDGPU::BI__builtin_amdgcn_ds_read_tr16_b64_v4bf16:
  case AMDGPU::BI__builtin_amdgcn_ds_read_tr16_b64_v4i16: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_global_load_monitor_b32:
  case AMDGPU::BI__builtin_amdgcn_global_load_monitor_b64:
  case AMDGPU::BI__builtin_amdgcn_global_load_monitor_b128:
  case AMDGPU::BI__builtin_amdgcn_flat_load_monitor_b32:
  case AMDGPU::BI__builtin_amdgcn_flat_load_monitor_b64:
  case AMDGPU::BI__builtin_amdgcn_flat_load_monitor_b128: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_cluster_load_b32:
  case AMDGPU::BI__builtin_amdgcn_cluster_load_b64:
  case AMDGPU::BI__builtin_amdgcn_cluster_load_b128: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_load_to_lds: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_cooperative_atomic_load_32x4B:
  case AMDGPU::BI__builtin_amdgcn_cooperative_atomic_store_32x4B:
  case AMDGPU::BI__builtin_amdgcn_cooperative_atomic_load_16x8B:
  case AMDGPU::BI__builtin_amdgcn_cooperative_atomic_store_16x8B:
  case AMDGPU::BI__builtin_amdgcn_cooperative_atomic_load_8x16B:
  case AMDGPU::BI__builtin_amdgcn_cooperative_atomic_store_8x16B: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_get_fpenv:
  case AMDGPU::BI__builtin_amdgcn_set_fpenv: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_read_exec:
  case AMDGPU::BI__builtin_amdgcn_read_exec_lo:
  case AMDGPU::BI__builtin_amdgcn_read_exec_hi: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_image_bvh_intersect_ray:
  case AMDGPU::BI__builtin_amdgcn_image_bvh_intersect_ray_h:
  case AMDGPU::BI__builtin_amdgcn_image_bvh_intersect_ray_l:
  case AMDGPU::BI__builtin_amdgcn_image_bvh_intersect_ray_lh: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_image_bvh8_intersect_ray:
  case AMDGPU::BI__builtin_amdgcn_image_bvh_dual_intersect_ray: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_ds_bvh_stack_rtn:
  case AMDGPU::BI__builtin_amdgcn_ds_bvh_stack_push4_pop1_rtn:
  case AMDGPU::BI__builtin_amdgcn_ds_bvh_stack_push8_pop1_rtn:
  case AMDGPU::BI__builtin_amdgcn_ds_bvh_stack_push8_pop2_rtn: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_image_load_1d_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_1d_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_1darray_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_1darray_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_2d_f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_2d_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_2d_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_2darray_f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_2darray_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_2darray_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_3d_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_3d_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_cube_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_cube_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_mip_1d_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_mip_1d_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_mip_2d_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_mip_2d_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_mip_2darray_f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_mip_2darray_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_mip_2darray_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_mip_3d_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_mip_3d_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_mip_cube_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_load_mip_cube_v4f16_i32: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_image_store_1d_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_1d_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_1darray_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_1darray_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_2d_f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_2d_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_2d_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_2darray_f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_2darray_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_2darray_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_3d_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_3d_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_cube_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_cube_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_mip_1d_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_mip_1d_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_mip_1darray_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_mip_1darray_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_mip_2d_f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_mip_2d_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_mip_2d_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_mip_2darray_f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_mip_2darray_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_mip_2darray_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_mip_3d_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_mip_3d_v4f16_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_mip_cube_v4f32_i32:
  case AMDGPU::BI__builtin_amdgcn_image_store_mip_cube_v4f16_i32: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_image_sample_1d_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_1d_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_1darray_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_1darray_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_2d_f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_2d_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_2d_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_2darray_f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_2darray_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_2darray_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_3d_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_3d_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_cube_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_cube_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_lz_1d_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_lz_1d_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_l_1d_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_l_1d_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_d_1d_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_d_1d_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_lz_2d_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_lz_2d_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_lz_2d_f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_l_2d_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_l_2d_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_l_2d_f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_d_2d_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_d_2d_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_d_2d_f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_lz_3d_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_lz_3d_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_l_3d_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_l_3d_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_d_3d_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_d_3d_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_lz_cube_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_lz_cube_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_l_cube_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_l_cube_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_lz_1darray_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_lz_1darray_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_l_1darray_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_l_1darray_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_d_1darray_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_d_1darray_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_lz_2darray_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_lz_2darray_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_lz_2darray_f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_l_2darray_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_l_2darray_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_l_2darray_f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_d_2darray_v4f32_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_d_2darray_v4f16_f32:
  case AMDGPU::BI__builtin_amdgcn_image_sample_d_2darray_f32_f32: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_image_gather4_lz_2d_v4f32_f32: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_mfma_scale_f32_16x16x128_f8f6f4:
  case AMDGPU::BI__builtin_amdgcn_mfma_scale_f32_32x32x64_f8f6f4: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_wmma_bf16_16x16x16_bf16_w32:
  case AMDGPU::BI__builtin_amdgcn_wmma_bf16_16x16x16_bf16_tied_w32:
  case AMDGPU::BI__builtin_amdgcn_wmma_bf16_16x16x16_bf16_w64:
  case AMDGPU::BI__builtin_amdgcn_wmma_bf16_16x16x16_bf16_tied_w64:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x16_f16_w32:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x16_f16_tied_w32:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x16_f16_w64:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x16_f16_tied_w64:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf16_w32:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf16_w64:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_f16_w32:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_f16_w64:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu4_w32:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu4_w64:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu8_w32:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu8_w64:
  case AMDGPU::BI__builtin_amdgcn_wmma_bf16_16x16x16_bf16_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_bf16_16x16x16_bf16_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x16_f16_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x16_f16_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf16_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf16_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_f16_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_f16_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu4_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu4_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu8_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x16_iu8_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_fp8_fp8_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_fp8_bf8_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_fp8_bf8_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf8_fp8_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf8_fp8_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf8_bf8_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x16_bf8_bf8_w64_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x32_iu4_w32_gfx12:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x32_iu4_w64_gfx12: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_f16_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_f16_w64:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_bf16_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_bf16_w64:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f16_16x16x32_f16_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f16_16x16x32_f16_w64:
  case AMDGPU::BI__builtin_amdgcn_swmmac_bf16_16x16x32_bf16_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_bf16_16x16x32_bf16_w64:
  case AMDGPU::BI__builtin_amdgcn_swmmac_i32_16x16x32_iu8_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_i32_16x16x32_iu8_w64:
  case AMDGPU::BI__builtin_amdgcn_swmmac_i32_16x16x32_iu4_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_i32_16x16x32_iu4_w64:
  case AMDGPU::BI__builtin_amdgcn_swmmac_i32_16x16x64_iu4_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_i32_16x16x64_iu4_w64:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_fp8_fp8_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_fp8_fp8_w64:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_fp8_bf8_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_fp8_bf8_w64:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_bf8_fp8_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_bf8_fp8_w64:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_bf8_bf8_w32:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x32_bf8_bf8_w64: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x4_f32:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x32_bf16:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x32_f16:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x32_f16:
  case AMDGPU::BI__builtin_amdgcn_wmma_bf16_16x16x32_bf16:
  case AMDGPU::BI__builtin_amdgcn_wmma_bf16f32_16x16x32_bf16:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x64_fp8_fp8:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x64_fp8_bf8:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x64_bf8_fp8:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x64_bf8_bf8:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x64_fp8_fp8:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x64_fp8_bf8:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x64_bf8_fp8:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x64_bf8_bf8:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x128_fp8_fp8:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x128_fp8_bf8:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x128_bf8_fp8:
  case AMDGPU::BI__builtin_amdgcn_wmma_f16_16x16x128_bf8_bf8:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x128_fp8_fp8:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x128_fp8_bf8:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x128_bf8_fp8:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x128_bf8_bf8:
  case AMDGPU::BI__builtin_amdgcn_wmma_i32_16x16x64_iu8:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_16x16x128_f8f6f4:
  case AMDGPU::BI__builtin_amdgcn_wmma_f32_32x16x128_f4:
  case AMDGPU::BI__builtin_amdgcn_wmma_scale_f32_16x16x128_f8f6f4:
  case AMDGPU::BI__builtin_amdgcn_wmma_scale16_f32_16x16x128_f8f6f4:
  case AMDGPU::BI__builtin_amdgcn_wmma_scale_f32_32x16x128_f4:
  case AMDGPU::BI__builtin_amdgcn_wmma_scale16_f32_32x16x128_f4: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x64_f16:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x64_bf16:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f16_16x16x64_f16:
  case AMDGPU::BI__builtin_amdgcn_swmmac_bf16_16x16x64_bf16:
  case AMDGPU::BI__builtin_amdgcn_swmmac_bf16f32_16x16x64_bf16:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x128_fp8_fp8:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x128_fp8_bf8:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x128_bf8_fp8:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f32_16x16x128_bf8_bf8:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f16_16x16x128_fp8_fp8:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f16_16x16x128_fp8_bf8:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f16_16x16x128_bf8_fp8:
  case AMDGPU::BI__builtin_amdgcn_swmmac_f16_16x16x128_bf8_bf8:
  case AMDGPU::BI__builtin_amdgcn_swmmac_i32_16x16x128_iu8: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  // amdgcn workgroup size (= blockDim.{x,y,z})
  case AMDGPU::BI__builtin_amdgcn_workgroup_size_x:
    return emitGPUDimension(*this, expr, mlir::gpu::Dimension::x, false);
  case AMDGPU::BI__builtin_amdgcn_workgroup_size_y:
    return emitGPUDimension(*this, expr, mlir::gpu::Dimension::y, false);
  case AMDGPU::BI__builtin_amdgcn_workgroup_size_z:
    return emitGPUDimension(*this, expr, mlir::gpu::Dimension::z, false);
  // amdgcn grid size (= blockDim * gridDim, total threads in dimension)
  case AMDGPU::BI__builtin_amdgcn_grid_size_x:
    return emitGPUDimension(*this, expr, mlir::gpu::Dimension::x, true);
  case AMDGPU::BI__builtin_amdgcn_grid_size_y:
    return emitGPUDimension(*this, expr, mlir::gpu::Dimension::y, true);
  case AMDGPU::BI__builtin_amdgcn_grid_size_z:
    return emitGPUDimension(*this, expr, mlir::gpu::Dimension::z, true);
  case AMDGPU::BI__builtin_r600_recipsqrt_ieee:
  case AMDGPU::BI__builtin_r600_recipsqrt_ieeef: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_alignbit: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_fence: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  // -----------------------------------------------------------------------
  // Wrapping atomic increment / decrement
  //
  // These do NOT map to a plain atomicrmw — they have wrapping semantics:
  //   inc: *ptr = (*ptr >= val) ? 0 : *ptr + 1
  //   dec: *ptr = (*ptr == 0 || *ptr > val) ? val : *ptr - 1
  // CIR AtomicFetchKind::UIncWrap / UDecWrap lower to the AMDGPU hardware
  // wrapping instructions via the standard CIR-to-LLVM lowering path.
  // -----------------------------------------------------------------------
  case AMDGPU::BI__builtin_amdgcn_atomic_inc32:
  case AMDGPU::BI__builtin_amdgcn_atomic_inc64:
    return emitCIRAtomicIncDec(*this, expr, cir::AtomicFetchKind::UIncWrap);
  case AMDGPU::BI__builtin_amdgcn_atomic_dec32:
  case AMDGPU::BI__builtin_amdgcn_atomic_dec64:
    return emitCIRAtomicIncDec(*this, expr, cir::AtomicFetchKind::UDecWrap);

  // -----------------------------------------------------------------------
  // Floating-point atomic add — DS (shared/local) address space
  // -----------------------------------------------------------------------
  case AMDGPU::BI__builtin_amdgcn_ds_atomic_fadd_f32:
  case AMDGPU::BI__builtin_amdgcn_ds_faddf:
  case AMDGPU::BI__builtin_amdgcn_ds_atomic_fadd_f64:
  case AMDGPU::BI__builtin_amdgcn_ds_atomic_fadd_v2f16:
  case AMDGPU::BI__builtin_amdgcn_ds_atomic_fadd_v2bf16:
    return emitCIRAtomicFetch(*this, expr, cir::AtomicFetchKind::Add);

  // -----------------------------------------------------------------------
  // Floating-point atomic min/max — DS (shared/local) address space
  // -----------------------------------------------------------------------
  case AMDGPU::BI__builtin_amdgcn_ds_fminf:
    return emitCIRAtomicFetch(*this, expr, cir::AtomicFetchKind::Min);
  case AMDGPU::BI__builtin_amdgcn_ds_fmaxf:
    return emitCIRAtomicFetch(*this, expr, cir::AtomicFetchKind::Max);

  // -----------------------------------------------------------------------
  // Floating-point atomic add — global address space
  // -----------------------------------------------------------------------
  case AMDGPU::BI__builtin_amdgcn_global_atomic_fadd_f32:
  case AMDGPU::BI__builtin_amdgcn_global_atomic_fadd_f64:
  case AMDGPU::BI__builtin_amdgcn_global_atomic_fadd_v2f16:
  case AMDGPU::BI__builtin_amdgcn_global_atomic_fadd_v2bf16:
    return emitCIRAtomicFetch(*this, expr, cir::AtomicFetchKind::Add);

  // -----------------------------------------------------------------------
  // Floating-point atomic min/max — global address space
  // -----------------------------------------------------------------------
  case AMDGPU::BI__builtin_amdgcn_global_atomic_fmin_f64:
    return emitCIRAtomicFetch(*this, expr, cir::AtomicFetchKind::Min);
  case AMDGPU::BI__builtin_amdgcn_global_atomic_fmax_f64:
    return emitCIRAtomicFetch(*this, expr, cir::AtomicFetchKind::Max);

  // -----------------------------------------------------------------------
  // Floating-point atomic add/min/max — flat (generic) address space
  // -----------------------------------------------------------------------
  case AMDGPU::BI__builtin_amdgcn_flat_atomic_fadd_f32:
  case AMDGPU::BI__builtin_amdgcn_flat_atomic_fadd_f64:
  case AMDGPU::BI__builtin_amdgcn_flat_atomic_fadd_v2f16:
  case AMDGPU::BI__builtin_amdgcn_flat_atomic_fadd_v2bf16:
    return emitCIRAtomicFetch(*this, expr, cir::AtomicFetchKind::Add);
  case AMDGPU::BI__builtin_amdgcn_flat_atomic_fmin_f64:
    return emitCIRAtomicFetch(*this, expr, cir::AtomicFetchKind::Min);
  case AMDGPU::BI__builtin_amdgcn_flat_atomic_fmax_f64:
    return emitCIRAtomicFetch(*this, expr, cir::AtomicFetchKind::Max);
  case AMDGPU::BI__builtin_amdgcn_s_sendmsg_rtn:
  case AMDGPU::BI__builtin_amdgcn_s_sendmsg_rtnl: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_permlane16_swap:
  case AMDGPU::BI__builtin_amdgcn_permlane32_swap: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_bitop3_b32:
  case AMDGPU::BI__builtin_amdgcn_bitop3_b16: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_make_buffer_rsrc: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_store_b8:
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_store_b16:
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_store_b32:
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_store_b64:
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_store_b96:
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_store_b128: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_load_b8:
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_load_b16:
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_load_b32:
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_load_b64:
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_load_b96:
  case AMDGPU::BI__builtin_amdgcn_raw_buffer_load_b128: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_raw_ptr_buffer_atomic_add_i32: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_raw_ptr_buffer_atomic_fadd_f32:
  case AMDGPU::BI__builtin_amdgcn_raw_ptr_buffer_atomic_fadd_v2f16: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_raw_ptr_buffer_atomic_fmin_f32:
  case AMDGPU::BI__builtin_amdgcn_raw_ptr_buffer_atomic_fmin_f64: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_raw_ptr_buffer_atomic_fmax_f32:
  case AMDGPU::BI__builtin_amdgcn_raw_ptr_buffer_atomic_fmax_f64: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case AMDGPU::BI__builtin_amdgcn_s_prefetch_data: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case Builtin::BIlogbf:
  case Builtin::BI__builtin_logbf: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  case Builtin::BIscalbnf:
  case Builtin::BI__builtin_scalbnf:
  case Builtin::BIscalbn:
  case Builtin::BI__builtin_scalbn: {
    cgm.errorNYI(expr->getSourceRange(),
                 std::string("unimplemented AMDGPU builtin call: ") +
                     getContext().BuiltinInfo.getName(builtinId));
    return mlir::Value{};
  }
  default:
    return std::nullopt;
  }
}
