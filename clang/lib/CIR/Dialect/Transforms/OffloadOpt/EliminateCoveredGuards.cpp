//===- EliminateCoveredGuards.cpp - Prove tail guards dead ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A kernel that covers `n` elements carries tail guards -- `if (index >= n)
// return;` -- for the threads of the last, partially populated block. When the
// launch has no such threads every guard is dead, which is why libraries
// hand-write a fast variant: ggml's `dequantize_block_q8_0_f16<need_check=
// false>` is exactly this, and it is worth ~6% of prompt processing.
//
// The fact needed to remove a guard relates the launch geometry to a runtime
// extent. LLVM cannot use it: its value analyses are per-value interval
// domains, so an `llvm.assume`, a dominating branch and a slack-variable
// rewrite were all measured to change nothing. The information has to be
// *acted on*, not described -- so this pass does it itself, in two halves:
//
//   * On the device, the largest value a guard's index can take is computed
//     symbolically in the launch dimensions -- the largest block id, the
//     largest thread id and the block size -- without knowing any of them.
//   * At each launch, the host evaluates that bound with the launch's actual
//     grid and block, recomputes the guard's extent from the launch's own
//     arguments, and dispatches to a copy of the kernel with the guard folded
//     away when the bound is below the extent.
//
// The premise is checked rather than derived, so it does not matter how the
// grid was computed: `ceil(n/d)`, `(n-1)/d+1`, or clamped to the device limit.
// With `gridDim = ceil(n/d)` and `blockDim = d` it is exactly `n % d == 0`.
//
// It runs before CIRFlattenCFG, where `cir.for` is still structured (giving
// exact induction variable bounds) and the launch geometry is still traceable.
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"
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

#include <limits>
#include <map>
#include <vector>

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

// Largest coefficient a bound may carry. With the launch dimensions within the
// hardware's limits (a grid of at most 2^31 blocks of at most 1024 threads),
// every term of a bound then fits comfortably in 64 bits on the host.
static constexpr int64_t kMaxCoeff = 1 << 20;

//===----------------------------------------------------------------------===//
// Symbolic bounds in the launch dimensions
//===----------------------------------------------------------------------===//

// The launch quantities a bound is written in.
enum LaunchSym : unsigned {
  BlockIdMax,  // gridDim.x - 1, the largest blockIdx.x
  ThreadIdMax, // blockDim.x - 1, the largest threadIdx.x
  BlockDimX,   // blockDim.x
};

// A polynomial in the launch symbols with integer coefficients, of degree at
// most two -- enough for `blockIdx.x * blockDim.x + threadIdx.x`. A monomial
// is the sorted list of the symbols it multiplies; the empty one is the
// constant term.
struct Poly {
  std::map<std::vector<unsigned>, int64_t> terms;

  static Poly constant(int64_t c) {
    Poly p;
    if (c)
      p.terms[{}] = c;
    return p;
  }
  static Poly symbol(LaunchSym s) {
    Poly p;
    p.terms[{s}] = 1;
    return p;
  }

  int64_t constantTerm() const {
    auto it = terms.find({});
    return it == terms.end() ? 0 : it->second;
  }
  bool hasSymbol() const {
    return llvm::any_of(terms, [](const auto &t) { return !t.first.empty(); });
  }
  // Whether every coefficient, the constant term included, is non-negative.
  bool nonNegative() const {
    return llvm::all_of(terms, [](const auto &t) { return t.second >= 0; });
  }
  // Whether the polynomial is non-decreasing in every symbol, so that its
  // largest value is taken where every symbol is largest.
  bool monotone() const {
    return llvm::all_of(
        terms, [](const auto &t) { return t.first.empty() || t.second >= 0; });
  }
  bool operator==(const Poly &o) const { return terms == o.terms; }
  bool operator<(const Poly &o) const { return terms < o.terms; }
};

