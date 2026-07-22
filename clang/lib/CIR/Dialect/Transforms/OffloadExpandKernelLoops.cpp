//===- OffloadExpandKernelLoops.cpp - Absorb host loops into kernels -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// When a cir.for loop body contains only kernel launches (and safe launch
// setup), this pass absorbs the loop into the kernel — replacing N host-side
// launches with a single launch where the kernel body includes the loop.
//
// This pass runs BEFORE FlattenCFG so it can work with structured cir.for ops.
//
// Expansion criteria (conservative):
//   1. Loop body contains exactly one cir.offload.kernel_launch.
//   2. No cir.call ops in the loop body (except dim3 constructors).
//   3. Grid/block dims are loop-invariant.
//   4. The loop induction variable flows into a kernel arg.
//   5. The kernel is defined in the offload module.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/CIRTransformUtils.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cir-offload-expand-kernel-loops"

using namespace mlir;

namespace {

/// Check if an op in the loop body is safe for expansion.
static bool isSafeLoopBodyOp(Operation *op) {
  if (isa<cir::OffloadKernelLaunchOp>(op))
    return true;

  // Structured ops that contain regions — recurse into them.
  if (isa<cir::ScopeOp, cir::IfOp>(op))
    return true;

  // Pure value-producing ops.
  if (isa<cir::LoadOp, cir::StoreOp, cir::ConstantOp, cir::CastOp,
          cir::GetMemberOp, cir::CmpOp, cir::AllocaOp, cir::SelectOp,
          cir::AddOp, cir::SubOp, cir::MulOp, cir::DivOp,
          cir::PtrStrideOp, cir::CopyOp>(op))
    return true;

  // Yield terminators.
  if (isa<cir::YieldOp, cir::ConditionOp>(op))
    return true;

  // dim3 constructor calls are launch setup.
  if (auto call = dyn_cast<cir::CallOp>(op)) {
    if (auto callee = call.getCalleeAttr()) {
      StringRef name = callee.getValue();
      if (name.starts_with("_ZN4dim3C"))
        return true;
    }
  }

  return false;
}

/// Recursively check if all ops in a region are safe.
static bool allOpsSafe(Region &region) {
  for (Block &block : region) {
    for (Operation &op : block) {
      if (!isSafeLoopBodyOp(&op))
        return false;
      for (Region &nested : op.getRegions()) {
        if (!allOpsSafe(nested))
          return false;
      }
    }
  }
  return true;
}

struct OffloadExpandKernelLoopsPass
    : public PassWrapper<OffloadExpandKernelLoopsPass,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(OffloadExpandKernelLoopsPass)

  OffloadExpandKernelLoopsPass() = default;
  OffloadExpandKernelLoopsPass(bool enabled) : passEnabled(enabled) {}

  StringRef getArgument() const override {
    return "cir-offload-expand-kernel-loops";
  }
  StringRef getDescription() const override {
    return "Absorb host loops around kernel launches into the kernel";
  }

  void runOnOperation() override {
    if (!passEnabled)
      return;

    ModuleOp module = getOperation();

    // Find expandable cir.for loops.
    SmallVector<cir::ForOp> candidates;
    module.walk([&](cir::ForOp forOp) {
      // Check: body contains at least one kernel_launch.
      SmallVector<cir::OffloadKernelLaunchOp> launches;
      forOp.getBody().walk([&](cir::OffloadKernelLaunchOp launch) {
        launches.push_back(launch);
      });

      if (launches.empty())
        return;

      if (launches.size() > 1) {
        LLVM_DEBUG(llvm::dbgs()
                   << "OffloadExpandKernelLoops: skipping loop with "
                   << launches.size() << " launches (multi-launch NYI)\n");
        return;
      }

      // Check: all ops in the body are safe (no side-effecting calls).
      if (!allOpsSafe(forOp.getBody())) {
        LLVM_DEBUG(llvm::dbgs()
                   << "OffloadExpandKernelLoops: skipping loop with "
                   << "unsafe body ops\n");
        return;
      }
      candidates.push_back(forOp);
    });

    for (auto forOp : candidates)
      expandLoop(module, forOp);
  }

