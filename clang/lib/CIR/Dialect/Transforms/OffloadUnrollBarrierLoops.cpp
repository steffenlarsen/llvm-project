//===- OffloadUnrollBarrierLoops.cpp - Unroll reduction loops with barriers ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Two-part optimisation for GPU reduction loops:
//
// Part 1 — Barrier-safe loop unrolling:
//   Detects `cir.for` loops whose trip count is known (from host launch site
//   block dimensions) and whose body contains an unconditional barrier
//   (__syncthreads).  Fully unrolls the loop, replacing the induction variable
//   with a constant in each copy.
//
// Part 2 — Sub-warp barrier elimination:
//   After unrolling, removes barriers from iterations where the reduction
//   stride `d` is less than the warp size, since all active threads are within
//   a single warp and are implicitly synchronised.
//
// This runs BEFORE FlattenCFG so structured cir.for ops are available.
// LLVM's LoopUnrollPass cannot do this because __syncthreads lowers to a
// `convergent` intrinsic that the unroller refuses to duplicate.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/CIRTransformUtils.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/TargetParser/AMDGPUTargetParser.h"

#define DEBUG_TYPE "cir-offload-unroll-barrier-loops"

using namespace mlir;

namespace {

static int64_t getArchWarpSize(StringRef arch) {
  if (arch.starts_with("gfx")) {
    auto gpuKind = llvm::AMDGPU::parseArchAMDGCN(arch);
    if (gpuKind != llvm::AMDGPU::GPUKind::GK_NONE) {
      unsigned features = llvm::AMDGPU::getArchAttrAMDGCN(gpuKind);
      return (features & llvm::AMDGPU::FEATURE_WAVE32) ? 32 : 64;
    }
  }
  if (arch.starts_with("sm_"))
    return 32;
  return 32;
}

static int64_t inferWarpSize(ModuleOp module) {
  auto archs = module->getAttrOfType<ArrayAttr>("offload.target");
  if (!archs)
    return 32;
  int64_t result = 0;
  for (Attribute archAttr : archs) {
    auto arch = cast<StringAttr>(archAttr).getValue();
    result = std::max(result, getArchWarpSize(arch));
  }
  return result ? result : 32;
}

static bool isBarrierCall(Operation *op) {
  if (isa<cir::OffloadBarrierOp>(op))
    return true;
  auto call = dyn_cast<cir::CallOp>(op);
  if (!call)
    return false;
  auto callee = call.getCalleeAttr();
  if (!callee)
    return false;
  StringRef name = callee.getValue();
  return name == "_Z13__syncthreadsv" || name == "__syncthreads";
}

static bool hasBarrierInBody(cir::ForOp forOp) {
  bool found = false;
  forOp.getBody().walk([&](Operation *op) {
    if (isBarrierCall(op))
      found = true;
  });
  return found;
}

/// Check that all barriers in the loop body are unconditional — i.e., every
/// thread that enters the body will reach every barrier regardless of any
/// cir.if conditions.  We check conservatively: a barrier is "unconditional"
/// if it is NOT nested inside a cir.if region.
static bool allBarriersUnconditional(cir::ForOp forOp) {
  bool allOk = true;
  forOp.getBody().walk([&](Operation *op) {
    if (!isBarrierCall(op))
      return;
    Operation *parent = op->getParentOp();
    while (parent && parent != forOp.getOperation()) {
      if (isa<cir::IfOp>(parent)) {
        allOk = false;
        return;
      }
      parent = parent->getParentOp();
    }
  });
  return allOk;
}

/// Try to resolve the block size for a kernel by walking all launch sites.
/// Returns the block size (product of bx*by*bz) if all sites agree, or
/// nullopt if they disagree or can't be resolved.
static std::optional<int64_t>
resolveBlockSizeFromLaunches(ModuleOp module, cir::OffloadFuncOp kernel) {
  std::optional<int64_t> agreed;
  module.walk([&](cir::OffloadKernelLaunchOp launch) {
    if (launch.getKernelLeafName() != kernel.getSymName())
      return;
    auto bx = cir::tryResolveToConstant(launch.getBlockSizeX());
    auto by = cir::tryResolveToConstant(launch.getBlockSizeY());
    auto bz = cir::tryResolveToConstant(launch.getBlockSizeZ());
    if (!bx || !by || !bz) {
      agreed = std::nullopt;
      return;
    }
    int64_t total = *bx * *by * *bz;
    if (!agreed)
      agreed = total;
    else if (*agreed != total)
      agreed = std::nullopt;
  });
  return agreed;
}

/// Try to resolve the init value of a for loop's induction variable.
/// Handles patterns like:
///   d = blockDim.x / 2   (where blockDim.x is known)
///   d = <constant>
static std::optional<int64_t> resolveInitValue(cir::ForOp forOp,
                                                int64_t blockSize) {
  // The cond region starts with loading the induction variable from an alloca.
  // The init value is the store to that alloca before the for loop.
  // Walk the cond region to find the loaded alloca.
  Block &condBlock = forOp.getCond().front();
  if (condBlock.empty())
    return std::nullopt;

  // Find the first load in the cond — this loads the induction variable.
  cir::LoadOp inductionLoad;
  for (auto &op : condBlock) {
    if (auto load = dyn_cast<cir::LoadOp>(op)) {
      inductionLoad = load;
      break;
    }
  }
  if (!inductionLoad)
    return std::nullopt;

  Value inductionAddr = inductionLoad.getAddr();

  // Find the store to this address that precedes the for loop.
  // The store is typically in the same block as the for op (inside an
  // enclosing cir.scope), or in a parent block.
  Value storedVal;
  Block *forBlock = forOp->getBlock();
  if (forBlock) {
    for (auto &op : *forBlock) {
      if (&op == forOp.getOperation())
        break;
      if (auto store = dyn_cast<cir::StoreOp>(op)) {
        if (store.getAddr() == inductionAddr)
          storedVal = store.getValue();
      }
    }
  }

  if (!storedVal)
    return std::nullopt;

  // Try direct constant.
  if (auto constVal = cir::tryResolveToConstant(storedVal))
    return constVal;

  // Try blockDim.x / 2 pattern.
  // The stored value may be a cast(div(blockDim.x, 2)) or similar.
  // Trace through casts.
  Value v = storedVal;
  while (auto castOp = v.getDefiningOp<cir::CastOp>())
    v = castOp.getSrc();

  if (auto divOp = v.getDefiningOp()) {
    if (divOp->getName().getStringRef() == "cir.div" &&
        divOp->getNumOperands() == 2) {
      Value lhs = divOp->getOperand(0);
      Value rhs = divOp->getOperand(1);
      // Check rhs is constant 2.
      auto rhsConst = cir::tryResolveToConstant(rhs);
      if (rhsConst && *rhsConst == 2) {
        // Check lhs is blockDim.
        Value lhsTraced = lhs;
        while (auto c = lhsTraced.getDefiningOp<cir::CastOp>())
          lhsTraced = c.getSrc();
        if (isa_and_nonnull<cir::OffloadBlockDimOp>(
                lhsTraced.getDefiningOp())) {
          return blockSize / 2;
        }
      }
    }
  }

  // Try shift pattern: blockDim.x >> 1.
  if (auto shiftOp = v.getDefiningOp()) {
    if (shiftOp->getName().getStringRef() == "cir.shift" &&
        shiftOp->getNumOperands() == 2) {
      Value lhs = shiftOp->getOperand(0);
      Value rhs = shiftOp->getOperand(1);
      auto rhsConst = cir::tryResolveToConstant(rhs);
      if (rhsConst && *rhsConst == 1) {
        Value lhsTraced = lhs;
        while (auto c = lhsTraced.getDefiningOp<cir::CastOp>())
          lhsTraced = c.getSrc();
        if (isa_and_nonnull<cir::OffloadBlockDimOp>(
                lhsTraced.getDefiningOp())) {
          return blockSize / 2;
        }
      }
    }
  }

  return std::nullopt;
}

/// Check if the step region is `d >>= 1` (i.e., d = d / 2 or d = d >> 1).
static bool isHalvingStep(cir::ForOp forOp) {
  Block &stepBlock = forOp.getStep().front();
  // Look for a store of (load >> 1) or (load / 2) back to the same alloca.
  for (auto &op : stepBlock) {
    auto store = dyn_cast<cir::StoreOp>(op);
    if (!store)
      continue;
    Value storedVal = store.getValue();
    // Trace through casts.
    while (auto castOp = storedVal.getDefiningOp<cir::CastOp>())
      storedVal = castOp.getSrc();

    Operation *defOp = storedVal.getDefiningOp();
    if (!defOp)
      continue;

    StringRef opName = defOp->getName().getStringRef();
    if ((opName == "cir.shift" || opName == "cir.div") &&
        defOp->getNumOperands() == 2) {
      auto rhs = cir::tryResolveToConstant(defOp->getOperand(1));
      if (opName == "cir.shift" && rhs && *rhs == 1)
        return true;
      if (opName == "cir.div" && rhs && *rhs == 2)
        return true;
    }
  }
  return false;
}

/// Check if the condition is `d > 0`.
static bool isGreaterThanZeroCond(cir::ForOp forOp) {
  Block &condBlock = forOp.getCond().front();
  for (auto &op : condBlock) {
    auto cmp = dyn_cast<cir::CmpOp>(op);
    if (!cmp)
      continue;
    // kind 2 = gt (greater than)
    if (static_cast<int>(cmp.getKind()) == 2) {
      auto rhs = cir::tryResolveToConstant(cmp.getRhs());
      if (rhs && *rhs == 0)
        return true;
    }
  }
  return false;
}

struct OffloadUnrollBarrierLoopsPass
    : public PassWrapper<OffloadUnrollBarrierLoopsPass,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(OffloadUnrollBarrierLoopsPass)