static std::optional<Poly> add(const Poly &a, const Poly &b) {
  Poly r = a;
  for (const auto &[mono, coeff] : b.terms) {
    int64_t sum = r.terms[mono] + coeff;
    if (sum > kMaxCoeff || sum < -kMaxCoeff)
      return std::nullopt;
    if (sum)
      r.terms[mono] = sum;
    else
      r.terms.erase(mono);
  }
  return r;
}

// The product, as long as it stays of degree two. Only non-negative factors
// are multiplied: then the product of two largest values is the largest
// product, which is what makes the result a bound.
static std::optional<Poly> mul(const Poly &a, const Poly &b) {
  if (!a.nonNegative() || !b.nonNegative())
    return std::nullopt;
  Poly r;
  for (const auto &[ma, ca] : a.terms) {
    for (const auto &[mb, cb] : b.terms) {
      std::vector<unsigned> mono = ma;
      mono.insert(mono.end(), mb.begin(), mb.end());
      if (mono.size() > 2)
        return std::nullopt;
      llvm::sort(mono);
      int64_t prod = ca * cb;
      if (ca && prod / ca != cb)
        return std::nullopt;
      int64_t sum = r.terms[mono] + prod;
      if (sum > kMaxCoeff)
        return std::nullopt;
      r.terms[mono] = sum;
    }
  }
  return r;
}

// Integer division floors, and a floored bound would be *smaller* than the
// true one, which would license deleting a guard that can still fire. So every
// coefficient has to divide exactly.
static std::optional<Poly> divExact(const Poly &a, int64_t c) {
  if (c <= 0 || !a.nonNegative())
    return std::nullopt;
  Poly r;
  for (const auto &[mono, coeff] : a.terms) {
    if (coeff % c)
      return std::nullopt;
    r.terms[mono] = coeff / c;
  }
  return r;
}

// The largest value of type `ty`, or nullopt for a non-integer type.
static std::optional<int64_t> typeMax(Type ty) {
  auto intTy = mlir::dyn_cast<cir::IntType>(ty);
  if (!intTy)
    return std::nullopt;
  unsigned bits = intTy.getWidth() - (intTy.isSigned() ? 1 : 0);
  if (bits >= 63)
    return std::numeric_limits<int64_t>::max();
  return (int64_t(1) << bits) - 1;
}

// The largest value an index can take, and what keeps it exact: every value
// it was computed through, paired with the largest value that value's type
// holds. The host checks them all, so a launch for which any step would wrap
// never reaches the copy.
struct IndexBound {
  Poly max;
  llvm::SmallVector<std::pair<Poly, int64_t>> limits;
};

// Record that `b`, computed in type `ty`, must not exceed what `ty` holds.
static std::optional<IndexBound> within(IndexBound b, Type ty) {
  std::optional<int64_t> limit = typeMax(ty);
  if (!limit)
    return std::nullopt;
  if (b.max.hasSymbol())
    b.limits.emplace_back(b.max, *limit);
  else if (b.max.constantTerm() > *limit)
    return std::nullopt;
  return b;
}

// Whether `a >= b` for every value of the symbols, which with every symbol
// non-negative holds when it holds coefficient by coefficient.
static bool dominates(const Poly &a, const Poly &b) {
  return llvm::all_of(b.terms,
                      [&](const auto &t) {
                        auto it = a.terms.find(t.first);
                        return (it == a.terms.end() ? 0 : it->second) >=
                               t.second;
                      }) &&
         llvm::all_of(a.terms, [&](const auto &t) {
           return b.terms.count(t.first) || t.second >= 0;
         });
}

// Whether `p <= limit` for any launch at all: the symbols come from 32-bit
// grid and block dimensions, so none exceeds 2^32 - 1.
static bool alwaysWithin(const Poly &p, int64_t limit) {
  const __int128 symbolMax = (__int128(1) << 32) - 1;
  __int128 max = 0;
  for (const auto &[mono, coeff] : p.terms) {
    __int128 term = coeff;
    for (size_t i = 0; i < mono.size(); ++i)
      term *= symbolMax;
    max += term > 0 ? term : 0;
  }
  return max <= limit;
}