  void expandLoop(ModuleOp module, cir::ForOp forOp) {
    MLIRContext *ctx = module.getContext();

    // Find the kernel launch in the body.
    cir::OffloadKernelLaunchOp launch;
    forOp.getBody().walk([&](cir::OffloadKernelLaunchOp op) { launch = op; });
    if (!launch)
      return;

    // Find the loop variable alloca from the cond region.
    Value loopVarAlloca;
    for (auto &op : forOp.getCond().front()) {
      if (auto load = dyn_cast<cir::LoadOp>(op))
        loopVarAlloca = load.getAddr();
    }
    if (!loopVarAlloca)
      return;

    // Find which kernel operand loads from the loop variable.
    int loopArgIdx = -1;
    for (auto [i, arg] : llvm::enumerate(launch.getKernelOperands())) {
      if (auto load = arg.getDefiningOp<cir::LoadOp>()) {
        if (load.getAddr() == loopVarAlloca) {
          loopArgIdx = i;
          break;
        }
      }
    }
    if (loopArgIdx < 0)
      return;

    // Find the loop bound from the cond region.
    std::optional<int64_t> loopBound;
    for (auto &op : forOp.getCond().front()) {
      if (auto constOp = dyn_cast<cir::ConstantOp>(op)) {
        loopBound = cir::tryResolveToConstant(constOp);
      }
    }
    if (!loopBound)
      return;

    // Find the kernel function definition.
    StringRef kernelName = launch.getKernel().getLeafReference().getValue();
    cir::OffloadFuncOp kernel;
    cir::OffloadModuleOp kernelMod;
    module.walk([&](cir::OffloadModuleOp mod) {
      if (auto fn = mod.lookupSymbol<cir::OffloadFuncOp>(kernelName))
        if (!fn.isExternal()) { kernel = fn; kernelMod = mod; }
    });
    if (!kernel || !kernelMod)
      return;

    // Create expanded kernel name.
    std::string expandedName = (kernelName + "$expanded").str();
    if (kernelMod.lookupSymbol(expandedName))
      return;

    // The expanded kernel replaces the loop-variable arg with a loop-bound
    // arg. Its body wraps the original body in a cir.for loop.
    //
    // Original: kernel(a, n, iter) { body using iter }
    // Expanded: kernel$expanded(a, n, loop_bound) {
    //             for (k = 0; k < loop_bound; k++) { body using k }
    //           }

    auto origFnTy = kernel.getFunctionType();
    auto loopVarType = origFnTy.getInputs()[loopArgIdx];

    // Clone the kernel.
    auto *cloneOp = kernel->clone();
    auto clone = cast<cir::OffloadFuncOp>(cloneOp);
    SymbolTable::setSymbolName(clone, expandedName);
    {
      OpBuilder insertBuilder(ctx);
      insertBuilder.setInsertionPointToEnd(&kernelMod.getBody().front());
      insertBuilder.insert(clone);
    }

    Block &entry = clone.getBody().front();
    Value iterArg = entry.getArgument(loopArgIdx);

    // Collect all original ops from the entry block (excluding terminator).
    SmallVector<Operation *> originalOps;
    for (auto &op : entry.without_terminator())
      originalOps.push_back(&op);

    // Create loop counter alloca and init at the TOP of the entry block.
    OpBuilder builder(ctx);
    builder.setInsertionPointToStart(&entry);
    auto ptrTy = cir::PointerType::get(ctx, loopVarType);
    auto counterAlloca = cir::AllocaOp::create(
        builder, clone.getLoc(), ptrTy, "loop_k",
        builder.getI64IntegerAttr(4));
    auto zero = cir::ConstantOp::create(
        builder, clone.getLoc(), cir::IntAttr::get(loopVarType, 0));
    cir::StoreOp::create(builder, clone.getLoc(), zero, counterAlloca);

    // Create the cir.for loop. The body builder callback moves the
    // original kernel ops into the loop body.
    auto forLoop = cir::ForOp::create(
        builder, clone.getLoc(),
        /*condBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          auto loadK = cir::LoadOp::create(b, loc, counterAlloca.getResult());
          auto cmpResult = cir::CmpOp::create(
              b, loc, cir::BoolType::get(ctx),
              cir::CmpOpKind::lt, loadK, iterArg);
          cir::ConditionOp::create(b, loc, cmpResult);
        },
        /*bodyBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          // Load the loop counter — this replaces the iter arg.
          auto loadK = cir::LoadOp::create(b, loc, counterAlloca.getResult());

          // Replace all uses of the original iter arg with the counter.
          iterArg.replaceAllUsesWith(loadK);

          // Move all original kernel ops into the for body.
          Block *bodyBlock = b.getInsertionBlock();
          for (auto *op : originalOps)
            op->moveBefore(bodyBlock, bodyBlock->end());

          cir::YieldOp::create(b, loc);
        },
        /*stepBuilder=*/
        [&](mlir::OpBuilder &b, mlir::Location loc) {
          auto loadK = cir::LoadOp::create(b, loc, counterAlloca.getResult());
          auto one = cir::ConstantOp::create(
              b, loc, cir::IntAttr::get(loopVarType, 1));
          auto inc = cir::AddOp::create(b, loc, loadK, one);
          cir::StoreOp::create(b, loc, inc, counterAlloca);
          cir::YieldOp::create(b, loc);
        });

    // Replace the HOST cir.for with a single launch to the expanded kernel.
    // Grid/block dims and non-loop kernel args must be re-derived outside
    // the for loop since the original SSA values are inside the loop body.
    OpBuilder hostBuilder(ctx);
    hostBuilder.setInsertionPoint(forOp);
    mlir::Location loc = forOp.getLoc();

    // Re-derive grid/block dims as constants (trace from launch operands).
    auto makeConstU32 = [&](Value origVal) -> Value {
      auto c = cir::tryResolveToConstant(origVal);
      if (c)
        return cir::ConstantOp::create(
            hostBuilder, loc,
            cir::IntAttr::get(origVal.getType(), *c));
      // Fallback: if not constant, this expansion isn't safe.
      return Value();
    };

    Value gx = makeConstU32(launch.getGridSizeX());
    Value gy = makeConstU32(launch.getGridSizeY());
    Value gz = makeConstU32(launch.getGridSizeZ());
    Value bx = makeConstU32(launch.getBlockSizeX());
    Value by = makeConstU32(launch.getBlockSizeY());
    Value bz = makeConstU32(launch.getBlockSizeZ());

    if (!gx || !gy || !gz || !bx || !by || !bz) {
      clone->erase();
      return;
    }

    // Re-derive kernel args. For the loop variable, use the loop bound.
    // For other args, trace to their source alloca and reload.
    SmallVector<Value> newLaunchArgs;
    for (auto [i, arg] : llvm::enumerate(launch.getKernelOperands())) {
      if ((int)i == loopArgIdx) {
        auto boundVal = cir::ConstantOp::create(
            hostBuilder, loc,
            cir::IntAttr::get(loopVarType, *loopBound));
        newLaunchArgs.push_back(boundVal);
      } else {
        // Trace to the source alloca and reload outside the loop.
        auto traceResult = cir::traceValueOrigin(arg);
        if (auto load = dyn_cast_or_null<cir::LoadOp>(
                arg.getDefiningOp())) {
          // The load's address is an alloca in the enclosing scope.
          Value addr = load.getAddr();
          if (addr.getParentBlock() == forOp->getBlock() ||
              addr.getParentBlock()->getParentOp() ==
                  forOp->getParentOp()) {
            auto newLoad = cir::LoadOp::create(
                hostBuilder, loc, addr);
            newLaunchArgs.push_back(newLoad);
          } else {
            clone->erase();
            return;
          }
        } else {
          clone->erase();
          return;
        }
      }
    }

    // Create the replacement launch.
    StringRef modName = launch.getKernel().getRootReference().getValue();
    cir::OffloadKernelLaunchOp::create(
        hostBuilder, loc,
        SymbolRefAttr::get(ctx, modName,
                           {FlatSymbolRefAttr::get(ctx, expandedName)}),
        gx, gy, gz, bx, by, bz,
        /*dynamicSharedMemorySize=*/Value(),
        newLaunchArgs,
        /*stream=*/Value());

    // Erase the host for loop (and everything inside it).
    forOp->erase();
  }

  bool passEnabled = true;
};

} // namespace

std::unique_ptr<Pass> mlir::createOffloadExpandKernelLoopsPass(bool enabled) {
  return std::make_unique<OffloadExpandKernelLoopsPass>(enabled);
}
