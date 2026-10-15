//===- KernelBindingTable.cpp - Host<->device kernel bindings -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelBindingTable.h"
#include "mlir/IR/Dominance.h"
#include "mlir/IR/SymbolTable.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/IR/CIRTypes.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/raw_ostream.h"

using namespace cir;

namespace {
// A launch argument is rarely a `cir.const` at the launch itself: the constant
// is typically stated a call frame or two above and arrives as a parameter, a
// cast, or the result of arithmetic on other constants. Chasing it costs a
// bounded walk per query, so cap the recursion.
constexpr int kMaxResolveDepth = 4;
} // namespace

static mlir::TypedAttr asConstAtDepth(mlir::Value v, int depth);

// The constant every caller of `arg`'s function passes in its position, or null
// if they disagree, any of them is not constant, or the function could be
// called from outside this module.
static mlir::TypedAttr resolveParameter(mlir::BlockArgument arg, int depth) {
  auto fn =
      mlir::dyn_cast<mlir::FunctionOpInterface>(arg.getOwner()->getParentOp());
  // Only a parameter of the function itself, not a block argument of some
  // nested control flow, is fixed by the call sites.
  if (!fn || arg.getOwner() != &fn->getRegion(0).front())
    return {};

  // Agreement among the call sites we can see says nothing unless they are all
  // the call sites there are. A function another translation unit can name, or
  // whose address is taken, can be entered with any arguments at all.
  if (mlir::SymbolTable::getSymbolVisibility(fn) !=
      mlir::SymbolTable::Visibility::Private)
    return {};

  auto mod = fn->getParentOfType<mlir::ModuleOp>();
  if (!mod)
    return {};
  std::optional<mlir::SymbolTable::UseRange> uses =
      mlir::SymbolTable::getSymbolUses(fn, mod);
  if (!uses)
    return {};

  unsigned argIdx = arg.getArgNumber();
  mlir::TypedAttr common;
  bool sawCall = false;
  for (const mlir::SymbolTable::SymbolUse &use : *uses) {
    auto call = mlir::dyn_cast<cir::CallOp>(use.getUser());
    // A reference that is not a direct call -- the address being taken, say --
    // leaves callers we cannot enumerate.
    if (!call || call.getCallee() != fn.getName())
      return {};
    mlir::OperandRange callArgs = call.getArgOperands();
    if (argIdx >= callArgs.size())
      return {};
    mlir::TypedAttr value = asConstAtDepth(callArgs[argIdx], depth + 1);
    if (!value)
      return {};
    if (!sawCall) {
      common = value;
      sawCall = true;
    } else if (common != value) {
      return {};
    }
  }
  // A private function with no calls is dead, and says nothing.
  return sawCall ? common : mlir::TypedAttr{};
}

// The value stored into `slot`, if exactly one store writes it and nothing can
// write it behind our back.
static mlir::Value uniqueStoredValue(mlir::Value slot) {
  auto alloca = slot.getDefiningOp<cir::AllocaOp>();
  if (!alloca)
    return {};
  mlir::Value stored;
  for (mlir::OpOperand &use : slot.getUses()) {
    mlir::Operation *user = use.getOwner();
    if (auto store = mlir::dyn_cast<cir::StoreOp>(user)) {
      if (store.getAddr() != slot)
        return {};
      if (stored)
        return {}; // more than one store: which one wins is not obvious here
      stored = store.getValue();
      continue;
    }
    if (mlir::isa<cir::LoadOp>(user))
      continue;
    // The address escapes, so the slot may be written through an alias --
    // unless it is const, where such a write would be undefined and the single
    // initialising store is the whole story.
    if (!alloca.getConstant())
      return {};
  }
  return stored;
}