static IndexBound merge(IndexBound a, const IndexBound &b, Poly max) {
  a.limits.append(b.limits.begin(), b.limits.end());
  a.max = std::move(max);
  return a;
}

/// Find the slot behind an address, looking through the address-space casts
/// CIRGen puts on every device-side local (allocas land in
/// target_address_space(5) and are cast to generic before use).
static cir::AllocaOp findSlot(Value addr) {
  while (auto cast = addr.getDefiningOp<cir::CastOp>())
    addr = cast.getOperand();
  return addr.getDefiningOp<cir::AllocaOp>();
}

/// Users of a slot, reached through those same casts. Lifetime markers are
/// left out: they neither write the slot nor let it escape.
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
      if (isa<cir::LifetimeStartOp, cir::LifetimeEndOp>(user))
        continue;
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

/// Compute the largest value \p v can take, or nullopt when anything is
/// unrecognised.
///
/// Deliberately conservative: every construct not explicitly handled bails,
/// because a bound that is too small would license deleting a live guard. Only
/// the x dimension is modelled; the launch geometry is traced per dimension,
/// but a guard over y or z is not something this bounds.
static std::optional<IndexBound> computeBound(Value v, unsigned depth = 0) {
  if (depth > 12)
    return std::nullopt;

  if (std::optional<int64_t> c = tryConstThroughSlot(v))
    return IndexBound{Poly::constant(*c), {}};

  Operation *def = v.getDefiningOp();
  if (!def)
    return std::nullopt;

  if (std::optional<cir::DeviceIndex> index = cir::matchDeviceIndex(def)) {
    if (index->dim != 0)
      return std::nullopt;
    switch (index->kind) {
    case cir::DeviceIndexKind::BlockId:
      return within(IndexBound{Poly::symbol(BlockIdMax), {}}, v.getType());
    case cir::DeviceIndexKind::ThreadId:
      return within(IndexBound{Poly::symbol(ThreadIdMax), {}}, v.getType());
    case cir::DeviceIndexKind::BlockDim:
      return within(IndexBound{Poly::symbol(BlockDimX), {}}, v.getType());
    case cir::DeviceIndexKind::GridDim:
      return std::nullopt;
    }
  }

  if (auto cast = dyn_cast<cir::CastOp>(def)) {
    if (cast.getKind() != cir::CastKind::integral)
      return std::nullopt;
    std::optional<IndexBound> o = computeBound(cast.getSrc(), depth + 1);
    return o ? within(*o, cast.getType()) : std::nullopt;
  }

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
          return IndexBound{Poly::constant(*m), {}};
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
    return computeBound(only.getValue(), depth + 1);
  }

  auto binary = [&](Value lhs, Value rhs,
                    auto combine) -> std::optional<IndexBound> {
    std::optional<IndexBound> l = computeBound(lhs, depth + 1);
    std::optional<IndexBound> r =
        l ? computeBound(rhs, depth + 1) : std::nullopt;
    if (!r)
      return std::nullopt;
    std::optional<Poly> max = combine(l->max, r->max);
    if (!max)
      return std::nullopt;
    return within(merge(*l, *r, *max), v.getType());
  };

  if (auto addOp = dyn_cast<cir::AddOp>(def))
    return binary(addOp.getLhs(), addOp.getRhs(), add);
  if (auto mulOp = dyn_cast<cir::MulOp>(def))
    return binary(mulOp.getLhs(), mulOp.getRhs(), mul);
  if (auto divOp = dyn_cast<cir::DivOp>(def)) {
    std::optional<int64_t> c = tryConstThroughSlot(divOp.getRhs());
    if (!c)
      return std::nullopt;
    std::optional<IndexBound> o = computeBound(divOp.getLhs(), depth + 1);
    std::optional<Poly> max = o ? divExact(o->max, *c) : std::nullopt;
    if (!max)
      return std::nullopt;
    o->max = *max;
    return within(*o, divOp.getType());
  }

  LLVM_DEBUG(llvm::dbgs() << "GUARDDBG     unhandled: "
                          << def->getName().getStringRef() << "\n");
  return std::nullopt;
}

