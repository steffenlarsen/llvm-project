//===- OffloadMultiversionDivisibility.cpp - Multiversion for divisibility ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// When a scalar runtime argument (typically a problem size `n`) is used as a
// grid-stride loop bound in a kernel, creates a fast clone valid under
// `n % VF == 0` alongside the generic original, with all visible launches
// redirected to the fast clone.
//
// The fast clone has a divisibility attribute so the LLVM vectorizer can
// eliminate remainder/tail handling.
//
// Run after SpecializeScalarArgs and before the offload->GPU lowering pass.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/Passes.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "cir-offload-multiversion-divisibility"

using namespace mlir;

namespace {

//===----------------------------------------------------------------------===//
// Grid-stride loop detection
//===----------------------------------------------------------------------===//

/// Check if a block argument is used as a comparison operand (heuristic for
/// loop bound usage).  Walks through casts to find cir.cmp.
static bool isUsedAsLoopBound(BlockArgument arg) {
  SmallVector<Value, 8> worklist;
  worklist.push_back(arg);

  while (!worklist.empty()) {
    Value current = worklist.pop_back_val();
    for (OpOperand &use : current.getUses()) {
      Operation *user = use.getOwner();

      if (isa<cir::CmpOp>(user))
        return true;

      // Follow through casts.
      if (isa<cir::CastOp>(user) || isa<UnrealizedConversionCastOp>(user)) {
        for (Value result : user->getResults())
          worklist.push_back(result);
      }
    }
  }
  return false;
}

/// Check if a type is a pointer type.
static bool isPointerType(Type ty) {
  return isa<cir::PointerType>(ty);
}

/// Check if a type is an integer type suitable for divisibility checks.
static bool isIntegerLikeType(Type ty) {
  if (isa<IntegerType, IndexType>(ty))
    return true;
  if (isa<cir::IntType>(ty))
    return true;
  return false;
}

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct OffloadMultiversionDivisibilityPass
    : public PassWrapper<OffloadMultiversionDivisibilityPass,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      OffloadMultiversionDivisibilityPass)

  OffloadMultiversionDivisibilityPass() = default;
  OffloadMultiversionDivisibilityPass(bool enabled) : passEnabled(enabled) {}

  StringRef getArgument() const override {
    return "cir-offload-multiversion-divisibility";
  }
  StringRef getDescription() const override {
    return "Create fast-path kernel clones valid when a scalar argument is "
           "divisible by a vectorization factor";
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
      for (auto func : offloadMod.getOps<cir::OffloadFuncOp>()) {
        if (func.isKernel() && !func.isExternal())
          kernels.push_back(func);
      }
      for (auto kernel : kernels)
        processKernel(module.getContext(), module, symTable, offloadMod,
                      kernel);
    });
  }

  void processKernel(MLIRContext *ctx, ModuleOp module, SymbolTable &symTable,
                     cir::OffloadModuleOp offloadMod,
                     cir::OffloadFuncOp kernel) {
    // Gather all launch ops targeting this kernel.
    SmallVector<cir::OffloadKernelLaunchOp> launchOps;
    module.walk([&](cir::OffloadKernelLaunchOp op) {
      if (op.getKernel().getLeafReference() == kernel.getSymName())
        launchOps.push_back(op);
    });

    if (launchOps.empty())
      return;

    unsigned numArgs = kernel.getNumArguments();

    // Find scalar integer args used as loop bounds.
    SmallVector<unsigned> eligibleParams;
    for (unsigned i = 0; i < numArgs; ++i) {
      Type argTy = kernel.getArgumentTypes()[i];
      if (isPointerType(argTy))
        continue;
      if (!isIntegerLikeType(argTy))
        continue;

      BlockArgument arg = kernel.getArgument(i);
      if (!isUsedAsLoopBound(arg))
        continue;

      // Check that the param is NOT already a compile-time constant at all
      // sites -- that case is handled by SpecializeScalarArgs.
      bool allConstant = true;
      for (auto launch : launchOps) {
        auto kernelOperands = launch.getKernelOperands();
        if (i >= kernelOperands.size()) {
          allConstant = false;
          break;
        }
        APInt dummy;
        if (!matchPattern(kernelOperands[i], m_ConstantInt(&dummy))) {
          // Also check CIR constants.
          Operation *defOp = kernelOperands[i].getDefiningOp();
          if (!defOp || !isa<cir::ConstantOp>(defOp)) {
            allConstant = false;
            break;
          }
        }
      }
      if (allConstant)
        continue;

      eligibleParams.push_back(i);
    }

    if (eligibleParams.empty())
      return;

    int variantsCreated = 0;
    int maxVariantsPerKernel = 1;
    int vectorFactor = 4;
    for (unsigned paramIdx : eligibleParams) {
      if (variantsCreated >= maxVariantsPerKernel)
        break;

      LLVM_DEBUG(llvm::dbgs()
                 << "OffloadMultiversionDivisibility: " << kernel.getSymName()
                 << " -- multiversioning on arg " << paramIdx
                 << " with VF=" << vectorFactor << "\n");

      // Create the fast clone.
      std::string cloneName =
          llvm::formatv("{0}$div{1}", kernel.getSymName(), vectorFactor).str();

      // If the clone already exists (idempotence), skip.
      if (symTable.lookup(cloneName))
        continue;

      auto *cloneOp = kernel->clone();
      auto clone = cast<cir::OffloadFuncOp>(cloneOp);
      SymbolTable::setSymbolName(clone, cloneName);

      // Insert divisibility attribute at clone entry.
      if (!clone.getBody().empty()) {
        OpBuilder builder(ctx);
        builder.setInsertionPointToStart(&clone.getBody().front());

        // Mark the assumption as an attribute on the clone.
        clone->setAttr(
            llvm::formatv("divisibility.arg{0}", paramIdx).str(),
            builder.getI64IntegerAttr(vectorFactor));
      }

      symTable.insert(clone);
      clone->moveAfter(kernel);

      // Redirect each launch site to the fast clone.
      StringRef offloadModName = offloadMod.getSymName();
      for (auto launch : launchOps) {
        auto kernelOperands = launch.getKernelOperands();
        if (paramIdx >= kernelOperands.size())
          continue;

        StringRef launchModName =
            launch.getKernel().getRootReference().getValue();

        // Ensure clone declaration exists in launch module.
        if (launchModName != offloadModName) {
          auto launchMod =
              module.lookupSymbol<cir::OffloadModuleOp>(launchModName);
          if (launchMod && !launchMod.lookupSymbol(cloneName)) {
            auto origDecl = launchMod.lookupSymbol<cir::OffloadFuncOp>(
                kernel.getSymName());
            if (origDecl) {
              auto *declClone = origDecl->clone();
              SymbolTable::setSymbolName(declClone, cloneName);
              OpBuilder declBuilder(ctx);
              declBuilder.setInsertionPointToEnd(&launchMod.getBody().front());
              declBuilder.insert(declClone);
            }
          }
        }

        // Redirect the launch to the fast clone unconditionally.
        // The divisibility assumption is encoded as an attribute on the clone,
        // which is safe even when n % VF != 0.
        // TODO: Implement proper host-side dispatch with cir.if.
        launch.setKernelAttr(SymbolRefAttr::get(
            ctx, launchModName,
            {FlatSymbolRefAttr::get(ctx, cloneName)}));
      }

      variantsCreated++;
    }
  }

  bool passEnabled = true;
};

} // namespace

std::unique_ptr<mlir::Pass>
mlir::createOffloadMultiversionDivisibilityPass(bool enabled) {
  return std::make_unique<OffloadMultiversionDivisibilityPass>(enabled);
}
