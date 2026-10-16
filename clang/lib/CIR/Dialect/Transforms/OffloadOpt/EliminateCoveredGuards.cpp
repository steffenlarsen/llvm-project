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
//     largest thread id, the block and the grid size, in any dimension --
//     without knowing any of them.
//   * At each launch, the host evaluates that bound with the launch's actual
//     grid and block, recomputes the guard's extent from the launch's own
//     arguments, and dispatches to a copy of the kernel with the guard folded
//     away when the bound is below the extent.
//
// The premise is checked rather than derived, so it does not matter how the
// grid was computed: `ceil(n/d)`, `(n-1)/d+1`, or clamped to the device limit.
// With `gridDim = ceil(n/d)` and `blockDim = d` it is exactly `n % d == 0`.
//
// The same reasoning removes the back edge of a grid-stride loop,
//
//   for (i = begin + index; i < end; i += stride)
//
// which runs its body at most once per thread when the launch covers the
// range, `begin + stride >= end`: the first step already leaves `i` at or past
// `end`. In the copy the step lands on `end` outright, and LLVM deletes the
// loop. Kokkos clamps its step, `i < end - stride ? i + stride : end`; there
// the copy folds that comparison to false, which says the same.
//
// What the host recomputes -- an extent, a loop bound, a stride, a `begin` --
// is a uniform value: one computed from the kernel's arguments, scalar or a
// field of an argument passed by value, from the launch dimensions and from
// constants. The host computes it from what the launch passes.
//
// It runs before CIRFlattenCFG, where `cir.for` is still structured (giving
// exact induction variable bounds) and the launch geometry is still traceable.
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/CIRTransformUtils.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/DeviceIndex.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelBindingTable.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelCloning.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/LocalSlots.h"
#include "clang/CIR/Interfaces/CIRLoopOpInterface.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"

#include <limits>
#include <map>
#include <set>
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

// Largest coefficient a bound may carry. A monomial multiplies at most one
// grid-sized quantity -- a block id or the grid size, below 2^31 -- by
// block-sized ones -- a thread id or the block size, at most 1024 -- for any
// launch the runtime accepts. With this bound every term of a bound fits in 64
// bits on the host with ample room to sum them.
static constexpr int64_t kMaxCoeff = 1 << 16;

// How deep the analyses below follow a value before giving up.
static constexpr unsigned kMaxDepth = 12;

//===----------------------------------------------------------------------===//
// Symbolic bounds in the launch dimensions
//===----------------------------------------------------------------------===//

// The launch quantities a bound is written in, each in all three dimensions.
enum class SymKind : unsigned {
  BlockIdMax,  // gridDim - 1, the largest blockIdx
  ThreadIdMax, // blockDim - 1, the largest threadIdx
  BlockDim,    // blockDim
  GridDim,     // gridDim
};