// The constant `v` holds, or null if it is not a constant.
static mlir::TypedAttr asConstAtDepth(mlir::Value v, int depth) {
  if (!v || depth > kMaxResolveDepth)
    return {};

  if (auto cst = v.getDefiningOp<cir::ConstantOp>())
    return cst.getValue();

  mlir::Operation *def = v.getDefiningOp();
  if (!def) {
    auto arg = mlir::dyn_cast<mlir::BlockArgument>(v);
    return arg ? resolveParameter(arg, depth) : mlir::TypedAttr{};
  }

  // Ask the op to fold itself once every operand is known. A helper handed
  // ne02 == 1 and ne03 == 1 passes their product, so without folding the launch
  // still looks like a runtime value one step short of where it matters.
  // Delegating to the op's own folder keeps one definition of what each op
  // means; no canonicalisation runs over host CIR before these passes.
  if (def->getNumResults() == 1 && def->getNumOperands() > 0) {
    llvm::SmallVector<mlir::Attribute> operands;
    operands.reserve(def->getNumOperands());
    for (mlir::Value operand : def->getOperands()) {
      mlir::TypedAttr value = asConstAtDepth(operand, depth + 1);
      if (!value)
        break;
      operands.push_back(value);
    }
    if (operands.size() == def->getNumOperands()) {
      llvm::SmallVector<mlir::OpFoldResult> results;
      if (mlir::succeeded(def->fold(operands, results)) && results.size() == 1)
        if (auto folded =
                mlir::dyn_cast_if_present<mlir::Attribute>(results[0]))
          if (auto typed = mlir::dyn_cast<mlir::TypedAttr>(folded))
            if (typed.getType() == v.getType())
              return typed;

      // The integer arithmetic ops have no folder -- nothing canonicalises CIR
      // before these passes, so nothing has needed one -- which leaves the
      // delegation above failing for precisely the ne02*ne03 case it describes.
      // Evaluate them here rather than giving them folders, which would also
      // change what SCCP does to every CIR module.
      if (operands.size() == 2) {
        auto lhs = mlir::dyn_cast<cir::IntAttr>(operands[0]);
        auto rhs = mlir::dyn_cast<cir::IntAttr>(operands[1]);
        auto ty = mlir::dyn_cast<cir::IntType>(v.getType());
        if (lhs && rhs && ty && lhs.getType() == ty && rhs.getType() == ty) {
          llvm::APInt a = lhs.getValue(), b = rhs.getValue(), r;
          bool overflow = true;
          bool isSigned = ty.isSigned();
          if (mlir::isa<cir::AddOp>(def))
            r = isSigned ? a.sadd_ov(b, overflow) : a.uadd_ov(b, overflow);
          else if (mlir::isa<cir::SubOp>(def))
            r = isSigned ? a.ssub_ov(b, overflow) : a.usub_ov(b, overflow);
          else if (mlir::isa<cir::MulOp>(def))
            r = isSigned ? a.smul_ov(b, overflow) : a.umul_ov(b, overflow);
          else if (mlir::isa<cir::DivOp>(def) && !b.isZero())
            r = isSigned ? a.sdiv_ov(b, overflow)
                         : (overflow = false, a.udiv(b));
          if (!overflow)
            return cir::IntAttr::get(ty, r);
        }
      }
    }
  }

  // A value loaded straight back out of the slot it was stored into.
  if (auto load = mlir::dyn_cast<cir::LoadOp>(def))
    if (mlir::Value stored = uniqueStoredValue(load.getAddr()))
      return asConstAtDepth(stored, depth + 1);

  return {};
}

static mlir::TypedAttr asConst(mlir::Value v) { return asConstAtDepth(v, 0); }

// Sema picks the launch configuration function from the language mode, in
// SemaCUDA::getConfigureFuncName. Of the five it can pick, CIRGen emits a stub
// body only for the two below: `-foffload-via-llvm` reports NYI when the CUDA
// runtime is constructed, and the legacy `cudaConfigureCall` /
// `hipConfigureCall` reach errorNYI("Emit Stub Body Legacy").
//
// The name and supported operand count identify the call. Before ABI lowering
// it has four logical operands; x86_64 SysV lowering flattens both dim3 values
// into two fields, producing six operands.
static bool isPushCallConfiguration(cir::CallOp call) {
  std::optional<llvm::StringRef> callee = call.getCallee();
  if (!callee)
    return false;
  assert(*callee != "__llvmPushCallConfiguration" &&
         *callee != "cudaConfigureCall" && *callee != "hipConfigureCall" &&
         "NYI: launch configuration form CIRGen does not emit");
  if (*callee != "__cudaPushCallConfiguration" &&
      *callee != "__hipPushCallConfiguration")
    return false;
  unsigned n = call.getArgOperands().size();
  return n == 4 || n == 6;
}

