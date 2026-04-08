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

      for (auto kernel : kernels) {
        processKernel(module, symTable, kernel);
        processGridStridedDims(module, kernel);
        multiversionCappedDims(module, symTable, offloadMod, kernel);
      }
    });
  }

  /// Build the host-side test `(u64)arg <= (u64)gridDim * divisor`.
  ///
  /// This is the very fact the clone will assume, evaluated where both values
  /// are in hand, so guard and assumption cannot drift apart.  Widening to
  /// unsigned 64 bits also disposes of a negative argument for free: it maps
  /// to a huge unsigned value, the test fails, and the launch takes the
  /// unannotated original.
  Value buildCoverageGuard(OpBuilder &builder, Location loc, Value gridDim,
                           Value arg, int64_t divisor) {
    MLIRContext *ctx = builder.getContext();
    auto u64Ty = cir::IntType::get(ctx, 64, /*isSigned=*/false);
    auto boolTy = cir::BoolType::get(ctx);

    auto widen = [&](Value v) -> Value {
      if (v.getType() == u64Ty)
        return v;
      return cir::CastOp::create(builder, loc, u64Ty, cir::CastKind::integral,
                                 v)
          .getResult();
    };

    Value bound = widen(gridDim);
    if (divisor != 1) {
      auto divConst = cir::ConstantOp::create(
          builder, loc, cir::IntAttr::get(u64Ty, divisor));
      bound = cir::MulOp::create(builder, loc, u64Ty, bound,
                                 divConst.getResult())
                  .getResult();
    }
    return cir::CmpOp::create(builder, loc, boolTy, cir::CmpOpKind::le,
                              widen(arg), bound)
        .getResult();
  }

  /// Whether to clone a kernel and dispatch to it when a clamped grid
  /// relation happens to hold.
  ///
  /// Off, on measurement rather than on doubt.
  ///
  /// The transform is sound and does what it claims: the clone is entered only
  /// when the host test passes, and the resulting assumption does remove the
  /// grid-strided loop. It was measured on a transformer prompt-processing
  /// workload where it reaches the dominant kernel, and it cost roughly half a
  /// percent -- a small but reproducible loss, well outside run-to-run spread.
  ///
  /// The loop it removes runs once, so there was little to win, while the
  /// clone doubles that kernel's device code and every launch gains a test.
  /// Worth revisiting for kernels whose grid-strided loop actually iterates,
  /// where the same fact would be worth much more.
  static constexpr bool kEnableCappedMultiversion = false;

  /// Give clamped grid relations a guarded fast path.
  ///
  /// A clamped extent bounds the argument only on the launches where the clamp
  /// did not bite, which is a runtime property, so the fact cannot simply be
  /// asserted.  Instead the kernel is cloned with the fact attached and the
  /// launch tests the condition, taking the clone when it holds and the
  /// untouched original otherwise.
  void multiversionCappedDims(ModuleOp module, SymbolTable &symTable,
                              cir::OffloadModuleOp offloadMod,
                              cir::OffloadFuncOp kernel) {
    if (!kEnableCappedMultiversion)
      return;
    // A clone of a clone gains nothing and would recurse on later runs.
    if (kernel.getSymName().contains("$gridcov"))
      return;

    SmallVector<cir::OffloadKernelLaunchOp> launchOps;
    module.walk([&](cir::OffloadKernelLaunchOp op) {
      if (op.getKernelLeafName() == kernel.getSymName())
        launchOps.push_back(op);
    });
    if (launchOps.empty())
      return;

    struct DimFact {
      unsigned dim;
      unsigned argIndex;
      int64_t divisor;
    };

    // Every launch must agree, otherwise one clone cannot serve them all.
    SmallVector<DimFact> facts;
    bool first = true;
    for (auto launch : launchOps) {
      Value dims[] = {launch.getGridSizeY(), launch.getGridSizeZ()};
      SmallVector<DimFact> siteFacts;
      for (unsigned i = 0; i < 2; ++i) {
        // Permissive on purpose: the relation is tested at run time below,
        // so a candidate that turns out not to hold simply takes the slow
        // path.  Skip dimensions the strict matcher already proved, since
        // those carry the fact unconditionally and need no clone.
        if (auto proven = cir::matchGridDimRelation(dims[i], launch))
          if (!proven->isCapped())
            continue;
        auto rel = cir::findGridDimCandidate(dims[i], launch);
        LLVM_DEBUG(llvm::dbgs()
                   << "  cand " << kernel.getSymName() << " dim" << (i + 1)
                   << ": " << (rel ? "arg" + std::to_string(rel->argIndex) +
                                         "/div" + std::to_string(rel->divisor)
                                   : std::string("none"))
                   << "\n");
        if (!rel)
          continue;
        siteFacts.push_back(DimFact{i + 1, rel->argIndex, rel->divisor});
      }
      if (first) {
        facts = std::move(siteFacts);
        first = false;
        continue;
      }
      if (facts.size() != siteFacts.size())
        return;
      for (unsigned i = 0; i < facts.size(); ++i)
        if (facts[i].dim != siteFacts[i].dim ||
            facts[i].argIndex != siteFacts[i].argIndex ||
            facts[i].divisor != siteFacts[i].divisor)
          return;
    }
    if (facts.empty())
      return;

    MLIRContext *ctx = module.getContext();
    std::string cloneName = (kernel.getSymName() + "$gridcov").str();
    auto clone = dyn_cast_or_null<cir::OffloadFuncOp>(symTable.lookup(cloneName));
    if (!clone) {
      auto *cloneOp = kernel->clone();
      clone = cast<cir::OffloadFuncOp>(cloneOp);
      SymbolTable::setSymbolName(clone, cloneName);
      SmallVector<int32_t> flat;
      for (const DimFact &f : facts) {
        flat.push_back(static_cast<int32_t>(f.dim));
        flat.push_back(static_cast<int32_t>(f.argIndex));
        flat.push_back(static_cast<int32_t>(f.divisor));
      }
      clone->setAttr("grid_covers_dims", DenseI32ArrayAttr::get(ctx, flat));
      symTable.insert(clone);
      clone->moveAfter(kernel);
      LLVM_DEBUG(llvm::dbgs() << "OffloadPropagateGridCoverage: created "
                              << cloneName << "\n");
    }

    StringRef offloadModName = offloadMod.getSymName();
    for (auto launch : launchOps) {
      StringRef launchModName =
          launch.getKernelAttr().getRootReference().getValue();
      // The launch may name a different module than the one holding the
      // definition; that module needs a declaration to refer to.
      if (launchModName != offloadModName) {
        auto launchMod =
            module.lookupSymbol<cir::OffloadModuleOp>(launchModName);
        if (launchMod && !launchMod.lookupSymbol(cloneName)) {
          if (auto origDecl = launchMod.lookupSymbol<cir::OffloadFuncOp>(
                  kernel.getSymName())) {
            auto *declClone = origDecl->clone();
            SymbolTable::setSymbolName(declClone, cloneName);
            OpBuilder declBuilder(ctx);
            declBuilder.setInsertionPointToEnd(&launchMod.getBody().front());
            declBuilder.insert(declClone);
          }
        }
      }

      Location loc = launch.getLoc();
      OpBuilder builder(launch);
      Value gridDims[] = {launch.getGridSizeY(), launch.getGridSizeZ()};
      mlir::OperandRange kernelArgs = launch.getKernelOperands();

      Value cond;
      bool bailed = false;
      for (const DimFact &f : facts) {
        if (f.argIndex >= kernelArgs.size() ||
            !mlir::isa<cir::IntType>(kernelArgs[f.argIndex].getType())) {
          bailed = true;
          break;
        }
        Value test = buildCoverageGuard(builder, loc, gridDims[f.dim - 1],
                                        kernelArgs[f.argIndex], f.divisor);
        // Conjunction via ternary, matching how && reaches CIR and keeping
        // the original launch to a single copy.
        cond = cond ? cir::TernaryOp::create(
                          builder, loc, cond,
                          [&](OpBuilder &b, Location l) {
                            cir::YieldOp::create(b, l, test);
                          },
                          [&](OpBuilder &b, Location l) {
                            auto f = cir::ConstantOp::create(
                                b, l, cir::BoolAttr::get(
                                          ctx, cir::BoolType::get(ctx), false));
                            cir::YieldOp::create(b, l, f.getResult());
                          })
                          .getResult()
                    : test;
      }
      if (bailed || !cond)
        continue;

      // This pass runs after CIRFlattenCFG, so the dispatch has to be built
      // as blocks and a conditional branch; a structured cir.if would reach
      // the lowering unlegalized.
      Block *entry = launch->getBlock();
      Block *tail = entry->splitBlock(std::next(launch->getIterator()));
      Block *fast = builder.createBlock(tail);
      Block *slow = builder.createBlock(tail);

      {
        OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToEnd(fast);
        auto *fastLaunch = builder.clone(*launch.getOperation());
        cast<cir::OffloadKernelLaunchOp>(fastLaunch).setKernelAttr(
            SymbolRefAttr::get(ctx, launchModName,
                               {FlatSymbolRefAttr::get(ctx, cloneName)}));
        cir::BrOp::create(builder, loc, tail);
      }
      {
        OpBuilder::InsertionGuard guard(builder);
        launch->moveBefore(slow, slow->end());
        builder.setInsertionPointToEnd(slow);
        cir::BrOp::create(builder, loc, tail);
      }
      {
        OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToEnd(entry);
        cir::BrCondOp::create(builder, loc, cond, fast, slow);
      }
    }
  }

  /// Record `gridDim.<d> * divisor >= arg[i]` for the Y and Z dimensions.
  ///
  /// Kept separate from processKernel, which handles X: there the fact is
  /// phrased in terms of blockDim.x so it lines up with a grid-stride loop
  /// whose stride is `gridDim.x * blockDim.x`, and it therefore needs a known
  /// block size.  Loops strided over Y or Z step by the grid extent alone, so
  /// the relation needs no block size and applies to unspecialized kernels
  /// too.
  ///
  /// Only uncapped relations are recorded.  A clamped grid extent, which is
  /// how these launches usually spell the 65535-per-dimension limit, does not
  /// bound the argument at all on the launches where the clamp bit.
  void processGridStridedDims(ModuleOp module, cir::OffloadFuncOp kernel) {
    SmallVector<cir::OffloadKernelLaunchOp> launchOps;
    module.walk([&](cir::OffloadKernelLaunchOp op) {
      if (op.getKernelLeafName() == kernel.getSymName())
        launchOps.push_back(op);
    });
    if (launchOps.empty())
      return;

    // dim -> (argIndex, divisor), agreed across every launch site.
    struct DimFact {
      unsigned argIndex;
      int64_t divisor;
    };
    llvm::SmallDenseMap<unsigned, DimFact> facts;
    bool first = true;

    for (auto launch : launchOps) {
      Value dims[] = {launch.getGridSizeY(), launch.getGridSizeZ()};
      llvm::SmallDenseMap<unsigned, DimFact> siteFacts;
      for (unsigned i = 0; i < 2; ++i) {
        unsigned dim = i + 1; // y = 1, z = 2
        auto rel = cir::matchGridDimRelation(dims[i], launch);
        if (!rel || rel->isCapped())
          continue;
        siteFacts[dim] = DimFact{rel->argIndex, rel->divisor};
      }

      if (first) {
        facts = std::move(siteFacts);
        first = false;
        continue;
      }
      // Keep only the dimensions every site agrees on, argument and divisor
      // included -- a fact that holds at one launch says nothing about
      // another.
      llvm::SmallDenseMap<unsigned, DimFact> agreed;
      for (auto &[dim, fact] : facts) {
        auto it = siteFacts.find(dim);
        if (it != siteFacts.end() && it->second.argIndex == fact.argIndex &&
            it->second.divisor == fact.divisor)
          agreed[dim] = fact;
      }
      facts = std::move(agreed);
      if (facts.empty())
        return;
    }

    if (facts.empty())
      return;

    SmallVector<unsigned> dimsSorted;
    for (auto &[dim, fact] : facts)
      dimsSorted.push_back(dim);
    llvm::sort(dimsSorted);

    SmallVector<int32_t> flat;
    for (unsigned dim : dimsSorted) {
      const DimFact &fact = facts[dim];
      flat.push_back(static_cast<int32_t>(dim));
      flat.push_back(static_cast<int32_t>(fact.argIndex));
      flat.push_back(static_cast<int32_t>(fact.divisor));
      LLVM_DEBUG(llvm::dbgs()
                 << "OffloadPropagateGridCoverage: " << kernel.getSymName()
                 << ": gridDim." << "xyz"[dim] << " * " << fact.divisor
                 << " >= arg[" << fact.argIndex << "]\n");
    }
    kernel->setAttr("grid_covers_dims",
                    DenseI32ArrayAttr::get(module.getContext(), flat));
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
      if (op.getKernelLeafName() == kernel.getSymName())
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

      // Report what the generalized matcher sees on every dimension, which
      // is a superset of what the X-only rule below can act on.  The Y and Z
      // relations, and the clamped forms, are what a grid-stride loop needs;
      // they are recognised here but not yet turned into facts.
      LLVM_DEBUG({
        static constexpr llvm::StringRef dimName[] = {"x", "y", "z"};
        Value dims[] = {launch.getGridSizeX(), launch.getGridSizeY(),
                        launch.getGridSizeZ()};
        for (unsigned d = 0; d < 3; ++d) {
          auto rel = cir::matchGridDimRelation(dims[d], launch);
          if (!rel)
            continue;
          llvm::dbgs() << "  " << kernel.getSymName() << ": grid." << dimName[d]
                       << " = ";
          if (rel->isCapped())
            llvm::dbgs() << "min(";
          if (rel->divisor != 1)
            llvm::dbgs() << "ceilDiv(arg[" << rel->argIndex << "], "
                         << rel->divisor << ")";
          else
            llvm::dbgs() << "arg[" << rel->argIndex << "]";
          if (rel->isCapped())
            llvm::dbgs() << ", " << *rel->cap << ")";
          llvm::dbgs() << "\n";
        }
      });

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
