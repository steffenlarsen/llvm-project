//===- OffloadEliminateCoveredGuards.cpp - Prove tail guards dead ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A kernel that covers `n` elements in blocks of `d` is launched with
// `gridDim = ceil(n/d)` and carries tail guards -- `if (index >= n) return;` --
// for the final, partially populated block.  When `n % d == 0` that block is
// full and every guard is dead, which is why libraries hand-write a fast
// variant: ggml's `dequantize_block_q8_0_f16<need_check=false>` is exactly
// this, and it is worth ~6% of prompt processing.
//
// The fact needed to remove the guard is `d*blockIdx.x + rest < n`, which
// relates two runtime unknowns.  LLVM cannot use it: its value analyses are
// per-value interval domains, so an `llvm.assume`, a dominating branch and a
// slack-variable rewrite were all measured to change nothing.  The information
// has to be *acted on*, not described -- so this pass does the proof itself.
//
// It runs before CIRFlattenCFG, where `cir.for` is still structured (giving
// exact induction variable bounds) and the launch is still visible (giving the
// grid relation and the block size).
//
// On success it clones the kernel, folds the proven comparisons away in the
// copy, and dispatches to it behind `n % d == 0`.  The original keeps its
// guards, so the fast copy is only ever entered where the premises hold.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/CIRTransformUtils.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "cir-offload-eliminate-covered-guards"

using namespace mlir;