static unsigned symbolId(SymKind kind, unsigned dim) {
  return static_cast<unsigned>(kind) * 3 + dim;
}
static SymKind symbolKind(unsigned id) { return static_cast<SymKind>(id / 3); }
static unsigned symbolDim(unsigned id) { return id % 3; }
static bool isGridSized(unsigned id) {
  SymKind kind = symbolKind(id);
  return kind == SymKind::BlockIdMax || kind == SymKind::GridDim;
}

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
  static Poly symbol(unsigned id) {
    Poly p;
    p.terms[{id}] = 1;
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
  // largest value is taken where every symbol is largest, and its smallest,
  // the constant term, where every symbol is zero.
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

// The product, as long as it stays of degree two with at most one grid-sized
// factor per monomial. Only non-negative factors are multiplied: then the
// product of two largest values is the largest product, which is what makes
// the result a bound.
static std::optional<Poly> mul(const Poly &a, const Poly &b) {
  if (!a.nonNegative() || !b.nonNegative())
    return std::nullopt;
  Poly r;
  for (const auto &[ma, ca] : a.terms) {
    for (const auto &[mb, cb] : b.terms) {
      std::vector<unsigned> mono = ma;
      mono.insert(mono.end(), mb.begin(), mb.end());
      if (mono.size() > 2 || llvm::count_if(mono, isGridSized) > 1)
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

// That a value, the polynomial plus the uniform terms, does not exceed what
// its type holds.
struct Limit {
  Poly poly;
  SmallVector<Value, 2> terms;
  int64_t limit;
};

// What an index is: a polynomial in the launch symbols plus uniform terms,
// values the same for every thread of a launch -- Kokkos's `begin` -- that the
// host computes. Every symbol is non-negative and the host checks every term
// is, so the value is at least the constant term plus the terms, and at most
// the polynomial's largest value plus them. `limits` are the values it was
// computed through, each of which the host checks fits its type, so that no
// launch for which a step would wrap reaches the copy.
struct IndexBound {
  Poly max;
  SmallVector<Value, 2> terms;
  SmallVector<Limit> limits;
};

// Record that `b`, computed in type `ty`, must not exceed what `ty` holds.
static std::optional<IndexBound> within(IndexBound b, Type ty) {
  std::optional<int64_t> limit = typeMax(ty);
  if (!limit)
    return std::nullopt;
  if (b.max.hasSymbol() || !b.terms.empty())
    b.limits.push_back({b.max, b.terms, *limit});
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
  a.terms.append(b.terms.begin(), b.terms.end());
  a.max = std::move(max);
  return a;
}

//===----------------------------------------------------------------------===//
// Locals
//===----------------------------------------------------------------------===//

// Locals assigned once, shared with the other offload passes.
using cir::addrIsSlot;
using cir::collectSlotUsers;
using cir::findSlot;
using cir::onlyStore;
using cir::tryConstThroughSlot;

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

//===----------------------------------------------------------------------===//
// Fields of an argument passed by value
//===----------------------------------------------------------------------===//

// What deciding uniformity needs about the kernel being analysed.
struct KernelContext {
  cir::FuncOp kernel;
  mlir::DataLayout dataLayout;

  explicit KernelContext(cir::FuncOp kernel)
      : kernel(kernel), dataLayout(kernel->getParentOfType<mlir::ModuleOp>()) {}

  bool isArgument(Value v) {
    auto arg = dyn_cast<BlockArgument>(v);
    return arg && arg.getOwner() == &kernel.getBody().front();
  }
};

// A read of a field of a kernel argument passed by value: the argument, and
// the member index at each level from the argument's record down to the field,
// together with the record at that level.
struct FieldRead {
  unsigned argIndex;
  SmallVector<unsigned, 4> path;
  SmallVector<cir::RecordType, 4> records;
};

// Whether `writer` can only run after `read`: it comes later in a block that
// encloses both, and no loop repeats that block.
static bool runsOnlyAfter(Operation *writer, Operation *read) {
  for (Operation *a = read; a && !isa<cir::FuncOp>(a); a = a->getParentOp()) {
    Block *block = a->getBlock();
    Operation *w = block->findAncestorOpInBlock(*writer);
    if (!w)
      continue;
    if (w == a || !a->isBeforeInBlock(w))
      return false;
    for (Operation *p = block->getParentOp(); p && !isa<cir::FuncOp>(p);
         p = p->getParentOp())
      if (isa<cir::LoopOpInterface>(p))
        return false;
    return true;
  }
  return false;
}

// Whether nothing writes the copy of an argument in `slot` -- spilled there by
// `spill` -- before `read` reads it. Every address derived from the slot is
// followed, including through locals that hold it; anything other than reading
// through one counts as a write, and has to come after `read`. A `const`
// parameter is not taken as unwritten: a `mutable` member of it may be.
static bool unwrittenBefore(cir::AllocaOp slot, cir::StoreOp spill,
                            Operation *read) {
  SmallVector<Value, 8> worklist{slot.getResult()};
  llvm::DenseSet<Value> seen;
  while (!worklist.empty()) {
    Value v = worklist.pop_back_val();
    if (!seen.insert(v).second)
      continue;
    for (OpOperand &use : v.getUses()) {
      Operation *user = use.getOwner();
      if (user == spill.getOperation() ||
          isa<cir::LoadOp, cir::LifetimeStartOp, cir::LifetimeEndOp>(user))
        continue;
      if (isa<cir::CastOp, cir::GetMemberOp, cir::BaseClassAddrOp>(user)) {
        worklist.push_back(user->getResult(0));
        continue;
      }
      if (auto store = dyn_cast<cir::StoreOp>(user);
          store && store.getValue() == v) {
        // The address is kept in a local: what is read back out of that local
        // is the address again.
        cir::AllocaOp holder = findSlot(store.getAddr());
        if (holder && onlyStore(holder) == store) {
          SmallVector<Operation *, 8> holderUsers;
          collectSlotUsers(holder, holderUsers);
          for (Operation *holderUser : holderUsers)
            if (auto load = dyn_cast<cir::LoadOp>(holderUser))
              worklist.push_back(load.getResult());
          continue;
        }
      }
      if (auto copy = dyn_cast<cir::CopyOp>(user);
          copy && copy.getSrc() == v && copy.getDst() != v)
        continue;
      if (!runsOnlyAfter(user, read))
        return false;
    }
  }
  return true;
}

// The member of `record` a base-class address at byte `offset` of type
// `baseTy` refers to.
static std::optional<unsigned> baseMemberIndex(cir::RecordType record,
                                               uint64_t offset, Type baseTy,
                                               const mlir::DataLayout &dl) {
  if (record.isUnion() || record.isIncomplete())
    return std::nullopt;
  for (unsigned i = 0, e = record.getNumElements(); i != e; ++i)
    if (record.getElementType(i) == baseTy &&
        record.getElementOffset(dl, i) == offset)
      return i;
  return std::nullopt;
}

// The field of a by-value kernel argument `load` reads, when nothing writes
// the argument's copy before the read.
static std::optional<FieldRead> matchFieldRead(cir::LoadOp load,
                                               KernelContext &ctx) {
  SmallVector<unsigned, 4> path;
  SmallVector<cir::RecordType, 4> records;
  Value addr = load.getAddr();
  for (unsigned depth = 0; depth < kMaxDepth; ++depth) {
    Operation *def = addr.getDefiningOp();
    if (!def)
      return std::nullopt;
    if (auto cast = dyn_cast<cir::CastOp>(def)) {
      if (cast.getKind() != cir::CastKind::address_space)
        return std::nullopt;
      addr = cast.getSrc();
      continue;
    }
    if (auto member = dyn_cast<cir::GetMemberOp>(def)) {
      auto record = mlir::dyn_cast<cir::RecordType>(
          mlir::cast<cir::PointerType>(member.getAddr().getType())
              .getPointee());
      if (!record || record.isUnion())
        return std::nullopt;
      path.push_back(member.getIndex());
      records.push_back(record);
      addr = member.getAddr();
      continue;
    }
    if (auto base = dyn_cast<cir::BaseClassAddrOp>(def)) {
      auto record = mlir::dyn_cast<cir::RecordType>(
          mlir::cast<cir::PointerType>(base.getDerivedAddr().getType())
              .getPointee());
      Type baseTy = mlir::cast<cir::PointerType>(base.getType()).getPointee();
      std::optional<unsigned> index =
          record ? baseMemberIndex(record, base.getOffset().getZExtValue(),
                                   baseTy, ctx.dataLayout)
                 : std::nullopt;
      if (!index)
        return std::nullopt;
      path.push_back(*index);
      records.push_back(record);
      addr = base.getDerivedAddr();
      continue;
    }
    if (auto held = dyn_cast<cir::LoadOp>(def)) {
      // An address kept in a local, such as `this`.
      cir::AllocaOp holder = findSlot(held.getAddr());
      cir::StoreOp only = holder ? onlyStore(holder) : cir::StoreOp();
      if (!only)
        return std::nullopt;
      addr = only.getValue();
      continue;
    }
    if (auto slot = dyn_cast<cir::AllocaOp>(def)) {
      // The argument's own copy: the slot it was spilled into on entry.
      if (path.empty())
        return std::nullopt;
      cir::StoreOp spill;
      SmallVector<Operation *, 8> slotUsers;
      collectSlotUsers(slot, slotUsers);
      for (Operation *user : slotUsers) {
        auto store = dyn_cast<cir::StoreOp>(user);
        if (!store || !addrIsSlot(store.getAddr(), slot) ||
            !ctx.isArgument(store.getValue()))
          continue;
        if (spill)
          return std::nullopt;
        spill = store;
      }
      if (!spill || !unwrittenBefore(slot, spill, load))
        return std::nullopt;
      std::reverse(path.begin(), path.end());
      std::reverse(records.begin(), records.end());
      return FieldRead{
          mlir::cast<BlockArgument>(spill.getValue()).getArgNumber(),
          std::move(path), std::move(records)};
    }
    return std::nullopt;
  }
  return std::nullopt;
}

//===----------------------------------------------------------------------===//
// Uniform values the host can recompute
//===----------------------------------------------------------------------===//

// The operations the host may redo: integer casts and arithmetic, dividing only
// by a positive constant so that recomputing cannot fault where the kernel
// would not have.
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

// Whether `v` is the same for every thread of a launch: computed from the
// kernel's arguments -- scalars, or fields of one passed by value -- the block
// and grid sizes, constants, and locals assigned once from those.
static bool isUniform(Value v, KernelContext &ctx, unsigned depth = 0) {
  if (depth > kMaxDepth || !mlir::isa<cir::IntType>(v.getType()))
    return false;
  if (ctx.isArgument(v))
    return true;
  Operation *def = v.getDefiningOp();
  if (!def)
    return false;
  if (auto constant = dyn_cast<cir::ConstantOp>(def))
    return mlir::isa<cir::IntAttr>(constant.getValue());
  if (std::optional<cir::DeviceIndex> index = cir::matchDeviceIndex(def))
    return index->kind == cir::DeviceIndexKind::BlockDim ||
           index->kind == cir::DeviceIndexKind::GridDim;
  if (auto load = dyn_cast<cir::LoadOp>(def)) {
    if (load.getIsVolatile())
      return false;
    if (matchFieldRead(load, ctx))
      return true;
    cir::AllocaOp slot = findSlot(load.getAddr());
    cir::StoreOp only = slot ? onlyStore(slot) : cir::StoreOp();
    return only && isUniform(only.getValue(), ctx, depth + 1);
  }
  return isRecomputable(def) &&
         llvm::all_of(def->getOperands(), [&](Value operand) {
           return isUniform(operand, ctx, depth + 1);
         });
}

// What a launch provides to recompute a uniform value with.
struct LaunchValues {
  mlir::ValueRange args;
  cir::LaunchSite::Dim3 grid, block;
};

static Value dimComponent(const cir::LaunchSite::Dim3 &dim3, unsigned dim) {
  return dim == 0 ? dim3.x : dim == 1 ? dim3.y : dim3.z;
}

// The type of `field` in the record a launch passes as `hostRecordTy`, or null
// when the host's record is not shaped like the device's: they are distinct
// types, so each level has to agree on the member read.
static Type hostFieldType(const FieldRead &field, Type hostRecordTy) {
  Type ty = hostRecordTy;
  for (auto [index, deviceRecord] : llvm::zip(field.path, field.records)) {
    auto hostRecord = mlir::dyn_cast<cir::RecordType>(ty);
    if (!hostRecord || hostRecord.isUnion() ||
        hostRecord.getNumElements() != deviceRecord.getNumElements())
      return {};
    Type member = hostRecord.getElementType(index);
    Type deviceMember = deviceRecord.getElementType(index);
    bool isRecord = mlir::isa<cir::RecordType>(member);
    if (isRecord != mlir::isa<cir::RecordType>(deviceMember) ||
        (!isRecord && member != deviceMember))
      return {};
    ty = member;
  }
  return ty;
}

// Redo the computation of the uniform value `v` with what `launch` provides,
// before `builder`'s insertion point -- or, with a null `builder`, only check
// that it can be: nullopt when it cannot, a null value when it can. The copies
// carry no overflow flags: the host evaluates these even for a launch that
// runs no thread, where the kernel would not, so a wrap there must not be
// undefined.
static std::optional<Value> recompute(Value v, KernelContext &ctx,
                                      const LaunchValues &launch,
                                      OpBuilder *builder, unsigned depth = 0) {
  if (depth > kMaxDepth || !mlir::isa<cir::IntType>(v.getType()))
    return std::nullopt;
  Location loc = v.getLoc();

  if (ctx.isArgument(v)) {
    unsigned i = mlir::cast<BlockArgument>(v).getArgNumber();
    if (i >= launch.args.size() || launch.args[i].getType() != v.getType())
      return std::nullopt;
    return builder ? launch.args[i] : Value();
  }
  Operation *def = v.getDefiningOp();
  if (!def)
    return std::nullopt;

  if (auto constant = dyn_cast<cir::ConstantOp>(def)) {
    if (!mlir::isa<cir::IntAttr>(constant.getValue()))
      return std::nullopt;
    return builder ? builder->clone(*def)->getResult(0) : Value();
  }

  if (std::optional<cir::DeviceIndex> index = cir::matchDeviceIndex(def)) {
    if (index->kind != cir::DeviceIndexKind::BlockDim &&
        index->kind != cir::DeviceIndexKind::GridDim)
      return std::nullopt;
    Value dim = dimComponent(index->kind == cir::DeviceIndexKind::BlockDim
                                 ? launch.block
                                 : launch.grid,
                             index->dim);
    if (!dim || !mlir::isa<cir::IntType>(dim.getType()))
      return std::nullopt;
    if (!builder)
      return Value();
    if (dim.getType() == v.getType())
      return dim;
    return cir::CastOp::create(*builder, loc, v.getType(),
                               cir::CastKind::integral, dim)
        .getResult();
  }

  if (auto load = dyn_cast<cir::LoadOp>(def)) {
    if (load.getIsVolatile())
      return std::nullopt;
    if (std::optional<FieldRead> field = matchFieldRead(load, ctx)) {
      if (field->argIndex >= launch.args.size())
        return std::nullopt;
      Value record = launch.args[field->argIndex];
      if (hostFieldType(*field, record.getType()) != v.getType())
        return std::nullopt;
      if (!builder)
        return Value();
      for (unsigned index : field->path)
        record = cir::ExtractMemberOp::create(*builder, loc, record, index)
                     .getResult();
      return record;
    }
    cir::AllocaOp slot = findSlot(load.getAddr());
    cir::StoreOp only = slot ? onlyStore(slot) : cir::StoreOp();
    if (!only)
      return std::nullopt;
    return recompute(only.getValue(), ctx, launch, builder, depth + 1);
  }

  if (!isRecomputable(def))
    return std::nullopt;
  IRMapping map;
  for (Value operand : def->getOperands()) {
    std::optional<Value> copy =
        recompute(operand, ctx, launch, builder, depth + 1);
    if (!copy)
      return std::nullopt;
    if (builder)
      map.map(operand, *copy);
  }
  if (!builder)
    return Value();
  Operation *copy = builder->clone(*def, map);
  if (auto addOp = dyn_cast<cir::AddOp>(copy)) {
    addOp.setNoSignedWrap(false);
    addOp.setNoUnsignedWrap(false);
  } else if (auto subOp = dyn_cast<cir::SubOp>(copy)) {
    subOp.setNoSignedWrap(false);
    subOp.setNoUnsignedWrap(false);
  } else if (auto mulOp = dyn_cast<cir::MulOp>(copy)) {
    mulOp.setNoSignedWrap(false);
    mulOp.setNoUnsignedWrap(false);
  }
  return copy->getResult(0);
}

static bool canRecompute(Value v, KernelContext &ctx,
                         const LaunchValues &launch) {
  return recompute(v, ctx, launch, /*builder=*/nullptr).has_value();
}

//===----------------------------------------------------------------------===//
// Index bounds
//===----------------------------------------------------------------------===//

/// What \p v is in the launch symbols and uniform terms, or nullopt when
/// anything is unrecognised.
///
/// Deliberately conservative: every construct not explicitly handled bails,
/// because a bound that is too small would license deleting a live guard.
static std::optional<IndexBound> computeBound(Value v, KernelContext &ctx,
                                              unsigned depth = 0) {
  if (depth > kMaxDepth)
    return std::nullopt;

  if (std::optional<int64_t> c = tryConstThroughSlot(v))
    return IndexBound{Poly::constant(*c), {}, {}};

  Operation *def = v.getDefiningOp();
  if (!def)
    return std::nullopt;

  if (std::optional<cir::DeviceIndex> index = cir::matchDeviceIndex(def)) {
    SymKind kind = SymKind::GridDim;
    switch (index->kind) {
    case cir::DeviceIndexKind::BlockId:
      kind = SymKind::BlockIdMax;
      break;
    case cir::DeviceIndexKind::ThreadId:
      kind = SymKind::ThreadIdMax;
      break;
    case cir::DeviceIndexKind::BlockDim:
      kind = SymKind::BlockDim;
      break;
    case cir::DeviceIndexKind::GridDim:
      kind = SymKind::GridDim;
      break;
    }
    return within(IndexBound{Poly::symbol(symbolId(kind, index->dim)), {}, {}},
                  v.getType());
  }

  if (auto cast = dyn_cast<cir::CastOp>(def)) {
    if (cast.getKind() != cir::CastKind::integral)
      return std::nullopt;
    std::optional<IndexBound> o = computeBound(cast.getSrc(), ctx, depth + 1);
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
          return IndexBound{Poly::constant(*m), {}, {}};
      }
    }

    // Otherwise a plain single-assignment local.
    cir::StoreOp only = onlyStore(slot);
    if (!only)
      return std::nullopt;
    return computeBound(only.getValue(), ctx, depth + 1);
  }

  // An operand of a sum may also be uniform, a term the host adds in.
  auto summand = [&](Value operand) -> std::optional<IndexBound> {
    if (std::optional<IndexBound> b = computeBound(operand, ctx, depth + 1))
      return b;
    if (isUniform(operand, ctx))
      return IndexBound{Poly(), {operand}, {}};
    return std::nullopt;
  };
  auto factor = [&](Value operand) -> std::optional<IndexBound> {
    std::optional<IndexBound> b = computeBound(operand, ctx, depth + 1);
    return b && b->terms.empty() ? b : std::nullopt;
  };

  if (auto addOp = dyn_cast<cir::AddOp>(def)) {
    std::optional<IndexBound> l = summand(addOp.getLhs());
    std::optional<IndexBound> r = l ? summand(addOp.getRhs()) : std::nullopt;
    std::optional<Poly> max = r ? add(l->max, r->max) : std::nullopt;
    if (!max)
      return std::nullopt;
    return within(merge(*l, *r, *max), v.getType());
  }
  if (auto mulOp = dyn_cast<cir::MulOp>(def)) {
    std::optional<IndexBound> l = factor(mulOp.getLhs());
    std::optional<IndexBound> r = l ? factor(mulOp.getRhs()) : std::nullopt;
    std::optional<Poly> max = r ? mul(l->max, r->max) : std::nullopt;
    if (!max)
      return std::nullopt;
    return within(merge(*l, *r, *max), v.getType());
  }
  if (auto divOp = dyn_cast<cir::DivOp>(def)) {
    std::optional<int64_t> c = tryConstThroughSlot(divOp.getRhs());
    if (!c)
      return std::nullopt;
    std::optional<IndexBound> o = factor(divOp.getLhs());
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
// Guards
//===----------------------------------------------------------------------===//

enum class GuardKind {
  // `index < extent` and its kin, settled by the index's largest value.
  Tail,
  // A grid-stride loop's clamped step, `i < end - stride ? i + stride : end`,
  // whose comparison is false on the only trip the loop makes: settled by
  // `i`'s smallest value.
  StrideCompare,
  // A grid-stride loop's step `i += stride`, which leaves `i` at or past the
  // loop's bound after the first trip; the copy stores the bound instead.
  StrideStep,
};

struct Guard {
  GuardKind kind;
  // The device kernel the guard is in; its uniform values are written in its
  // arguments.
  cir::FuncOp kernel;
  // The index, or for a stride loop the induction variable's first value.
  IndexBound bound;
  // Tail and StrideCompare: the comparison, and what the copy folds it to.
  cir::CmpOp cmp;
  bool foldTo = false;
  // Tail: the premise is `bound < extent`, or `<=` when not strict.
  bool strict = true;
  // Tail: the extent. StrideCompare: the step comparison's uniform side.
  // StrideStep: the loop's bound.
  Value extent;
  // StrideStep: the step's store, the stride it adds, and the largest value
  // the sum may reach without wrapping in the types it is computed in.
  cir::StoreOp stepStore;
  Value stride;
  int64_t stepLimit = 0;
};

// Whether `op` sits in the condition or the step of the innermost structured
// loop around it, where a comparison is loop control rather than a guard.
static bool isLoopControl(Operation *op) {
  auto forOp = op->getParentOfType<cir::ForOp>();
  Region *region = op->getParentRegion();
  return forOp && (forOp.getCond().isAncestor(region) ||
                   forOp.getStep().isAncestor(region));
}

// Find the tail guards of `kernel`: comparisons of an index bounded in the
// launch dimensions against a uniform extent.
static void findTailGuards(KernelContext &ctx, SmallVectorImpl<Guard> &out) {
  ctx.kernel.walk([&](cir::CmpOp cmp) {
    cir::CmpOpKind kind = cmp.getKind();
    if (kind != cir::CmpOpKind::ge && kind != cir::CmpOpKind::gt &&
        kind != cir::CmpOpKind::lt && kind != cir::CmpOpKind::le)
      return;
    if (isLoopControl(cmp))
      return;

    for (bool indexOnLhs : {true, false}) {
      Value index = indexOnLhs ? cmp.getLhs() : cmp.getRhs();
      Value extent = indexOnLhs ? cmp.getRhs() : cmp.getLhs();
      if (!isUniform(extent, ctx))
        continue;
      std::optional<IndexBound> bound = computeBound(index, ctx);
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
      Guard guard;
      guard.kind = GuardKind::Tail;
      guard.kernel = ctx.kernel;
      guard.bound = std::move(*bound);
      guard.cmp = cmp;
      guard.strict = k == cir::CmpOpKind::lt || k == cir::CmpOpKind::ge;
      guard.foldTo = k == cir::CmpOpKind::lt || k == cir::CmpOpKind::le;
      guard.extent = extent;
      out.push_back(std::move(guard));
      LLVM_DEBUG(llvm::dbgs() << "GUARDDBG   cmp: guard, folds to "
                              << (guard.foldTo ? "true" : "false") << "\n");
      return;
    }
  });
}

// `v` without the integral casts that keep its width, which change how its
// bits are read but not the bits.
static Value stripSameWidthCasts(Value v) {
  while (auto cast = v.getDefiningOp<cir::CastOp>()) {
    auto from = mlir::dyn_cast<cir::IntType>(cast.getSrc().getType());
    auto to = mlir::dyn_cast<cir::IntType>(cast.getType());
    if (cast.getKind() != cir::CastKind::integral || !from || !to ||
        from.getWidth() != to.getWidth())
      break;
    v = cast.getSrc();
  }
  return v;
}

// `v` without those casts, and without the locals assigned once it was passed
// through.
static Value canonical(Value v) {
  for (unsigned i = 0; i < kMaxDepth; ++i) {
    if (auto cast = v.getDefiningOp<cir::CastOp>()) {
      auto from = mlir::dyn_cast<cir::IntType>(cast.getSrc().getType());
      auto to = mlir::dyn_cast<cir::IntType>(cast.getType());
      if (cast.getKind() == cir::CastKind::integral && from && to &&
          from.getWidth() == to.getWidth()) {
        v = cast.getSrc();
        continue;
      }
    }
    if (auto load = v.getDefiningOp<cir::LoadOp>()) {
      cir::AllocaOp slot = findSlot(load.getAddr());
      if (cir::StoreOp only = slot ? onlyStore(slot) : cir::StoreOp()) {
        v = only.getValue();
        continue;
      }
    }
    break;
  }
  return v;
}

static bool readsSlot(Value v, cir::AllocaOp slot) {
  auto load = stripSameWidthCasts(v).getDefiningOp<cir::LoadOp>();
  return load && addrIsSlot(load.getAddr(), slot);
}

// A copy of `bound`, the loop's bound read in its condition, usable at
// `builder`'s insertion point inside the loop -- or, with a null `builder`,
// whether one can be made: the bound itself when it is defined outside the
// loop, or a new read of the place it is read from.
static std::optional<Value> materializeBound(Value bound, cir::ForOp forOp,
                                             OpBuilder *builder) {
  Operation *def = bound.getDefiningOp();
  if (!def || !forOp->isAncestor(def))
    return bound;
  auto load = dyn_cast<cir::LoadOp>(def);
  Operation *addrDef = load ? load.getAddr().getDefiningOp() : nullptr;
  if (!load || (addrDef && forOp->isAncestor(addrDef)))
    return std::nullopt;
  if (!builder)
    return Value();
  return builder->clone(*load)->getResult(0);
}

// Match `forOp` as a grid-stride loop whose single trip the launch can be
// checked for:
//
//   iv = first;                        first: an index plus uniform terms
//   for (; iv < bound; iv = step)      bound: uniform
//
// with a step that is either `iv < r ? ... : bound` (r uniform, the clamped
// form Kokkos writes) or `iv + stride` (stride uniform). The induction
// variable is written only by those two stores.
static std::optional<Guard> matchStrideLoop(cir::ForOp forOp,
                                            KernelContext &ctx) {
  auto condition =
      dyn_cast<cir::ConditionOp>(forOp.getCond().front().getTerminator());
  auto cmp = condition ? condition.getCondition().getDefiningOp<cir::CmpOp>()
                       : cir::CmpOp();
  if (!cmp || cmp.getKind() != cir::CmpOpKind::lt)
    return std::nullopt;
  auto ivLoad = cmp.getLhs().getDefiningOp<cir::LoadOp>();
  cir::AllocaOp iv = ivLoad ? findSlot(ivLoad.getAddr()) : cir::AllocaOp();
  Value bound = cmp.getRhs();
  Type ivTy = cmp.getLhs().getType();
  if (!iv || !isUniform(bound, ctx))
    return std::nullopt;

  cir::StoreOp init, step;
  bool readOutside = false;
  SmallVector<Operation *, 8> ivUsers;
  collectSlotUsers(iv, ivUsers);
  for (Operation *user : ivUsers) {
    if (auto store = dyn_cast<cir::StoreOp>(user)) {
      if (!addrIsSlot(store.getAddr(), iv))
        return std::nullopt;
      if (forOp.getStep().isAncestor(store->getParentRegion())) {
        if (step)
          return std::nullopt;
        step = store;
      } else if (!forOp->isAncestor(store)) {
        if (init)
          return std::nullopt;
        init = store;
      } else {
        return std::nullopt;
      }
      continue;
    }
    if (!isa<cir::LoadOp>(user))
      return std::nullopt;
    readOutside |= !forOp->isAncestor(user);
  }
  // Whenever the loop is entered, the variable holds `first`, or what a
  // previous run of the loop left it at -- at or past the bound, exactly the
  // bound in the copy, so neither version takes a trip -- or nothing yet,
  // which the source may not read.
  if (!init || !step || init.getValue().getType() != ivTy)
    return std::nullopt;

  std::optional<IndexBound> first = computeBound(init.getValue(), ctx);
  if (!first || !first->max.hasSymbol() || !first->max.monotone() ||
      first->max.constantTerm() < 0)
    return std::nullopt;

  Guard guard;
  guard.kernel = ctx.kernel;
  guard.bound = std::move(*first);

  Value stepped = step.getValue();
  if (auto ternary = canonical(stepped).getDefiningOp<cir::TernaryOp>()) {
    // The clamped step. Its comparison against the induction variable is what
    // folds; its other arm has to land on the loop's bound.
    auto stepCmp = ternary.getCond().getDefiningOp<cir::CmpOp>();
    if (!stepCmp || stepCmp.getKind() != cir::CmpOpKind::lt ||
        !readsSlot(stepCmp.getLhs(), iv) ||
        stepCmp.getLhs().getType() != ivTy || !isUniform(stepCmp.getRhs(), ctx))
      return std::nullopt;
    auto yield = dyn_cast<cir::YieldOp>(
        ternary.getFalseRegion().front().getTerminator());
    if (!yield || yield.getNumOperands() != 1 ||
        canonical(yield.getOperand(0)) != canonical(bound))
      return std::nullopt;
    guard.kind = GuardKind::StrideCompare;
    guard.cmp = stepCmp;
    guard.foldTo = false;
    guard.extent = stepCmp.getRhs();
    return guard;
  }

  if (auto addOp = stripSameWidthCasts(stepped).getDefiningOp<cir::AddOp>()) {
    // The plain step, possibly computed in the unsigned type of the stride.
    // Storing the bound instead changes the value the induction variable is
    // left with, so nothing may read it after the loop.
    Value stride = readsSlot(addOp.getLhs(), iv)   ? addOp.getRhs()
                   : readsSlot(addOp.getRhs(), iv) ? addOp.getLhs()
                                                   : Value();
    std::optional<int64_t> addMax = typeMax(addOp.getType());
    if (!stride || readOutside || !addMax || !isUniform(stride, ctx) ||
        !materializeBound(bound, forOp, /*builder=*/nullptr))
      return std::nullopt;
    guard.kind = GuardKind::StrideStep;
    guard.extent = bound;
    guard.stepStore = step;
    guard.stride = stride;
    guard.stepLimit = std::min(*addMax, *typeMax(ivTy));
    return guard;
  }
  return std::nullopt;
}

// The guards of `kernel`, in an order that finding them again in a copy of the
// kernel reproduces.
static void findGuards(cir::FuncOp kernel, SmallVectorImpl<Guard> &out) {
  KernelContext ctx(kernel);
  findTailGuards(ctx, out);
  kernel.walk([&](cir::ForOp forOp) {
    if (std::optional<Guard> guard = matchStrideLoop(forOp, ctx))
      out.push_back(std::move(*guard));
  });
}

//===----------------------------------------------------------------------===//
// The dispatch
//===----------------------------------------------------------------------===//

static void collectSymbols(const Poly &p, std::set<unsigned> &out) {
  for (const auto &[mono, coeff] : p.terms)
    out.insert(mono.begin(), mono.end());
}

// Every value `guard`'s premise is computed from.
static SmallVector<Value> uniformInputs(const Guard &guard) {
  SmallVector<Value> values(guard.bound.terms.begin(), guard.bound.terms.end());
  for (const Limit &limit : guard.bound.limits)
    values.append(limit.terms.begin(), limit.terms.end());
  values.push_back(guard.extent);
  if (guard.stride)
    values.push_back(guard.stride);
  return values;
}

// Whether `launch` provides everything `guard`'s premise is computed from.
static bool canTestPremise(const Guard &guard, const LaunchValues &launch) {
  std::set<unsigned> symbols;
  collectSymbols(guard.bound.max, symbols);
  for (const Limit &limit : guard.bound.limits)
    collectSymbols(limit.poly, symbols);
  for (unsigned id : symbols) {
    bool isBlock = symbolKind(id) == SymKind::ThreadIdMax ||
                   symbolKind(id) == SymKind::BlockDim;
    if (!dimComponent(isBlock ? launch.block : launch.grid, symbolDim(id)))
      return false;
  }
  KernelContext ctx(guard.kernel);
  return llvm::all_of(uniformInputs(guard),
                      [&](Value v) { return canRecompute(v, ctx, launch); });
}

// Whether the premise of every guard holds for this launch, evaluated before
// `builder`'s insertion point. Every guard's premise has to be testable with
// `launch` (canTestPremise).
static Value emitPremise(OpBuilder &builder, Location loc,
                         const LaunchValues &launch,
                         llvm::ArrayRef<Guard> guards) {
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
  auto addI64 = [&](Value a, Value b) -> Value {
    return cir::AddOp::create(builder, loc, i64, a, b).getResult();
  };
  auto compare = [&](cir::CmpOpKind kind, Value a, Value b) -> Value {
    return cir::CmpOp::create(builder, loc, boolTy, kind, a, b);
  };

  // The symbols' values for this launch, for every dimension a premise uses.
  // Arithmetic wraps rather than being undefined; it can only wrap for a
  // launch the runtime rejects anyway.
  std::set<unsigned> used;
  for (const Guard &guard : guards) {
    collectSymbols(guard.bound.max, used);
    for (const Limit &limit : guard.bound.limits)
      collectSymbols(limit.poly, used);
  }
  std::set<unsigned> dims;
  for (unsigned id : used)
    dims.insert(symbolDim(id));
  Value symbols[12];
  for (unsigned dim : dims) {
    symbols[symbolId(SymKind::GridDim, dim)] =
        widen(dimComponent(launch.grid, dim));
    symbols[symbolId(SymKind::BlockDim, dim)] =
        widen(dimComponent(launch.block, dim));
  }
  Value one = constant(1);
  for (unsigned dim : dims) {
    symbols[symbolId(SymKind::BlockIdMax, dim)] = cir::SubOp::create(
        builder, loc, i64, symbols[symbolId(SymKind::GridDim, dim)], one);
    symbols[symbolId(SymKind::ThreadIdMax, dim)] = cir::SubOp::create(
        builder, loc, i64, symbols[symbolId(SymKind::BlockDim, dim)], one);
  }
  auto symbolValue = [&](unsigned id) -> Value { return symbols[id]; };
  std::map<Poly, Value> evaluated;
  auto evaluate = [&](const Poly &p) -> Value {
    auto [it, inserted] = evaluated.try_emplace(p);
    if (!inserted)
      return it->second;
    Value sum;
    for (const auto &[mono, coeff] : p.terms) {
      Value term = mono.empty() || coeff != 1 ? constant(coeff) : Value();
      for (unsigned s : mono)
        term = term
                   ? cir::MulOp::create(builder, loc, i64, term, symbolValue(s))
                         .getResult()
                   : symbolValue(s);
      sum = sum ? addI64(sum, term) : term;
    }
    return it->second = sum ? sum : constant(0);
  };

  // Uniform values, each recomputed once and widened.
  llvm::DenseMap<Value, Value> recomputed;
  auto uniform = [&](const Guard &guard, Value v) -> Value {
    auto [it, inserted] = recomputed.try_emplace(v);
    if (inserted) {
      KernelContext kctx(guard.kernel);
      std::optional<Value> value = recompute(v, kctx, launch, &builder);
      assert(value && *value && "premise for a launch canTestPremise rejects");
      it->second = widen(*value);
    }
    return it->second;
  };
  // `base` plus the terms.
  auto plusTerms = [&](const Guard &guard, Value base,
                       llvm::ArrayRef<Value> terms) {
    for (Value term : terms)
      base = addI64(base, uniform(guard, term));
    return base;
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

  // Limits, each with the tightest bound any value computed through it
  // carries. Those with terms are keyed by them too: only limits on the same
  // terms are comparable.
  using TermKey = std::vector<void *>;
  std::map<Poly, int64_t> limits;
  std::map<TermKey, std::pair<const Guard *, std::map<Poly, int64_t>>>
      termLimits;
  for (const Guard &guard : guards) {
    const IndexBound &b = guard.bound;
    // A term is a per-launch addend, and the bounds assume it does not
    // subtract: a negative one would let the index go below its smallest
    // value, and wrap if the type is unsigned.
    for (Value term : b.terms)
      if (mlir::cast<cir::IntType>(term.getType()).isSigned())
        conjoin(compare(cir::CmpOpKind::ge, uniform(guard, term), constant(0)));
    Value extent = uniform(guard, guard.extent);
    auto upper = [&] { return plusTerms(guard, evaluate(b.max), b.terms); };
    auto lower = [&] {
      int64_t c = b.max.constantTerm();
      if (c || b.terms.empty())
        return plusTerms(guard, constant(c), b.terms);
      return plusTerms(guard, uniform(guard, b.terms.front()),
                       llvm::ArrayRef(b.terms).drop_front());
    };

    switch (guard.kind) {
    case GuardKind::Tail:
      conjoin(compare(guard.strict ? cir::CmpOpKind::lt : cir::CmpOpKind::le,
                      upper(), extent));
      break;
    case GuardKind::StrideCompare:
      conjoin(compare(cir::CmpOpKind::ge, lower(), extent));
      break;
    case GuardKind::StrideStep: {
      // The first step leaves the variable at or past the bound, and does not
      // wrap getting there.
      Value stride = uniform(guard, guard.stride);
      conjoin(compare(cir::CmpOpKind::ge, addI64(lower(), stride), extent));
      conjoin(compare(cir::CmpOpKind::le, addI64(upper(), stride),
                      constant(guard.stepLimit)));
      break;
    }
    }

    for (const Limit &limit : b.limits) {
      std::map<Poly, int64_t> *bucket = &limits;
      if (!limit.terms.empty()) {
        TermKey key;
        for (Value term : limit.terms)
          key.push_back(term.getAsOpaquePointer());
        llvm::sort(key);
        auto &entry = termLimits[key];
        entry.first = &guard;
        bucket = &entry.second;
      }
      auto [it, inserted] = bucket->try_emplace(limit.poly, limit.limit);
      if (!inserted)
        it->second = std::min(it->second, limit.limit);
    }
  }
  // A limit implied by a larger bound on the same terms under a smaller limit
  // needs no test of its own.
  auto implied = [](const std::map<Poly, int64_t> &bucket, const Poly &poly,
                    int64_t limit) {
    return llvm::any_of(bucket, [&](const auto &other) {
      return !(other.first == poly) && dominates(other.first, poly) &&
             other.second <= limit;
    });
  };
  for (auto &[key, entry] : termLimits) {
    const Guard &guard = *entry.first;
    for (const auto &[poly, limit] : entry.second) {
      if (implied(entry.second, poly, limit))
        continue;
      SmallVector<Value, 2> terms;
      for (void *term : key)
        terms.push_back(Value::getFromOpaquePointer(term));
      conjoin(compare(cir::CmpOpKind::le,
                      plusTerms(guard, evaluate(poly), terms),
                      constant(limit)));
    }
  }
  // Only the term-free limits a launch could actually break: one no 32-bit
  // grid and block can reach, or one implied by a larger bound under a smaller
  // limit, needs no test.
  for (const auto &[poly, limit] : limits) {
    if (alwaysWithin(poly, limit) || implied(limits, poly, limit))
      continue;
    conjoin(compare(cir::CmpOpKind::le, evaluate(poly), constant(limit)));
  }
  return premise;
}

// `if (premise) fast(...) else original(...)`. `call` already targets the
// fast clone, since cloning retargeted it; the slow path is a copy of it
// pointed back at what it used to call.
static void emitGuardedDispatch(cir::CallOp call, const LaunchValues &launch,
                                llvm::ArrayRef<Guard> guards,
                                mlir::FlatSymbolRefAttr originalStub,
                                cir::CUDAKernelNameAttr originalKernel) {
  OpBuilder builder(call);
  Location loc = call.getLoc();
  Value premise = emitPremise(builder, loc, launch, guards);

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

// Apply `guard` in the copy it was found in.
static void foldGuard(Guard &guard) {
  if (guard.kind == GuardKind::StrideStep) {
    OpBuilder builder(guard.stepStore);
    auto forOp = guard.stepStore->getParentOfType<cir::ForOp>();
    std::optional<Value> bound =
        materializeBound(guard.extent, forOp, &builder);
    assert(bound && *bound && "matchStrideLoop checked the bound");
    guard.stepStore.getValueMutable().assign(*bound);
    return;
  }
  OpBuilder builder(guard.cmp);
  auto boolTy = cir::BoolType::get(builder.getContext());
  auto cst = cir::ConstantOp::create(
      builder, guard.cmp.getLoc(),
      cir::BoolAttr::get(builder.getContext(), boolTy, guard.foldTo));
  guard.cmp.getResult().replaceAllUsesWith(cst.getResult());
  guard.cmp.erase();
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

    // Only a launch that provides everything the premises are computed from
    // can test them; the others keep calling the original. This is settled
    // before cloning, because cloning retargets the selected launches at the
    // guard-free copy.
    SmallVector<cir::LaunchSite> sites;
    SmallVector<LaunchValues> siteValues;
    for (const cir::LaunchSite &site : binding.launchSites) {
      if (!site.hasGeometry())
        continue;
      LaunchValues launch{cir::CallOp(site.stubCall).getArgOperands(),
                          site.getGridDim(), site.getBlockDim()};
      if (llvm::all_of(guards, [&](const Guard &g) {
            return canTestPremise(g, launch);
          })) {
        sites.push_back(site);
        siteValues.push_back(launch);
      }
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
    for (auto [site, launch] : llvm::zip(sites, siteValues))
      emitGuardedDispatch(site.stubCall, launch, guards, originalStub,
                          originalKernel);

    // Fold the guards in the copy. Finding them again there finds the same
    // ones, which avoids having to map operations across the clone.
    for (cir::FuncOp clone : copy->deviceKernels) {
      if (clone.isDeclaration())
        continue;
      SmallVector<Guard> cloneGuards;
      findGuards(clone, cloneGuards);
      for (Guard &g : cloneGuards)
        foldGuard(g);
    }
  }

  if (!changed)
    markAllAnalysesPreserved();
}

} // namespace

std::unique_ptr<Pass> mlir::createOffloadEliminateCoveredGuardsPass() {
  return std::make_unique<OffloadEliminateCoveredGuardsPass>();
}
