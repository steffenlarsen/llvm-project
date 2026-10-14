//===- LaunchBoundsPropagation.cpp - Infer kernel launch bounds -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelBindingTable.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelCloning.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/NVVMAttributes.h"

#include <limits>

namespace mlir {
#define GEN_PASS_DEF_OFFLOADLAUNCHBOUNDSPROPAGATION
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

using namespace mlir;
using namespace cir;

namespace {

// Threads per block for one launch, or nullopt when the geometry was not traced
// or is not a compile-time value. Those are different answers -- see
// `LaunchSite::Dim3` -- but neither yields a bound, so both stop here. A
// zero-sized block is not a launch any bound could describe, so it stops here
// too.
static std::optional<uint64_t> constBlockSize(const cir::LaunchSite &site) {
  if (!site.hasGeometry())
    return std::nullopt;
  cir::LaunchSite::Dim3 block = site.getBlockDim();
  if (!block.isFullyConstant())
    return std::nullopt;

  auto asInt = [](mlir::TypedAttr a) -> std::optional<uint64_t> {
    auto i = mlir::dyn_cast_or_null<cir::IntAttr>(a);
    if (!i)
      return std::nullopt;
    return i.getUInt();
  };
  std::optional<uint64_t> x = asInt(block.constX());
  std::optional<uint64_t> y = asInt(block.constY());
  std::optional<uint64_t> z = asInt(block.constZ());
  if (!x || !y || !z)
    return std::nullopt;

  // `getOverallMaxNTID` takes the product of the vector, so a flattened scalar
  // and a 3-vector mean the same thing to the backend. A dim3 component is
  // 32-bit unsigned, so the product cannot overflow.
  uint64_t size = *x * *y * *z;
  if (!size)
    return std::nullopt;
  return size;
}

// Largest block any visible launch uses. Null when a site's geometry is opaque:
// an unseen launch may use a larger block, and the bound is a hard contract.
static std::optional<uint64_t>
maxBlockSize(llvm::ArrayRef<cir::LaunchSite> sites) {
  uint64_t maxSize = 0;
  for (const cir::LaunchSite &site : sites) {
    std::optional<uint64_t> size = constBlockSize(site);
    if (!size)
      return std::nullopt;
    maxSize = std::max(maxSize, *size);
  }
  return maxSize ? std::optional<uint64_t>(maxSize) : std::nullopt;
}

// The attribute CIRGen puts on every AMDGPU kernel, holding "min,max" flat
// work group sizes. `__launch_bounds__` sets it; otherwise it defaults to
// "1,<--gpu-max-threads-per-block>", which is 1024 unless the user narrowed it.
constexpr llvm::StringRef kFlatWorkGroupSizeAttr =
    "cir.amdgpu-flat-work-group-size";

struct FlatWorkGroupSize {
  uint64_t min, max;
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

// The NVPTX attr name has to match what CIRGen writes in Targets/NVPTX.cpp or
// the two mechanisms drift apart.
static std::string maxNTIDAttrName() {
  return ("cir." + llvm::NVVMAttr::MaxNTID).str();
}

// The largest block `kernel` currently promises to handle, or nullopt if its
// target has no launch bound this pass knows how to tighten. An NVPTX kernel
// without `__launch_bounds__` promises nothing, so it reports no limit.
static std::optional<uint64_t> currentLaunchBound(cir::FuncOp kernel) {
  switch (kernel.getCallingConv()) {
  case cir::CallingConv::PTXKernel: {
    auto existing = kernel->getAttrOfType<mlir::StringAttr>(maxNTIDAttrName());
    if (!existing)
      return std::numeric_limits<uint64_t>::max();
    uint64_t prev;
    if (!llvm::to_integer(existing.getValue(), prev, 10))
      return std::nullopt;
    return prev;
  }
  case cir::CallingConv::AMDGPUKernel: {
    std::optional<FlatWorkGroupSize> size = parseFlatWorkGroupSize(kernel);
    if (!size)
      return std::nullopt;
    return size->max;
  }
  default:
    return std::nullopt;
  }
}

// Tighten, never loosen: a user's __launch_bounds__ is a promise we may
// sharpen but must not widen.
static bool tightens(cir::FuncOp kernel, uint64_t bound) {
  std::optional<uint64_t> current = currentLaunchBound(kernel);
  return current && bound < *current;
}

// Stamp the target launch-bound attr on a kernel `tightens` accepted.
static void setLaunchBound(cir::FuncOp kernel, uint64_t bound) {
  mlir::MLIRContext *ctx = kernel.getContext();
  switch (kernel.getCallingConv()) {
  case cir::CallingConv::PTXKernel:
    kernel->setAttr(maxNTIDAttrName(),
                    mlir::StringAttr::get(ctx, llvm::utostr(bound)));
    return;
  case cir::CallingConv::AMDGPUKernel: {
    std::optional<FlatWorkGroupSize> current = parseFlatWorkGroupSize(kernel);
    assert(current && "tightens() accepted an unparsable flat work group size");
    // The minimum is the user's promise, not ours, so it is preserved --
    // except where it would now exceed the maximum, which no launch could
    // satisfy.
    uint64_t min = std::min(current->min, bound);
    kernel->setAttr(kFlatWorkGroupSizeAttr,
                    mlir::StringAttr::get(ctx, llvm::utostr(min) + "," +
                                                   llvm::utostr(bound)));
    return;
  }
  default:
    llvm_unreachable("tightens() accepted a kernel without a launch bound");
  }
}

struct OffloadLaunchBoundsPropagationPass
    : public impl::OffloadLaunchBoundsPropagationBase<
          OffloadLaunchBoundsPropagationPass> {
  void runOnOperation() override;
};

void OffloadLaunchBoundsPropagationPass::runOnOperation() {
  mlir::ModuleOp container = getOperation();
  // The pass is anchored on a module, but only an offload container holds the
  // host<->device correspondence it reasons about. Any other module is left
  // alone.
  if (!cir::isOffloadContainer(container))
    return;

  cir::KernelBindingTable &table = getAnalysis<cir::KernelBindingTable>();
  bool changed = false;

  for (const auto &entry : table) {
    const cir::KernelBinding &binding = entry.second;
    llvm::ArrayRef<cir::LaunchSite> sites = binding.launchSites;

    // A kernel launched nowhere in this TU says nothing about its geometry.
    if (sites.empty())
      continue;

    std::optional<uint64_t> bound = maxBlockSize(sites);
    if (!bound)
      continue;

    // Ask the originals whether the bound would say anything new before
    // deciding where to apply it, so no kernel is cloned for nothing.
    if (llvm::none_of(binding.deviceKernels, [&](cir::FuncOp kernel) {
          return !kernel.isDeclaration() && tightens(kernel, *bound);
        }))
      continue;

    // In place is only sound when every launch is visible: one this pass
    // cannot see may use a larger block, and exceeding the bound is a launch
    // failure rather than a slowdown. Otherwise the bound goes on a clone that
    // only the visible launches reach. Which of the two a kernel admits
    // depends on the target: a HIP kernel's handle global references its stub,
    // so its launches never all count as visible, while a CUDA kernel has no
    // handle global and so cannot be cloned.
    llvm::ArrayRef<cir::FuncOp> kernels = binding.deviceKernels;
    cir::SpecializationTarget target;
    if (!table.allLaunchSitesVisible(entry.first)) {
      target =
          cir::getSpecializationTarget(container, binding, ".bounds", sites);
      if (!target)
        continue;
      changed |= target.cloned;
      kernels = target.deviceKernels;
    }

    // The launch configuration is host-side, so one bound applies to every
    // arch's copy of the kernel even when __CUDA_ARCH__ made the bodies differ.
    for (cir::FuncOp kernel : kernels) {
      if (kernel.isDeclaration() || !tightens(kernel, *bound))
        continue;
      setLaunchBound(kernel, *bound);
      changed = true;
    }
  }

  if (!changed)
    markAllAnalysesPreserved();
}

} // namespace

std::unique_ptr<Pass> mlir::createOffloadLaunchBoundsPropagationPass() {
  return std::make_unique<OffloadLaunchBoundsPropagationPass>();
}
