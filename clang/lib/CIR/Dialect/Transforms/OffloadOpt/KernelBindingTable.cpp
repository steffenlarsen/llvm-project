//===- KernelBindingTable.cpp - Host<->device kernel bindings -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelBindingTable.h"
#include "mlir/IR/SymbolTable.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
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
// The operand layout is part of the test because the geometry accessors index
// the operands directly; without it, only the name stands between a same-named
// declaration and a read past the end.
namespace {
// Operand positions in the push call, for whichever shape it is in. `grid` and
// `block` index the dim3 itself in the un-coerced shape and the low half of its
// coerced pair otherwise. See `unwrapCoercedDim3` for what coercion does.
struct PushCallLayout {
  unsigned grid, block, sharedMem, stream;
  bool coerced;
};
} // namespace

static std::optional<PushCallLayout> pushCallLayout(cir::CallOp push) {
  switch (push.getArgOperands().size()) {
  case 4:
    return PushCallLayout{0, 1, 2, 3, /*coerced=*/false};
  case 6:
    return PushCallLayout{0, 2, 4, 5, /*coerced=*/true};
  default:
    return std::nullopt;
  }
}

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
  return pushCallLayout(call).has_value();
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

// A dim3 argument of the push call is a load of a stack temporary that a
// constructor filled in:
//
//   %t = cir.alloca "agg.tmp0" : !cir.ptr<!rec_dim3>
//   cir.call @_ZN4dim3C1Ejjj(%t, %x, %y, %z)
//   %d = cir.load %t
//
// The constructor is found through the slot, so the components belong to the
// temporary this launch reads rather than to any dim3 in the function.
static cir::LaunchSite::Dim3 traceDim3(mlir::Value dim) {
  if (!dim)
    return {};
  auto load = dim.getDefiningOp<cir::LoadOp>();
  if (!load)
    return {};
  mlir::Value slot = load.getAddr();
  auto slotType = mlir::dyn_cast<cir::PointerType>(slot.getType());
  if (!slotType)
    return {};

  cir::CallOp ctor;
  for (mlir::Operation *user : slot.getUsers()) {
    auto call = mlir::dyn_cast<cir::CallOp>(user);
    if (!call || !constructsRecord(call, slotType.getPointee()))
      continue;
    // Users are unordered, so a second construction of the same slot leaves
    // the one reaching the load undecided; report no geometry instead.
    if (ctor)
      return {};
    ctor = call;
  }
  if (!ctor || ctor->getBlock() != load->getBlock() ||
      !ctor->isBeforeInBlock(load))
    return {};

  mlir::OperandRange args = ctor.getArgOperands();
  if (args.size() != 4 || args[0] != slot)
    return {};
  return {args[1], args[2], args[3]};
}

llvm::StringRef cir::LaunchSite::getKernelName() const {
  cir::CallOp call = stubCall;
  return cir::getLaunchedKernel(call).getKernelName();
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

// The push call reaches this pass in one of two shapes. Written as declared it
// takes the two dim3s by value, but the HIP target ABI coerces a dim3 into an
// {i64, i32} pair, and CIRGen emits the call already coerced:
//
//   %c = cir.alloca "coerce" : !cir.ptr<!rec_anon_struct>
//   %b = cir.cast bitcast %c : ... -> !cir.ptr<!rec_dim3>
//   cir.store %d, %b
//   %lo = cir.load (cir.get_member %c[0])
//   %hi = cir.load (cir.get_member %c[1])
//
// so each dim3 occupies two operands instead of one. Both shapes describe the
// same launch and every caller wants the same answer, so the difference is
// resolved here rather than in each pass.
//
// Recovering the dim3 from a coerced pair means going back through the scratch
// slot to the whole-record store, whose value is the dim3 the launch built.
static mlir::Value unwrapCoercedDim3(mlir::Value lowHalf) {
  auto load = lowHalf.getDefiningOp<cir::LoadOp>();
  if (!load)
    return {};
  auto member = load.getAddr().getDefiningOp<cir::GetMemberOp>();
  if (!member)
    return {};

  mlir::Value stored;
  for (mlir::Operation *user : member.getAddr().getUsers()) {
    auto cast = mlir::dyn_cast<cir::CastOp>(user);
    if (!cast || cast.getKind() != cir::CastKind::bitcast)
      continue;
    for (mlir::Operation *castUser : cast.getResult().getUsers()) {
      auto store = mlir::dyn_cast<cir::StoreOp>(castUser);
      if (!store || store.getAddr() != cast.getResult())
        continue;
      // A slot written more than once leaves the value the loads see
      // undecided; report no geometry rather than guess which store wins.
      if (stored)
        return {};
      stored = store.getValue();
    }
  }
  return stored;
}

static cir::LaunchSite::Dim3 traceDimOperand(cir::CallOp push, bool wantBlock) {
  std::optional<PushCallLayout> layout = pushCallLayout(push);
  if (!layout)
    return {};
  mlir::Value operand =
      push.getArgOperands()[wantBlock ? layout->block : layout->grid];
  return traceDim3(layout->coerced ? unwrapCoercedDim3(operand) : operand);
}

cir::LaunchSite::Dim3 cir::LaunchSite::getGridDim() const {
  if (!hasGeometry())
    return {};
  return traceDimOperand(cir::CallOp(pushConfigCall), /*wantBlock=*/false);
}

cir::LaunchSite::Dim3 cir::LaunchSite::getBlockDim() const {
  if (!hasGeometry())
    return {};
  return traceDimOperand(cir::CallOp(pushConfigCall), /*wantBlock=*/true);
}

mlir::Value cir::LaunchSite::getSharedMemBytes() const {
  if (!hasGeometry())
    return {};
  cir::CallOp push = pushConfigCall;
  std::optional<PushCallLayout> layout = pushCallLayout(push);
  return layout ? push.getArgOperands()[layout->sharedMem] : mlir::Value{};
}

mlir::TypedAttr cir::LaunchSite::getConstSharedMem() const {
  return asConst(getSharedMemBytes());
}

mlir::Value cir::LaunchSite::getStream() const {
  if (!hasGeometry())
    return {};
  cir::CallOp push = pushConfigCall;
  std::optional<PushCallLayout> layout = pushCallLayout(push);
  return layout ? push.getArgOperands()[layout->stream] : mlir::Value{};
}

bool cir::LaunchSite::isDefaultStream() const {
  auto ptr = mlir::dyn_cast_if_present<cir::ConstPtrAttr>(asConst(getStream()));
  return ptr && ptr.isNullValue();
}

cir::CUDAKernelNameAttr cir::getLaunchedKernel(mlir::Operation *op) {
  if (!mlir::isa<cir::CallOp>(op))
    return {};
  return op->getAttrOfType<cir::CUDAKernelNameAttr>(
      cir::CUDAKernelNameAttr::getMnemonic());
}

KernelBindingTable::KernelBindingTable(mlir::Operation *container) {
  auto containerOp = mlir::cast<cir::OffloadContainerOp>(container);
  mlir::ModuleOp hostModule = containerOp.getHostModule();

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
  hostModule.walk([&](mlir::Operation *op) {
    cir::CUDAKernelNameAttr kernel = cir::getLaunchedKernel(op);
    if (!kernel)
      return;
    auto it = bindings.find(kernel.getKernelName());
    if (it == bindings.end())
      return;
    cir::CallOp stubCall = mlir::cast<cir::CallOp>(op);
    it->second.launchSites.push_back(
        LaunchSite{stubCall, tracePushConfiguration(stubCall)});
  });

  for (mlir::ModuleOp deviceMod : containerOp.getDeviceModules()) {
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
