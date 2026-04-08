//===- OffloadDevirtualizeLaunches.cpp - Recover indirect kernel launches -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// CIRGen only emits cir.offload.kernel_launch when the launched callee is a
// symbol it can resolve at codegen time.  A launch through a function-pointer
// *value* is emitted as the pre-offload HIP sequence instead:
//
//   %r = cir.call @__hipPushCallConfiguration(%grid, %block, %shmem, %stream)
//   %c = cir.cast int_to_bool %r
//   cir.if %c { } else {
//     %f = <function pointer>
//     cir.call %f(%args...)          // call to the device stub
//   }
//
// Every offload optimization pass matches on cir.offload.kernel_launch, so a
// launch left in this form is invisible to all of them.  When the callee can
// be traced back to a specific device stub this pass rewrites the sequence
// into the canonical launch op, which puts it back in view.
//
// The transformation is all-or-nothing per launch: if any part of the pattern
// does not match -- unresolvable callee, unrecognised grid/block construction
// -- the sequence is left exactly as it was.  Leaving an indirect launch alone
// only costs optimization; rewriting one incorrectly would miscompile.
//
// Run before the offload optimization passes.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Matchers.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cir-offload-devirtualize-launches"

using namespace mlir;

namespace {

/// Name of the HIP runtime entry point CIRGen uses to stage a launch
/// configuration before calling the device stub.
constexpr llvm::StringRef kPushCallConfiguration = "__hipPushCallConfiguration";

/// Look through pointer-preserving casts to the value actually being used.
static Value stripCasts(Value v) {
  while (auto cast = v.getDefiningOp<cir::CastOp>()) {
    if (cast.getKind() != cir::CastKind::bitcast)
      break;
    v = cast.getSrc();
  }
  return v;
}

/// Trace \p callee back to the device stub it must refer to, or return {} if
/// it cannot be pinned down to exactly one.
///
/// Handles the forms CIRGen produces once the callee is a compile-time
/// constant: a direct cir.get_global, or a load from a slot that is stored to
/// exactly once.  Anything less definite (two stores, a stored value that is
/// itself dynamic, an address that escapes) yields {}.
static cir::GetGlobalOp resolveCalleeStub(Value callee) {
  callee = stripCasts(callee);

  if (auto get = callee.getDefiningOp<cir::GetGlobalOp>())
    return get;

  auto load = callee.getDefiningOp<cir::LoadOp>();
  if (!load)
    return {};

  Value addr = stripCasts(load.getAddr());
  auto alloca = addr.getDefiningOp<cir::AllocaOp>();
  if (!alloca)
    return {};

  // Exactly one store into the slot, and nothing else that could change it.
  cir::StoreOp uniqueStore;
  for (Operation *user : alloca.getResult().getUsers()) {
    if (auto store = dyn_cast<cir::StoreOp>(user)) {
      // A store *of* the address rather than *to* it means the slot escapes.
      if (store.getValue() == alloca.getResult())
        return {};
      if (uniqueStore)
        return {};
      uniqueStore = store;
      continue;
    }
    if (isa<cir::LoadOp>(user))
      continue;
    // Any other use (passed to a call, address taken, ...) is not tracked.
    return {};
  }
  if (!uniqueStore)
    return {};

  return stripCasts(uniqueStore.getValue()).getDefiningOp<cir::GetGlobalOp>();
}

/// Recover the three components of a dim3 value passed to
/// __hipPushCallConfiguration.
///
/// The value is always a load of a dim3 record from memory, whether that
/// memory is a constructor-initialised temporary at a direct launch site:
///
///   %slot = cir.alloca ... : !cir.ptr<!rec_dim3>
///   cir.call @_ZN4dim3C2Ejjj(%slot, %x, %y, %z)
///   %v = cir.load %slot
///
/// or the spill slot of a `dim3` parameter in a launch wrapper.  Rather than
/// pattern-matching the constructor -- which only covers the first case --
/// read the three fields back out of the address.  \p builder must already be
/// positioned where the extracted values need to be available.
/// Cheap check for the shape extractDim3() needs, so the rewrite can bail
/// before mutating anything.
static bool canExtractDim3(Value v) {
  auto load = v.getDefiningOp<cir::LoadOp>();
  if (!load)
    return false;
  auto ptrTy =
      dyn_cast<cir::PointerType>(stripCasts(load.getAddr()).getType());
  if (!ptrTy)
    return false;
  auto recTy = dyn_cast<cir::RecordType>(ptrTy.getPointee());
  if (!recTy || recTy.getNumElements() != 3)
    return false;
  return llvm::all_of(recTy.getMembers(),
                      [](Type t) { return isa<cir::IntType>(t); });
}

static bool extractDim3(OpBuilder &builder, Value v, Value &x, Value &y,
                        Value &z) {
  auto load = v.getDefiningOp<cir::LoadOp>();
  if (!load)
    return false;
  Value addr = stripCasts(load.getAddr());

  auto ptrTy = dyn_cast<cir::PointerType>(addr.getType());
  if (!ptrTy)
    return false;
  auto recTy = dyn_cast<cir::RecordType>(ptrTy.getPointee());
  if (!recTy || recTy.getNumElements() != 3)
    return false;

  Value *out[3] = {&x, &y, &z};
  for (unsigned f = 0; f < 3; ++f) {
    Type fieldTy = recTy.getMembers()[f];
    // The launch op requires CIR integer dimensions.
    if (!isa<cir::IntType>(fieldTy))
      return false;
    Value fieldPtr = cir::GetMemberOp::create(
        builder, load.getLoc(), cir::PointerType::get(fieldTy), addr,
        /*name=*/"", /*index=*/f);
    *out[f] = cir::LoadOp::create(builder, load.getLoc(), fieldPtr);
  }
  return true;
}

struct OffloadDevirtualizeLaunchesPass
    : public PassWrapper<OffloadDevirtualizeLaunchesPass,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(OffloadDevirtualizeLaunchesPass)

