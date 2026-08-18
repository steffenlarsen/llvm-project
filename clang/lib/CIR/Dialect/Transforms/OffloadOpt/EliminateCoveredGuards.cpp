//===- EliminateCoveredGuards.cpp - Prove tail guards dead ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A kernel that covers `n` elements in blocks of `d` is launched with
// `gridDim = ceil(n/d)` and carries tail guards -- `if (index >= n) return;` --
// for the final, partially populated block. When `n % d == 0` that block is
// full and every guard is dead, which is why libraries hand-write a fast
// variant: ggml's `dequantize_block_q8_0_f16<need_check=false>` is exactly
// this, and it is worth ~6% of prompt processing.
//
// The fact needed to remove the guard is `d*blockIdx.x + rest < n`, which
// relates two runtime unknowns. LLVM cannot use it: its value analyses are
// per-value interval domains, so an `llvm.assume`, a dominating branch and a
// slack-variable rewrite were all measured to change nothing. The information
// has to be *acted on*, not described -- so this pass does the proof itself.
//
// It runs before CIRFlattenCFG, where `cir.for` is still structured (giving
// exact induction variable bounds) and the launch is still visible (giving the
// grid relation and the block size).
//
// On success it specialises a clone of the kernel, folds the proven
// comparisons away in the copy, and dispatches to it behind `n % d == 0`. The
// original keeps its guards, so the fast copy is only ever entered where the
// premises hold.
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/Builders.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/SymbolTable.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/CIRTransformUtils.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/DeviceIndex.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelBindingTable.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelCloning.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"

#include <string>

#define DEBUG_TYPE "cir-offload-eliminate-covered-guards"

namespace mlir {
#define GEN_PASS_DEF_OFFLOADELIMINATECOVEREDGUARDS
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

using namespace mlir;

namespace {

// Suffix for the specialised copy, matching the convention the other offload
// passes use for their clones.
static constexpr llvm::StringRef kCoveredSuffix = ".guards";

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
  if (std::optional<cir::DeviceIndex> index = cir::matchDeviceIndex(cur))
    if (index->kind == cir::DeviceIndexKind::BlockDim && index->dim == 0 &&
        blockDimX > 0)
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
    if (cmp.getKind() != cir::CmpOpKind::lt &&
        cmp.getKind() != cir::CmpOpKind::le)
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

