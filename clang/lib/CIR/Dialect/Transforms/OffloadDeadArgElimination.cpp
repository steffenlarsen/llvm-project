//===- OffloadDeadArgElimination.cpp - Remove dead kernel arguments --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Removes unused arguments from offload kernels using a clone-and-redirect
// strategy: creates a $dae clone with dead args removed, redirects all visible
// launch sites to it, and preserves the original kernel for external callers.
//
// Runs after SpecializeScalarArgs/MultiversionDivisibility (which may create
// use-empty args) and before ConvertCIROffloadToGPU.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"

#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormatVariadic.h"

#define DEBUG_TYPE "cir-offload-dead-arg-elimination"

using namespace mlir;

namespace {

struct OffloadDeadArgEliminationPass
    : public PassWrapper<OffloadDeadArgEliminationPass,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(OffloadDeadArgEliminationPass)

  OffloadDeadArgEliminationPass() = default;
  OffloadDeadArgEliminationPass(bool enabled) : passEnabled(enabled) {}

  StringRef getArgument() const override {
    return "cir-offload-dead-arg-elimination";
  }
  StringRef getDescription() const override {
    return "Remove unused arguments from offload kernels via "
           "clone-and-redirect";
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
      if (op.getKernelLeafName() == kernel.getSymName())
        launchOps.push_back(op);
    });

    if (launchOps.empty())
      return;

    // Build BitVector of dead args.
    unsigned numArgs = kernel.getNumArguments();
    llvm::BitVector deadArgs(numArgs, false);
    for (unsigned i = 0; i < numArgs; ++i) {
      if (isArgDead(kernel.getArgument(i)))
        deadArgs.set(i);
    }

    if (deadArgs.none())
      return;

    LLVM_DEBUG({
      llvm::dbgs() << "OffloadDeadArgElimination: " << kernel.getSymName()
                   << " -- dead args: [";
      llvm::interleaveComma(deadArgs.set_bits(), llvm::dbgs());
      llvm::dbgs() << "] out of " << numArgs << "\n";
    });

    // Check if ALL launches target existing $-suffixed clones.
    bool allOnClones = true;
    for (auto launch : launchOps) {
      StringRef leafName = launch.getKernelLeafName();
      if (!leafName.contains('$')) {
        allOnClones = false;
        break;
      }
    }

    if (allOnClones) {
      // Apply dead-arg removal to existing clone(s) in-place.
      llvm::DenseMap<StringRef, cir::OffloadFuncOp> cloneMap;
      for (auto launch : launchOps) {
        StringRef leafName = launch.getKernelLeafName();
        if (cloneMap.count(leafName))
          continue;
        if (auto clone = symTable.lookup<cir::OffloadFuncOp>(leafName))
          cloneMap[leafName] = clone;
      }
      for (auto &[name, clone] : cloneMap)
        applyDeadArgRemoval(clone, deadArgs);

      // Erase corresponding operands from all launch ops.
      for (auto launch : launchOps)
        eraseLaunchOperands(launch, deadArgs);
    } else {
      // Apply to any existing clones that launches target.
      for (auto launch : launchOps) {
        StringRef leafName = launch.getKernelLeafName();
        if (!leafName.contains('$'))
          continue;
        if (auto clone = symTable.lookup<cir::OffloadFuncOp>(leafName)) {
          applyDeadArgRemoval(clone, deadArgs);
          eraseLaunchOperands(launch, deadArgs);
        }
      }

      // Create a $dae clone for launches on the original.
      std::string cloneName =
          llvm::formatv("{0}$dae", kernel.getSymName()).str();

      if (symTable.lookup(cloneName))
        return;

      auto *cloneOp = kernel->clone();
      auto clone = cast<cir::OffloadFuncOp>(cloneOp);
      SymbolTable::setSymbolName(clone, cloneName);

      applyDeadArgRemoval(clone, deadArgs);

      symTable.insert(clone);
      clone->moveAfter(kernel);

      // Redirect launches on the original to the clone.
      StringRef offloadModName = offloadMod.getSymName();
      for (auto launch : launchOps) {
        StringRef leafName = launch.getKernelLeafName();
        if (leafName.contains('$'))
          continue;

        StringRef launchModName =
            launch.getKernelAttr().getRootReference().getValue();

        // Ensure clone declaration exists in other offload modules.
        if (launchModName != offloadModName) {
          auto launchMod =
              module.lookupSymbol<cir::OffloadModuleOp>(launchModName);
          if (launchMod && !launchMod.lookupSymbol(cloneName)) {
            auto origDecl = launchMod.lookupSymbol<cir::OffloadFuncOp>(
                kernel.getSymName());
            if (origDecl) {
              auto *declClone = origDecl->clone();
              SymbolTable::setSymbolName(declClone, cloneName);
              // Update the declaration's function type to match the clone.
              auto declFunc = cast<cir::OffloadFuncOp>(declClone);
              applyDeadArgRemoval(declFunc, deadArgs);
              OpBuilder builder(ctx);
              builder.setInsertionPointToEnd(&launchMod.getBody().front());
              builder.insert(declClone);
            }
          }
        }

        launch.setKernelAttr(SymbolRefAttr::get(
            ctx, launchModName,
            {FlatSymbolRefAttr::get(ctx, cloneName)}));

        eraseLaunchOperands(launch, deadArgs);
      }
    }
  }

  /// Return the alloca that \p arg is spilled into by \p store, if that
  /// alloca exists only to hold the spill (never read, never otherwise used).
  static cir::AllocaOp getDeadSpillSlot(BlockArgument arg, cir::StoreOp store) {
    if (store.getValue() != Value(arg))
      return {};
    auto alloca = store.getAddr().getDefiningOp<cir::AllocaOp>();
    if (!alloca)
      return {};
    for (Operation *user : alloca.getResult().getUsers())
      if (user != store.getOperation())
        return {};
    return alloca;
  }

  /// An argument is dead when nothing reads it.
  ///
  /// Checking use_empty() alone is not enough: CIRGen spills every kernel
  /// parameter into an entry-block alloca, so even a parameter that the body
  /// never mentions -- for instance one guarded by a discarded
  /// `if constexpr` branch -- still has that store as a use.  Treat an
  /// argument whose only uses are spills into never-read slots as dead.
  static bool isArgDead(BlockArgument arg) {
    if (arg.use_empty())
      return true;
    for (Operation *user : arg.getUsers()) {
      auto store = dyn_cast<cir::StoreOp>(user);
      if (!store || !getDeadSpillSlot(arg, store))
        return false;
    }
    return true;
  }

  static void applyDeadArgRemoval(cir::OffloadFuncOp func,
                                  const llvm::BitVector &deadArgs) {
    // For definitions, drop uses of dead args before erasing.  The spill
    // store and its slot are dead once the argument goes away, and leaving
    // a store of a detached value behind would be invalid IR.
    if (!func.isExternal()) {
      for (unsigned idx : deadArgs.set_bits()) {
        if (idx >= func.getNumArguments())
          continue;
        BlockArgument arg = func.getArgument(idx);
        SmallVector<cir::AllocaOp> deadSlots;
        SmallVector<cir::StoreOp> deadStores;
        for (Operation *user : arg.getUsers()) {
          auto store = dyn_cast<cir::StoreOp>(user);
          if (!store)
            continue;
          if (cir::AllocaOp slot = getDeadSpillSlot(arg, store)) {
            deadStores.push_back(store);
            deadSlots.push_back(slot);
          }
        }
        for (cir::StoreOp store : deadStores)
          store.erase();
        for (cir::AllocaOp slot : deadSlots)
          if (slot.getResult().use_empty())
            slot.erase();
        arg.dropAllUses();
      }
    }

    // Cannot use FunctionOpInterface::eraseArguments because
    // cir::FuncType::clone asserts results.size() == 1, which fails for
    // void-returning kernels (getResultTypes() returns {}). Rebuild manually.
    auto oldFnTy = func.getFunctionType();
    SmallVector<Type> newInputs;
    for (auto [i, ty] : llvm::enumerate(oldFnTy.getInputs())) {
      if (!deadArgs.test(i))
        newInputs.push_back(ty);
    }
    auto newFnTy = cir::FuncType::get(newInputs, oldFnTy.getReturnType(),
                                      oldFnTy.isVarArg());
    func.setFunctionTypeAttr(TypeAttr::get(newFnTy));

    // Erase block arguments (reverse to preserve indices).
    if (!func.isExternal()) {
      Block &entry = func.getBody().front();
      for (unsigned idx : llvm::reverse(deadArgs.set_bits())) {
        if (idx < entry.getNumArguments())
          entry.eraseArgument(idx);
      }
    }

    // Update arg_attrs if present.
    if (auto argAttrs = func->getAttrOfType<ArrayAttr>("arg_attrs")) {
      SmallVector<Attribute> newArgAttrs;
      for (auto [i, attr] : llvm::enumerate(argAttrs)) {
        if (!deadArgs.test(i))
          newArgAttrs.push_back(attr);
      }
      func->setAttr("arg_attrs",
                     ArrayAttr::get(func->getContext(), newArgAttrs));
    }
  }

  static void eraseLaunchOperands(cir::OffloadKernelLaunchOp launch,
                                  const llvm::BitVector &deadArgs) {
    auto mutableOperands = launch.getKernelOperandsMutable();
    for (unsigned idx : llvm::reverse(deadArgs.set_bits())) {
      if (idx < mutableOperands.size())
        mutableOperands.erase(idx);
    }
  }

  bool passEnabled = true;
};

} // namespace

std::unique_ptr<mlir::Pass>
mlir::createOffloadDeadArgEliminationPass(bool enabled) {
  return std::make_unique<OffloadDeadArgEliminationPass>(enabled);
}
