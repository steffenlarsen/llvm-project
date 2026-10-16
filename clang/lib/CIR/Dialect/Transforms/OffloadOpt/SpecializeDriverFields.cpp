//===- SpecializeDriverFields.cpp - Unswitch kernels on driver flags ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A programming model such as Kokkos launches a kernel with a driver object --
// the user's functor and the iteration policy -- passed whole. Its fields
// include flags that never change during a run but that the kernel tests in
// its hot loops, such as LAMMPS' neighbor build testing whether exclusions or
// molecular topology are in use. The device compiler cannot know their values,
// and LLVM does not unswitch loops on them: non-trivial loop unswitching is
// disabled on targets with divergent branches.
//
// The host knows the values at each launch. This pass finds the flag fields a
// kernel tests in its loops, clones the kernel once per combination of the
// tests' outcomes with each test folded to its outcome, and has every launch it
// can see read the fields and pick the clone that matches them. The original
// kernel serves every other launch, and any combination no clone matches.
//
// The driver reaches the kernel in one of two ways:
//
//   * As a by-value kernel argument, which the kernel spills to a local.
//   * Through a `__constant__` buffer: the host copies the driver into it with
//     a copy-to-symbol call and launches the kernel with no arguments, and the
//     kernel reads the buffer as the driver's type. This is how Kokkos launches
//     a functor of 512 bytes or more. The pass relies on the order that
//     protocol has to keep for the kernel to see its driver at all: the buffer
//     holds the copied bytes when the launch runs.
//
// A field is identified by its byte offset in the driver: the device's record
// may differ from the host's in name and members, but the bytes are what is
// copied. A test is a `cir.cast int_to_bool` of a field load, a `cir.cmp` of
// one against a constant, or the load of a `bool` field itself. The driver is
// followed through members, base classes, casts, locals holding its address,
// and into the functions the kernel passes it to, which are cloned along with
// the kernel when they hold a test.
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/Builders.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/DataLayoutInterfaces.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelBindingTable.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelCloning.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/LocalSlots.h"
#include "clang/CIR/Interfaces/CIRLoopOpInterface.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"

#include <map>
#include <set>
#include <vector>

#define DEBUG_TYPE "cir-offload-specialize-driver-fields"

