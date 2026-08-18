//===- KernelCloning.cpp - Clone a kernel for a subset of launches --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelCloning.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/SymbolTable.h"
#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "llvm/ADT/StringSet.h"

using namespace cir;

// A name not already taken anywhere in `container`. Device modules and the
// host module are separate symbol tables, but the clone's name has to be
// unique across all of them: it names the device kernels *and* the host handle
// global, and `LoweringPrepare` resolves the latter by a single symbol lookup.
static std::string uniqueName(cir::OffloadContainerOp container,
                              llvm::StringRef base, llvm::StringRef suffix) {
  llvm::StringSet<> taken;
  container->walk([&](mlir::Operation *op) {
    if (auto sym = mlir::dyn_cast<mlir::SymbolOpInterface>(op))
      taken.insert(sym.getName());
  });
  std::string candidate = (base + suffix).str();
  if (!taken.contains(candidate))
    return candidate;
  for (unsigned i = 1;; ++i) {
    std::string next = candidate + "." + std::to_string(i);
    if (!taken.contains(next))
      return next;
  }
}

// The handle global a device stub launches through: CIRGen names it with the
// kernel's mangled name and initialises it with the stub's address, and the
// stub body reads it with a cir.get_global. Returned along with the read, so
// the clone's copy of the read can be pointed at the clone's own handle.
static cir::GetGlobalOp findHandleRead(cir::FuncOp stub,
                                       llvm::StringRef kernelName) {
  cir::GetGlobalOp found;
  stub.walk([&](cir::GetGlobalOp get) {
    if (get.getName() == kernelName)
      found = get;
  });
  return found;
}

std::optional<cir::KernelClone> cir::cloneKernelForSites(
    cir::OffloadContainerOp container, const cir::KernelBinding &binding,
    llvm::StringRef suffix, llvm::ArrayRef<cir::LaunchSite> sites) {
  if (binding.deviceKernels.empty() || !binding.hostStub || sites.empty())
    return std::nullopt;

  cir::FuncOp stub = binding.hostStub;
  mlir::ModuleOp hostModule = container.getHostModule();
  auto kernelNameAttr = stub->getAttrOfType<cir::CUDAKernelNameAttr>(
      cir::CUDAKernelNameAttr::getMnemonic());
  if (!kernelNameAttr)
    return std::nullopt;
  llvm::StringRef oldKernelName = kernelNameAttr.getKernelName();

  // The stub must launch through its handle; without that read there is no way
  // to make the copy launch the copied kernel.
  cir::GetGlobalOp handleRead = findHandleRead(stub, oldKernelName);
  auto handle = mlir::dyn_cast_or_null<cir::GlobalOp>(
      hostModule.lookupSymbol(oldKernelName));
  if (!handleRead || !handle)
    return std::nullopt;

  cir::KernelClone clone;
  clone.kernelName = uniqueName(container, oldKernelName, suffix);
  std::string newStubName = uniqueName(container, stub.getSymName(), suffix);

  mlir::MLIRContext *ctx = container.getContext();
  auto newKernelNameAttr = cir::CUDAKernelNameAttr::get(
      ctx, mlir::StringAttr::get(ctx, clone.kernelName));

  // Device side: one copy per module that held the original.
  for (cir::FuncOp kernel : binding.deviceKernels) {
    auto deviceModule = kernel->getParentOfType<mlir::ModuleOp>();
    if (!deviceModule)
      continue;
    auto copy = mlir::cast<cir::FuncOp>(kernel->clone());
    mlir::SymbolTable::setSymbolName(copy, clone.kernelName);
    mlir::SymbolTable(deviceModule).insert(copy);
    copy->moveAfter(kernel);
    clone.deviceKernels.push_back(copy);
  }
  if (clone.deviceKernels.empty())
    return std::nullopt;

  // Host side: copy the stub, rebind it to the cloned kernel, and give it a
  // handle global of its own. `LoweringPrepare` emits the registration by
  // looking up a GlobalOp named exactly like `cu.kernel_name`, so the handle
  // has to exist under that name or registration will fail on the cast.
  auto newStub = mlir::cast<cir::FuncOp>(stub->clone());
  mlir::SymbolTable::setSymbolName(newStub, newStubName);
  newStub->setAttr(cir::CUDAKernelNameAttr::getMnemonic(), newKernelNameAttr);
  newStub->setAttr(kSpecializationCloneAttr, mlir::UnitAttr::get(ctx));
  mlir::SymbolTable(hostModule).insert(newStub);
  newStub->moveAfter(stub);
  clone.hostStub = newStub;

  // Point the copy's handle read at the copy's handle.
  if (cir::GetGlobalOp copiedRead = findHandleRead(newStub, oldKernelName))
    copiedRead.setName(clone.kernelName);

  mlir::OpBuilder builder(handle);
  auto newHandle = mlir::cast<cir::GlobalOp>(builder.clone(*handle));
  mlir::SymbolTable::setSymbolName(newHandle, clone.kernelName);
  newHandle.setInitialValueAttr(cir::GlobalViewAttr::get(
      newHandle.getSymType(), mlir::FlatSymbolRefAttr::get(ctx, newStubName)));

  // Retarget the requested launches. The callee and the `cu.kernel_name` on
  // the call have to move together: KernelBindingTable reads the attribute to
  // decide what a call launches, so leaving it stale would make the table
  // disagree with the IR.
  for (const cir::LaunchSite &site : sites) {
    cir::CallOp call = site.stubCall;
    call.setCalleeAttr(mlir::FlatSymbolRefAttr::get(ctx, newStubName));
    call->setAttr(cir::CUDAKernelNameAttr::getMnemonic(), newKernelNameAttr);
  }

  return clone;
}