//===----------------------------------------------------------------------===//
// Extents the host can recompute
//===----------------------------------------------------------------------===//

// The kernel argument `v` is, read directly or back out of the slot CIRGen
// spilled it into, or null.
static BlockArgument asKernelArg(Value v, cir::FuncOp kernel) {
  auto isArg = [&](Value x) -> BlockArgument {
    auto arg = dyn_cast<BlockArgument>(x);
    return arg && arg.getOwner() == &kernel.getBody().front() ? arg
                                                              : BlockArgument();
  };
  if (BlockArgument arg = isArg(v))
    return arg;
  auto load = v.getDefiningOp<cir::LoadOp>();
  cir::AllocaOp slot = load ? findSlot(load.getAddr()) : cir::AllocaOp();
  if (!slot)
    return {};
  cir::StoreOp only;
  SmallVector<Operation *, 8> slotUsers;
  collectSlotUsers(slot, slotUsers);
  for (Operation *user : slotUsers) {
    if (auto store = dyn_cast<cir::StoreOp>(user)) {
      if (only)
        return {};
      only = store;
      continue;
    }
    if (!isa<cir::LoadOp>(user))
      return {};
  }
  return only ? isArg(only.getValue()) : BlockArgument();
}

// The operations the host may redo to recompute an extent: integer casts and
// arithmetic, dividing only by a positive constant so that recomputing cannot
// fault where the kernel would not have.
static bool isRecomputable(Operation *op) {
  if (auto cast = dyn_cast<cir::CastOp>(op))
    return cast.getKind() == cir::CastKind::integral;
  if (isa<cir::AddOp, cir::SubOp, cir::MulOp>(op))
    return true;
  if (isa<cir::DivOp, cir::RemOp>(op)) {
    std::optional<int64_t> c = cir::tryResolveToConstant(op->getOperand(1));
    return c && *c > 0;
  }
  return false;
}

// Whether `v` is computed from the kernel's arguments and constants alone, by
// operations the host can redo.
static bool isArgExpr(Value v, cir::FuncOp kernel, unsigned depth = 0) {
  if (depth > 8 || !mlir::isa<cir::IntType>(v.getType()))
    return false;
  if (asKernelArg(v, kernel))
    return true;
  Operation *def = v.getDefiningOp();
  if (!def)
    return false;
  if (auto constant = dyn_cast<cir::ConstantOp>(def))
    return mlir::isa<cir::IntAttr>(constant.getValue());
  if (!isRecomputable(def))
    return false;
  return llvm::all_of(def->getOperands(), [&](Value operand) {
    return isArgExpr(operand, kernel, depth + 1);
  });
}

// Whether `recompute` succeeds for `v` with these launch arguments: every
// argument it reads is passed with the kernel parameter's own type.
static bool canRecompute(Value v, cir::FuncOp kernel, mlir::ValueRange args) {
  if (BlockArgument arg = asKernelArg(v, kernel))
    return arg.getArgNumber() < args.size() &&
           args[arg.getArgNumber()].getType() == v.getType();
  Operation *def = v.getDefiningOp();
  return def && llvm::all_of(def->getOperands(), [&](Value operand) {
           return canRecompute(operand, kernel, args);
         });
}