  OffloadDevirtualizeLaunchesPass() = default;
  OffloadDevirtualizeLaunchesPass(bool enabled) : passEnabled(enabled) {}

  StringRef getArgument() const override {
    return "cir-offload-devirtualize-launches";
  }
  StringRef getDescription() const override {
    return "Rewrite traceable indirect kernel launches into "
           "cir.offload.kernel_launch";
  }

  void runOnOperation() override {
    if (!passEnabled)
      return;

    ModuleOp module = getOperation();

    // stub symbol name -> kernel name, from the stub's cu.kernel_name.
    llvm::StringMap<llvm::StringRef> stubToKernel;
    module.walk([&](cir::FuncOp fn) {
      if (auto attr = fn->getAttrOfType<cir::CUDAKernelNameAttr>(
              cir::CUDAKernelNameAttr::getMnemonic()))
        stubToKernel[fn.getSymName()] = attr.getKernelName();
    });
    if (stubToKernel.empty())
      return;

    // kernel name -> the offload module that defines it, so the rewritten
    // launch names the same module the merge step used.
    llvm::StringMap<llvm::StringRef> kernelToModule;
    module.walk([&](cir::OffloadModuleOp offloadMod) {
      offloadMod.walk([&](cir::OffloadFuncOp fn) {
        if (fn.isKernel())
          kernelToModule[fn.getSymName()] = offloadMod.getSymName();
      });
    });

    SmallVector<cir::CallOp> pushCalls;
    module.walk([&](cir::CallOp call) {
      std::optional<llvm::StringRef> callee = call.getCallee();
      if (callee && *callee == kPushCallConfiguration)
        pushCalls.push_back(call);
    });

    for (cir::CallOp push : pushCalls)
      tryRewrite(push, stubToKernel, kernelToModule);
  }

