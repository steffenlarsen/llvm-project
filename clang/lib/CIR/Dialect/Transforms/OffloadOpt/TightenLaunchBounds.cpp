//===- TightenLaunchBounds.cpp - Narrow the promised block size -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/BuiltinAttributes.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelBindingTable.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelCloning.h"
#include "llvm/ADT/StringExtras.h"

namespace mlir {
#define GEN_PASS_DEF_OFFLOADTIGHTENLAUNCHBOUNDS
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

using namespace mlir;
using namespace cir;

namespace {

// The attribute CIRGen puts on every AMDGPU kernel, holding "min,max" flat
// work group sizes. `__launch_bounds__` sets it; otherwise it defaults to
// "1,<--gpu-max-threads-per-block>", which is 1024 unless the user narrowed it.
constexpr llvm::StringRef kFlatWorkGroupSizeAttr =
    "cir.amdgpu-flat-work-group-size";

struct FlatWorkGroupSize {
  int64_t min, max;
};

static std::optional<FlatWorkGroupSize> parseFlatWorkGroupSize(cir::FuncOp fn) {
  auto attr = fn->getAttrOfType<mlir::StringAttr>(kFlatWorkGroupSizeAttr);
  if (!attr)
    return std::nullopt;
  auto [minText, maxText] = attr.getValue().split(',');
  FlatWorkGroupSize size;
  if (minText.getAsInteger(10, size.min) || maxText.getAsInteger(10, size.max))
    return std::nullopt;
  return size;
}

// The largest block a launch site can produce, or nullopt if any site leaves a
// block dimension unresolved. The bound has to cover every launch that reaches
// the kernel, so an unknown dimension means no bound at all rather than a bound
// derived from the sites that happen to be readable.
static std::optional<int64_t>
maxBlockTotal(llvm::ArrayRef<cir::LaunchSite> sites) {
  int64_t largest = 0;
  for (const cir::LaunchSite &site : sites) {
    if (!site.hasGeometry())
      return std::nullopt;
    cir::LaunchSite::Dim3 block = site.getBlockDim();
    auto x = mlir::dyn_cast_or_null<cir::IntAttr>(block.constX());
    auto y = mlir::dyn_cast_or_null<cir::IntAttr>(block.constY());
    auto z = mlir::dyn_cast_or_null<cir::IntAttr>(block.constZ());
    if (!x || !y || !z)
      return std::nullopt;
    // A dim3 component is 32-bit unsigned, so the product fits in 64 bits and
    // cannot overflow here.
    int64_t total = x.getValue().getZExtValue() * y.getValue().getZExtValue() *
                    z.getValue().getZExtValue();
    if (total == 0)
      return std::nullopt;
    largest = std::max(largest, total);
  }
  return largest ? std::optional<int64_t>(largest) : std::nullopt;
}

// Whether narrowing `kernel`'s promised maximum to `newMax` says anything the
// attribute does not already say.
static bool tightens(cir::FuncOp kernel, int64_t newMax) {
  std::optional<FlatWorkGroupSize> current = parseFlatWorkGroupSize(kernel);
  return current && newMax < current->max;
}

static void setFlatWorkGroupMax(cir::FuncOp kernel, int64_t newMax) {
  std::optional<FlatWorkGroupSize> current = parseFlatWorkGroupSize(kernel);
  if (!current)
    return;
  // The minimum is the user's promise, not ours, so it is preserved -- except
  // where it would now exceed the maximum, which no launch could satisfy.
  int64_t min = std::min(current->min, newMax);
  kernel->setAttr(
      kFlatWorkGroupSizeAttr,
      mlir::StringAttr::get(
          kernel.getContext(),
          llvm::Twine(min).concat(",").concat(llvm::Twine(newMax)).str()));
}

struct OffloadTightenLaunchBoundsPass
    : public impl::OffloadTightenLaunchBoundsBase<
          OffloadTightenLaunchBoundsPass> {
  void runOnOperation() override;
};

void OffloadTightenLaunchBoundsPass::runOnOperation() {
  cir::OffloadContainerOp container = getOperation();
  cir::KernelBindingTable &table = getAnalysis<cir::KernelBindingTable>();
  bool changed = false;

  for (const auto &entry : table) {
    const cir::KernelBinding &binding = entry.second;
    llvm::ArrayRef<cir::LaunchSite> sites = binding.launchSites;

    // A kernel launched nowhere in this TU says nothing about its block size.
    if (sites.empty())
      continue;

    std::optional<int64_t> newMax = maxBlockTotal(sites);
    if (!newMax)
      continue;

    // Ask the originals whether the bound would say anything new before
    // deciding how to apply it, so no kernel is cloned for nothing.
    if (llvm::none_of(binding.deviceKernels, [&](cir::FuncOp kernel) {
          return tightens(kernel, *newMax);
        }))
      continue;

    cir::SpecializationTarget target = cir::getSpecializationTarget(
        container, entry.first, binding, ".bounds", sites);
    if (!target)
      continue;
    llvm::ArrayRef<cir::FuncOp> targets = target.deviceKernels;

    for (cir::FuncOp kernel : targets)
      if (tightens(kernel, *newMax))
        setFlatWorkGroupMax(kernel, *newMax);
    changed = true;
  }

  if (!changed)
    markAllAnalysesPreserved();
}

} // namespace

std::unique_ptr<Pass> mlir::createOffloadTightenLaunchBoundsPass() {
  return std::make_unique<OffloadTightenLaunchBoundsPass>();
}