// CIRGen guards a launch with the result of the push-call-configuration call:
//
//   %c = cir.call @__cudaPushCallConfiguration(...)
//   %b = cir.cast int_to_bool %c
//   cir.if %b { } else { cir.call @stub(...) }
//
// Walk that back from the stub call. Returns null for any other shape,
// including a launch whose CFG has already been flattened.
//
// Every enclosing conditional is tried rather than just the innermost: a pass
// may have wrapped the call in a dispatch of its own -- EliminateCoveredGuards
// does exactly that -- and the geometry is still the one this launch uses.
static cir::CallOp tracePushConfiguration(cir::CallOp stubCall) {
  for (auto ifOp = stubCall->getParentOfType<cir::IfOp>(); ifOp;
       ifOp = ifOp->getParentOfType<cir::IfOp>()) {
    auto cast = ifOp.getCondition().getDefiningOp<cir::CastOp>();
    if (!cast)
      continue;
    auto call = cast.getSrc().getDefiningOp<cir::CallOp>();
    if (call && isPushCallConfiguration(call))
      return call;
  }
  return {};
}

// Whether `call` runs a constructor of `recordType`. CIR marks a constructor
// with a cxx_ctor attribute naming the record it builds, so the call is
// identified by that rather than by a mangled name: dim3 has more than one
// constructor symbol, and a name check would also accept an unrelated function
// with a matching signature.
static bool constructsRecord(cir::CallOp call, mlir::Type recordType) {
  mlir::FlatSymbolRefAttr callee = call.getCalleeAttr();
  if (!callee)
    return false;
  auto fn =
      mlir::SymbolTable::lookupNearestSymbolFrom<cir::FuncOp>(call, callee);
  if (!fn)
    return false;
  auto ctor = mlir::dyn_cast_if_present<cir::CXXCtorAttr>(fn.getFuncInfoAttr());
  return ctor && ctor.getType() == recordType;
}

static cir::LaunchSite::Dim3
traceDim3(mlir::Value dim, mlir::DominanceInfo &dom, unsigned depth = 0);