  OffloadUnrollBarrierLoopsPass() = default;
  OffloadUnrollBarrierLoopsPass(bool enabled) : passEnabled(enabled) {}

  StringRef getArgument() const override {
    return "cir-offload-unroll-barrier-loops";
  }
  StringRef getDescription() const override {
    return "Unroll reduction loops with barriers using known block dimensions";
  }

  void runOnOperation() override {
    if (!passEnabled)
      return;

    ModuleOp module = getOperation();
    MLIRContext *ctx = module.getContext();
    int64_t warpSize = inferWarpSize(module);


    module.walk([&](cir::OffloadModuleOp offloadMod) {
      if (offloadMod.getBody().empty())
        return;

      offloadMod.walk([&](cir::OffloadFuncOp kernel) {
        if (!kernel.isKernel() || kernel.isExternal())
          return;

        auto blockSize = resolveBlockSizeFromLaunches(module, kernel);
        if (!blockSize) {
          LLVM_DEBUG(llvm::dbgs() << "[UnrollBarrier] " << kernel.getSymName()
                                  << ": could not resolve block size\n");
          return;
        }

        LLVM_DEBUG(llvm::dbgs() << "[UnrollBarrier] " << kernel.getSymName()
                                << ": blockSize=" << *blockSize << "\n");

        SmallVector<cir::ForOp> toUnroll;
        kernel.walk([&](cir::ForOp forOp) {
          if (!hasBarrierInBody(forOp))
            return;
          if (!allBarriersUnconditional(forOp))
            return;
          if (!isHalvingStep(forOp))
            return;
          if (!isGreaterThanZeroCond(forOp))
            return;

          auto initVal = resolveInitValue(forOp, *blockSize);
          if (!initVal || *initVal <= 0)
            return;

          LLVM_DEBUG(llvm::dbgs() << "  Found reduction loop with init="
                                  << *initVal << "\n");
          toUnroll.push_back(forOp);
        });

        for (auto forOp : toUnroll)
          unrollReductionLoop(ctx, forOp, *blockSize, warpSize);
      });
    });
  }

