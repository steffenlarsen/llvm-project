//===- OffloadSpecializeScalarArgs.cpp - Specialize scalar kernel args -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// For each cir.offload.func kernel, inspects all cir.offload.kernel_launch
// call sites and attempts to resolve each scalar (non-pointer) operand to a
// compile-time constant.  When ALL launch sites agree on the same constant
// value for a parameter, a specialised clone is created where that block
// argument is RAUW'd with the constant.  The original kernel is always
// preserved.
//
// This pass exploits CIR's single-source property: host launch sites and
// device kernel bodies are co-visible, so a value that is constant at every
// launch site (but typed as runtime in the kernel signature) can be baked in.
//
// Run after TightenLaunchBounds and PropagatePointerFacts, before the
// offload->GPU lowering pass.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/Passes.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Interfaces/FunctionInterfaces.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "cir-offload-specialize-scalar-args"

using namespace mlir;

namespace {

//===----------------------------------------------------------------------===//
// Constant resolution for scalar operands
//===----------------------------------------------------------------------===//

/// Print an attribute to a canonical string for comparison.
static std::string attrToString(Attribute attr) {
  std::string buf;
  llvm::raw_string_ostream os(buf);
  attr.print(os);
  return buf;
}

/// Try to extract the value attribute from a cir.const op.  Returns the
/// attribute (a cir::IntAttr, cir::FPAttr, or similar) or nullptr.
static Attribute tryGetCIRConstAttr(Operation *defOp) {
  if (!defOp || !isa<cir::ConstantOp>(defOp) || defOp->getNumResults() != 1)
    return {};

  Attribute valAttr = defOp->getAttr("value");
  if (!valAttr) {
    for (NamedAttribute na : defOp->getAttrDictionary()) {
      valAttr = na.getValue();
      break;
    }
  }
  return valAttr;
}

/// Try to resolve a Value to a compile-time constant attribute.
/// Returns the constant attribute (cir::IntAttr, cir::FPAttr, IntegerAttr,
/// FloatAttr) or nullptr if not resolvable.
///
/// Handles:
///   - arith constant int (m_ConstantInt)
///   - cir.const #cir.int<N> / #cir.fp<V>
///   - Strip casts: cir.cast, unrealized_conversion_cast
///   - cir.load -> cir.alloca -> unique cir.store -> follow stored value
///   - Interprocedural: function parameter -> trace through all callers
static Attribute tryResolveScalarToConstant(Value v, int depth = 0) {
  // Fast path: standard arith integer constant.
  APInt directConst;
  if (matchPattern(v, m_ConstantInt(&directConst))) {
    auto intTy = cast<IntegerType>(v.getType());
    return IntegerAttr::get(intTy, directConst);
  }

  Operation *defOp = v.getDefiningOp();
  if (!defOp) {
    // v is a block argument -- possibly a function parameter.
    // Trace interprocedurally: find all callers of the enclosing function
    // and check if they all pass the same constant for this arg position.
    if (depth >= 4) {
      LLVM_DEBUG(llvm::dbgs() << "  [interproc] depth limit reached\n");
      return {};
    }

    auto blockArg = dyn_cast<BlockArgument>(v);
    if (!blockArg) {
      LLVM_DEBUG(llvm::dbgs() << "  [interproc] not a block arg\n");
      return {};
    }

    Block *block = blockArg.getOwner();
    auto parentOp = block->getParentOp();
    if (!parentOp) {
      LLVM_DEBUG(llvm::dbgs() << "  [interproc] no parent op\n");
      return {};
    }

    LLVM_DEBUG(llvm::dbgs()
               << "  [interproc] parent op: "
               << parentOp->getName().getStringRef() << "\n");

    // Get the enclosing function (cir.func or func.func).
    StringRef parentName;
    if (auto funcOp = dyn_cast<FunctionOpInterface>(parentOp))
      parentName = funcOp.getName();
    else {
      LLVM_DEBUG(llvm::dbgs()
                 << "  [interproc] parent not FunctionOpInterface\n");
      return {};
    }

    // Only trace through the entry block (function parameters).
    if (block != &parentOp->getRegion(0).front()) {
      LLVM_DEBUG(llvm::dbgs() << "  [interproc] not entry block\n");
      return {};
    }

    unsigned argIdx = blockArg.getArgNumber();
    LLVM_DEBUG(llvm::dbgs()
               << "  [interproc] tracing arg " << argIdx << " of "
               << parentName << " (depth=" << depth << ")\n");

    // Walk the module for all call sites targeting this function.
    auto moduleOp = parentOp->getParentOfType<ModuleOp>();
    if (!moduleOp)
      return {};

    Attribute agreedAttr;
    bool foundAnyCaller = false;

    moduleOp.walk([&](Operation *op) {
      bool isCall = isa<cir::CallOp>(op) || isa<cir::TryCallOp>(op);
      if (!isCall)
        return;

      // Get the callee name from the op.
      auto calleeAttr = op->getAttrOfType<FlatSymbolRefAttr>("callee");
      if (!calleeAttr || calleeAttr.getValue() != parentName)
        return;

      LLVM_DEBUG(llvm::dbgs() << "  [interproc] found caller with "
                              << op->getNumOperands() << " operands, need idx "
                              << argIdx << "\n");

      if (argIdx >= op->getNumOperands()) {
        LLVM_DEBUG(llvm::dbgs() << "  [interproc] argIdx out of range\n");
        return;
      }

      Attribute callerAttr =
          tryResolveScalarToConstant(op->getOperand(argIdx), depth + 1);
      if (!callerAttr) {
        agreedAttr = {};
        foundAnyCaller = true;
        return;
      }

      if (!foundAnyCaller) {
        agreedAttr = callerAttr;
        foundAnyCaller = true;
      } else if (agreedAttr) {
        if (attrToString(agreedAttr) != attrToString(callerAttr))
          agreedAttr = {};
      }
    });

    if (foundAnyCaller && agreedAttr) {
      LLVM_DEBUG(llvm::dbgs()
                 << "  Interprocedural: resolved arg " << argIdx << " of "
                 << parentName << " to ";
                 agreedAttr.print(llvm::dbgs()); llvm::dbgs() << "\n");
      return agreedAttr;
    }
    return {};
  }

  // CIR constant: cir.const with #cir.int<N> or #cir.fp<V>.
  if (Attribute attr = tryGetCIRConstAttr(defOp))
    return attr;

  // Strip type-cast wrappers.
  if (isa<cir::CastOp>(defOp) || isa<UnrealizedConversionCastOp>(defOp)) {
    if (defOp->getNumOperands() == 1)
      return tryResolveScalarToConstant(defOp->getOperand(0), depth);
    return {};
  }

  // cir.load -- value comes from memory.
  if (isa<cir::LoadOp>(defOp) && defOp->getNumOperands() >= 1) {
    Value ptrVal = defOp->getOperand(0);
    Operation *ptrDefOp = ptrVal.getDefiningOp();
    if (!ptrDefOp)
      return {};

    if (isa<cir::AllocaOp>(ptrDefOp)) {
      Operation *uniqueStore = nullptr;
      for (OpOperand &use : ptrVal.getUses()) {
        Operation *userOp = use.getOwner();
        if (!isa<cir::StoreOp>(userOp) || userOp->getNumOperands() < 2 ||
            userOp->getOperand(1) != ptrVal)
          continue;
        if (uniqueStore)
          return {}; // multiple stores -- give up
        uniqueStore = userOp;
      }
      if (uniqueStore)
        return tryResolveScalarToConstant(uniqueStore->getOperand(0), depth);
    }
    return {};
  }

  return {};
}

/// Check if a type is a pointer type (CIR ptr).
static bool isPointerType(Type ty) {
  return isa<cir::PointerType>(ty);
}

struct SpecEntry {
  unsigned paramIndex;
  Attribute constantAttr;
};

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct OffloadSpecializeScalarArgsPass
    : public PassWrapper<OffloadSpecializeScalarArgsPass,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      OffloadSpecializeScalarArgsPass)

  OffloadSpecializeScalarArgsPass() = default;
  OffloadSpecializeScalarArgsPass(bool enabled) : passEnabled(enabled) {}

  StringRef getArgument() const override {
    return "cir-offload-specialize-scalar-args";
  }
  StringRef getDescription() const override {
    return "Specialize scalar kernel arguments that are constant at all launch "
           "sites";
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

    LLVM_DEBUG(llvm::dbgs() << "[SpecScalar] kernel: " << kernel.getSymName()
                            << " launches=" << launchOps.size() << "\n");

    if (launchOps.empty())
      return;

    unsigned numArgs = kernel.getNumArguments();

    // Identify which arguments are scalar (non-pointer).
    SmallVector<bool> isScalarArg(numArgs, false);
    for (unsigned i = 0; i < numArgs; ++i) {
      Type argTy = kernel.getArgumentTypes()[i];
      if (!isPointerType(argTy))
        isScalarArg[i] = true;
    }

    // For each launch site, try to resolve each scalar operand to a constant.
    struct PerSiteConstants {
      SmallVector<Attribute> constants; // indexed by kernel arg position
    };
    SmallVector<PerSiteConstants> siteConstants;

    for (auto launch : launchOps) {
      PerSiteConstants sc;
      auto kernelOperands = launch.getKernelOperands();
      sc.constants.resize(numArgs);

      for (unsigned i = 0; i < numArgs && i < kernelOperands.size(); ++i) {
        if (!isScalarArg[i])
          continue;
        sc.constants[i] = tryResolveScalarToConstant(kernelOperands[i]);
      }
      siteConstants.push_back(std::move(sc));
    }

    // Multi-site reconciliation: a parameter is specializable only if ALL
    // launch sites resolve to the same constant.
    SmallVector<SpecEntry> specKey;

    for (unsigned i = 0; i < numArgs; ++i) {
      if (!isScalarArg[i])
        continue;

      // Check if all sites agree.
      Attribute first = siteConstants[0].constants[i];
      if (!first)
        continue;

      std::string firstStr = attrToString(first);
      bool allAgree = true;
      for (size_t s = 1; s < siteConstants.size(); ++s) {
        Attribute other = siteConstants[s].constants[i];
        if (!other || attrToString(other) != firstStr) {
          allAgree = false;
          break;
        }
      }

      if (!allAgree)
        continue;

      // Skip float types for now.
      {
        Type argTy = kernel.getArgumentTypes()[i];
        if (isa<cir::SingleType, cir::DoubleType, cir::FP16Type>(argTy))
          continue;
      }

      specKey.push_back({i, first});
    }

    LLVM_DEBUG(llvm::dbgs() << "[SpecScalar] specKey size: " << specKey.size()
                            << "\n");
    if (specKey.empty())
      return;

    LLVM_DEBUG({
      llvm::dbgs() << "OffloadSpecializeScalarArgs: " << kernel.getSymName()
                   << " -- specializing " << specKey.size() << " params:\n";
      for (auto &e : specKey) {
        llvm::dbgs() << "  arg " << e.paramIndex << " = ";
        e.constantAttr.print(llvm::dbgs());
        llvm::dbgs() << "\n";
      }
    });

    // Check if ALL launches target existing $-suffixed clones.
    bool allOnClones = true;
    for (auto launch : launchOps) {
      StringRef leafName = launch.getKernel().getLeafReference().getValue();
      if (!leafName.contains('$')) {
        allOnClones = false;
        break;
      }
    }

    if (allOnClones) {
      // Apply specialization to existing clone(s) in-place.
      llvm::DenseMap<StringRef, cir::OffloadFuncOp> cloneMap;
      for (auto launch : launchOps) {
        StringRef leafName = launch.getKernel().getLeafReference().getValue();
        if (cloneMap.count(leafName))
          continue;
        if (auto clone = symTable.lookup<cir::OffloadFuncOp>(leafName))
          cloneMap[leafName] = clone;
      }
      for (auto &[name, clone] : cloneMap)
        applySpecialization(ctx, clone, specKey);
    } else {
      // Apply to any existing clones that launches target.
      for (auto launch : launchOps) {
        StringRef leafName = launch.getKernel().getLeafReference().getValue();
        if (!leafName.contains('$'))
          continue;
        if (auto clone = symTable.lookup<cir::OffloadFuncOp>(leafName))
          applySpecialization(ctx, clone, specKey);
      }

      // Create a $scalarspec clone for launches on the original.
      std::string cloneName =
          llvm::formatv("{0}$scalarspec", kernel.getSymName()).str();

      auto *cloneOp = kernel->clone();
      auto clone = cast<cir::OffloadFuncOp>(cloneOp);
      SymbolTable::setSymbolName(clone, cloneName);

      applySpecialization(ctx, clone, specKey);

      symTable.insert(clone);
      clone->moveAfter(kernel);

      // Redirect launches on the original to the clone.
      StringRef offloadModName = offloadMod.getSymName();
      for (auto launch : launchOps) {
        StringRef leafName = launch.getKernel().getLeafReference().getValue();
        if (leafName.contains('$'))
          continue; // already on a clone

        StringRef launchModName =
            launch.getKernel().getRootReference().getValue();

        if (launchModName != offloadModName) {
          auto launchMod =
              module.lookupSymbol<cir::OffloadModuleOp>(launchModName);
          if (launchMod && !launchMod.lookupSymbol(cloneName)) {
            auto origDecl = launchMod.lookupSymbol<cir::OffloadFuncOp>(
                kernel.getSymName());
            if (origDecl) {
              auto *declClone = origDecl->clone();
              SymbolTable::setSymbolName(declClone, cloneName);
              OpBuilder builder(ctx);
              builder.setInsertionPointToEnd(&launchMod.getBody().front());
              builder.insert(declClone);
            }
          }
        }

        launch.setKernelAttr(SymbolRefAttr::get(
            ctx, launchModName,
            {FlatSymbolRefAttr::get(ctx, cloneName)}));
      }
    }
  }

  void applySpecialization(MLIRContext *ctx, cir::OffloadFuncOp func,
                           ArrayRef<SpecEntry> specKey) {
    if (func.getBody().empty())
      return;

    OpBuilder builder(ctx);
    builder.setInsertionPointToStart(&func.getBody().front());

    for (auto &entry : specKey) {
      if (entry.paramIndex >= func.getNumArguments())
        continue;

      BlockArgument arg = func.getArgument(entry.paramIndex);
      Type argTy = arg.getType();

      // Ensure the resolved constant's type matches the kernel argument type.
      if (auto typedAttr = dyn_cast<TypedAttr>(entry.constantAttr)) {
        if (typedAttr.getType() != argTy) {
          LLVM_DEBUG(llvm::dbgs()
                     << "  SKIP arg " << entry.paramIndex << ": attr type "
                     << typedAttr.getType() << " != argTy " << argTy << "\n");
          continue;
        }
      }

      // Create a cir.const op with the constant attribute.
      auto typedAttr = mlir::cast<mlir::TypedAttr>(entry.constantAttr);
      Value constVal = cir::ConstantOp::create(builder, func.getLoc(), argTy,
                                               typedAttr);

      arg.replaceAllUsesWith(constVal);

      LLVM_DEBUG(llvm::dbgs() << "  RAUW arg " << entry.paramIndex << " in "
                              << func.getSymName() << " with ";
                 entry.constantAttr.print(llvm::dbgs());
                 llvm::dbgs() << "\n");
    }
  }

  bool passEnabled = true;
};

} // namespace

std::unique_ptr<mlir::Pass>
mlir::createOffloadSpecializeScalarArgsPass(bool enabled) {
  return std::make_unique<OffloadSpecializeScalarArgsPass>(enabled);
}