// Redo `v`'s computation before `builder`'s insertion point with the launch's
// arguments in place of the kernel's. The copies carry no overflow flags: the
// host evaluates the extent even for a launch that runs no thread, where the
// kernel would not, so a wrap there must not be undefined.
static Value recompute(Value v, cir::FuncOp kernel, mlir::ValueRange args,
                       OpBuilder &builder) {
  if (BlockArgument arg = asKernelArg(v, kernel)) {
    Value launched = args[arg.getArgNumber()];
    return launched.getType() == v.getType() ? launched : Value();
  }
  Operation *def = v.getDefiningOp();
  IRMapping map;
  for (Value operand : def->getOperands()) {
    Value copy = recompute(operand, kernel, args, builder);
    if (!copy)
      return {};
    map.map(operand, copy);
  }
  Operation *copy = builder.clone(*def, map);
  if (auto addOp = dyn_cast<cir::AddOp>(copy)) {
    addOp.setNoSignedWrap(false);
    addOp.setNoUnsignedWrap(false);
  } else if (auto subOp = dyn_cast<cir::SubOp>(copy)) {
    subOp.setNoSignedWrap(false);
    subOp.setNoUnsignedWrap(false);
  }
  return copy->getResult(0);
}

//===----------------------------------------------------------------------===//
// Guards
//===----------------------------------------------------------------------===//

/// A comparison of an index against an extent that is constant whenever the
/// index stays below the extent: what it folds to, and which premise does it.
struct Guard {
  // The device kernel the guard is in; its extent is written in its arguments.
  cir::FuncOp kernel;
  cir::CmpOp cmp;
  bool foldTo;
  // The premise is `bound.max < extent`, or `<=` when not strict.
  bool strict;
  Value extent;
  IndexBound bound;
};

// Find the guards of `kernel` whose index is bounded in the launch dimensions
// and whose extent the host can recompute.
static void findGuards(cir::FuncOp kernel, SmallVectorImpl<Guard> &out) {
  kernel.walk([&](cir::CmpOp cmp) {
    cir::CmpOpKind kind = cmp.getKind();
    if (kind != cir::CmpOpKind::ge && kind != cir::CmpOpKind::gt &&
        kind != cir::CmpOpKind::lt && kind != cir::CmpOpKind::le)
      return;

    for (bool indexOnLhs : {true, false}) {
      Value index = indexOnLhs ? cmp.getLhs() : cmp.getRhs();
      Value extent = indexOnLhs ? cmp.getRhs() : cmp.getLhs();
      if (!isArgExpr(extent, kernel))
        continue;
      std::optional<IndexBound> bound = computeBound(index);
      // The index has to depend on the launch, and must not go negative: an
      // unsigned index that wraps below zero compares as huge.
      if (!bound || !bound->max.hasSymbol() || !bound->max.monotone() ||
          bound->max.constantTerm() < 0) {
        LLVM_DEBUG(llvm::dbgs() << "GUARDDBG   cmp: index not bounded\n");
        continue;
      }
      // Normalise to `index OP extent`; flipping the operands mirrors it.
      cir::CmpOpKind k = kind;
      if (!indexOnLhs)
        k = k == cir::CmpOpKind::lt   ? cir::CmpOpKind::gt
            : k == cir::CmpOpKind::le ? cir::CmpOpKind::ge
            : k == cir::CmpOpKind::gt ? cir::CmpOpKind::lt
                                      : cir::CmpOpKind::le;
      // `index < extent` and `index >= extent` are settled by max < extent;
      // `index <= extent` and `index > extent` by max <= extent.
      bool strict = k == cir::CmpOpKind::lt || k == cir::CmpOpKind::ge;
      bool foldTo = k == cir::CmpOpKind::lt || k == cir::CmpOpKind::le;
      out.push_back({kernel, cmp, foldTo, strict, extent, *bound});
      LLVM_DEBUG(llvm::dbgs() << "GUARDDBG   cmp: guard, folds to "
                              << (foldTo ? "true" : "false") << "\n");
      return;
    }
  });
}

//===----------------------------------------------------------------------===//
// The dispatch
//===----------------------------------------------------------------------===//

