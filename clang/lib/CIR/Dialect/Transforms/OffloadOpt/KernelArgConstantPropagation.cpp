//===- KernelArgConstantPropagation.cpp - Constant launch arguments -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/Remarks.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelBindingTable.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelCloning.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/DebugLog.h"

#include <optional>

#define DEBUG_TYPE "cir-offload-kernel-arg-const-prop"

namespace mlir {
#define GEN_PASS_DEF_OFFLOADKERNELARGCONSTANTPROPAGATION
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

using namespace mlir;
using namespace cir;

namespace {

static constexpr llvm::StringLiteral kRemarkName = "KernelArgConstProp";
static constexpr llvm::StringLiteral kRemarkCategory = "cir-offload-const-prop";

static void remarkSkipped(cir::FuncOp stub, llvm::StringRef kernelName,
                          const char *reason, size_t numLaunchSites) {
  remark::missed(stub.getLoc(), remark::RemarkOpts::name(kRemarkName)
                                   .category(kRemarkCategory)
                                   .function(kernelName))
      << remark::add("kernel arguments not propagated")
      << remark::reason(reason)
      << remark::metric("launchSites", numLaunchSites);
}

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

struct OffloadKernelArgConstantPropagationPass
    : public impl::OffloadKernelArgConstantPropagationBase<
          OffloadKernelArgConstantPropagationPass> {
  void runOnOperation() override;

private:
  bool applyConstants(llvm::ArrayRef<cir::FuncOp> kernels,
                      const ArgConstants &constants, size_t numLaunchSites,
                      llvm::SmallPtrSetImpl<mlir::Operation *> &rewritten);
};

// Pin `constants` in every one of `kernels`.
bool OffloadKernelArgConstantPropagationPass::applyConstants(
    llvm::ArrayRef<cir::FuncOp> kernels, const ArgConstants &constants,
    size_t numLaunchSites,
    llvm::SmallPtrSetImpl<mlir::Operation *> &rewritten) {
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

      LDBG() << "  '" << kernel.getSymName() << "': replaced arg " << i;

      remark::passed(kernel.getLoc(), remark::RemarkOpts::name(kRemarkName)
                                          .category(kRemarkCategory)
                                          .function(kernel.getSymName()))
          << remark::add("propagated constant launch argument")
          << remark::metric("argIndex", i)
          << remark::metric("kernelArgCount", kernel.getNumArguments())
          << remark::metric("launchSites", numLaunchSites);
      ++numArgsPropagated;
      rewritten.insert(kernel);
    }
  }
  return changed;
}

void OffloadKernelArgConstantPropagationPass::runOnOperation() {
  mlir::ModuleOp module = getOperation();
  // The pass is anchored on a module, but only an offload container holds the
  // host<->device correspondence it rewrites. Any other module is left alone.
  if (!cir::isOffloadContainer(module))
    return;

  cir::KernelBindingTable &table = getAnalysis<cir::KernelBindingTable>();
  bool changed = false;
  llvm::SmallPtrSet<mlir::Operation *, 4> rewrittenKernels;

  LDBG() << "container has " << table.size() << " kernel binding(s)";

  for (const auto &entry : table) {
    const cir::KernelBinding &binding = entry.second;
    cir::FuncOp stub = binding.hostStub;
    llvm::ArrayRef<cir::LaunchSite> sites = binding.launchSites;

    LDBG() << "kernel '" << entry.first << "': " << sites.size()
           << " launch site(s), " << binding.deviceKernels.size()
           << " device kernel(s)";

    // A kernel launched nowhere in this TU says nothing about its arguments.
    if (sites.empty()) {
      LDBG() << "  skipped: no launch site in this TU";
      remarkSkipped(stub, entry.first, "no launch site in this translation unit",
                    sites.size());
      ++numKernelsSkipped;
      continue;
    }

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
    // reverted.
    llvm::SmallVector<cir::LaunchSite> splitSites, restSites;
    for (const cir::LaunchSite &site : sites)
      (isConstArgsSite(site) ? splitSites : restSites).push_back(site);

    ArgConstants splitConstants;
    if (!splitSites.empty() && !restSites.empty()) {
      splitConstants = agreedConstants(splitSites, stub);
      if (!pinsMoreThan(splitConstants, constants))
        splitConstants.clear();
    }

    LDBG() << "  " << constants.size() << " unanimous, " << splitSites.size()
           << " site(s) in a constant-args clone, " << splitConstants.size()
           << " split";

    if (constants.empty() && splitConstants.empty())
      continue;

    if (!splitConstants.empty()) {
      // Must be a clone, never in-place: the split group is deliberately a
      // subset of the visible launches, and pinning its constants into a
      // kernel the other launches also reach would hand them arguments they
      // never passed.
      std::optional<cir::KernelClone> clone =
          cir::cloneKernelForSites(module, binding, ".cargsprop", splitSites);
      if (clone) {
        changed = true;
        changed |= applyConstants(clone->deviceKernels, splitConstants,
                                  splitSites.size(), rewrittenKernels);
      } else {
        // Could not copy it, so the split cannot be served; fall back to
        // whatever every site agrees on, applied below to all of them.
        splitConstants.clear();
      }
    }

    if (constants.empty())
      continue;

    // Once the split sites are on their own copy they are served; the rest
    // get what they all agree on.
    llvm::ArrayRef<cir::LaunchSite> remaining =
        splitConstants.empty() ? llvm::ArrayRef<cir::LaunchSite>(sites)
                               : llvm::ArrayRef<cir::LaunchSite>(restSites);

    // Rewriting the kernel in place is only sound when nothing outside the
    // recorded sites can reach it. Otherwise specialise a copy and retarget
    // only the launches we can see, leaving the original to serve the rest.
    llvm::ArrayRef<cir::FuncOp> kernels = binding.deviceKernels;
    cir::SpecializationTarget target;
    if (!table.allLaunchSitesVisible(entry.first)) {
      target = cir::getSpecializationTarget(module, binding, ".constprop",
                                            remaining);
      if (!target) {
        LDBG() << "  skipped: launch sites not all visible (stub linkage "
               << stringifyGlobalLinkageKind(stub.getLinkage())
               << ") and the kernel cannot be cloned";
        remarkSkipped(stub, entry.first,
                      "launch sites not all visible and the kernel cannot be "
                      "cloned",
                      sites.size());
        ++numKernelsSkipped;
        continue;
      }
      changed |= target.cloned;
      kernels = target.deviceKernels;
    }
    changed |=
        applyConstants(kernels, constants, remaining.size(), rewrittenKernels);
  }

  numKernelsRewritten = rewrittenKernels.size();

  if (!changed)
    markAllAnalysesPreserved();
}

} // namespace

std::unique_ptr<Pass> mlir::createOffloadKernelArgConstantPropagationPass() {
  return std::make_unique<OffloadKernelArgConstantPropagationPass>();
}
