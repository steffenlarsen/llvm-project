//===- OffloadAnnotateLoopInvariantArgs.cpp - Tag loop-invariant args ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Phase 1 of constant memory promotion: walks structured cir.for/cir.while
// loops to identify kernel launch arguments that are loop-invariant scalars.
//
// For each kernel with invariant args, this pass:
// 1. Annotates the launch op with "loop_invariant_args" (for Phase 2)
// 2. Inserts a cir.offload.memcpy_to_symbol op BEFORE the loop, carrying
//    the invariant values. This op survives FlattenCFG and is lowered to
//    hipMemcpyToSymbol by Phase 2 after merge.
//
// This runs BEFORE FlattenCFG so structured loops are visible.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Interfaces/CIRLoopOpInterface.h"

#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cir-offload-annotate-loop-invariant-args"

using namespace mlir;

namespace {

static bool isDefinedOutsideLoop(mlir::Value v, mlir::Operation *loopOp) {
  if (auto ba = dyn_cast<BlockArgument>(v))
    return !loopOp->isAncestor(ba.getOwner()->getParentOp());
  Operation *defOp = v.getDefiningOp();
  if (!defOp)
    return true;
  if (!loopOp->isAncestor(defOp))
    return true;

  if (auto loadOp = dyn_cast<cir::LoadOp>(defOp)) {
    mlir::Value addr = loadOp.getAddr();
    mlir::Operation *addrDef = addr.getDefiningOp();
    if (!addrDef || loopOp->isAncestor(addrDef))
      return false;
    for (mlir::OpOperand &use : addr.getUses()) {
      auto *user = use.getOwner();
      if (auto store = dyn_cast<cir::StoreOp>(user)) {
        if (store.getAddr() == addr && loopOp->isAncestor(store))
          return false;
      }
    }
    return true;
  }

  return false;
}

/// Get the value that dominates the loop for a loop-invariant operand.
/// If the operand is a cir.load from an alloca outside the loop, reload
/// from that alloca. Otherwise use the value directly if it dominates.
static mlir::Value getPreLoopValue(mlir::Value v, mlir::Operation *loopOp,
                                   OpBuilder &builder, mlir::Location loc) {
  Operation *defOp = v.getDefiningOp();
  if (!defOp)
    return v; // function arg — dominates everything

  if (!loopOp->isAncestor(defOp))
    return v; // defined outside the loop — dominates

  // Defined inside the loop (e.g., a load from an alloca outside).
  if (auto loadOp = dyn_cast<cir::LoadOp>(defOp)) {
    mlir::Value addr = loadOp.getAddr();
    if (addr.getDefiningOp() && !loopOp->isAncestor(addr.getDefiningOp()))
      return cir::LoadOp::create(builder, loc, addr).getResult();
  }

  return {}; // can't hoist
}

struct OffloadAnnotateLoopInvariantArgsPass
    : public PassWrapper<OffloadAnnotateLoopInvariantArgsPass,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      OffloadAnnotateLoopInvariantArgsPass)

  OffloadAnnotateLoopInvariantArgsPass() = default;
  OffloadAnnotateLoopInvariantArgsPass(bool enabled) : passEnabled(enabled) {}

  StringRef getArgument() const override {
    return "cir-offload-annotate-loop-invariant-args";
  }
  StringRef getDescription() const override {
    return "Annotate loop-invariant scalar kernel args and emit "
           "memcpy_to_symbol ops for constant memory promotion";
  }

  void runOnOperation() override {
    if (!passEnabled)
      return;

    ModuleOp module = getOperation();
    MLIRContext *ctx = module.getContext();

    // Track which kernels already have a memcpy_to_symbol op emitted
    // (avoid duplicates from nested loops or multiple launches).
    llvm::StringSet<> emittedKernels;

    module.walk([&](cir::LoopOpInterface loopOp) {
      Operation *loopOperation = loopOp.getOperation();

      loopOperation->walk([&](cir::OffloadKernelLaunchOp launch) {
        SmallVector<int32_t> invariantIndices;
        SmallVector<mlir::Value> invariantValues;

        for (auto [i, arg] : llvm::enumerate(launch.getKernelOperands())) {
          if (isa<cir::PointerType>(arg.getType()))
            continue;

          if (isDefinedOutsideLoop(arg, loopOperation)) {
            invariantIndices.push_back(i);
            invariantValues.push_back(arg);
          }
        }

        if (invariantIndices.empty())
          return;

        // Annotate the launch (for Phase 2's kernel rewrite).
        launch->setAttr("loop_invariant_args",
                        DenseI32ArrayAttr::get(ctx, invariantIndices));

        // Emit cir.offload.memcpy_to_symbol BEFORE the loop.
        // Only one per kernel (the first loop encountered wins).
        StringRef kernelRef =
            launch.getKernelLeafName();
        if (!emittedKernels.insert(kernelRef).second)
          return;

        OpBuilder preBuilder(ctx);
        preBuilder.setInsertionPoint(loopOperation);
        mlir::Location loc = loopOperation->getLoc();

        // Resolve pre-loop values (reload from allocas if needed).
        SmallVector<mlir::Value> preLoopValues;
        SmallVector<int32_t> validIndices;
        for (auto [idx, val] :
             llvm::zip(invariantIndices, invariantValues)) {
          mlir::Value preVal =
              getPreLoopValue(val, loopOperation, preBuilder, loc);
          if (preVal) {
            preLoopValues.push_back(preVal);
            validIndices.push_back(idx);
          }
        }

        if (preLoopValues.empty())
          return;

        cir::OffloadMemcpyToSymbolOp::create(
            preBuilder, loc, launch.getKernelAttr(),
            DenseI32ArrayAttr::get(ctx, validIndices), preLoopValues);

        LLVM_DEBUG(llvm::dbgs()
                   << "AnnotateLoopInvariantArgs: " << kernelRef << " — "
                   << validIndices.size()
                   << " args, memcpy_to_symbol emitted\n");
      });
    });
  }

  bool passEnabled = true;
};

} // namespace

std::unique_ptr<Pass>
mlir::createOffloadAnnotateLoopInvariantArgsPass(bool enabled) {
  return std::make_unique<OffloadAnnotateLoopInvariantArgsPass>(enabled);
}