namespace mlir {
#define GEN_PASS_DECL_OFFLOADSPECIALIZEDRIVERFIELDS
#define GEN_PASS_DEF_OFFLOADSPECIALIZEDRIVERFIELDS
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

using namespace mlir;

namespace {

// Suffix for the specialised copies, numbered per combination of outcomes.
static constexpr llvm::StringRef kFlagsSuffix = ".flags";

// Most copies of one kernel, one per combination of outcomes.
static constexpr unsigned kMaxClones = 8;

//===----------------------------------------------------------------------===//
// Where the driver comes from
//===----------------------------------------------------------------------===//

// A function that copies bytes into a device symbol, and the position of the
// symbol operand. The source, the size and the offset follow it.
struct CopyToSymbolFn {
  llvm::StringRef name;
  unsigned symbolArg;
  // Match the name as a substring of the mangled callee, for a member function.
  bool isSubstring;
};

static const CopyToSymbolFn kCopyToSymbolFns[] = {
    {"hipMemcpyToSymbol", 0, false},
    {"hipMemcpyToSymbolAsync", 0, false},
    {"cudaMemcpyToSymbol", 0, false},
    {"cudaMemcpyToSymbolAsync", 0, false},
    // Kokkos' HIPInternal::hip_memcpy_to_symbol_async_wrapper, which takes
    // `this` first.
    {"hip_memcpy_to_symbol_async_wrapper", 1, true},
};

static std::optional<unsigned> copyToSymbolArg(cir::CallOp call) {
  std::optional<llvm::StringRef> callee = call.getCallee();
  if (!callee)
    return std::nullopt;
  for (const CopyToSymbolFn &fn : kCopyToSymbolFns)
    if (fn.isSubstring ? callee->contains(fn.name) : *callee == fn.name)
      return fn.symbolArg;
  return std::nullopt;
}

static Value stripCasts(Value v) {
  while (auto cast = v.getDefiningOp<cir::CastOp>())
    v = cast.getSrc();
  return v;
}

// Whether `a` and `b` are the same pointer: modulo casts, or loaded from the
// same local assigned once.
static bool samePointer(Value a, Value b) {
  a = stripCasts(a);
  b = stripCasts(b);
  if (a == b)
    return true;
  auto loadA = a.getDefiningOp<cir::LoadOp>();
  auto loadB = b.getDefiningOp<cir::LoadOp>();
  if (!loadA || !loadB)
    return false;
  cir::AllocaOp slot = cir::findSlot(loadA.getAddr());
  return slot && slot == cir::findSlot(loadB.getAddr()) && cir::onlyStore(slot);
}

// How a kernel receives its driver.
struct Source {
  enum Kind { Argument, ConstantBuffer } kind;
  // Argument: the argument and the local it is spilled into.
  unsigned argIndex = 0;
  cir::AllocaOp slot;
  cir::StoreOp spill;
  // ConstantBuffer: the device symbol the driver is copied into.
  llvm::StringRef buffer;
  // The device's type of the driver.
  cir::RecordType driverType;
};

// The `__constant__` global `name`, if `module` defines one.
static cir::GlobalOp constantGlobal(mlir::ModuleOp module,
                                    llvm::StringRef name) {
  auto global = module.lookupSymbol<cir::GlobalOp>(name);
  return global && global.getConstant() ? global : cir::GlobalOp();
}

// The driver source of `kernel`: a `__constant__` buffer it reads as a record
// from its start, or else its one record argument spilled to a local.
static std::optional<Source> findSource(cir::FuncOp kernel) {
  auto module = kernel->getParentOfType<mlir::ModuleOp>();
  std::optional<Source> buffer;
  bool ambiguous = false;
  auto note = [&](llvm::StringRef name, cir::RecordType record) {
    if (!record || !constantGlobal(module, name))
      return;
    if (buffer && (buffer->buffer != name || buffer->driverType != record)) {
      ambiguous = true;
      return;
    }
    buffer = Source{Source::ConstantBuffer, 0, {}, {}, name, record};
  };
  auto recordPointee = [](Type ty) -> cir::RecordType {
    auto ptrTy = mlir::dyn_cast<cir::PointerType>(ty);
    return ptrTy ? mlir::dyn_cast<cir::RecordType>(ptrTy.getPointee())
                 : nullptr;
  };
  kernel.walk([&](Operation *op) {
    // The buffer's address as a constant of the driver's pointer type, as a
    // reference bound to it is initialised.
    if (auto cst = dyn_cast<cir::ConstantOp>(op)) {
      auto view = mlir::dyn_cast<cir::GlobalViewAttr>(cst.getValue());
      if (view && (!view.getIndices() || view.getIndices().empty()))
        note(view.getSymbol().getValue(), recordPointee(cst.getType()));
      return;
    }
    // Or the buffer's address cast to the driver's pointer type.
    if (auto get = dyn_cast<cir::GetGlobalOp>(op)) {
      SmallVector<Value> worklist{get.getAddr()};
      while (!worklist.empty()) {
        Value v = worklist.pop_back_val();
        for (Operation *user : v.getUsers())
          if (auto cast = dyn_cast<cir::CastOp>(user)) {
            if (cir::RecordType record = recordPointee(cast.getType()))
              note(get.getName(), record);
            else
              worklist.push_back(cast.getResult());
          }
      }
    }
  });
  if (buffer && !ambiguous)
    return buffer;
  if (ambiguous)
    return std::nullopt;

  std::optional<Source> argument;
  Block &entry = kernel.getBody().front();
  for (BlockArgument arg : entry.getArguments()) {
    auto record = mlir::dyn_cast<cir::RecordType>(arg.getType());
    if (!record)
      continue;
    cir::StoreOp spill;
    for (Operation *user : arg.getUsers()) {
      auto store = dyn_cast<cir::StoreOp>(user);
      if (!store || store.getValue() != arg || spill)
        return std::nullopt;
      spill = store;
    }
    cir::AllocaOp slot = spill ? cir::findSlot(spill.getAddr()) : nullptr;
    if (!slot || argument)
      return std::nullopt;
    argument =
        Source{Source::Argument, arg.getArgNumber(), slot, spill, {}, record};
  }
  return argument;
}

//===----------------------------------------------------------------------===//
// Following the driver through the kernel and the functions it calls
//===----------------------------------------------------------------------===//

// A field of the driver: its byte offset and the type it is read as. A `bool`
// field is read as `!cir.bool`, any other flag as an integer.
using FieldKey = std::pair<int64_t, Type>;

// A comparison a test makes: `field <kind> rhs`. `int_to_bool` and a `bool`
// field read are `field != 0`.
struct TestClass {
  cir::CmpOpKind kind;
  int64_t rhs;
  bool operator<(const TestClass &o) const {
    return std::tie(kind, rhs) < std::tie(o.kind, o.rhs);
  }
  bool operator==(const TestClass &o) const {
    return kind == o.kind && rhs == o.rhs;
  }
};

// One test of a field, and the operation whose boolean result it is.
struct FieldTest {
  Operation *op;
  FieldKey field;
  TestClass cls;
  bool inLoop;
};

// Whether `op` runs inside a loop of its own function.
static bool isInLoop(Operation *op) {
  for (Operation *p = op->getParentOp(); p && !isa<cir::FuncOp>(p);
       p = p->getParentOp())
    if (isa<cir::LoopOpInterface>(p))
      return true;
  return false;
}

static cir::CmpOpKind swapped(cir::CmpOpKind kind) {
  switch (kind) {
  case cir::CmpOpKind::lt:
    return cir::CmpOpKind::gt;
  case cir::CmpOpKind::le:
    return cir::CmpOpKind::ge;
  case cir::CmpOpKind::gt:
    return cir::CmpOpKind::lt;
  case cir::CmpOpKind::ge:
    return cir::CmpOpKind::le;
  default:
    return kind;
  }
}

// The functions a kernel's driver reaches and the tests in them. A walk either
// only analyses, or -- given a `followCall` hook -- may redirect each call it
// follows before descending into it, which is how the copies are built.
struct DriverWalk {
  const Source &source;
  mlir::DataLayout dataLayout;
  unsigned maxDepth;
  // Decides where a call leads, given the callee it names: the function to
  // descend into, possibly after redirecting the call, or null to stop.
  std::function<cir::FuncOp(cir::CallOp, cir::FuncOp)> followCall;