  void unrollReductionLoop(MLIRContext *ctx, cir::ForOp forOp,
                           int64_t blockSize, int64_t warpSize) {
    auto initVal = resolveInitValue(forOp, blockSize);
    if (!initVal)
      return;

    OpBuilder builder(forOp);
    Location loc = forOp.getLoc();

    // Find the induction variable's alloca address.
    Block &condBlock = forOp.getCond().front();
    cir::LoadOp inductionLoad;
    for (auto &op : condBlock) {
      if (auto load = dyn_cast<cir::LoadOp>(op)) {
        inductionLoad = load;
        break;
      }
    }
    if (!inductionLoad)
      return;

    Value inductionAddr = inductionLoad.getAddr();
    mlir::Type inductionTy = inductionLoad.getResult().getType();

    // Compute the sequence of d values: initVal, initVal/2, ..., 1.
    SmallVector<int64_t> dValues;
    for (int64_t d = *initVal; d > 0; d >>= 1)
      dValues.push_back(d);

    LLVM_DEBUG(llvm::dbgs() << "  Unrolling " << dValues.size()
                            << " iterations\n");

    // For each d value, clone the body and substitute d.
    for (int64_t d : dValues) {
      // Store the constant d to the induction variable alloca.
      auto constD = cir::ConstantOp::create(
          builder, loc, inductionTy,
          cir::IntAttr::get(inductionTy, d));
      cir::StoreOp::create(builder, loc, constD, inductionAddr);

      // Clone the body region's operations.
      IRMapping mapping;
      for (auto &op : forOp.getBody().front()) {
        if (isa<cir::YieldOp>(op))
          continue;
        builder.clone(op, mapping);
      }

      // Part 2: Remove sub-warp barriers.
      if (d < warpSize) {
        // Walk the just-cloned ops and erase barriers.
        // The cloned ops are right before the builder's insertion point.
        // We need to find them. Since we inserted after the forOp,
        // walk backward from the insertion point.
        SmallVector<Operation *> toErase;
        // The ops were cloned inline. Walk the parent block from after
        // the last store to find barrier calls.
        Operation *lastInserted = builder.getInsertionBlock()->getTerminator();
        for (auto it = std::prev(Block::iterator(lastInserted));
             it != Block::iterator(constD); --it) {
          it->walk([&](Operation *inner) {
            if (isBarrierCall(inner))
              toErase.push_back(inner);
          });
        }
        for (auto *op : toErase) {
          LLVM_DEBUG(llvm::dbgs() << "  Removing sub-warp barrier (d=" << d
                                  << " < warpSize=" << warpSize << ")\n");
          op->erase();
        }
      }
    }

    // Find and erase the enclosing scope that contains the alloca + for loop.
    // The pattern is: cir.scope { alloca d; store init, d; cir.for {...} }
    // We want to replace this entire scope with our unrolled code.
    // The forOp is now dead — erase it.
    forOp.erase();
  }

  bool passEnabled = true;
};

} // namespace

std::unique_ptr<mlir::Pass>
mlir::createOffloadUnrollBarrierLoopsPass(bool enabled) {
  return std::make_unique<OffloadUnrollBarrierLoopsPass>(enabled);
}