  if (std::optional<cir::DeviceIndex> index = cir::matchDeviceIndex(def)) {
    // Only the x dimension is modelled: the grid relation this pass works from
    // is one-dimensional, so a y or z read is not something it can bound.
    if (index->dim != 0)
      return std::nullopt;
    Type ty = v.getType();
    switch (index->kind) {
    case cir::DeviceIndexKind::BlockId:
      return joinWidth(MaxBound{1, 0}, ty);
    case cir::DeviceIndexKind::ThreadId:
      if (blockDimX <= 0)
        return std::nullopt;
      return joinWidth(MaxBound{0, blockDimX - 1}, ty);
    case cir::DeviceIndexKind::BlockDim:
      if (blockDimX <= 0)
        return std::nullopt;
      return joinWidth(MaxBound{0, blockDimX}, ty);
    case cir::DeviceIndexKind::GridDim:
      return std::nullopt;
    }
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
// Does `v` read kernel argument `argIdx` (through the usual spill)?
static bool tracesToArg(Value v, cir::FuncOp kernel, unsigned argIdx) {
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

// Is `v` a rational multiple of kernel argument `argIdx`?
//
// `tracesToArg` stops at arithmetic, so constant multiplies and divides are
// peeled here and everything below them is left to it.
static std::optional<ArgScale> matchScaledArg(Value v, cir::FuncOp kernel,
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

// Find guards this launch shape proves dead. Returns the narrowest arithmetic
// width across them.
static unsigned proveGuards(cir::FuncOp kernel, unsigned argIdx, int64_t d,
                            int64_t blockDimX,
                            SmallVectorImpl<DeadGuard> &out) {
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

    // The block stride in the units the guard counts in. Requiring this to be
    // exact is also what makes the *runtime* extent exact: integer division
    // truncates, so `(num*n)/den` is only the rational value when `den`
    // divides `num*n`. The host guard already establishes `n = m*d`, so
    // `den | num*d` gives `den | num*n` for free.
    if (scale->num > kMaxScale / std::max<int64_t>(d, 1))
      return;
    const int64_t scaledNumer = scale->num * d;
    if (scaledNumer % scale->den != 0) {
      LLVM_DEBUG(llvm::dbgs()
                 << "GUARDDBG   cmp: extent scale " << scale->num << "/"
                 << scale->den << " does not divide the block stride " << d
                 << ", skipped\n");
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

    // max(index) = blockIdCoeff*(n/d - 1) + constant. With the coefficient
    // equal to the scaled block stride this is `extent - dScaled + constant`,
    // so the guard is dead exactly when constant < dScaled. Other coefficients
    // are not expressible as a multiple of the extent and are left alone.
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

    // index < extent is established. Map that onto this predicate.
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

// The launch facts every site of a kernel agrees on. One conclusion has to hold
// for all of them, so a single disagreeing site abandons the kernel.
struct LaunchFacts {
  unsigned argIdx;
  int64_t divisor;
  int64_t blockDimX;
};

static std::optional<LaunchFacts>
commonLaunchFacts(llvm::ArrayRef<cir::LaunchSite> sites) {
  std::optional<LaunchFacts> facts;
  for (const cir::LaunchSite &site : sites) {
    if (!site.hasGeometry()) {
      LLVM_DEBUG(llvm::dbgs() << "GUARDDBG   launch: no geometry\n");
      return std::nullopt;
    }

    cir::LaunchSite::Dim3 block = site.getBlockDim();
    auto blockX = mlir::dyn_cast_or_null<cir::IntAttr>(block.constX());
    if (!blockX) {
      LLVM_DEBUG(llvm::dbgs() << "GUARDDBG   launch: block.x not constant\n");
      return std::nullopt;
    }
    int64_t blockDimX = blockX.getValue().getZExtValue();
    if (blockDimX <= 0)
      return std::nullopt;

    // A grid dimension that could not be traced is not a relation to reason
    // about; the x component is the only one this pass models.
    mlir::Value gridX = site.getGridDim().x;
    if (!gridX) {
      LLVM_DEBUG(llvm::dbgs() << "GUARDDBG   launch: grid.x not traced\n");
      return std::nullopt;
    }

    llvm::SmallVector<Value, 8> args;
    for (unsigned i = 0, e = site.getNumArgs(); i != e; ++i)
      args.push_back(site.getArg(i));

    std::optional<cir::GridDimRelation> rel =
        cir::findGridDimCandidate(gridX, args);
    if (!rel || rel->divisor <= 1) {
      LLVM_DEBUG(llvm::dbgs()
                 << "GUARDDBG   launch: grid.x is not ceil(arg/d) ("
                 << (rel ? "divisor " + std::to_string(rel->divisor)
                         : std::string("no candidate arg"))
                 << ")\n");
      return std::nullopt;
    }

    LaunchFacts here{rel->argIndex, rel->divisor, blockDimX};
    if (!facts) {
      facts = here;
      continue;
    }
    if (facts->argIdx != here.argIdx || facts->divisor != here.divisor ||
        facts->blockDimX != here.blockDimX) {
      LLVM_DEBUG(llvm::dbgs()
                 << "GUARDDBG   launch: sites disagree (arg " << facts->argIdx
                 << "/" << here.argIdx << ", d " << facts->divisor << "/"
                 << here.divisor << ", block " << facts->blockDimX << "/"
                 << here.blockDimX << ")\n");
      return std::nullopt;
    }
  }
  return facts;
}

// `if (n % d == 0 && n <= widthLimit) fast(...) else original(...)`.
//
// The width term is what makes the fold sound when the index arithmetic is
// evaluated in fewer bits than the comparison: the proof assumes `d*blockIdx.x`
// does not wrap, so the clone is simply not entered for extents large enough to
// make it wrap.
//
// `call` already targets the fast clone, since cloning retargeted it; the slow
// path is a copy of it pointed back at what it used to call.
static void emitGuardedDispatch(cir::CallOp call, unsigned argIdx, int64_t d,
                                unsigned minWidth,
                                mlir::FlatSymbolRefAttr originalStub,
                                cir::CUDAKernelNameAttr originalKernel) {
  mlir::OperandRange args = call.getArgOperands();
  if (argIdx >= args.size())
    return;
  Value extent = args[argIdx];
  auto intTy = mlir::dyn_cast<cir::IntType>(extent.getType());
  if (!intTy || intTy.getWidth() > 64)
    return;

  MLIRContext *ctx = call.getContext();
  OpBuilder builder(call);
  Location loc = call.getLoc();
  auto boolTy = cir::BoolType::get(ctx);

  auto constOf = [&](int64_t v) -> Value {
    llvm::APInt wide(64, static_cast<uint64_t>(v), /*isSigned=*/true);
    llvm::APInt fitted = wide.trunc(intTy.getWidth());
    return cir::ConstantOp::create(builder, loc,
                                   cir::IntAttr::get(intTy, fitted))
        .getResult();
  };

  Value rem =
      cir::RemOp::create(builder, loc, intTy, extent, constOf(d)).getResult();
  Value cond = cir::CmpOp::create(builder, loc, boolTy, cir::CmpOpKind::eq, rem,
                                  constOf(0))
                   .getResult();

  if (minWidth < 64 && minWidth < intTy.getWidth()) {
    // Largest extent for which the narrow arithmetic cannot wrap.
    const int64_t limit = (int64_t(1) << minWidth) - 1;
    Value fits = cir::CmpOp::create(builder, loc, boolTy, cir::CmpOpKind::le,
                                    extent, constOf(limit))
                     .getResult();
    // Conjunction the way && reaches CIR.
    cond =
        cir::TernaryOp::create(
            builder, loc, cond,
            [&](OpBuilder &b, Location l) { cir::YieldOp::create(b, l, fits); },
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
    Block &elseBlock = ifOp.getElseRegion().front();
    builder.setInsertionPointToStart(&elseBlock);
    auto slow = cast<cir::CallOp>(builder.clone(*call.getOperation()));
    slow.setCalleeAttr(originalStub);
    slow->setAttr(cir::CUDAKernelNameAttr::getMnemonic(), originalKernel);
    builder.setInsertionPointToEnd(&elseBlock);
    cir::YieldOp::create(builder, loc);
  }
  {
    OpBuilder::InsertionGuard guard(builder);
    Block &thenBlock = ifOp.getThenRegion().front();
    call->moveBefore(&thenBlock, thenBlock.begin());
    builder.setInsertionPointToEnd(&thenBlock);
    cir::YieldOp::create(builder, loc);
  }
}

struct OffloadEliminateCoveredGuardsPass
    : public impl::OffloadEliminateCoveredGuardsBase<
          OffloadEliminateCoveredGuardsPass> {
  void runOnOperation() override;
};

void OffloadEliminateCoveredGuardsPass::runOnOperation() {
  cir::OffloadContainerOp container = getOperation();
  cir::KernelBindingTable &table = getAnalysis<cir::KernelBindingTable>();
  bool changed = false;

  for (const auto &entry : table) {
    llvm::StringRef kernelName = entry.first;
    const cir::KernelBinding &binding = entry.second;
    llvm::ArrayRef<cir::LaunchSite> sites = binding.launchSites;

    // A kernel launched nowhere in this TU says nothing about its grid, and a
    // copy of a copy gains nothing.
    if (sites.empty() || kernelName.ends_with(kCoveredSuffix))
      continue;

    std::optional<LaunchFacts> facts = commonLaunchFacts(sites);
    if (!facts)
      continue;

    // Prove against the originals first: cloning a kernel with no dead guard
    // would leave a copy nobody benefits from, and the dispatch that goes with
    // it costs a division at every launch.
    unsigned minWidth = 64;
    bool anyDead = false;
    for (cir::FuncOp kernel : binding.deviceKernels) {
      if (kernel.isDeclaration() || facts->argIdx >= kernel.getNumArguments())
        continue;
      SmallVector<DeadGuard> dead;
      unsigned w = proveGuards(kernel, facts->argIdx, facts->divisor,
                               facts->blockDimX, dead);
      if (dead.empty())
        continue;
      anyDead = true;
      minWidth = std::min(minWidth, w);
    }
    if (!anyDead)
      continue;

    // What the launches called before cloning retargets them; the slow path
    // has to be pointed back at it.
    cir::FuncOp stub = binding.hostStub;
    auto originalStub =
        mlir::FlatSymbolRefAttr::get(container.getContext(), stub.getSymName());
    auto originalKernel = stub->getAttrOfType<cir::CUDAKernelNameAttr>(
        cir::CUDAKernelNameAttr::getMnemonic());
    if (!originalKernel)
      continue;

    // The dispatch below re-tests the premise at run time, so the copy is only
    // reached where the proof holds -- but the copy still has to be a copy.
    // Rewriting in place would strip guards from launches this pass never saw.
    llvm::SmallVector<cir::CallOp, 2> calls;
    for (const cir::LaunchSite &site : sites)
      calls.push_back(site.stubCall);

    std::optional<cir::KernelClone> copy =
        cir::cloneKernelForSites(container, binding, kCoveredSuffix, sites);
    if (!copy)
      continue;

    // Re-prove on the copy: the second run finds the same comparisons in it,
    // which avoids having to map operations across the clone.
    unsigned folded = 0;
    for (cir::FuncOp kernel : copy->deviceKernels) {
      if (kernel.isDeclaration() || facts->argIdx >= kernel.getNumArguments())
        continue;
      SmallVector<DeadGuard> dead;
      proveGuards(kernel, facts->argIdx, facts->divisor, facts->blockDimX,
                  dead);
      for (DeadGuard &g : dead) {
        OpBuilder b(g.cmp);
        auto boolTy = cir::BoolType::get(b.getContext());
        auto cst = cir::ConstantOp::create(
            b, g.cmp.getLoc(),
            cir::BoolAttr::get(b.getContext(), boolTy, g.foldTo));
        g.cmp.getResult().replaceAllUsesWith(cst.getResult());
        g.cmp.erase();
        ++folded;
      }
    }
    if (!folded) {
      // The copy proved nothing after all; it is still a faithful copy and the
      // retargeted launches reach it, so it is left rather than unwound.
      changed = true;
      continue;
    }

    for (cir::CallOp call : calls)
      emitGuardedDispatch(call, facts->argIdx, facts->divisor, minWidth,
                          originalStub, originalKernel);

    // Tell MultiversionDivisibility to leave this kernel alone; it would
    // otherwise build a second dispatch on the very same condition.
    for (cir::FuncOp kernel : binding.deviceKernels)
      kernel->setAttr("covered_guards_handled",
                      UnitAttr::get(kernel.getContext()));
    changed = true;
  }

  if (!changed)
    markAllAnalysesPreserved();
}

} // namespace

std::unique_ptr<Pass> mlir::createOffloadEliminateCoveredGuardsPass() {
  return std::make_unique<OffloadEliminateCoveredGuardsPass>();
}