  SmallVector<FieldTest> tests;
  // Fields whose value is used other than by a test.
  llvm::DenseSet<FieldKey> otherUses;
  // A by-value driver may be written, so nothing about its fields holds.
  bool mayWriteDriver = false;
  // The functions holding a test, and the call edges walked: caller to callee.
  llvm::DenseSet<Operation *> funcsWithTests;
  SmallVector<std::pair<cir::FuncOp, cir::FuncOp>> edges;

  DriverWalk(const Source &source, mlir::ModuleOp deviceModule,
             unsigned maxDepth)
      : source(source), dataLayout(deviceModule), maxDepth(maxDepth) {}

  void run(cir::FuncOp kernel) {
    llvm::DenseMap<Value, int64_t> seeds;
    if (source.kind == Source::Argument)
      seeds[cir::AllocaOp(source.slot).getResult()] = 0;
    walk(kernel, seeds, 0);
  }

private:
  bool isBase(Operation *op) {
    if (source.kind != Source::ConstantBuffer)
      return false;
    if (auto cst = dyn_cast<cir::ConstantOp>(op)) {
      auto view = mlir::dyn_cast<cir::GlobalViewAttr>(cst.getValue());
      return view && view.getSymbol().getValue() == source.buffer &&
             (!view.getIndices() || view.getIndices().empty());
    }
    if (auto get = dyn_cast<cir::GetGlobalOp>(op))
      return get.getName() == source.buffer;
    return false;
  }

  void recordTests(cir::LoadOp load, int64_t offset) {
    Type ty = load.getType();
    FieldKey key{offset, ty};
    bool inLoop = isInLoop(load);
    if (mlir::isa<cir::BoolType>(ty)) {
      tests.push_back({load, key, {cir::CmpOpKind::ne, 0}, inLoop});
      funcsWithTests.insert(
          load->getParentOfType<cir::FuncOp>().getOperation());
      return;
    }
    for (Operation *user : load->getUsers()) {
      std::optional<TestClass> cls;
      if (auto cast = dyn_cast<cir::CastOp>(user);
          cast && cast.getKind() == cir::CastKind::int_to_bool) {
        cls = TestClass{cir::CmpOpKind::ne, 0};
      } else if (auto cmp = dyn_cast<cir::CmpOp>(user)) {
        bool loadOnLeft = cmp.getLhs() == load.getResult();
        Value other = loadOnLeft ? cmp.getRhs() : cmp.getLhs();
        if (other != load.getResult())
          if (std::optional<int64_t> c = cir::tryConstThroughSlot(other))
            cls = TestClass{loadOnLeft ? cmp.getKind() : swapped(cmp.getKind()),
                            *c};
      }
      if (!cls) {
        otherUses.insert(key);
        continue;
      }
      tests.push_back({user, key, *cls, isInLoop(user)});
      funcsWithTests.insert(
          user->getParentOfType<cir::FuncOp>().getOperation());
    }
  }

  // Whether `user` keeps the driver unwritten when it uses `v`, a pointer into
  // it. Uses that derive another pointer are judged where that one is used.
  bool readsOnly(Operation *user, Value v,
                 const llvm::DenseMap<Value, int64_t> &tracked,
                 const llvm::DenseSet<Value> &derived) {
    if (isa<cir::GetMemberOp, cir::BaseClassAddrOp, cir::CastOp,
            cir::PtrStrideOp, cir::LifetimeStartOp, cir::LifetimeEndOp>(user))
      return true;
    if (auto load = dyn_cast<cir::LoadOp>(user))
      return load.getAddr() == v && !load.getIsVolatile();
    if (auto copy = dyn_cast<cir::CopyOp>(user))
      return copy.getSrc() == v && copy.getDst() != v;
    if (auto store = dyn_cast<cir::StoreOp>(user)) {
      if (store == source.spill)
        return true;
      // The address kept in a local, such as `this`: what is read back out of
      // that local is followed like the address itself.
      cir::AllocaOp holder = cir::findSlot(store.getAddr());
      return store.getValue() == v && holder && cir::onlyStore(holder) == store;
    }
    // A call the walk followed is judged inside the callee.
    if (auto call = dyn_cast<cir::CallOp>(user))
      return followedCalls.contains(call);
    return false;
  }

  llvm::DenseSet<Operation *> followedCalls;