// Recover the dim3 a local slot holds when `use` reads it, from the one write
// that initializes it:
//
//   cir.call @_ZN4dim3C1Ejjj(%t, %x, %y, %z)     a constructor
//   cir.store %x, (cir.get_member %t[0]) ...     one store per field, the shape
//                                                an inlined constructor leaves
//   cir.store %d, %t                             a whole dim3
//   cir.copy %named to %t                        a copy of another dim3 slot,
//                                                `dim3 b(...); k<<<g, b>>>()`
//
// The write may sit in an enclosing scope, as long as it dominates `use`. The
// slot has to be a local that nothing else writes and whose address goes
// nowhere else; a second write, or an escape, leaves the value undecided, so
// report no geometry rather than a guess. A `const` local is the exception to
// the escape rule: modifying a const object after its construction is
// undefined, so handing its address to `f(const dim3 &)` cannot change it.
static cir::LaunchSite::Dim3 traceDim3Slot(mlir::Value slot,
                                           mlir::Operation *use,
                                           mlir::DominanceInfo &dom,
                                           unsigned depth) {
  auto alloca = slot.getDefiningOp<cir::AllocaOp>();
  if (depth > 4 || !alloca)
    return {};
  const bool isConst = alloca.getConstant();
  auto slotType = mlir::dyn_cast<cir::PointerType>(slot.getType());
  auto record =
      slotType ? mlir::dyn_cast<cir::RecordType>(slotType.getPointee())
               : cir::RecordType{};
  if (!record || !record.getName() || record.getName().getValue() != "dim3")
    return {};

  cir::CallOp ctor;
  cir::CopyOp copy;
  cir::StoreOp whole;
  cir::StoreOp fields[3];
  unsigned writes = 0;
  for (mlir::Operation *user : slot.getUsers()) {
    if (auto load = mlir::dyn_cast<cir::LoadOp>(user)) {
      if (load.getAddr() != slot)
        return {};
      continue;
    }
    if (mlir::isa<cir::LifetimeStartOp, cir::LifetimeEndOp>(user))
      continue;
    if (auto c = mlir::dyn_cast<cir::CopyOp>(user)) {
      if (c.getSrc() == slot && c.getDst() != slot)
        continue;
      if (c.getDst() != slot || copy)
        return {};
      copy = c;
      ++writes;
      continue;
    }
    if (auto store = mlir::dyn_cast<cir::StoreOp>(user)) {
      if (store.getAddr() != slot || store.getValue() == slot || whole)
        return {};
      whole = store;
      ++writes;
      continue;
    }
    if (auto call = mlir::dyn_cast<cir::CallOp>(user)) {
      mlir::OperandRange args = call.getArgOperands();
      bool constructs = constructsRecord(call, record) && args.size() == 4 &&
                        args[0] == slot &&
                        !llvm::is_contained(args.drop_front(), slot);
      if (!constructs) {
        if (isConst)
          continue;
        return {};
      }
      if (ctor)
        return {};
      ctor = call;
      ++writes;
      continue;
    }
    auto member = mlir::dyn_cast<cir::GetMemberOp>(user);
    if (!member && isConst)
      continue;
    if (!member || member.getAddr() != slot || member.getIndex() > 2)
      return {};
    for (mlir::Operation *memberUser : member.getResult().getUsers()) {
      if (mlir::isa<cir::LoadOp>(memberUser))
        continue;
      auto store = mlir::dyn_cast<cir::StoreOp>(memberUser);
      if (!store || store.getAddr() != member.getResult() ||
          fields[member.getIndex()])
        return {};
      fields[member.getIndex()] = store;
    }
  }
  bool byFields = fields[0] || fields[1] || fields[2];
  if (writes + byFields != 1)
    return {};

  if (ctor) {
    if (!dom.properlyDominates(ctor, use))
      return {};
    mlir::OperandRange args = ctor.getArgOperands();
    return {args[1], args[2], args[3]};
  }
  if (copy)
    return dom.properlyDominates(copy, use)
               ? traceDim3Slot(copy.getSrc(), copy, dom, depth + 1)
               : cir::LaunchSite::Dim3{};
  if (whole)
    return dom.properlyDominates(whole, use)
               ? traceDim3(whole.getValue(), dom, depth + 1)
               : cir::LaunchSite::Dim3{};
  for (cir::StoreOp store : fields)
    if (!store || !dom.properlyDominates(store, use))
      return {};
  return {fields[0].getValue(), fields[1].getValue(), fields[2].getValue()};
}

// Recover a dim3's components by walking its loaded value back to the stack
// slot it was read from, which ties the components to the exact dim3 value
// used by this launch.
static cir::LaunchSite::Dim3
traceDim3(mlir::Value dim, mlir::DominanceInfo &dom, unsigned depth) {
  if (!dim)
    return {};
  auto load = dim.getDefiningOp<cir::LoadOp>();
  if (!load)
    return {};
  return traceDim3Slot(load.getAddr(), load, dom, depth);
}