bool cir::allLaunchSitesVisible(cir::FuncOp stub, llvm::StringRef kernelName,
                                mlir::Operation *scope) {
  // Do not "fix" this to ignore the handle global. CIRGen initialises that
  // global with the stub's address, so it is a symbol use that is not a launch
  // and this returns false for every kernel CIRGen emits -- which is the
  // behaviour we need, not an oversight.
  //
  // A kernel keeps its source-derived mangled name in the device image, so the
  // runtime API can reach it by *string*: hipModuleGetFunction and friends,
  // possibly from another translation unit. No symbol-use walk can see a string
  // literal, so no analysis here can prove a kernel is unreachable. The
  // original must therefore survive every semantics-changing specialisation;
  // only the clones may be rewritten in place, which is what
  // kSpecializationCloneAttr marks -- their names are synthesised and nothing
  // can name them.
  //
  // Deleting an original that really is unused is the dead-kernel pass's job,
  // where it is an explicit user-visible choice rather than a silent inference.
  if (!cir::isLocalLinkage(stub.getLinkage()))
    return false;
  auto uses = mlir::SymbolTable::getSymbolUses(stub, scope);
  if (!uses)
    return false;
  // The stub references itself, since it hands its own address to
  // cudaLaunchKernel, so only uses outside its own body are considered.
  return llvm::all_of(*uses, [&](const mlir::SymbolTable::SymbolUse &use) {
    if (stub->isProperAncestor(use.getUser()))
      return true;
    cir::CUDAKernelNameAttr launched = cir::getLaunchedKernel(use.getUser());
    return launched && launched.getKernelName() == kernelName;
  });
}

cir::SpecializationTarget cir::getSpecializationTarget(
    cir::OffloadContainerOp container, llvm::StringRef kernelName,
    const cir::KernelBinding &binding, llvm::StringRef suffix,
    llvm::ArrayRef<cir::LaunchSite> sites) {
  if (!binding.hostStub || sites.empty())
    return {};

  if (binding.hostStub->hasAttr(kSpecializationCloneAttr) ||
      allLaunchSitesVisible(binding.hostStub, kernelName,
                            container.getHostModule()))
    return {binding.hostStub,
            {binding.deviceKernels.begin(), binding.deviceKernels.end()},
            /*cloned=*/false};

  std::optional<cir::KernelClone> copy =
      cloneKernelForSites(container, binding, suffix, sites);
  if (!copy)
    return {};
  return {copy->hostStub, copy->deviceKernels, /*cloned=*/true};
}