// Whether the premise of every guard holds for this launch, evaluated before
// `call` with its grid and block size and arguments. Every extent has to be
// recomputable at this launch (canRecompute).
static Value emitPremise(OpBuilder &builder, Location loc, Value gridX,
                         Value blockX, llvm::ArrayRef<Guard> guards,
                         mlir::ValueRange args) {
  MLIRContext *ctx = builder.getContext();
  auto i64 = cir::IntType::get(ctx, 64, /*isSigned=*/true);
  auto boolTy = cir::BoolType::get(ctx);
  auto constant = [&](int64_t v) -> Value {
    return cir::ConstantOp::create(builder, loc, cir::IntAttr::get(i64, v));
  };
  auto widen = [&](Value v) -> Value {
    if (v.getType() == i64)
      return v;
    return cir::CastOp::create(builder, loc, i64, cir::CastKind::integral, v);
  };

  // The symbols' values for this launch. Arithmetic wraps rather than being
  // undefined; it can only wrap for a launch the runtime rejects anyway.
  Value grid = widen(gridX), block = widen(blockX), one = constant(1);
  Value symbols[] = {cir::SubOp::create(builder, loc, i64, grid, one),
                     cir::SubOp::create(builder, loc, i64, block, one), block};
  std::map<Poly, Value> evaluated;
  auto evaluate = [&](const Poly &p) -> Value {
    auto [it, inserted] = evaluated.try_emplace(p);
    if (!inserted)
      return it->second;
    Value sum;
    for (const auto &[mono, coeff] : p.terms) {
      Value term = mono.empty() || coeff != 1 ? constant(coeff) : Value();
      for (unsigned s : mono)
        term = term ? cir::MulOp::create(builder, loc, i64, term, symbols[s])
                          .getResult()
                    : symbols[s];
      sum = sum ? cir::AddOp::create(builder, loc, i64, sum, term).getResult()
                : term;
    }
    return it->second = sum ? sum : constant(0);
  };

  Value premise;
  auto conjoin = [&](Value cond) {
    if (!premise) {
      premise = cond;
      return;
    }
    premise =
        cir::TernaryOp::create(
            builder, loc, premise,
            [&](OpBuilder &b, Location l) { cir::YieldOp::create(b, l, cond); },
            [&](OpBuilder &b, Location l) {
              auto f = cir::ConstantOp::create(
                  b, l, cir::BoolAttr::get(ctx, boolTy, false));
              cir::YieldOp::create(b, l, f.getResult());
            })
            .getResult();
  };

  std::map<Poly, int64_t> limits;
  for (const Guard &guard : guards) {
    Value extent = recompute(guard.extent, guard.kernel, args, builder);
    assert(extent && "dispatch at a launch whose extents canRecompute rejects");
    conjoin(cir::CmpOp::create(builder, loc, boolTy,
                               guard.strict ? cir::CmpOpKind::lt
                                            : cir::CmpOpKind::le,
                               evaluate(guard.bound.max), widen(extent)));
    for (const auto &[poly, limit] : guard.bound.limits) {
      auto [it, inserted] = limits.try_emplace(poly, limit);
      if (!inserted)
        it->second = std::min(it->second, limit);
    }
  }
  // Only the limits a launch could actually break: one no 32-bit grid and
  // block can reach, or one implied by a larger bound under a smaller limit,
  // needs no test.
  for (const auto &[poly, limit] : limits) {
    if (alwaysWithin(poly, limit))
      continue;
    if (llvm::any_of(limits, [&](const auto &other) {
          return !(other.first == poly) && dominates(other.first, poly) &&
                 other.second <= limit;
        }))
      continue;
    conjoin(cir::CmpOp::create(builder, loc, boolTy, cir::CmpOpKind::le,
                               evaluate(poly), constant(limit)));
  }
  return premise;
}