// Recover the record stored into the common coerce slot from which the two
// flattened fields were loaded. This is the inverse of the x86_64 SysV
// CallConvLowering pattern for dim3; it deliberately handles no other layout.
static mlir::Value recordFromCoercedFields(mlir::Value first,
                                           mlir::Value second) {
  auto firstLoad = first.getDefiningOp<cir::LoadOp>();
  auto secondLoad = second.getDefiningOp<cir::LoadOp>();
  auto firstField = firstLoad
                        ? firstLoad.getAddr().getDefiningOp<cir::GetMemberOp>()
                        : nullptr;
  auto secondField =
      secondLoad ? secondLoad.getAddr().getDefiningOp<cir::GetMemberOp>()
                 : nullptr;
  if (!firstField || !secondField || firstField.getIndex() != 0 ||
      secondField.getIndex() != 1 ||
      firstField.getAddr() != secondField.getAddr())
    return {};

  cir::StoreOp rec;
  for (mlir::Operation *user : firstField.getAddr().getUsers()) {
    auto cast = mlir::dyn_cast<cir::CastOp>(user);
    if (!cast || cast.getKind() != cir::CastKind::bitcast)
      continue;
    for (mlir::Operation *castUser : cast.getResult().getUsers()) {
      auto store = mlir::dyn_cast<cir::StoreOp>(castUser);
      // This detour only recovers dim3; any other record stored through the
      // same coerce shape is not launch geometry.
      auto record = store
                        ? mlir::dyn_cast<cir::RecordType>(
                              store.getValue().getType())
                        : cir::RecordType{};
      if (!store || store.getAddr() != cast.getResult() ||
          !record || record.getName().getValue() != "dim3")
        continue;
      // Users are unordered, so a second store leaves the one reaching the
      // load undecided; report no record instead.
      if (rec)
        return {};
      rec = store;
    }
  }
  if (!rec || rec->getBlock() != firstLoad->getBlock() ||
      rec->getBlock() != secondLoad->getBlock() ||
      !rec->isBeforeInBlock(firstLoad) || !rec->isBeforeInBlock(secondLoad))
    return {};
  return rec.getValue();
}

// Recover grid (0) or block (1) from the push call. Before ABI lowering the
// operand is the dim3 record load and traceDim3 can follow it directly. After
// x86_64 SysV lowering the record has been stored into a {i64, i32} coerce
// slot and the call receives loads of its two fields. Walk those fields back
// to their common slot, recover the stored record, then let traceDim3 continue
// back to the constructor and its x, y and z operands.
//
// This ABI-specific detour is temporary. Once the merge pipeline consumes CIR
// before CallConvLowering, launch geometry should be captured there and this
// six-operand path can be removed.
static cir::LaunchSite::Dim3 tracePushDim(cir::CallOp call,
                                          unsigned logicalDimIndex) {
  assert(logicalDimIndex < 2 &&
         "push configuration has only grid and block dims");
  mlir::OperandRange ops = call.getArgOperands();
  mlir::DominanceInfo dom(call->getParentOfType<cir::FuncOp>());
  if (ops.size() == 4)
    return traceDim3(ops[logicalDimIndex], dom);
  if (ops.size() != 6)
    return {};
  unsigned firstField = logicalDimIndex * 2;
  return traceDim3(
      recordFromCoercedFields(ops[firstField], ops[firstField + 1]), dom);
}

// Shared memory (0) and stream (1) are the two trailing operands in both
// supported call shapes and are never subject to dim3 recovery.
static mlir::Value pushConfigScalar(cir::CallOp call, unsigned scalarIndex) {
  assert(scalarIndex < 2 &&
         "push configuration has only shared-memory and stream scalars");
  mlir::OperandRange ops = call.getArgOperands();
  if (ops.size() != 4 && ops.size() != 6)
    return {};
  return ops[ops.size() - 2 + scalarIndex];
}

llvm::StringRef cir::LaunchSite::getKernelName() const {
  cir::CallOp call = stubCall;
  mlir::SymbolTableCollection symbols;
  return cir::getLaunchedKernel(call, symbols).getKernelName();
}

unsigned cir::LaunchSite::getNumArgs() const {
  return cir::CallOp(stubCall).getArgOperands().size();
}

mlir::Value cir::LaunchSite::getArg(unsigned i) const {
  mlir::OperandRange args = cir::CallOp(stubCall).getArgOperands();
  return i < args.size() ? args[i] : mlir::Value{};
}

mlir::TypedAttr cir::LaunchSite::getConstArg(unsigned i) const {
  return asConst(getArg(i));
}

mlir::TypedAttr cir::LaunchSite::Dim3::constX() const { return asConst(x); }
mlir::TypedAttr cir::LaunchSite::Dim3::constY() const { return asConst(y); }
mlir::TypedAttr cir::LaunchSite::Dim3::constZ() const { return asConst(z); }

bool cir::LaunchSite::Dim3::isTraced() const { return x && y && z; }

bool cir::LaunchSite::Dim3::isFullyConstant() const {
  return constX() && constY() && constZ();
}

