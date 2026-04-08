//===- OffloadPropagateGridCoverage.cpp - Grid-covers-arg annotation ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// For each cir.offload.func kernel clone that has a known_block_size (from
// TightenLaunchBounds), this pass inspects the grid dimension operands at
// each launch site to detect the pattern:
//
//   gridSizeX = ceil(N / blockSizeX)
//
// where N traces back to a kernel argument.  When this holds, the grid total
// (gridDim.x * blockDim.x) is guaranteed >= N.  This fact is recorded as a
// "grid_covers_args" attribute on the kernel clone, listing the covered
// argument indices.
//
// The lowering pass (ConvertCIRInGpuModulePass or the ROCDL serializer) then
// emits `llvm.assume(gridDim.x * blockDim.x >= arg[i])` at kernel entry,
// giving LLVM's ScalarEvolution enough information to prove grid-stride loops
// have trip count <= 1 and enabling LoopFullUnroll to eliminate them.
//
// This is a CIR-unique optimization: OGCG cannot do it because the grid-total
// >= N relationship is only visible at the host launch site.
//
// Must run after TightenLaunchBounds and SpecializeScalarArgs, before
// ConvertCIROffloadToGPU.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/CIRTransformUtils.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cir-offload-propagate-grid-coverage"

using namespace mlir;

namespace {

struct OffloadPropagateGridCoveragePass
    : public PassWrapper<OffloadPropagateGridCoveragePass,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      OffloadPropagateGridCoveragePass)

  OffloadPropagateGridCoveragePass() = default;
  OffloadPropagateGridCoveragePass(bool enabled) : passEnabled(enabled) {}

  StringRef getArgument() const override {
    return "cir-offload-propagate-grid-coverage";
  }
  StringRef getDescription() const override {
    return "Annotate kernels where grid_total >= kernel arg (grid-stride "
           "loop elimination)";
  }

  void runOnOperation() override {
    if (!passEnabled)
      return;

    ModuleOp module = getOperation();

    module.walk([&](cir::OffloadModuleOp offloadMod) {
      if (offloadMod.getBody().empty())
        return;
      SymbolTable symTable(offloadMod);
      SmallVector<cir::OffloadFuncOp> kernels;
      offloadMod.walk([&](cir::OffloadFuncOp fn) {
        if (fn.isKernel() && !fn.isExternal())
          kernels.push_back(fn);
      });

      for (auto kernel : kernels)
        processKernel(module, symTable, kernel);
    });
  }

  void processKernel(ModuleOp module, SymbolTable &symTable,
                     cir::OffloadFuncOp kernel) {
    // Only process clones that have known_block_size.
    auto knownSize =
        kernel->getAttrOfType<DenseI32ArrayAttr>("known_block_size");
    if (!knownSize)
      return;
    auto elems = knownSize.asArrayRef();
    if (elems.empty())
      return;

    int64_t blockSizeX = elems[0];
    if (blockSizeX <= 0)
      return;

    // Gather launch ops targeting this kernel.
    SmallVector<cir::OffloadKernelLaunchOp> launchOps;
    module.walk([&](cir::OffloadKernelLaunchOp op) {
      if (op.getKernel().getLeafReference() == kernel.getSymName())
        launchOps.push_back(op);
    });

    if (launchOps.empty())
      return;

    // For each launch, check if gridSizeX = ceil(arg / blockSizeX).
    // We intersect covered args across all launch sites.
    llvm::DenseSet<unsigned> coveredArgs;
    bool firstLaunch = true;

    for (auto launch : launchOps) {
      llvm::DenseSet<unsigned> siteCovered;

      Value gridSizeX = launch.getGridSizeX();

      // Trace gridSizeX backward to find ceil-div pattern.
      // First, trace through the dim3 chain to get the actual grid.x value
      // passed to the dim3 constructor.
      auto gridTrace = cir::traceValueOrigin(gridSizeX);
      Value gridExpr = gridSizeX;
      if (gridTrace.kind == cir::ValueTraceResult::Dim3CtorArg)
        gridExpr = gridTrace.terminal;

      // Match ceil-div: gridExpr = (N + blockSizeX - 1) / blockSizeX
      Value dividend;
      auto divisor = cir::matchCeilDiv(gridExpr, dividend);
      if (!divisor || *divisor != blockSizeX) {
        // If any launch doesn't match, intersection is empty.
        coveredArgs.clear();
        return;
      }

      // Trace dividend back to a kernel arg index.
      auto argIdx = cir::traceToKernelArgIndex(dividend, launch);
      if (argIdx) {
        siteCovered.insert(*argIdx);
        LLVM_DEBUG(llvm::dbgs()
                   << "  " << kernel.getSymName() << ": gridSizeX covers arg["
                   << *argIdx << "]\n");
      }

      // Intersect with running set.
      if (firstLaunch) {
        coveredArgs = std::move(siteCovered);
        firstLaunch = false;
      } else {
        llvm::DenseSet<unsigned> intersection;
        for (unsigned idx : coveredArgs) {
          if (siteCovered.contains(idx))
            intersection.insert(idx);
        }
        coveredArgs = std::move(intersection);
      }
    }

    if (coveredArgs.empty())
      return;

    // Set grid_covers_args attribute on the kernel.
    SmallVector<int32_t> sorted(coveredArgs.begin(), coveredArgs.end());
    llvm::sort(sorted);

    MLIRContext *ctx = module.getContext();
    kernel->setAttr("grid_covers_args",
                    DenseI32ArrayAttr::get(ctx, sorted));

    LLVM_DEBUG({
      llvm::dbgs() << "OffloadPropagateGridCoverage: " << kernel.getSymName()
                   << " covers args [";
      for (unsigned i = 0; i < sorted.size(); ++i) {
        if (i > 0) llvm::dbgs() << ", ";
        llvm::dbgs() << sorted[i];
      }
      llvm::dbgs() << "]\n";
    });
  }

  bool passEnabled = true;
};

} // namespace

std::unique_ptr<mlir::Pass>
mlir::createOffloadPropagateGridCoveragePass(bool enabled) {
  return std::make_unique<OffloadPropagateGridCoveragePass>(enabled);
}