// `if (premise) fast(...) else original(...)`. `call` already targets the
// fast clone, since cloning retargeted it; the slow path is a copy of it
// pointed back at what it used to call.
static void emitGuardedDispatch(cir::CallOp call, Value gridX, Value blockX,
                                llvm::ArrayRef<Guard> guards,
                                mlir::FlatSymbolRefAttr originalStub,
                                cir::CUDAKernelNameAttr originalKernel) {
  OpBuilder builder(call);
  Location loc = call.getLoc();
  Value premise =
      emitPremise(builder, loc, gridX, blockX, guards, call.getArgOperands());

  auto emptyBuilder = [](OpBuilder &, Location) {};
  auto ifOp = cir::IfOp::create(builder, loc, premise, /*withElseRegion=*/true,
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
  mlir::ModuleOp container = getOperation();
  // The pass is anchored on a module, but only an offload container holds the
  // host<->device correspondence it reasons about. Any other module is left
  // alone.
  if (!cir::isOffloadContainer(container))
    return;

  cir::KernelBindingTable &table = getAnalysis<cir::KernelBindingTable>();
  bool changed = false;

  for (const auto &entry : table) {
    llvm::StringRef kernelName = entry.first;
    const cir::KernelBinding &binding = entry.second;

    // A copy of a copy gains nothing.
    if (kernelName.ends_with(kCoveredSuffix))
      continue;

    // The proof is independent of any launch, so it is done once, on the
    // originals: cloning a kernel with no guard to fold would leave a copy
    // nobody benefits from. A kernel compiled for several devices has a copy
    // per device, and the premise has to cover the guards of each.
    SmallVector<Guard> guards;
    for (cir::FuncOp kernel : binding.deviceKernels)
      if (!kernel.isDeclaration())
        findGuards(kernel, guards);
    if (guards.empty())
      continue;

    // Only a launch whose grid and block size are known, and whose arguments
    // the extents can be recomputed from, can test the premise; the others
    // keep calling the original. This is settled before cloning, because
    // cloning retargets the selected launches at the guard-free copy.
    SmallVector<cir::LaunchSite> sites;
    for (const cir::LaunchSite &site : binding.launchSites) {
      if (!site.hasGeometry() || !site.getGridDim().x || !site.getBlockDim().x)
        continue;
      mlir::ValueRange args = cir::CallOp(site.stubCall).getArgOperands();
      if (llvm::all_of(guards, [&](const Guard &g) {
            return canRecompute(g.extent, g.kernel, args);
          }))
        sites.push_back(site);
    }
    if (sites.empty())
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

    // Rewriting in place would strip guards from launches this pass never saw,
    // so the guards go from a copy that only these launches reach.
    std::optional<cir::KernelClone> copy =
        cir::cloneKernelForSites(container, binding, kCoveredSuffix, sites);
    if (!copy)
      continue;
    changed = true;

    // Each launch tests the premise with its own geometry and arguments, the
    // ones the original's guards were proven against.
    for (const cir::LaunchSite &site : sites)
      emitGuardedDispatch(site.stubCall, site.getGridDim().x,
                          site.getBlockDim().x, guards, originalStub,
                          originalKernel);

    // Fold the guards in the copy. Proving them again there finds the same
    // comparisons, which avoids having to map operations across the clone.
    for (cir::FuncOp clone : copy->deviceKernels) {
      if (clone.isDeclaration())
        continue;
      SmallVector<Guard> cloneGuards;
      findGuards(clone, cloneGuards);
      for (Guard &g : cloneGuards) {
        OpBuilder b(g.cmp);
        auto boolTy = cir::BoolType::get(b.getContext());
        auto cst = cir::ConstantOp::create(
            b, g.cmp.getLoc(),
            cir::BoolAttr::get(b.getContext(), boolTy, g.foldTo));
        g.cmp.getResult().replaceAllUsesWith(cst.getResult());
        g.cmp.erase();
      }
    }
  }

  if (!changed)
    markAllAnalysesPreserved();
}

} // namespace

std::unique_ptr<Pass> mlir::createOffloadEliminateCoveredGuardsPass() {
  return std::make_unique<OffloadEliminateCoveredGuardsPass>();
}
