//===- KernelArgConstantPropagation.cpp - Constant launch arguments -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/SymbolTable.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelBindingTable.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelCloning.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"

#include <optional>

#define DEBUG_TYPE "cir-offload-kernel-arg-const-prop"

namespace mlir {
#define GEN_PASS_DEF_OFFLOADKERNELARGCONSTANTPROPAGATION
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

using namespace mlir;
using namespace cir;

namespace {

// The constant every site passes for argument `argIdx`, or null if they
// disagree or any of them passes a non-constant. Agreement is a property of the
// set of sites, so it lives here rather than on a single site.
static mlir::TypedAttr
commonConstantArg(llvm::ArrayRef<cir::LaunchSite> sites, unsigned argIdx) {
  mlir::TypedAttr common;
  for (const cir::LaunchSite &site : sites) {
    mlir::TypedAttr value = site.getConstArg(argIdx);
    if (!value)
      return {};
    if (!common)
      common = value;
    else if (common != value)
      return {};
  }
  return common;
}

// Per-parameter constants, as `commonConstantArg` reports them for a set of
// sites.
using ArgConstants = llvm::SmallVector<std::pair<unsigned, mlir::TypedAttr>, 4>;

static ArgConstants agreedConstants(llvm::ArrayRef<cir::LaunchSite> sites,
                                    cir::FuncOp stub) {
  ArgConstants out;
  for (unsigned i = 0, e = stub.getNumArguments(); i != e; ++i)
    if (mlir::TypedAttr value = commonConstantArg(sites, i))
      out.emplace_back(i, value);
  return out;
}

// Whether `candidate` pins a parameter `baseline` leaves as a runtime value.
// Only then is a second clone worth its code size.
static bool pinsMoreThan(const ArgConstants &candidate,
                         const ArgConstants &baseline) {
  return llvm::any_of(candidate, [&](const auto &pin) {
    return llvm::none_of(
        baseline, [&](const auto &known) { return known.first == pin.first; });
  });
}

// Whether this launch sits in a helper the compiler cloned to carry constants.
static bool isConstArgsSite(const cir::LaunchSite &site) {
  auto fn = site.stubCall->getParentOfType<cir::FuncOp>();
  return fn && fn->hasAttr(cir::kConstArgsCloneAttr);
}

// Pin `constants` in every one of `kernels`.
static bool applyConstants(llvm::ArrayRef<cir::FuncOp> kernels,
                           const ArgConstants &constants) {
  bool changed = false;
  for (auto [i, value] : constants) {
    // The stub argument and the kernel argument at the same index are the
    // same kernel parameter; nothing has rewritten either signature yet.
    for (cir::FuncOp kernel : kernels) {
      if (kernel.isDeclaration() || i >= kernel.getNumArguments())
        continue;
      mlir::BlockArgument arg = kernel.getArgument(i);
      if (arg.use_empty() || arg.getType() != value.getType())
        continue;

      mlir::OpBuilder builder(&kernel.getBody().front(),
                              kernel.getBody().front().begin());
      auto constant = cir::ConstantOp::create(builder, kernel.getLoc(), value);
      arg.replaceAllUsesWith(constant);
      changed = true;
    }
  }
  return changed;
}

struct OffloadKernelArgConstantPropagationPass
    : public impl::OffloadKernelArgConstantPropagationBase<
          OffloadKernelArgConstantPropagationPass> {
  void runOnOperation() override;
};

void OffloadKernelArgConstantPropagationPass::runOnOperation() {
  cir::OffloadContainerOp container = getOperation();
  cir::KernelBindingTable &table = getAnalysis<cir::KernelBindingTable>();
  bool changed = false;

  for (const auto &entry : table) {
    const cir::KernelBinding &binding = entry.second;
    cir::FuncOp stub = binding.hostStub;
    llvm::ArrayRef<cir::LaunchSite> sites = binding.launchSites;

    // A kernel launched nowhere in this TU says nothing about its arguments.
    if (sites.empty())
      continue;

    // The constant every recorded site agrees on, per parameter.
    ArgConstants constants = agreedConstants(sites, stub);

    // A launch helper whose address is also stored in a dispatch table -- which
    // is how ggml reaches its conversion kernels -- keeps one launch passing
    // runtime values however many of its callers agree, so unanimity pins
    // nothing. Split off the launches sitting in a constant-args clone: those
    // helpers exist only to carry constants, so they are a group by
    // construction and every other launch keeps the unsplit kernel.
    //
    // Deliberately limited to that case. Splitting on *any* disagreement was
    // measured at -1% prefill where mul_mat_q is hot for +7% binary, and
    // reverted; see the per-group-clone-specialization note.
    llvm::SmallVector<cir::LaunchSite> splitSites, restSites;
    for (const cir::LaunchSite &site : sites)
      (isConstArgsSite(site) ? splitSites : restSites).push_back(site);

    ArgConstants splitConstants;
    if (!splitSites.empty() && !restSites.empty()) {
      splitConstants = agreedConstants(splitSites, stub);
      if (!pinsMoreThan(splitConstants, constants))
        splitConstants.clear();
    }

    LLVM_DEBUG(llvm::dbgs()
               << "CONSTPROP " << entry.first << ": " << sites.size()
               << " sites (" << splitSites.size() << " in a $cargs clone), "
               << constants.size() << " unanimous, " << splitConstants.size()
               << " split\n");

    if (constants.empty() && splitConstants.empty())
      continue;

    // Rewriting the kernel in place is only sound when nothing outside the
    // recorded sites can reach it. CIRGen emits a handle global for every
    // kernel, so `&kernel` can escape and be launched with other arguments;
    // for those kernels specialise a *copy* and retarget only the launches we
    // can see, leaving the original to serve the rest.
    if (!splitConstants.empty()) {
      // Must be a clone, never in-place. getSpecializationTarget will rewrite a
      // kernel in place once every launch of it is visible -- which is true of
      // a clone by construction -- but that reasoning holds for the *whole* set
      // of visible launches, and this is deliberately a subset of them. Pinning
      // the split group's constants into a kernel the other launches also reach
      // would hand them arguments they never passed.
      std::optional<cir::KernelClone> clone = cir::cloneKernelForSites(
          container, binding, ".cargsprop", splitSites);
      if (clone) {
        changed = true;
        changed |= applyConstants(clone->deviceKernels, splitConstants);
      } else {
        // Could not copy it, so the split cannot be served; fall back to
        // whatever every site agrees on, applied below to all of them.
        splitConstants.clear();
      }
    }

    if (!constants.empty()) {
      // Once the split sites are on their own copy they are served; the rest
      // get what they all agree on.
      llvm::ArrayRef<cir::LaunchSite> remaining =
          splitConstants.empty() ? llvm::ArrayRef<cir::LaunchSite>(sites)
                                 : llvm::ArrayRef<cir::LaunchSite>(restSites);
      cir::SpecializationTarget target = cir::getSpecializationTarget(
          container, entry.first, binding, ".constprop", remaining);
      if (!target)
        continue;
      changed |= target.cloned;
      changed |= applyConstants(target.deviceKernels, constants);
    }
  }

  if (!changed)
    markAllAnalysesPreserved();
}

} // namespace

std::unique_ptr<Pass> mlir::createOffloadKernelArgConstantPropagationPass() {
  return std::make_unique<OffloadKernelArgConstantPropagationPass>();
}