  void walk(cir::FuncOp fn, llvm::DenseMap<Value, int64_t> tracked,
            unsigned depth) {
    // Pointers derived from the driver at an unknown offset, such as an
    // element of an array member: their reads say nothing about a field, but
    // their writes write the driver.
    llvm::DenseSet<Value> derived;
    fn.walk<WalkOrder::PreOrder>([&](Operation *op) {
      if (isBase(op)) {
        tracked[op->getResult(0)] = 0;
        return;
      }
      if (auto member = dyn_cast<cir::GetMemberOp>(op)) {
        auto it = tracked.find(member.getAddr());
        if (it == tracked.end()) {
          if (derived.contains(member.getAddr()))
            derived.insert(member.getResult());
          return;
        }
        auto record = mlir::dyn_cast<cir::RecordType>(
            mlir::cast<cir::PointerType>(member.getAddr().getType())
                .getPointee());
        if (!record || record.isIncomplete()) {
          derived.insert(member.getResult());
          return;
        }
        int64_t offset =
            record.isUnion()
                ? 0
                : record.getElementOffset(dataLayout, member.getIndex());
        tracked[member.getResult()] = it->second + offset;
        return;
      }
      if (auto base = dyn_cast<cir::BaseClassAddrOp>(op)) {
        auto it = tracked.find(base.getDerivedAddr());
        if (it != tracked.end())
          tracked[base.getResult()] =
              it->second + base.getOffset().getZExtValue();
        else if (derived.contains(base.getDerivedAddr()))
          derived.insert(base.getResult());
        return;
      }
      if (auto cast = dyn_cast<cir::CastOp>(op)) {
        auto it = tracked.find(cast.getSrc());
        bool keepsAddress = cast.getKind() == cir::CastKind::bitcast ||
                            cast.getKind() == cir::CastKind::address_space ||
                            cast.getKind() == cir::CastKind::array_to_ptrdecay;
        if (it != tracked.end() && keepsAddress)
          tracked[cast.getResult()] = it->second;
        else if ((it != tracked.end() || derived.contains(cast.getSrc())) &&
                 mlir::isa<cir::PointerType>(cast.getType()))
          derived.insert(cast.getResult());
        return;
      }
      if (auto stride = dyn_cast<cir::PtrStrideOp>(op)) {
        if (tracked.contains(stride.getBase()) ||
            derived.contains(stride.getBase()))
          derived.insert(stride.getResult());
        return;
      }
      if (auto load = dyn_cast<cir::LoadOp>(op)) {
        if (auto it = tracked.find(load.getAddr()); it != tracked.end()) {
          if (!load.getIsVolatile() &&
              mlir::isa<cir::IntType, cir::BoolType>(load.getType()))
            recordTests(load, it->second);
          return;
        }
        // A local holding a driver address.
        cir::AllocaOp slot = cir::findSlot(load.getAddr());
        cir::StoreOp only = slot ? cir::onlyStore(slot) : cir::StoreOp();
        if (!only)
          return;
        if (auto it = tracked.find(only.getValue()); it != tracked.end())
          tracked[load.getResult()] = it->second;
        else if (derived.contains(only.getValue()))
          derived.insert(load.getResult());
        return;
      }
      if (auto call = dyn_cast<cir::CallOp>(op)) {
        if (depth >= maxDepth)
          return;
        std::optional<llvm::StringRef> name = call.getCallee();
        auto module = fn->getParentOfType<mlir::ModuleOp>();
        auto callee =
            name ? module.lookupSymbol<cir::FuncOp>(*name) : cir::FuncOp();
        if (!callee || callee.isDeclaration())
          return;
        llvm::DenseMap<Value, int64_t> seeds;
        bool passesDerived = false;
        for (auto [i, arg] : llvm::enumerate(call.getArgOperands())) {
          if (auto it = tracked.find(arg); it != tracked.end())
            seeds[callee.getArgument(i)] = it->second;
          else if (derived.contains(arg))
            passesDerived = true;
        }
        if (seeds.empty() || passesDerived)
          return;
        cir::FuncOp target = followCall ? followCall(call, callee) : callee;
        if (!target)
          return;
        followedCalls.insert(call);
        edges.push_back({fn, callee});
        llvm::DenseMap<Value, int64_t> targetSeeds;
        for (auto [arg, offset] : seeds)
          targetSeeds[target.getArgument(
              mlir::cast<BlockArgument>(arg).getArgNumber())] = offset;
        walk(target, targetSeeds, depth + 1);
      }
    });

    // A by-value driver is the kernel's own copy, which the kernel may write.
    // A `__constant__` one cannot be.
    if (source.kind != Source::Argument)
      return;
    auto check = [&](Value v) {
      for (Operation *user : v.getUsers())
        if (!readsOnly(user, v, tracked, derived))
          mayWriteDriver = true;
    };
    for (auto [v, offset] : tracked)
      check(v);
    for (Value v : derived)
      check(v);
  }
};

//===----------------------------------------------------------------------===//
// Choosing the fields and their outcomes
//===----------------------------------------------------------------------===//

// A field the copies are specialised on: its tests' classes, and each distinct
// outcome of those tests -- one truth value per class -- that some value of
// the field produces.
struct Field {
  FieldKey key;
  SmallVector<TestClass> classes;
  std::vector<std::vector<bool>> outcomes;
  unsigned testsInLoops = 0;
};

static bool evaluate(cir::CmpOpKind kind, const llvm::APSInt &v,
                     const llvm::APSInt &rhs) {
  switch (kind) {
  case cir::CmpOpKind::lt:
    return v < rhs;
  case cir::CmpOpKind::le:
    return v <= rhs;
  case cir::CmpOpKind::gt:
    return v > rhs;
  case cir::CmpOpKind::ge:
    return v >= rhs;
  case cir::CmpOpKind::eq:
    return v == rhs;
  case cir::CmpOpKind::ne:
    return v != rhs;
  case cir::CmpOpKind::one:
  case cir::CmpOpKind::uno:
    break;
  }
  llvm_unreachable("an integer test has no unordered comparison");
}

// Every distinct outcome of `field`'s tests. The tests compare against
// constants, so their outcomes only change at those: each constant, its
// neighbours and the ends of the type's range cover every outcome.
static void computeOutcomes(Field &field) {
  unsigned width = 8;
  bool isUnsigned = true;
  if (auto intTy = mlir::dyn_cast<cir::IntType>(field.key.second)) {
    width = intTy.getWidth();
    isUnsigned = intTy.isUnsigned();
  }
  auto make = [&](int64_t c) {
    return llvm::APSInt(llvm::APInt(width, c, /*isSigned=*/!isUnsigned),
                        isUnsigned);
  };
  SmallVector<llvm::APSInt> points{
      llvm::APSInt::getMinValue(width, isUnsigned),
      llvm::APSInt::getMaxValue(width, isUnsigned)};
  if (mlir::isa<cir::BoolType>(field.key.second))
    points = {make(0), make(1)};
  for (const TestClass &cls : field.classes) {
    llvm::APSInt c = make(cls.rhs);
    points.push_back(c);
    if (c != llvm::APSInt::getMinValue(width, isUnsigned))
      points.push_back(c - make(1));
    if (c != llvm::APSInt::getMaxValue(width, isUnsigned))
      points.push_back(c + make(1));
  }
  std::set<std::vector<bool>> outcomes;
  for (const llvm::APSInt &p : points) {
    std::vector<bool> outcome;
    for (const TestClass &cls : field.classes)
      outcome.push_back(evaluate(cls.kind, p, make(cls.rhs)));
    outcomes.insert(outcome);
  }
  field.outcomes.assign(outcomes.begin(), outcomes.end());
}

// The fields to specialise on: those only ever tested, at least once in a
// loop, most tests in loops first, as many as the copies allow.
static SmallVector<Field> chooseFields(const DriverWalk &walk,
                                       unsigned maxFields) {
  llvm::MapVector<FieldKey, Field> byKey;
  for (const FieldTest &test : walk.tests) {
    if (walk.otherUses.contains(test.field))
      continue;
    Field &field = byKey[test.field];
    field.key = test.field;
    if (!llvm::is_contained(field.classes, test.cls))
      field.classes.push_back(test.cls);
    field.testsInLoops += test.inLoop;
  }
  SmallVector<Field> fields;
  for (auto &[key, field] : byKey) {
    if (!field.testsInLoops)
      continue;
    llvm::sort(field.classes);
    computeOutcomes(field);
    if (field.outcomes.size() > 1)
      fields.push_back(std::move(field));
  }
  llvm::stable_sort(fields, [](const Field &a, const Field &b) {
    return a.testsInLoops > b.testsInLoops;
  });
  if (fields.size() > maxFields)
    fields.resize(maxFields);
  auto clones = [&] {
    unsigned n = 1;
    for (const Field &field : fields)
      n *= field.outcomes.size();
    return n;
  };
  while (!fields.empty() && clones() > kMaxClones)
    fields.pop_back();
  return fields;
}

// Every combination of the fields' outcomes: one outcome index per field.
static std::vector<std::vector<unsigned>>
combinations(llvm::ArrayRef<Field> fields) {
  std::vector<std::vector<unsigned>> out{{}};
  for (const Field &field : fields) {
    std::vector<std::vector<unsigned>> next;
    for (const std::vector<unsigned> &prefix : out)
      for (unsigned i = 0, e = field.outcomes.size(); i != e; ++i) {
        next.push_back(prefix);
        next.back().push_back(i);
      }
    out = std::move(next);
  }
  return out;
}

// The truth value `combination` gives `test`, if it tests a chosen field.
static std::optional<bool> outcomeOf(const FieldTest &test,
                                     llvm::ArrayRef<Field> fields,
                                     llvm::ArrayRef<unsigned> combination) {
  for (auto [field, outcome] : llvm::zip(fields, combination)) {
    if (field.key != test.field)
      continue;
    auto it = llvm::find(field.classes, test.cls);
    if (it == field.classes.end())
      return std::nullopt;
    return field.outcomes[outcome][it - field.classes.begin()];
  }
  return std::nullopt;
}

//===----------------------------------------------------------------------===//
// The host side of a launch
//===----------------------------------------------------------------------===//

// Where a launch's driver bytes are on the host, and where reading them gives
// the values the launch passes.
struct HostDriver {
  Value bytes;
  Operation *readBefore;
};

// The driver bytes of a launch through `__constant__` buffer `buffer`: the
// source of the copy into the buffer the launch is the next use of, and the
// bytes copied into that source just before. Only side-effect-free operations
// may separate the two copies, so the source still holds what was copied.
static std::optional<HostDriver>
findBufferCopy(cir::CallOp launch, llvm::StringRef buffer, uint64_t size) {
  cir::CallOp copy;
  for (Operation *op = launch; op && !isa<cir::FuncOp>(op) && !copy;
       op = op->getParentOp()) {
    for (Operation *prev = op->getPrevNode(); prev;
         prev = prev->getPrevNode()) {
      auto call = dyn_cast<cir::CallOp>(prev);
      std::optional<unsigned> symbolArg =
          call ? copyToSymbolArg(call) : std::nullopt;
      if (!symbolArg)
        continue;
      if (call.getArgOperands().size() < *symbolArg + 4)
        return std::nullopt;
      auto symbol = stripCasts(call.getArgOperands()[*symbolArg])
                        .getDefiningOp<cir::GetGlobalOp>();
      if (!symbol)
        return std::nullopt;
      if (symbol.getName() != buffer)
        continue;
      copy = call;
      break;
    }
  }
  if (!copy)
    return std::nullopt;
  unsigned symbolArg = *copyToSymbolArg(copy);
  Value staging = copy.getArgOperands()[symbolArg + 1];
  std::optional<int64_t> copied =
      cir::tryConstThroughSlot(copy.getArgOperands()[symbolArg + 2]);
  std::optional<int64_t> offset =
      cir::tryConstThroughSlot(copy.getArgOperands()[symbolArg + 3]);
  if (!copied || *copied != static_cast<int64_t>(size) || !offset ||
      *offset != 0)
    return std::nullopt;

  // The source itself, when it is the driver, or a staging area filled from
  // the driver just before.
  for (Operation *prev = copy->getPrevNode(); prev;
       prev = prev->getPrevNode()) {
    if (auto memcpy = dyn_cast<cir::MemCpyOp>(prev)) {
      std::optional<int64_t> len = cir::tryConstThroughSlot(memcpy.getLen());
      if (!samePointer(memcpy.getDst(), staging) || !len ||
          *len != static_cast<int64_t>(size))
        return std::nullopt;
      return HostDriver{memcpy.getSrc(), memcpy->getNextNode()};
    }
    if (!isa<cir::LoadOp, cir::CastOp, cir::GetGlobalOp, cir::ConstantOp,
             cir::GetMemberOp>(prev))
      break;
  }
  return HostDriver{staging, copy};
}

// The driver bytes of `launch` from `source`, or nullopt when the launch does
// not pass them in a way the host can read back.
static std::optional<HostDriver> findHostDriver(cir::CallOp launch,
                                                const Source &source,
                                                const mlir::DataLayout &dl) {
  if (source.kind == Source::ConstantBuffer) {
    if (!launch.getArgOperands().empty())
      return std::nullopt;
    return findBufferCopy(launch, source.buffer,
                          dl.getTypeSize(source.driverType).getFixedValue());
  }
  if (source.argIndex >= launch.getArgOperands().size())
    return std::nullopt;
  Value arg = launch.getArgOperands()[source.argIndex];
  if (!mlir::isa<cir::RecordType>(arg.getType()))
    return std::nullopt;
  return HostDriver{arg, launch};
}

// A field read on the host: the bytes at `offset` of the driver, as `type`.
static Value readField(OpBuilder &builder, Location loc, Value bytes,
                       int64_t offset, Type type) {
  MLIRContext *ctx = builder.getContext();
  auto u8 = cir::IntType::get(ctx, 8, /*isSigned=*/false);
  auto s64 = cir::IntType::get(ctx, 64, /*isSigned=*/true);
  auto bytePtr = cir::PointerType::get(u8);
  Value base =
      cir::CastOp::create(builder, loc, bytePtr, cir::CastKind::bitcast, bytes);
  Value at = cir::PtrStrideOp::create(
      builder, loc, bytePtr, base,
      cir::ConstantOp::create(builder, loc, cir::IntAttr::get(s64, offset)));
  Value fieldPtr = cir::CastOp::create(
      builder, loc, cir::PointerType::get(type), cir::CastKind::bitcast, at);
  // The driver's bytes need not be aligned for the field on the host.
  return cir::LoadOp::create(builder, loc, type, fieldPtr,
                             /*isDeref=*/mlir::UnitAttr(),
                             /*isVolatile=*/mlir::UnitAttr(),
                             /*is_nontemporal=*/mlir::UnitAttr(),
                             builder.getI64IntegerAttr(1),
                             /*sync_scope=*/cir::SyncScopeKindAttr(),
                             /*mem_order=*/cir::MemOrderAttr(),
                             /*invariant=*/mlir::UnitAttr());
}

// The truth value of each test class of each field at a launch, read from the
// launch's driver bytes.
static SmallVector<SmallVector<Value>>
evaluateTests(OpBuilder &builder, Location loc, Value bytes,
              llvm::ArrayRef<Field> fields) {
  MLIRContext *ctx = builder.getContext();
  auto boolTy = cir::BoolType::get(ctx);
  SmallVector<SmallVector<Value>> truths;
  for (const Field &field : fields) {
    Value value =
        readField(builder, loc, bytes, field.key.first, field.key.second);
    SmallVector<Value> fieldTruths;
    for (const TestClass &cls : field.classes) {
      if (mlir::isa<cir::BoolType>(field.key.second)) {
        fieldTruths.push_back(value);
        continue;
      }
      Value rhs = cir::ConstantOp::create(
          builder, loc,
          cir::IntAttr::get(mlir::cast<cir::IntType>(field.key.second),
                            cls.rhs));
      fieldTruths.push_back(
          cir::CmpOp::create(builder, loc, boolTy, cls.kind, value, rhs));
    }
    truths.push_back(std::move(fieldTruths));
  }
  return truths;
}

// `a && b`, as the other offload passes conjoin their premises.
static Value conjoin(OpBuilder &builder, Location loc, Value a, Value b) {
  if (!a)
    return b;
  MLIRContext *ctx = builder.getContext();
  auto boolTy = cir::BoolType::get(ctx);
  return cir::TernaryOp::create(
             builder, loc, a,
             [&](OpBuilder &bb, Location l) { cir::YieldOp::create(bb, l, b); },
             [&](OpBuilder &bb, Location l) {
               auto f = cir::ConstantOp::create(
                   bb, l, cir::BoolAttr::get(ctx, boolTy, false));
               cir::YieldOp::create(bb, l, f.getResult());
             })
      .getResult();
}

// Replace `launch` with a choice among the copies: the first whose combination
// the launch's tests match, and the original when none does.
static void emitDispatch(cir::CallOp launch,
                         llvm::ArrayRef<SmallVector<Value>> truths,
                         llvm::ArrayRef<Field> fields,
                         llvm::ArrayRef<std::vector<unsigned>> combos,
                         llvm::ArrayRef<cir::KernelClone> copies,
                         mlir::FlatSymbolRefAttr originalStub,
                         cir::CUDAKernelNameAttr originalKernel) {
  MLIRContext *ctx = launch.getContext();
  Location loc = launch.getLoc();
  OpBuilder builder(launch);
  auto emptyBuilder = [](OpBuilder &, Location) {};
  auto callTo = [&](OpBuilder &b, mlir::FlatSymbolRefAttr stub,
                    cir::CUDAKernelNameAttr kernel) {
    auto call = cast<cir::CallOp>(b.clone(*launch.getOperation()));
    call.setCalleeAttr(stub);
    call->setAttr(cir::CUDAKernelNameAttr::getMnemonic(), kernel);
  };
  for (auto [combo, copy] : llvm::zip(combos, copies)) {
    Value matches;
    for (auto [f, field] : llvm::enumerate(fields))
      for (auto [c, truth] : llvm::enumerate(truths[f])) {
        Value want = field.outcomes[combo[f]][c]
                         ? truth
                         : cir::NotOp::create(builder, loc, truth).getResult();
        matches = conjoin(builder, loc, matches, want);
      }
    auto ifOp =
        cir::IfOp::create(builder, loc, matches,
                          /*withElseRegion=*/true, emptyBuilder, emptyBuilder);
    Block &thenBlock = ifOp.getThenRegion().front();
    builder.setInsertionPointToStart(&thenBlock);
    callTo(builder,
           mlir::FlatSymbolRefAttr::get(
               ctx, cir::FuncOp(copy.hostStub).getSymName()),
           cir::CUDAKernelNameAttr::get(
               ctx, mlir::StringAttr::get(ctx, copy.kernelName)));
    cir::YieldOp::create(builder, loc);
    Block &elseBlock = ifOp.getElseRegion().front();
    builder.setInsertionPointToStart(&elseBlock);
    cir::YieldOp::create(builder, loc);
    builder.setInsertionPointToStart(&elseBlock);
  }
  callTo(builder, originalStub, originalKernel);
  launch.erase();
}

//===----------------------------------------------------------------------===//
// The pass
//===----------------------------------------------------------------------===//

// Fold every test of a chosen field in `fn`'s copies to its outcome under
// `combination`.
static void foldTests(llvm::ArrayRef<FieldTest> tests,
                      llvm::ArrayRef<Field> fields,
                      llvm::ArrayRef<unsigned> combination) {
  for (const FieldTest &test : tests) {
    std::optional<bool> outcome = outcomeOf(test, fields, combination);
    if (!outcome)
      continue;
    OpBuilder builder(test.op);
    auto boolTy = cir::BoolType::get(builder.getContext());
    auto cst = cir::ConstantOp::create(
        builder, test.op->getLoc(),
        cir::BoolAttr::get(builder.getContext(), boolTy, *outcome));
    test.op->getResult(0).replaceAllUsesWith(cst.getResult());
    test.op->erase();
  }
}

struct OffloadSpecializeDriverFieldsPass
    : public impl::OffloadSpecializeDriverFieldsBase<
          OffloadSpecializeDriverFieldsPass> {
  using Base::Base;
  void runOnOperation() override;
};

void OffloadSpecializeDriverFieldsPass::runOnOperation() {
  mlir::ModuleOp container = getOperation();
  if (!cir::isOffloadContainer(container))
    return;

  cir::KernelBindingTable &table = getAnalysis<cir::KernelBindingTable>();
  mlir::ModuleOp hostModule = cir::getOffloadContainerHostModule(container);
  bool changed = false;

  for (const auto &entry : table) {
    llvm::StringRef kernelName = entry.first;
    const cir::KernelBinding &binding = entry.second;
    if (kernelName.contains(kFlagsSuffix) || !binding.hostStub ||
        binding.deviceKernels.size() != 1)
      continue;
    cir::FuncOp kernel = binding.deviceKernels.front();
    if (kernel.isDeclaration())
      continue;
    auto deviceModule = kernel->getParentOfType<mlir::ModuleOp>();

    std::optional<Source> source = findSource(kernel);
    if (!source)
      continue;
    // The host has to copy the buffer the device reads under the same name.
    if (source->kind == Source::ConstantBuffer &&
        !hostModule.lookupSymbol<cir::GlobalOp>(source->buffer))
      continue;

    DriverWalk walk(*source, deviceModule, maxCallDepth);
    walk.run(kernel);
    if (walk.mayWriteDriver)
      continue;
    SmallVector<Field> fields = chooseFields(walk, maxFields);
    if (fields.empty())
      continue;

    // Only a launch whose driver bytes the host can read takes part; the
    // others keep calling the original.
    mlir::DataLayout deviceLayout(deviceModule);
    SmallVector<cir::LaunchSite> sites;
    SmallVector<HostDriver> hostDrivers;
    for (const cir::LaunchSite &site : binding.launchSites)
      if (std::optional<HostDriver> host =
              findHostDriver(site.stubCall, *source, deviceLayout)) {
        sites.push_back(site);
        hostDrivers.push_back(*host);
      }
    if (sites.empty())
      continue;

    cir::FuncOp stub = binding.hostStub;
    auto originalStub =
        mlir::FlatSymbolRefAttr::get(container.getContext(), stub.getSymName());
    auto originalKernel = stub->getAttrOfType<cir::CUDAKernelNameAttr>(
        cir::CUDAKernelNameAttr::getMnemonic());
    if (!originalKernel)
      continue;

    // The functions on the way from the kernel to a test, which each copy
    // needs its own copy of.
    llvm::DenseSet<Operation *> onPath(walk.funcsWithTests.begin(),
                                       walk.funcsWithTests.end());
    for (bool grew = true; grew;) {
      grew = false;
      for (auto [caller, callee] : walk.edges)
        if (onPath.contains(callee.getOperation()) &&
            onPath.insert(caller.getOperation()).second)
          grew = true;
    }

    std::vector<std::vector<unsigned>> combos = combinations(fields);
    SmallVector<cir::KernelClone> copies;
    for (auto [n, combo] : llvm::enumerate(combos)) {
      std::optional<cir::KernelClone> copy = cir::cloneKernelForSites(
          container, binding, (kFlagsSuffix + llvm::Twine(n)).str(), sites);
      if (!copy)
        break;
      cir::FuncOp copyKernel = copy->deviceKernels.front();
      // The copy spills its argument to a local of its own.
      std::optional<Source> copySource = findSource(copyKernel);
      if (!copySource)
        break;

      // Walk the copy, giving it its own copy of every function on the path
      // to a test, and fold the tests it finds there.
      mlir::SymbolTable symbols(deviceModule);
      llvm::DenseMap<Operation *, cir::FuncOp> helperCopies;
      DriverWalk copyWalk(*copySource, deviceModule, maxCallDepth);
      copyWalk.followCall = [&](cir::CallOp call,
                                cir::FuncOp callee) -> cir::FuncOp {
        if (!onPath.contains(callee.getOperation()))
          return nullptr;
        cir::FuncOp &helper = helperCopies[callee.getOperation()];
        if (!helper) {
          helper = mlir::cast<cir::FuncOp>(callee->clone());
          mlir::SymbolTable::setSymbolName(
              helper,
              (callee.getSymName() + kFlagsSuffix + llvm::Twine(n)).str());
          // Only this copy of the kernel calls it. A local symbol has default
          // visibility, whatever the original's was.
          helper.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
          helper.setGlobalVisibility(cir::VisibilityKind::Default);
          mlir::SymbolTable::setSymbolVisibility(
              helper, mlir::SymbolTable::Visibility::Private);
          helper.removeComdatAttr();
          symbols.insert(helper, mlir::Block::iterator(callee->getNextNode()));
        }
        call.setCalleeAttr(mlir::FlatSymbolRefAttr::get(call.getContext(),
                                                        helper.getSymName()));
        return helper;
      };
      copyWalk.run(copyKernel);
      foldTests(copyWalk.tests, fields, combo);
      copies.push_back(*copy);
    }
    if (copies.size() != combos.size()) {
      // A copy could not be made: the launches already retargeted are pointed
      // back at the original below, and the copies left without a launch are
      // for DeadKernelElimination.
      combos.resize(copies.size());
    }
    if (copies.empty())
      continue;
    changed = true;

    for (auto [site, host] : llvm::zip(sites, hostDrivers)) {
      cir::CallOp launch = site.stubCall;
      OpBuilder builder(host.readBefore);
      Value bytes = host.bytes;
      if (!mlir::isa<cir::PointerType>(bytes.getType())) {
        // A by-value driver is read from a copy of its own.
        auto func = launch->getParentOfType<cir::FuncOp>();
        OpBuilder entryBuilder(&func.getBody().front(),
                               func.getBody().front().begin());
        mlir::DataLayout hostLayout(hostModule);
        auto slot = cir::AllocaOp::create(
            entryBuilder, launch.getLoc(),
            cir::PointerType::get(bytes.getType()),
            entryBuilder.getStringAttr("driver"),
            entryBuilder.getI64IntegerAttr(
                hostLayout.getTypeABIAlignment(bytes.getType())));
        cir::StoreOp::create(builder, launch.getLoc(), bytes, slot);
        bytes = slot;
      }
      SmallVector<SmallVector<Value>> truths =
          evaluateTests(builder, launch.getLoc(), bytes, fields);
      emitDispatch(launch, truths, fields, combos, copies, originalStub,
                   originalKernel);
    }
  }

  if (!changed)
    markAllAnalysesPreserved();
}

} // namespace

std::unique_ptr<Pass> mlir::createOffloadSpecializeDriverFieldsPass() {
  return std::make_unique<OffloadSpecializeDriverFieldsPass>();
}