// Logical push-config operands: grid, block, shared memory, stream.
cir::LaunchSite::Dim3 cir::LaunchSite::getGridDim() const {
  if (!hasGeometry())
    return {};
  return tracePushDim(cir::CallOp(pushConfigCall), 0);
}

cir::LaunchSite::Dim3 cir::LaunchSite::getBlockDim() const {
  if (!hasGeometry())
    return {};
  return tracePushDim(cir::CallOp(pushConfigCall), 1);
}

mlir::Value cir::LaunchSite::getSharedMemBytes() const {
  if (!hasGeometry())
    return {};
  return pushConfigScalar(cir::CallOp(pushConfigCall), 0);
}

mlir::TypedAttr cir::LaunchSite::getConstSharedMem() const {
  return asConst(getSharedMemBytes());
}

mlir::Value cir::LaunchSite::getStream() const {
  if (!hasGeometry())
    return {};
  return pushConfigScalar(cir::CallOp(pushConfigCall), 1);
}

bool cir::LaunchSite::isDefaultStream() const {
  auto ptr = mlir::dyn_cast_if_present<cir::ConstPtrAttr>(asConst(getStream()));
  return ptr && ptr.isNullValue();
}

cir::CUDAKernelNameAttr
cir::getLaunchedKernel(mlir::Operation *op,
                       mlir::SymbolTableCollection &symbols) {
  auto call = mlir::dyn_cast<cir::CallOp>(op);
  if (!call)
    return {};
  if (auto tag = call->getAttrOfType<cir::CUDAKernelNameAttr>(
          cir::CUDAKernelNameAttr::getMnemonic()))
    return tag;
  cir::FuncOp callee = call.resolveCalleeInTable(symbols);
  if (!callee)
    return {};
  return callee->getAttrOfType<cir::CUDAKernelNameAttr>(
      cir::CUDAKernelNameAttr::getMnemonic());
}

bool cir::isCallUnderLaunchConfiguration(cir::CallOp call) {
  return call.isIndirect() && tracePushConfiguration(call);
}

bool cir::hasUseOutsideSelf(cir::FuncOp fn, mlir::Operation *scope) {
  auto uses = mlir::SymbolTable::getSymbolUses(fn, scope);
  if (!uses)
    return true; // Uses could not be enumerated: conservatively assume one.
  return llvm::any_of(*uses, [&](const mlir::SymbolTable::SymbolUse &use) {
    return !fn->isProperAncestor(use.getUser());
  });
}

// The launch sites recorded for a stub are all of them when no other TU can
// call it and every reference to it is one the table recorded. A reference that
// is not a launch site of this kernel -- an escaping address, or a call the
// table did not collect -- may reach the kernel in a way this table cannot
// describe. Uses inside the stub's own body are its self-reference.
bool KernelBindingTable::allLaunchSitesVisible(
    llvm::StringRef kernelName) const {
  const KernelBinding *binding = lookup(kernelName);
  if (!binding)
    return false;
  cir::FuncOp stub = binding->hostStub;
  if (!cir::isLocalLinkage(stub.getLinkage()))
    return false;
  auto uses = mlir::SymbolTable::getSymbolUses(stub, hostModule);
  if (!uses)
    return false;
  mlir::SymbolTableCollection symbols;
  return llvm::all_of(*uses, [&](const mlir::SymbolTable::SymbolUse &use) {
    if (stub->isProperAncestor(use.getUser()))
      return true;
    cir::CUDAKernelNameAttr launched =
        cir::getLaunchedKernel(use.getUser(), symbols);
    return launched && launched.getKernelName() == kernelName;
  });
}

