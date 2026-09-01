//===- PropagateBlockShape.cpp - Constant block dimensions ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/Builders.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/DeviceIndex.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelBindingTable.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelCloning.h"

namespace mlir {
#define GEN_PASS_DEF_OFFLOADPROPAGATEBLOCKSHAPE
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

using namespace mlir;
using namespace cir;

namespace {

// The block dimensions every site agrees on, indexed by dimension. A component
// is null when a site left it non-constant or the sites disagree; the three are
// independent, so a launch with a constant x and a runtime y still yields x.
using BlockShape = std::array<mlir::TypedAttr, 3>;

static BlockShape commonBlockShape(llvm::ArrayRef<cir::LaunchSite> sites) {
  BlockShape common;
  bool first = true;
  for (const cir::LaunchSite &site : sites) {
    if (!site.hasGeometry())
      return {};
    cir::LaunchSite::Dim3 block = site.getBlockDim();
    mlir::TypedAttr dims[3] = {block.constX(), block.constY(), block.constZ()};
    for (unsigned d = 0; d != 3; ++d) {
      if (first)
        common[d] = dims[d];
      else if (common[d] != dims[d])
        common[d] = {};
    }
    first = false;
  }
  return common;
}

// The blockDim reads in `kernel` this pass could replace given `shape`, paired
// with the constant to replace each by.
//
// Collected before anything is rewritten so the caller can tell whether a
// kernel is worth cloning: a clone with nothing to specialise is pure cost.
using BlockDimUses = llvm::SmallVector<std::pair<cir::CallOp, cir::IntAttr>, 8>;

static BlockDimUses findBlockDimReads(cir::FuncOp kernel,
                                      const BlockShape &shape) {
  BlockDimUses uses;
  if (kernel.isDeclaration())
    return uses;
  kernel.walk([&](cir::CallOp call) {
    std::optional<cir::DeviceIndex> index = cir::matchDeviceIndex(call);
    if (!index || index->kind != cir::DeviceIndexKind::BlockDim)
      return;
    auto value = mlir::dyn_cast_or_null<cir::IntAttr>(shape[index->dim]);
    if (!value || call.getNumResults() != 1)
      return;
    auto resultTy = mlir::dyn_cast<cir::IntType>(call.getResult().getType());
    if (!resultTy)
      return;
    // The read's type is the kernel's business, not the launch's; a dimension
    // that does not fit in it is not something to silently truncate.
    if (value.getValue().getActiveBits() > resultTy.getWidth())
      return;
    uses.emplace_back(call, value);
  });
  return uses;
}

// A read returns whatever type the call was declared with -- the accessor gives
// `unsigned`, the device-library entry point `size_t` -- while the constant
// comes from the host-side dim3, which is always 32-bit unsigned. The value is
// therefore re-widened to the read's own type; a block dimension is unsigned
// and fits in 32 bits, so zero-extending it is exact.
static void replaceBlockDimReads(const BlockDimUses &uses) {
  for (auto [call, value] : uses) {
    auto resultTy = mlir::cast<cir::IntType>(call.getResult().getType());
    llvm::APInt widened = value.getValue().zextOrTrunc(resultTy.getWidth());
    mlir::OpBuilder builder(call);
    auto constant = cir::ConstantOp::create(
        builder, call.getLoc(), resultTy, cir::IntAttr::get(resultTy, widened));
    call.getResult().replaceAllUsesWith(constant);
    call.erase();
  }
}

struct OffloadPropagateBlockShapePass
    : public impl::OffloadPropagateBlockShapeBase<
          OffloadPropagateBlockShapePass> {
  void runOnOperation() override;
};

void OffloadPropagateBlockShapePass::runOnOperation() {
  cir::OffloadContainerOp container = getOperation();
  cir::KernelBindingTable &table = getAnalysis<cir::KernelBindingTable>();
  bool changed = false;

  for (const auto &entry : table) {
    const cir::KernelBinding &binding = entry.second;
    llvm::ArrayRef<cir::LaunchSite> sites = binding.launchSites;

    // A kernel launched nowhere in this TU says nothing about its geometry.
    if (sites.empty())
      continue;

    BlockShape shape = commonBlockShape(sites);
    if (!shape[0] && !shape[1] && !shape[2])
      continue;

    // Ask the originals whether there is anything to do before deciding how to
    // do it, so a kernel that never reads blockDim is not cloned for nothing.
    if (llvm::none_of(binding.deviceKernels, [&](cir::FuncOp kernel) {
          return !findBlockDimReads(kernel, shape).empty();
        }))
      continue;

    cir::SpecializationTarget target = cir::getSpecializationTarget(
        container, entry.first, binding, ".blockshape", sites);
    if (!target)
      continue;
    llvm::ArrayRef<cir::FuncOp> targets = target.deviceKernels;

    for (cir::FuncOp kernel : targets)
      replaceBlockDimReads(findBlockDimReads(kernel, shape));
    changed = true;
  }

  if (!changed)
    markAllAnalysesPreserved();
}

} // namespace

std::unique_ptr<Pass> mlir::createOffloadPropagateBlockShapePass() {
  return std::make_unique<OffloadPropagateBlockShapePass>();
}