namespace {

static constexpr llvm::StringRef kCoveredInfix = "$guards";

/// A comparison the launch shape proves constant, and what it folds to.
struct DeadGuard {
  cir::CmpOp cmp;
  bool foldTo;
};

/// The extent a guard is written against, as a rational multiple of the
/// kernel argument the grid covers: `num/den * arg`.
///
/// A guard does not have to compare against the argument itself.  ggml's
/// dequantize loop bounds a *byte* offset, so both sides carry a factor of
/// `sizeof(block_q8_0)/QK8_0` and the extent reaches CIR as `(k*34)/32`.
struct ArgScale {
  int64_t num = 1;
  int64_t den = 1;
};

/// Largest scale factor worth following; keeps `num * divisor` well inside
/// int64 no matter how the expression nests.
static constexpr int64_t kMaxScale = 1 << 20;

/// Upper bound of an expression, as `blockIdTerm * (n/d) + constant`.
///
/// The block id has to be carried symbolically because its bound is `n/d - 1`,
/// which is not an integer until it is combined with the `d*blockIdx` factor
/// the guard is written against.  Everything else must reduce to a constant.
struct MaxBound {
  /// Coefficient of `blockIdx.x`; the bound contributed is coeff * (n/d - 1).
  int64_t blockIdCoeff = 0;
  /// Everything that bounds to a constant.
  int64_t constant = 0;
  /// Narrowest integer width any step of the expression was computed in.
  ///
  /// `d*blockIdx.x` is frequently evaluated in 32 bits and only then widened
  /// (`CUDA_Q8_0_NE_ALIGN*blockIdx.x` assigned to an int64_t), so the product
  /// can wrap before the comparison ever sees it.  Rather than argue about
  /// whether that is reachable, the width is recorded and the host guard is
  /// extended to exclude it.
  unsigned minWidth = 64;
};

static unsigned widthOf(Type ty) {
  if (auto intTy = mlir::dyn_cast<cir::IntType>(ty))
    return intTy.getWidth();
  return 64;
}

static MaxBound joinWidth(MaxBound b, Type ty) {
  b.minWidth = std::min(b.minWidth, widthOf(ty));
  return b;
}

/// Find the slot behind an address, looking through the address-space casts
/// CIRGen puts on every device-side local (allocas land in
/// target_address_space(5) and are cast to generic before use).
static cir::AllocaOp findSlot(Value addr) {
  while (auto cast = addr.getDefiningOp<cir::CastOp>())
    addr = cast.getOperand();
  return addr.getDefiningOp<cir::AllocaOp>();
}

/// Users of a slot, reached through those same casts.
static void collectSlotUsers(cir::AllocaOp slot,
                             SmallVectorImpl<Operation *> &out) {
  SmallVector<Value, 4> worklist{slot.getResult()};
  while (!worklist.empty()) {
    Value v = worklist.pop_back_val();
    for (Operation *user : v.getUsers()) {
      if (auto cast = dyn_cast<cir::CastOp>(user)) {
        worklist.push_back(cast.getResult());
        continue;
      }
      out.push_back(user);
    }
  }
}

/// Does \p addr refer to \p slot, modulo those casts?
static bool addrIsSlot(Value addr, cir::AllocaOp slot) {
  return findSlot(addr) == slot;
}

/// Resolve \p v to a constant, additionally looking through a local slot that
/// is written exactly once and never escapes.
///
/// `tryResolveToConstant` stops at the address-space cast every device-side
/// local carries, so a `constexpr` that CIRGen spilled to a slot is invisible
/// to it.  ggml's loop bound `nint` is exactly this, and without the hop the
/// enclosing loop looks unbounded.
static std::optional<int64_t> tryConstThroughSlot(Value v) {
  if (std::optional<int64_t> c = cir::tryResolveToConstant(v))
    return c;

  auto load = v.getDefiningOp<cir::LoadOp>();
  if (!load)
    return std::nullopt;
  cir::AllocaOp slot = findSlot(load.getAddr());
  if (!slot)
    return std::nullopt;

  cir::StoreOp only;
  SmallVector<Operation *, 8> slotUsers;
  collectSlotUsers(slot, slotUsers);
  for (Operation *user : slotUsers) {
    if (auto store = dyn_cast<cir::StoreOp>(user)) {
      if (only)
        return std::nullopt;
      only = store;
      continue;
    }
    if (!isa<cir::LoadOp>(user))
      return std::nullopt;
  }
  if (!only)
    return std::nullopt;
  return cir::tryResolveToConstant(only.getValue());
}

/// Resolve \p v to a constant, additionally recognising `blockDim.x`.
///
/// The launch pins the block size and the pass has already checked that every
/// launch site of this kernel agrees on it, so `blockDim.x` is as good as a
/// literal here.  It has to be, because the common way to write the stride is
/// `blockDim.x*blockIdx.x` rather than a spelled-out constant -- ggml uses the
/// literal form in `dequantize_block_q8_0_f16` but the `blockDim.x` form in
/// `quantize_q8_1`, and a literal-only test sees only the first.
static std::optional<int64_t> tryConstWithLaunchFacts(Value v,
                                                      int64_t blockDimX) {
  if (std::optional<int64_t> c = tryConstThroughSlot(v))
    return c;

  Value cur = v;
  while (auto cast = cur.getDefiningOp<cir::CastOp>())
    cur = cast.getOperand();
  if (auto blockDim = cur.getDefiningOp<cir::OffloadBlockDimOp>())
    if (blockDim.getDimension() == cir::OffloadDimension::X && blockDimX > 0)
      return blockDimX;

  return std::nullopt;
}

/// Largest value a structured `cir.for` induction variable reaches.
///
/// Only constant init/bound/step: `for (i = A; i < B; i += S)` peaks at
/// `A + floor((B-1-A)/S)*S`, not `B-1`, and the difference is what decides
/// whether the guard is provably dead.
static std::optional<int64_t> maxOfForInductionVar(cir::ForOp forOp,
                                                   cir::AllocaOp ivSlot) {
  // cond region: `%iv = load ivSlot; %c = cmp lt %iv, BOUND; condition(%c)`.
  std::optional<int64_t> bound;
  bool strict = true;
  forOp.getCond().walk([&](cir::CmpOp cmp) {
    if (bound)
      return;
    Value lhs = cmp.getLhs();
    auto load = lhs.getDefiningOp<cir::LoadOp>();
    if (!load || !addrIsSlot(load.getAddr(), ivSlot))
      return;
    if (cmp.getKind() != cir::CmpOpKind::lt && cmp.getKind() != cir::CmpOpKind::le)
      return;
    strict = cmp.getKind() == cir::CmpOpKind::lt;
    bound = tryConstThroughSlot(cmp.getRhs());
  });
  if (!bound)
    return std::nullopt;

  // step region: `%iv = load ivSlot; %next = add %iv, STEP; store %next`.
  std::optional<int64_t> step;
  forOp.getStep().walk([&](cir::AddOp add) {
    if (step)
      return;
    auto load = add.getLhs().getDefiningOp<cir::LoadOp>();
    if (!load || !addrIsSlot(load.getAddr(), ivSlot))
      return;
    step = tryConstThroughSlot(add.getRhs());
  });
  if (!step || *step <= 0)
    return std::nullopt;

  // init: the single store into the slot that dominates the loop.
  std::optional<int64_t> init;
  SmallVector<Operation *, 8> slotUsers;
  collectSlotUsers(ivSlot, slotUsers);
  for (Operation *user : slotUsers) {
    auto store = dyn_cast<cir::StoreOp>(user);
    if (!store || forOp->isProperAncestor(store))
      continue;
    if (std::optional<int64_t> c = tryConstThroughSlot(store.getValue()))
      init = c;
  }
  if (!init)
    return std::nullopt;

  const int64_t last = strict ? *bound - 1 : *bound;
  if (last < *init)
    return *init;
  return *init + ((last - *init) / *step) * *step;
}

/// Compute an upper bound for \p v, or nullopt when anything is unrecognised.
///
/// Deliberately conservative: every construct not explicitly handled bails,
/// because an over-estimate that is too small would license deleting a live
/// guard.
static std::optional<MaxBound> computeMax(Value v, int64_t blockDimX,
                                          unsigned depth = 0) {
  if (depth > 12)
    return std::nullopt;

  if (std::optional<int64_t> c = cir::tryResolveToConstant(v))
    return MaxBound{0, *c};

  Operation *def = v.getDefiningOp();
  if (!def)
    return std::nullopt;

  if (auto blockId = dyn_cast<cir::OffloadBlockIdOp>(def)) {
    if (blockId.getDimension() != cir::OffloadDimension::X)
      return std::nullopt;
    return joinWidth(MaxBound{1, 0}, blockId.getType());
  }

  if (auto threadId = dyn_cast<cir::OffloadThreadIdOp>(def)) {
    if (threadId.getDimension() != cir::OffloadDimension::X || blockDimX <= 0)
      return std::nullopt;
    return joinWidth(MaxBound{0, blockDimX - 1}, threadId.getType());
  }

  if (auto blockDim = dyn_cast<cir::OffloadBlockDimOp>(def)) {
    if (blockDim.getDimension() != cir::OffloadDimension::X || blockDimX <= 0)
      return std::nullopt;
    return joinWidth(MaxBound{0, blockDimX}, blockDim.getType());
  }

  if (isa<cir::CastOp>(def) && def->getNumOperands() == 1)
    return computeMax(def->getOperand(0), blockDimX, depth + 1);

  // A local or a loop induction variable, read back out of its slot.
  if (auto load = dyn_cast<cir::LoadOp>(def)) {
    cir::AllocaOp slot = findSlot(load.getAddr());
    if (!slot)
      return std::nullopt;

    // Induction variable of an enclosing structured loop.
    for (Operation *parent = load->getParentOp(); parent;
         parent = parent->getParentOp()) {
      if (auto forOp = dyn_cast<cir::ForOp>(parent)) {
        if (std::optional<int64_t> m = maxOfForInductionVar(forOp, slot))
          return MaxBound{0, *m};
      }
    }

    // Otherwise a plain single-assignment local.
    cir::StoreOp only;
    SmallVector<Operation *, 8> slotUsers;
    collectSlotUsers(slot, slotUsers);
    for (Operation *user : slotUsers) {
      if (auto store = dyn_cast<cir::StoreOp>(user)) {
        if (only)
          return std::nullopt;
        only = store;
        continue;
      }
      if (!isa<cir::LoadOp>(user))
        return std::nullopt;
    }
    if (!only)
      return std::nullopt;
    return computeMax(only.getValue(), blockDimX, depth + 1);
  }

  if (auto add = dyn_cast<cir::AddOp>(def)) {
    auto l = computeMax(add.getLhs(), blockDimX, depth + 1);
    auto r = computeMax(add.getRhs(), blockDimX, depth + 1);
    if (!l || !r)
      return std::nullopt;
    MaxBound sum{l->blockIdCoeff + r->blockIdCoeff, l->constant + r->constant,
                 std::min(l->minWidth, r->minWidth)};
    return joinWidth(sum, add.getType());
  }

  if (auto mul = dyn_cast<cir::MulOp>(def)) {
    // One side has to be a constant, otherwise the product of two bounds is
    // not a bound this representation can hold.
    auto lc = tryConstWithLaunchFacts(mul.getLhs(), blockDimX);
    auto rc = tryConstWithLaunchFacts(mul.getRhs(), blockDimX);
    Value other;
    int64_t factor = 0;
    if (lc && *lc > 0) {
      factor = *lc;
      other = mul.getRhs();
    } else if (rc && *rc > 0) {
      factor = *rc;
      other = mul.getLhs();
    } else {
      return std::nullopt;
    }
    auto o = computeMax(other, blockDimX, depth + 1);
    if (!o)
      return std::nullopt;
    MaxBound prod{o->blockIdCoeff * factor, o->constant * factor, o->minWidth};
    return joinWidth(prod, mul.getType());
  }

  if (auto div = dyn_cast<cir::DivOp>(def)) {
    std::optional<int64_t> c = tryConstWithLaunchFacts(div.getRhs(), blockDimX);
    if (!c || *c <= 0)
      return std::nullopt;
    auto o = computeMax(div.getLhs(), blockDimX, depth + 1);
    if (!o)
      return std::nullopt;
    // Both terms have to divide exactly.  Integer division floors, and a
    // floored bound is *smaller* than the true one, which would license
    // deleting a guard that can still fire.  Rounding up is not expressible
    // in this representation, so anything inexact bails.
    if (o->blockIdCoeff % *c != 0 || o->constant % *c != 0)
      return std::nullopt;
    MaxBound quot{o->blockIdCoeff / *c, o->constant / *c, o->minWidth};
    return joinWidth(quot, div.getType());
  }

  LLVM_DEBUG(llvm::dbgs() << "GUARDDBG     unhandled: "
                          << def->getName().getStringRef() << "\n");
  return std::nullopt;
}

struct OffloadEliminateCoveredGuardsPass
    : public PassWrapper<OffloadEliminateCoveredGuardsPass,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      OffloadEliminateCoveredGuardsPass)

  OffloadEliminateCoveredGuardsPass() = default;
  OffloadEliminateCoveredGuardsPass(bool enabled) : passEnabled(enabled) {}

  StringRef getArgument() const override {
    return "cir-offload-eliminate-covered-guards";
  }
  StringRef getDescription() const override {
    return "Prove tail guards dead when the launch covers the range exactly";
  }

  void runOnOperation() override {
    if (!passEnabled)
      return;
    ModuleOp module = getOperation();

    module.walk([&](cir::OffloadModuleOp offloadMod) {
      for (auto kernel : offloadMod.getOps<cir::OffloadFuncOp>()) {
        if (!kernel.isKernel() || kernel.isExternal())
          continue;
        analyzeKernel(module, kernel);
      }
    });
  }

  void analyzeKernel(ModuleOp module, cir::OffloadFuncOp kernel) {
    // A clone of a clone gains nothing and would recurse on a later run.
    if (kernel.getSymName().contains(kCoveredInfix))
      return;

    SmallVector<cir::OffloadKernelLaunchOp> launchOps;
    module.walk([&](cir::OffloadKernelLaunchOp op) {
      if (op.getKernelLeafName() == kernel.getSymName())
        launchOps.push_back(op);
    });
    if (launchOps.empty())
      return;

    // The grid relation and the block size both have to agree across sites,
    // since one conclusion has to hold for all of them.
    std::optional<cir::GridDimRelation> rel;
    std::optional<int64_t> blockDimX;
    for (auto launch : launchOps) {
      std::optional<cir::GridDimRelation> r =
          cir::findGridDimCandidate(launch.getGridSizeX(), launch);
      if (!r || r->divisor <= 1)
        return;
      std::optional<int64_t> b =
          cir::tryResolveToConstant(launch.getBlockSizeX());
      if (!b || *b <= 0)
        return;
      if (!rel) {
        rel = *r;
        blockDimX = *b;
        continue;
      }
      if (rel->argIndex != r->argIndex || rel->divisor != r->divisor ||
          *blockDimX != *b)
        return;
    }
    if (!rel || !blockDimX)
      return;

    const unsigned argIdx = rel->argIndex;
    const int64_t d = rel->divisor;
    if (argIdx >= kernel.getNumArguments())
      return;

    LLVM_DEBUG(llvm::dbgs()
               << "GUARDDBG " << kernel.getSymName() << ": arg" << argIdx
               << " d=" << d << " blockDim.x=" << *blockDimX << "\n");

    SmallVector<DeadGuard> dead;
    unsigned minWidth = proveGuards(kernel, argIdx, d, *blockDimX, dead);
    if (dead.empty())
      return;

    // Clone, then re-prove on the clone: the second run finds the same
    // comparisons in the copy, which avoids having to map operations across
    // the clone.
    std::string cloneName = (kernel.getSymName() + kCoveredInfix).str();
    SymbolTable symTable(offloadModOf(kernel));
    if (symTable.lookup(cloneName))
      return;

    auto clone = cast<cir::OffloadFuncOp>(kernel->clone());
    SymbolTable::setSymbolName(clone, cloneName);
    symTable.insert(clone);
    clone->moveAfter(kernel);

    SmallVector<DeadGuard> cloneDead;
    proveGuards(clone, argIdx, d, *blockDimX, cloneDead);
    if (cloneDead.size() != dead.size()) {
      clone->erase();
      return;
    }

    MLIRContext *ctx = module.getContext();
    auto boolTy = cir::BoolType::get(ctx);
    for (DeadGuard &g : cloneDead) {
      OpBuilder b(g.cmp);
      auto cst = cir::ConstantOp::create(
          b, g.cmp.getLoc(), cir::BoolAttr::get(ctx, boolTy, g.foldTo));
      g.cmp.getResult().replaceAllUsesWith(cst.getResult());
      g.cmp.erase();
    }

    // The original keeps its guards and stays reachable, so the fast clone
    // only has to be entered when its premises hold.
    for (auto launch : launchOps)
      emitGuardedDispatch(ctx, launch, argIdx, d, minWidth, cloneName);

    // Tell MultiversionDivisibility to leave this kernel alone; it would
    // otherwise build a second dispatch on the very same condition.
    kernel->setAttr("covered_guards_handled", UnitAttr::get(ctx));

    LLVM_DEBUG(llvm::dbgs() << "GUARDDBG   -> " << cloneName << " with "
                            << cloneDead.size() << " guard(s) folded, minWidth="
                            << minWidth << "\n");
  }

  /// Find guards this launch shape proves dead.  Returns the narrowest
  /// arithmetic width across them.
  unsigned proveGuards(cir::OffloadFuncOp kernel, unsigned argIdx, int64_t d,
                       int64_t blockDimX, SmallVectorImpl<DeadGuard> &out) {
    unsigned minWidth = 64;
    kernel.walk([&](cir::CmpOp cmp) {
      cir::CmpOpKind kind = cmp.getKind();
      if (kind != cir::CmpOpKind::ge && kind != cir::CmpOpKind::gt &&
          kind != cir::CmpOpKind::lt && kind != cir::CmpOpKind::le)
        return;

      // Which side is the covered extent, and at what scale?
      Value indexSide;
      bool extentOnRhs;
      std::optional<ArgScale> scale;
      if ((scale = matchScaledArg(cmp.getRhs(), kernel, argIdx))) {
        indexSide = cmp.getLhs();
        extentOnRhs = true;
      } else if ((scale = matchScaledArg(cmp.getLhs(), kernel, argIdx))) {
        indexSide = cmp.getRhs();
        extentOnRhs = false;
      } else {
        return;
      }

      // The block stride in the units the guard counts in.  Requiring this to
      // be exact is also what makes the *runtime* extent exact: integer
      // division truncates, so `(num*n)/den` is only the rational value when
      // `den` divides `num*n`.  The host guard already establishes `n = m*d`,
      // so `den | num*d` gives `den | num*n` for free.
      if (scale->num > kMaxScale / std::max<int64_t>(d, 1))
        return;
      const int64_t scaledNumer = scale->num * d;
      if (scaledNumer % scale->den != 0) {
        LLVM_DEBUG(llvm::dbgs()
                   << "GUARDDBG   cmp: extent scale " << scale->num << "/"
                   << scale->den << " does not divide the block stride "
                   << d << ", skipped\n");
        return;
      }
      const int64_t dScaled = scaledNumer / scale->den;
      if (dScaled <= 0)
        return;

      std::optional<MaxBound> bound = computeMax(indexSide, blockDimX);
      if (!bound) {
        LLVM_DEBUG(llvm::dbgs()
                   << "GUARDDBG   cmp: index side not affine, skipped\n");
        return;
      }

      // max(index) = blockIdCoeff*(n/d - 1) + constant.  With the coefficient
      // equal to the scaled block stride this is `extent - dScaled + constant`,
      // so the guard is dead exactly when constant < dScaled.  Other
      // coefficients are not expressible as a multiple of the extent and are
      // left alone.
      if (bound->blockIdCoeff != dScaled) {
        LLVM_DEBUG(llvm::dbgs()
                   << "GUARDDBG   cmp: blockId coeff " << bound->blockIdCoeff
                   << " != " << dScaled << ", skipped\n");
        return;
      }
      if (bound->constant >= dScaled) {
        LLVM_DEBUG(llvm::dbgs() << "GUARDDBG   cmp: max(index) = extent - "
                                << dScaled << " + " << bound->constant
                                << "  => can exceed extent, guard is live\n");
        return;
      }

      // index < extent is established.  Map that onto this predicate.
      bool foldTo;
      if (extentOnRhs)
        foldTo = kind == cir::CmpOpKind::lt || kind == cir::CmpOpKind::le;
      else
        foldTo = kind == cir::CmpOpKind::gt || kind == cir::CmpOpKind::ge;

      minWidth = std::min(minWidth, bound->minWidth);
      out.push_back({cmp, foldTo});
      LLVM_DEBUG(llvm::dbgs()
                 << "GUARDDBG   cmp: extent = " << scale->num << "*n/"
                 << scale->den << ", max(index) = extent - " << dScaled << " + "
                 << bound->constant << "  => PROVABLY DEAD (fold to "
                 << (foldTo ? "true" : "false") << ")\n");
    });
    return minWidth;
  }

  /// `if (n % d == 0 && n <= widthLimit) fast(...) else original(...)`.
  ///
  /// The width term is what makes the fold sound when the index arithmetic is
  /// evaluated in fewer bits than the comparison: the proof assumes
  /// `d*blockIdx.x` does not wrap, so the clone is simply not entered for
  /// extents large enough to make it wrap.
  void emitGuardedDispatch(MLIRContext *ctx, cir::OffloadKernelLaunchOp launch,
                           unsigned argIdx, int64_t d, unsigned minWidth,
                           StringRef cloneName) {
    Value extent = launch.getKernelOperands()[argIdx];
    auto intTy = mlir::dyn_cast<cir::IntType>(extent.getType());
    if (!intTy || intTy.getWidth() > 64)
      return;

    OpBuilder builder(launch);
    Location loc = launch.getLoc();
    auto boolTy = cir::BoolType::get(ctx);

    auto constOf = [&](int64_t v) -> Value {
      llvm::APInt wide(64, static_cast<uint64_t>(v), /*isSigned=*/true);
      llvm::APInt fitted = wide.trunc(intTy.getWidth());
      return cir::ConstantOp::create(builder, loc,
                                     cir::IntAttr::get(intTy, fitted))
          .getResult();
    };

    Value rem = cir::RemOp::create(builder, loc, intTy, extent, constOf(d))
                    .getResult();
    Value cond = cir::CmpOp::create(builder, loc, boolTy, cir::CmpOpKind::eq,
                                    rem, constOf(0))
                     .getResult();

    if (minWidth < 64 && minWidth < intTy.getWidth()) {
      // Largest extent for which the narrow arithmetic cannot wrap.
      const int64_t limit = (int64_t(1) << minWidth) - 1;
      Value fits = cir::CmpOp::create(builder, loc, boolTy, cir::CmpOpKind::le,
                                      extent, constOf(limit))
                       .getResult();
      // Conjunction the way && reaches CIR.
      cond = cir::TernaryOp::create(
                 builder, loc, cond,
                 [&](OpBuilder &b, Location l) {
                   cir::YieldOp::create(b, l, fits);
                 },
                 [&](OpBuilder &b, Location l) {
                   auto f = cir::ConstantOp::create(
                       b, l, cir::BoolAttr::get(ctx, boolTy, false));
                   cir::YieldOp::create(b, l, f.getResult());
                 })
                 .getResult();
    }

    auto emptyBuilder = [](OpBuilder &, Location) {};
    auto ifOp = cir::IfOp::create(builder, loc, cond, /*withElseRegion=*/true,
                                  emptyBuilder, emptyBuilder);
    {
      OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToStart(&ifOp.getThenRegion().front());
      auto *fast = builder.clone(*launch.getOperation());
      StringRef modName = launch.getKernelAttr().getRootReference().getValue();
      cast<cir::OffloadKernelLaunchOp>(fast).setKernelAttr(SymbolRefAttr::get(
          ctx, modName, {FlatSymbolRefAttr::get(ctx, cloneName)}));
      cir::YieldOp::create(builder, loc);
    }
    {
      OpBuilder::InsertionGuard guard(builder);
      Block &elseBlock = ifOp.getElseRegion().front();
      launch->moveBefore(&elseBlock, elseBlock.begin());
      builder.setInsertionPointToEnd(&elseBlock);
      cir::YieldOp::create(builder, loc);
    }
  }

  static cir::OffloadModuleOp offloadModOf(cir::OffloadFuncOp kernel) {
    return kernel->getParentOfType<cir::OffloadModuleOp>();
  }

  /// Does \p v read kernel argument \p argIdx (through the usual spill)?
  static bool tracesToArg(Value v, cir::OffloadFuncOp kernel, unsigned argIdx) {
    cir::ValueTraceResult t = cir::traceValueOrigin(v);
    if (!t.terminal)
      return false;
    if (auto arg = dyn_cast<BlockArgument>(t.terminal))
      return arg.getOwner() == &kernel.getBody().front() &&
             arg.getArgNumber() == argIdx;
    // traceValueOrigin stops at the spill slot; check its single store.
    if (cir::AllocaOp slot = findSlot(t.terminal)) {
      cir::StoreOp only;
      SmallVector<Operation *, 8> slotUsers;
      collectSlotUsers(slot, slotUsers);
      for (Operation *user : slotUsers) {
        if (auto store = dyn_cast<cir::StoreOp>(user)) {
          if (only)
            return false;
          only = store;
        }
      }
      if (!only)
        return false;
      if (auto arg = dyn_cast<BlockArgument>(only.getValue()))
        return arg.getOwner() == &kernel.getBody().front() &&
               arg.getArgNumber() == argIdx;
    }
    return false;
  }

  /// Is \p v a rational multiple of kernel argument \p argIdx?
  ///
  /// `tracesToArg` stops at arithmetic, so constant multiplies and divides are
  /// peeled here and everything below them is left to it.
  static std::optional<ArgScale> matchScaledArg(Value v,
                                                cir::OffloadFuncOp kernel,
                                                unsigned argIdx,
                                                unsigned depth = 0) {
    if (depth > 8)
      return std::nullopt;
    if (tracesToArg(v, kernel, argIdx))
      return ArgScale{};

    Operation *def = v.getDefiningOp();
    if (!def)
      return std::nullopt;

    if (isa<cir::CastOp>(def) && def->getNumOperands() == 1)
      return matchScaledArg(def->getOperand(0), kernel, argIdx, depth + 1);

    auto scaled = [&](Value inner, int64_t factor,
                      bool isDivisor) -> std::optional<ArgScale> {
      std::optional<ArgScale> s =
          matchScaledArg(inner, kernel, argIdx, depth + 1);
      if (!s)
        return std::nullopt;
      (isDivisor ? s->den : s->num) *= factor;
      if (s->num > kMaxScale || s->den > kMaxScale)
        return std::nullopt;
      return s;
    };

    if (auto mul = dyn_cast<cir::MulOp>(def)) {
      if (std::optional<int64_t> c = cir::tryResolveToConstant(mul.getRhs()))
        if (*c > 0)
          return scaled(mul.getLhs(), *c, /*isDivisor=*/false);
      if (std::optional<int64_t> c = cir::tryResolveToConstant(mul.getLhs()))
        if (*c > 0)
          return scaled(mul.getRhs(), *c, /*isDivisor=*/false);
      return std::nullopt;
    }

    if (auto div = dyn_cast<cir::DivOp>(def)) {
      if (std::optional<int64_t> c = cir::tryResolveToConstant(div.getRhs()))
        if (*c > 0)
          return scaled(div.getLhs(), *c, /*isDivisor=*/true);
    }

    return std::nullopt;
  }

  bool passEnabled = true;
};

} // namespace

std::unique_ptr<mlir::Pass>
mlir::createOffloadEliminateCoveredGuardsPass(bool enabled) {
  return std::make_unique<OffloadEliminateCoveredGuardsPass>(enabled);
}