KernelBindingTable::KernelBindingTable(mlir::ModuleOp container) {
  // `container` must be an offload container; the helpers assert the unit attr
  // and recover the nested host and device modules.
  mlir::ModuleOp hostModule = cir::getOffloadContainerHostModule(container);
  this->hostModule = hostModule;

  hostModule.walk([&](cir::FuncOp hostFn) {
    auto kernelNameAttr = hostFn->getAttrOfType<cir::CUDAKernelNameAttr>(
        cir::CUDAKernelNameAttr::getMnemonic());
    if (kernelNameAttr) {
      assert(!lookup(kernelNameAttr.getKernelName()) &&
             "Duplicate stub found for Host TU");
      bindings[kernelNameAttr.getKernelName()].hostStub = hostFn;
    }
  });

  // Walked after the stubs, so every binding already has its host stub. A
  // launch site naming a kernel with no stub here launches nothing this table
  // describes, and recording it would leave a binding without a stub.
  mlir::SymbolTableCollection symbols;
  hostModule.walk([&](mlir::Operation *op) {
    cir::CUDAKernelNameAttr kernel = cir::getLaunchedKernel(op, symbols);
    if (!kernel)
      return;
    auto it = bindings.find(kernel.getKernelName());
    if (it == bindings.end())
      return;
    cir::CallOp stubCall = mlir::cast<cir::CallOp>(op);
    it->second.launchSites.push_back(
        LaunchSite{stubCall, tracePushConfiguration(stubCall)});
  });

  for (mlir::ModuleOp deviceMod : cir::getOffloadContainerDeviceModules(container)) {
    for (auto &binding : bindings) {
      if (cir::FuncOp kernel = llvm::dyn_cast_if_present<cir::FuncOp>(
              deviceMod.lookupSymbol(binding.first))) {
        binding.second.deviceKernels.push_back(kernel);
      }
    }
  }
}

const KernelBinding *KernelBindingTable::lookup(llvm::StringRef kernelName) const {
  auto it = bindings.find(kernelName);
  return it == bindings.end() ? nullptr : &it->second;
}

llvm::ArrayRef<cir::FuncOp>
KernelBindingTable::getDeviceFuncs(cir::FuncOp hostFn) const {
  auto kernelNameAttr = hostFn->getAttrOfType<cir::CUDAKernelNameAttr>(
      cir::CUDAKernelNameAttr::getMnemonic());
  if (!kernelNameAttr)
    return {};
  const KernelBinding *binding = lookup(kernelNameAttr.getKernelName());
  return binding ? binding->deviceKernels : llvm::ArrayRef<cir::FuncOp>{};
}

llvm::ArrayRef<cir::LaunchSite>
KernelBindingTable::getLaunchSites(llvm::StringRef kernelName) const {
  const KernelBinding *binding = lookup(kernelName);
  return binding ? binding->launchSites : llvm::ArrayRef<cir::LaunchSite>{};
}

void KernelBindingTable::print(llvm::raw_ostream &os) const {
  os << "// ---- KernelBindingTable ----\n";
  os << "// size " << size() << " empty " << (empty() ? "true" : "false")
     << "\n";
  for (const auto &binding : bindings) {
    cir::FuncOp stub = binding.second.hostStub;
    os << "// - kernel " << binding.first << "\n";
    os << "//   host-stub @" << stub.getName() << "\n";
    if (binding.second.deviceKernels.empty())
      os << "//   <no device kernel>\n";
    for (cir::FuncOp kernel : binding.second.deviceKernels) {
      auto mod = kernel->getParentOfType<mlir::ModuleOp>();
      os << "//   device @" << mod.getSymName().value_or("<unnamed>") << " : @"
         << kernel.getName() << "\n";
    }
    if (binding.second.launchSites.empty())
      os << "//   <no launch site>\n";
    for (const cir::LaunchSite &launch : binding.second.launchSites) {
      cir::CallOp stubCall = launch.stubCall;
      auto caller = stubCall->getParentOfType<cir::FuncOp>();
      os << "//   launch in @" << caller.getName() << " : "
         << launch.getNumArgs() << " args";
      if (!launch.hasGeometry()) {
        os << ", <no geometry>\n";
        continue;
      }
      // "untraced" means the dimension was not recovered; "dynamic" means it
      // was, and holds a runtime value.
      auto label = [](const cir::LaunchSite::Dim3 &dim) -> llvm::StringRef {
        if (!dim.isTraced())
          return "untraced";
        return dim.isFullyConstant() ? "const" : "dynamic";
      };
      os << ", grid " << label(launch.getGridDim()) << ", block "
         << label(launch.getBlockDim()) << ", stream "
         << (launch.isDefaultStream() ? "default" : "explicit") << "\n";
    }
    os << "//\n";
  }
  os << "// ----------------------------\n";
}