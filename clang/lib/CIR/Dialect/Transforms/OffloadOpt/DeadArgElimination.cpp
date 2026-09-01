//===- DeadArgElimination.cpp - Drop kernel arguments nothing reads -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A kernel argument the device never reads still costs a slot in the kernarg
// segment and the host-side stores that fill it. Dropping it means rewriting
// three things in step: the device kernel's signature, the host stub that
// builds the argument buffer, and every call to that stub.
//
// The argument buffer is the reason this pass is fussier than the others. The
// runtime copies `kernel_args[i]` into the kernarg segment according to the
// *device* kernel's signature, so the buffer has to stay densely packed in the
// surviving order. Getting that wrong does not miss an optimisation, it passes
// the wrong values -- so the stub is matched structurally against the exact
// shape CIRGen emits and left alone if it does not fit.
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/Builders.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/CIRTransformUtils.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelBindingTable.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelCloning.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"

namespace mlir {
#define GEN_PASS_DEF_OFFLOADDEADARGELIMINATION
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

using namespace mlir;
using namespace cir;

namespace {

//===----------------------------------------------------------------------===//
// Liveness on the device side
//===----------------------------------------------------------------------===//

// The alloca `arg` is spilled into by `store`, if that alloca exists only to
// hold the spill: never read, never otherwise used.
static cir::AllocaOp getDeadSpillSlot(BlockArgument arg, cir::StoreOp store) {
  if (store.getValue() != Value(arg))
    return {};
  auto alloca = store.getAddr().getDefiningOp<cir::AllocaOp>();
  if (!alloca)
    return {};
  for (Operation *user : alloca.getResult().getUsers())
    if (user != store.getOperation())
      return {};
  return alloca;
}

// An argument is dead when nothing reads it.
//
// Checking use_empty() alone is not enough: CIRGen spills every kernel
// parameter into an entry-block alloca, so even a parameter the body never
// mentions still has that store as a use. Treat an argument whose only uses are
// spills into never-read slots as dead.
static bool isArgDead(BlockArgument arg) {
  if (arg.use_empty())
    return true;
  for (Operation *user : arg.getUsers()) {
    auto store = dyn_cast<cir::StoreOp>(user);
    if (!store || !getDeadSpillSlot(arg, store))
      return false;
  }
  return true;
}

//===----------------------------------------------------------------------===//
// The host stub's argument buffer
//===----------------------------------------------------------------------===//

// One entry of the buffer the stub hands to hipLaunchKernel:
//
//   %slot  = cir.alloca "<param name>"           <- the parameter's spill slot
//   cir.store %argI, %slot
//   ...
//   %e     = cir.ptr_stride %base, %index
//   %c     = cir.cast bitcast %slot -> !cir.ptr<!void>
//   cir.store %c, %e
//
// Held as the ops rather than as indices, because renumbering the survivors
// means rewriting `index` in place.
struct ArgBufferEntry {
  cir::PtrStrideOp element;
  cir::CastOp cast;
  cir::StoreOp store;
  cir::AllocaOp slot;
  cir::StoreOp spill;
  // Set only for a parameter the ABI passes indirectly: the load that reads
  // the aggregate out of the caller's copy before it is spilled to `slot`.
  cir::LoadOp indirectLoad;
};

// Match the stub's argument buffer, in parameter order.
//
// Fails unless every parameter is accounted for exactly once and every entry
// has the shape above, so a stub CIRGen emits differently is declined rather
// than half-rewritten.
static std::optional<llvm::SmallVector<ArgBufferEntry, 8>>
matchArgBuffer(cir::FuncOp stub) {
  const unsigned numArgs = stub.getNumArguments();
  if (stub.isDeclaration() || numArgs == 0)
    return std::nullopt;

  // The buffer is the only `[N x ptr<void>]` alloca in the stub, with N the
  // parameter count.
  cir::AllocaOp buffer;
  for (auto alloca : stub.getBody().getOps<cir::AllocaOp>()) {
    auto arrayTy = mlir::dyn_cast<cir::ArrayType>(alloca.getAllocaType());
    if (!arrayTy || arrayTy.getSize() != numArgs)
      continue;
    if (!mlir::isa<cir::PointerType>(arrayTy.getElementType()))
      continue;
    if (buffer)
      return std::nullopt;
    buffer = alloca;
  }
  if (!buffer)
    return std::nullopt;

  // The decayed base pointer the entries are strided from.
  cir::CastOp decay;
  for (Operation *user : buffer.getResult().getUsers()) {
    auto cast = mlir::dyn_cast<cir::CastOp>(user);
    if (!cast || cast.getKind() != cir::CastKind::array_to_ptrdecay)
      return std::nullopt;
    if (decay)
      return std::nullopt;
    decay = cast;
  }
  if (!decay)
    return std::nullopt;

  // Where each parameter was spilled. A parameter stored to more than one slot,
  // or to something that is not a plain alloca, is not a shape this handles.
  llvm::SmallVector<cir::AllocaOp, 8> slots(numArgs);
  llvm::SmallVector<cir::StoreOp, 8> spills(numArgs);
  llvm::SmallVector<cir::LoadOp, 8> indirectLoads(numArgs);
  for (unsigned i = 0; i != numArgs; ++i) {
    for (Operation *user : stub.getArgument(i).getUsers()) {
      // The common shape: the parameter is spilled straight to its slot.
      if (auto store = mlir::dyn_cast<cir::StoreOp>(user)) {
        if (store.getValue() != stub.getArgument(i))
          return std::nullopt;
        auto slot = store.getAddr().getDefiningOp<cir::AllocaOp>();
        if (!slot || slots[i])
          return std::nullopt;
        slots[i] = slot;
        spills[i] = store;
        continue;
      }
      // A parameter the ABI passes indirectly -- an aggregate such as the
      // `uint3` ggml threads through its conversion kernels -- arrives as a
      // pointer to the caller's copy and is read out before being spilled:
      //   %v = cir.load %arg  /  cir.store %v, %slot
      // Declining the whole stub over this one shape costs every other
      // parameter too.
      auto load = mlir::dyn_cast<cir::LoadOp>(user);
      if (!load || load.getAddr() != stub.getArgument(i) ||
          !load.getResult().hasOneUse())
        return std::nullopt;
      auto store =
          mlir::dyn_cast<cir::StoreOp>(*load.getResult().getUsers().begin());
      if (!store || store.getValue() != load.getResult())
        return std::nullopt;
      auto slot = store.getAddr().getDefiningOp<cir::AllocaOp>();
      if (!slot || slots[i])
        return std::nullopt;
      slots[i] = slot;
      spills[i] = store;
      indirectLoads[i] = load;
    }
    if (!slots[i])
      return std::nullopt;
  }

  llvm::SmallVector<ArgBufferEntry, 8> entries(numArgs);
  for (Operation *user : decay.getResult().getUsers()) {
    auto element = mlir::dyn_cast<cir::PtrStrideOp>(user);
    if (!element) {
      // The base itself is handed to hipLaunchKernel; that use is expected.
      if (mlir::isa<cir::CallOp>(user))
        continue;
      return std::nullopt;
    }

    std::optional<int64_t> index =
        cir::tryResolveToConstant(element.getStride());
    if (!index || *index < 0 || *index >= numArgs || entries[*index].element)
      return std::nullopt;

    if (!element.getResult().hasOneUse())
      return std::nullopt;
    auto store =
        mlir::dyn_cast<cir::StoreOp>(*element.getResult().getUsers().begin());
    if (!store || store.getAddr() != element.getResult())
      return std::nullopt;
    auto cast = store.getValue().getDefiningOp<cir::CastOp>();
    if (!cast || cast.getKind() != cir::CastKind::bitcast)
      return std::nullopt;

    // The entry has to point at the slot of the parameter with that index, or
    // the buffer does not mean what its order says.
    auto slot = cast.getSrc().getDefiningOp<cir::AllocaOp>();
    if (!slot || slot != slots[*index])
      return std::nullopt;

    entries[*index] = {element,       cast,           store,
                       slots[*index], spills[*index], indirectLoads[*index]};
  }

  if (llvm::any_of(entries, [](const ArgBufferEntry &e) { return !e.element; }))
    return std::nullopt;
  return entries;
}

//===----------------------------------------------------------------------===//
// Rewriting
//===----------------------------------------------------------------------===//

// Drop the dead entries and renumber the survivors so the buffer stays densely
// packed in the order the device kernel now expects.
static void rewriteArgBuffer(llvm::ArrayRef<ArgBufferEntry> entries,
                             const llvm::BitVector &dead) {
  unsigned next = 0;
  for (auto [i, entry] :
       llvm::enumerate(llvm::SmallVector<ArgBufferEntry, 8>(entries))) {
    if (dead.test(i)) {
      entry.store.erase();
      entry.cast.erase();
      entry.element.erase();
      entry.spill.erase();
      if (entry.indirectLoad)
        entry.indirectLoad.erase();
      if (entry.slot.getResult().use_empty())
        entry.slot.erase();
      continue;
    }
    if (next != i) {
      mlir::OpBuilder builder(entry.element);
      auto indexTy =
          mlir::cast<cir::IntType>(entry.element.getStride().getType());
      auto renumbered = cir::ConstantOp::create(
          builder, entry.element.getLoc(),
          cir::IntAttr::get(indexTy, llvm::APInt(indexTy.getWidth(), next)));
      entry.element.getStrideMutable().assign(renumbered);
    }
    ++next;
  }
}

// Erase the dead parameters from a cir.func and shrink its type.
//
// FunctionOpInterface::eraseArguments cannot be used: cir::FuncType::clone
// asserts a single result, which does not hold for a void-returning kernel.
static void eraseParams(cir::FuncOp func, const llvm::BitVector &dead) {
  if (!func.isDeclaration()) {
    Block &entry = func.getBody().front();
    for (unsigned i : dead.set_bits())
      if (i < entry.getNumArguments())
        entry.getArgument(i).dropAllUses();
    for (unsigned i : llvm::reverse(dead.set_bits()))
      if (i < entry.getNumArguments())
        entry.eraseArgument(i);
  }

  cir::FuncType oldTy = func.getFunctionType();
  llvm::SmallVector<Type> inputs;
  for (auto [i, ty] : llvm::enumerate(oldTy.getInputs()))
    if (!dead.test(i))
      inputs.push_back(ty);
  func.setFunctionTypeAttr(TypeAttr::get(
      cir::FuncType::get(inputs, oldTy.getReturnType(), oldTy.isVarArg())));

  if (auto argAttrs = func->getAttrOfType<ArrayAttr>("arg_attrs")) {
    llvm::SmallVector<Attribute> kept;
    for (auto [i, attr] : llvm::enumerate(argAttrs))
      if (!dead.test(i))
        kept.push_back(attr);
    func->setAttr("arg_attrs", ArrayAttr::get(func->getContext(), kept));
  }
}

// Drop the dead spill slots and stores in the device kernel, then its
// parameters. The stores go first: erasing a block argument still referenced by
// a store would leave the store holding a detached value.
static void shrinkDeviceKernel(cir::FuncOp kernel,
                               const llvm::BitVector &dead) {
  for (unsigned i : dead.set_bits()) {
    if (i >= kernel.getNumArguments())
      continue;
    BlockArgument arg = kernel.getArgument(i);
    llvm::SmallVector<cir::StoreOp, 2> stores;
    llvm::SmallVector<cir::AllocaOp, 2> slots;
    for (Operation *user : arg.getUsers()) {
      auto store = dyn_cast<cir::StoreOp>(user);
      if (!store)
        continue;
      if (cir::AllocaOp slot = getDeadSpillSlot(arg, store)) {
        stores.push_back(store);
        slots.push_back(slot);
      }
    }
    for (cir::StoreOp store : stores)
      store.erase();
    for (cir::AllocaOp slot : slots)
      if (slot.getResult().use_empty())
        slot.erase();
  }
  eraseParams(kernel, dead);
}

// Drop the dead operands from a call, and the per-argument attributes that go
// with them.
//
// `arg_attrs` is positional: leaving it at the old length rebinds every
// attribute after a removed operand to its neighbour, which is how a `zeroext`
// meant for a dropped `bool` ends up on a pointer and the LLVM verifier
// rejects the call.
static void eraseCallArgs(cir::CallOp call, const llvm::BitVector &dead) {
  auto argAttrs = call->getAttrOfType<ArrayAttr>("arg_attrs");
  for (unsigned i : llvm::reverse(dead.set_bits()))
    if (i < call.getArgOperandsMutable().size())
      call.getArgOperandsMutable().erase(i);

  if (!argAttrs)
    return;
  llvm::SmallVector<Attribute> kept;
  for (auto [i, attr] : llvm::enumerate(argAttrs))
    if (!dead.test(i))
      kept.push_back(attr);
  call->setAttr("arg_attrs", ArrayAttr::get(call.getContext(), kept));
}

// The handle global's type spells out the stub's signature, so it has to shrink
// with it or the two disagree.
static void retypeHandle(cir::OffloadContainerOp container, cir::FuncOp stub,
                         llvm::StringRef kernelName) {
  auto handle = mlir::dyn_cast_or_null<cir::GlobalOp>(
      container.getHostModule().lookupSymbol(kernelName));
  if (!handle)
    return;
  auto ptrTy = mlir::dyn_cast<cir::PointerType>(handle.getSymType());
  if (!ptrTy || !mlir::isa<cir::FuncType>(ptrTy.getPointee()))
    return;
  auto newTy = cir::PointerType::get(stub.getFunctionType());
  handle.setSymType(newTy);
  if (auto view = mlir::dyn_cast_or_null<cir::GlobalViewAttr>(
          handle.getInitialValueAttr()))
    handle.setInitialValueAttr(
        cir::GlobalViewAttr::get(newTy, view.getSymbol()));

  // The stub reads its own handle; that read has to agree with the new type.
  stub.walk([&](cir::GetGlobalOp get) {
    if (get.getName() == kernelName)
      get.getResult().setType(cir::PointerType::get(newTy));
  });
}

struct OffloadDeadArgEliminationPass
    : public impl::OffloadDeadArgEliminationBase<
          OffloadDeadArgEliminationPass> {
  void runOnOperation() override;
};

void OffloadDeadArgEliminationPass::runOnOperation() {
  cir::OffloadContainerOp container = getOperation();
  cir::KernelBindingTable &table = getAnalysis<cir::KernelBindingTable>();
  bool changed = false;

  for (const auto &entry : table) {
    const cir::KernelBinding &binding = entry.second;
    llvm::ArrayRef<cir::LaunchSite> sites = binding.launchSites;
    if (sites.empty() || !binding.hostStub)
      continue;

    // Every device kernel bound to this stub has to agree the argument is
    // dead: they share one signature, so one reader keeps it for all.
    std::optional<llvm::BitVector> dead;
    for (cir::FuncOp kernel : binding.deviceKernels) {
      if (kernel.isDeclaration())
        continue;
      llvm::BitVector here(kernel.getNumArguments());
      for (unsigned i = 0, e = kernel.getNumArguments(); i != e; ++i)
        if (isArgDead(kernel.getArgument(i)))
          here.set(i);
      if (!dead)
        dead = here;
      else if (dead->size() == here.size())
        *dead &= here;
      else
        dead->clear();
    }
    if (!dead || dead->none())
      continue;

    // Whether the stub can be rewritten at all is decided on the original: a
    // clone is a byte-for-byte copy, so what does not match here will not match
    // there either, and cloning first would leave a copy for nothing.
    if (!matchArgBuffer(binding.hostStub))
      continue;

    // Only narrow a kernel that can be narrowed in place. Cloning a whole
    // kernel to save a few bytes of kernarg does not pay: allowing it was
    // measured at ~-0.9% decode for no prefill gain, with the shipped library
    // 10% larger. The case that matters is still covered, because a
    // specialisation clone -- which is where constant propagation leaves dead
    // arguments -- is rewritten in place by getSpecializationTarget.
    if (!binding.hostStub->hasAttr(cir::kSpecializationCloneAttr) &&
        !cir::allLaunchSitesVisible(binding.hostStub, entry.first,
                                    container.getHostModule()))
      continue;

    cir::SpecializationTarget target = cir::getSpecializationTarget(
        container, entry.first, binding, ".dae", sites);
    if (!target)
      continue;

    std::optional<llvm::SmallVector<ArgBufferEntry, 8>> buffer =
        matchArgBuffer(target.hostStub);
    if (!buffer || buffer->size() != dead->size()) {
      changed |= target.cloned;
      continue;
    }

    rewriteArgBuffer(*buffer, *dead);
    for (cir::FuncOp kernel : target.deviceKernels)
      if (!kernel.isDeclaration())
        shrinkDeviceKernel(kernel, *dead);

    // Call sites lose the same operands; the stub itself loses the parameters.
    auto stubKernelName =
        target.hostStub->getAttrOfType<cir::CUDAKernelNameAttr>(
            cir::CUDAKernelNameAttr::getMnemonic());
    for (mlir::Operation *user : llvm::to_vector(llvm::map_range(
             target.hostStub.getSymbolUses(container.getHostModule())
                 ? *target.hostStub.getSymbolUses(container.getHostModule())
                 : mlir::SymbolTable::UseRange({}),
             [](const mlir::SymbolTable::SymbolUse &use) {
               return use.getUser();
             }))) {
      auto call = mlir::dyn_cast<cir::CallOp>(user);
      if (!call || target.hostStub->isProperAncestor(call))
        continue;
      eraseCallArgs(call, *dead);
    }
    eraseParams(target.hostStub, *dead);
    if (stubKernelName)
      retypeHandle(container, target.hostStub, stubKernelName.getKernelName());

    changed = true;
  }

  if (!changed)
    markAllAnalysesPreserved();
}

} // namespace

std::unique_ptr<Pass> mlir::createOffloadDeadArgEliminationPass() {
  return std::make_unique<OffloadDeadArgEliminationPass>();
}