  void tryRewrite(cir::CallOp push,
                  const llvm::StringMap<llvm::StringRef> &stubToKernel,
                  const llvm::StringMap<llvm::StringRef> &kernelToModule) {
    if (push.getNumOperands() != 4 || push.getNumResults() != 1)
      return;

    // The result feeds a bool cast that is the condition of a cir.if whose
    // *else* region performs the launch (the push returns zero on success).
    if (!push.getResult().hasOneUse())
      return;
    auto cast = dyn_cast<cir::CastOp>(*push.getResult().getUsers().begin());
    if (!cast || cast.getKind() != cir::CastKind::int_to_bool)
      return;
    if (!cast.getResult().hasOneUse())
      return;
    auto ifOp = dyn_cast<cir::IfOp>(*cast.getResult().getUsers().begin());
    if (!ifOp || ifOp.getElseRegion().empty())
      return;

    Block &elseBlock = ifOp.getElseRegion().front();

    // Exactly one indirect call in the else region: the stub call.
    cir::CallOp stubCall;
    for (Operation &op : elseBlock) {
      auto call = dyn_cast<cir::CallOp>(&op);
      if (!call)
        continue;
      if (call.getCallee())
        return; // A direct call here means this is not the shape we expect.
      if (stubCall)
        return;
      stubCall = call;
    }
    if (!stubCall)
      return;

    // The "then" region must be empty -- nothing happens when the push fails.
    if (!ifOp.getThenRegion().empty()) {
      Block &thenBlock = ifOp.getThenRegion().front();
      for (Operation &op : thenBlock)
        if (!op.hasTrait<OpTrait::IsTerminator>())
          return;
    }

    cir::GetGlobalOp stub = resolveCalleeStub(stubCall.getIndirectCall());
    if (!stub)
      return;
    auto kernelIt = stubToKernel.find(stub.getName());
    if (kernelIt == stubToKernel.end())
      return;
    llvm::StringRef kernelName = kernelIt->second;
    auto modIt = kernelToModule.find(kernelName);
    if (modIt == kernelToModule.end())
      return;

    // Validate before mutating: everything below rewrites the IR.
    if (!canExtractDim3(push.getOperand(0)) ||
        !canExtractDim3(push.getOperand(1)))
      return;

    // Kernel arguments are computed inside the else region; hoist that
    // computation (and everything else in the region except the terminator)
    // ahead of the cir.if so the values dominate the new launch op.
    SmallVector<Operation *> toHoist;
    for (Operation &op : elseBlock) {
      if (&op == stubCall.getOperation())
        continue;
      if (op.hasTrait<OpTrait::IsTerminator>())
        continue;
      toHoist.push_back(&op);
    }
    for (Operation *op : toHoist)
      op->moveBefore(ifOp);

    OpBuilder builder(ifOp);
    Value gridX, gridY, gridZ, blockX, blockY, blockZ;
    // Guaranteed to succeed: canExtractDim3() checked the shape above.
    if (!extractDim3(builder, push.getOperand(0), gridX, gridY, gridZ) ||
        !extractDim3(builder, push.getOperand(1), blockX, blockY, blockZ))
      llvm_unreachable("dim3 shape validated but extraction failed");

    SmallVector<Attribute> kernels{SymbolRefAttr::get(
        builder.getContext(), modIt->second,
        {FlatSymbolRefAttr::get(builder.getContext(), kernelName)})};

    // Operand 2 is the dynamic shared memory size, operand 3 the stream.
    Value shmem = push.getOperand(2);
    Value stream = push.getOperand(3);

    LLVM_DEBUG(llvm::dbgs()
               << "OffloadDevirtualizeLaunches: " << stub.getName() << " -> "
               << modIt->second << "::" << kernelName << "\n");

    cir::OffloadKernelLaunchOp::create(
        builder, stubCall.getLoc(),
        ArrayAttr::get(builder.getContext(), kernels), gridX, gridY, gridZ,
        blockX, blockY, blockZ, shmem,
        SmallVector<Value>(stubCall.getArgOperands()), stream);

    stubCall.erase();
    ifOp.erase();
    cast.erase();
    push.erase();
    ++numRewritten;
  }

  bool passEnabled = true;
  unsigned numRewritten = 0;
};

} // namespace

std::unique_ptr<mlir::Pass>
mlir::createOffloadDevirtualizeLaunchesPass(bool enabled) {
  return std::make_unique<OffloadDevirtualizeLaunchesPass>(enabled);
}
