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

#include <map>
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

  // Fold through any op that knows how to fold itself.
  //
  // A pinned parameter rarely reaches the launch untouched: a helper given
  // ne02 == 1 and ne03 == 1 passes their product, so without folding here the
  // launch still looks runtime and the constant is lost one step short of
  // where it matters.
  //
  // Delegating to the op's own folder rather than special-casing arithmetic
  // keeps one implementation of what each op means, and picks up any op that
  // gains a folder later.  Note this cannot be left to the canonicalizer:
  // no canonicalization runs over host CIR before this pass.
  if (defOp->getNumResults() == 1 && defOp->getNumOperands() > 0) {
    SmallVector<Attribute> operandAttrs;
    operandAttrs.reserve(defOp->getNumOperands());
    for (Value operand : defOp->getOperands()) {
      Attribute a = tryResolveScalarToConstant(operand, depth + 1);
      if (!a)
        break;
      operandAttrs.push_back(a);
    }
    if (operandAttrs.size() == defOp->getNumOperands()) {
      SmallVector<OpFoldResult> results;
      if (succeeded(defOp->fold(operandAttrs, results)) && results.size() == 1)
        if (auto folded = dyn_cast_if_present<Attribute>(results[0]))
          return folded;
    }
    // Not foldable -- fall through to the structural cases below.
  }

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
      bool escaped = false;
      for (OpOperand &use : ptrVal.getUses()) {
        Operation *userOp = use.getOwner();
        if (isa<cir::StoreOp>(userOp) && userOp->getNumOperands() >= 2 &&
            userOp->getOperand(1) == ptrVal) {
          if (uniqueStore)
            return {}; // multiple stores -- give up
          uniqueStore = userOp;
          continue;
        }
        if (isa<cir::LoadOp>(userOp))
          continue;
        // Address escapes (passed to a call, cast, etc.) — the alloca
        // may be written through the pointer, so we cannot trust that
        // the unique direct store is the only write.
        //
        // Unless the slot is constant, in which case writing through any
        // alias would be undefined and the single initializing store is the
        // whole story.  This is not a corner case: passing a const scalar to
        // anything taking it by reference escapes its address, and
        // `std::min(ne01, ...)` on a const parameter does exactly that.
        if (!cast<cir::AllocaOp>(ptrDefOp).getConstant())
          escaped = true;
      }
      if (uniqueStore && !escaped)
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

/// Render a constant for use inside a symbol name.  attrToString prints the
/// attribute with its type, which contains spaces and punctuation and does not
/// belong in a symbol.
static std::string constKeyString(mlir::Attribute a) {
  llvm::APInt v;
  if (auto i = mlir::dyn_cast<cir::IntAttr>(a))
    v = i.getValue();
  else if (auto i = mlir::dyn_cast<mlir::IntegerAttr>(a))
    v = i.getValue();
  else
    return "x";
  llvm::SmallString<24> buf;
  v.toString(buf, 10, /*Signed=*/true);
  std::string out(buf.str());
  for (char &c : out)
    if (c == '-')
      c = 'n';
  return out;
}

/// Constants resolved at one launch site, indexed by kernel argument.
struct PerSiteConstantsTy {
  llvm::SmallVector<mlir::Attribute> constants;
};

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
      if (op.getKernelLeafName() == kernel.getSymName())
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
    SmallVector<PerSiteConstantsTy> siteConstants;

    for (auto launch : launchOps) {
      PerSiteConstantsTy sc;
      auto kernelOperands = launch.getKernelOperands();
      sc.constants.resize(numArgs);

      for (unsigned i = 0; i < numArgs && i < kernelOperands.size(); ++i) {
        if (!isScalarArg[i])
          continue;
        sc.constants[i] = tryResolveScalarToConstant(kernelOperands[i]);
      }
      siteConstants.push_back(std::move(sc));
    }

    // Launch sites that disagree are handled first, one clone per distinct
    // set of constants.
    //
    // Requiring unanimity below is right when there is one way to call a
    // kernel, but a kernel reached both through a thin fixed-shape wrapper and
    // through a general one has two callers that will never agree, and the
    // unanimous rule then gives up on both.  Specializing per group keeps the
    // general path on the original and still bakes the constants into the
    // path that has them.
    if (specializePerSite(module, symTable, ctx, offloadMod, launchOps,
                          siteConstants, isScalarArg, numArgs))
      return;

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
      StringRef leafName = launch.getKernelLeafName();
      if (!leafName.contains('$')) {
        allOnClones = false;
        break;
      }
    }

    if (allOnClones) {
      // Apply specialization to existing clone(s) in-place.
      llvm::DenseMap<StringRef, cir::OffloadFuncOp> cloneMap;
      for (auto launch : launchOps) {
        StringRef leafName = launch.getKernelLeafName();
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
        StringRef leafName = launch.getKernelLeafName();
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
        StringRef leafName = launch.getKernelLeafName();
        if (leafName.contains('$'))
          continue; // already on a clone

        StringRef launchModName =
            launch.getKernelAttr().getRootReference().getValue();

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

  /// Give each group of launch sites that agree on their constants its own
  /// specialized kernel.  Returns true when it handled the kernel, leaving
  /// nothing for the unanimous path to do.
  bool specializePerSite(ModuleOp module, SymbolTable &symTable,
                         MLIRContext *ctx, cir::OffloadModuleOp offloadMod,
                         SmallVectorImpl<cir::OffloadKernelLaunchOp> &launchOps,
                         ArrayRef<PerSiteConstantsTy> siteConstants,
                         ArrayRef<bool> isScalarArg, unsigned numArgs) {
    if (launchOps.size() < 2)
      return false;

    auto signatureOf = [&](const PerSiteConstantsTy &sc) {
      std::string sig;
      for (unsigned i = 0; i < numArgs; ++i) {
        if (!isScalarArg[i] || !sc.constants[i])
          continue;
        Type argTy = launchOps.front()
                         .getKernelOperands()[i]
                         .getType();
        if (isa<cir::SingleType, cir::DoubleType, cir::FP16Type>(argTy))
          continue;
        sig += llvm::formatv("_{0}v{1}", i, constKeyString(sc.constants[i]));
      }
      return sig;
    };

    // Group by signature, preserving order for determinism.
    std::map<std::string, SmallVector<unsigned>> groups;
    for (unsigned s = 0; s < launchOps.size(); ++s)
      groups[signatureOf(siteConstants[s])].push_back(s);

    // One group means every site agrees; the unanimous path handles it.
    if (groups.size() < 2)
      return false;

    bool changed = false;
    for (auto &[sig, members] : groups) {
      if (sig.empty())
        continue; // nothing constant here -- leave these on the original

      SmallVector<SpecEntry> specKey;
      const PerSiteConstantsTy &rep = siteConstants[members.front()];
      for (unsigned i = 0; i < numArgs; ++i) {
        if (!isScalarArg[i] || !rep.constants[i])
          continue;
        Type argTy = launchOps.front().getKernelOperands()[i].getType();
        if (isa<cir::SingleType, cir::DoubleType, cir::FP16Type>(argTy))
          continue;
        specKey.push_back({i, rep.constants[i]});
      }
      if (specKey.empty())
        continue;

      // Sites in a group may already sit on different clones (a $maxN from
      // launch-bound tightening, say), so specialize whatever each targets.
      llvm::MapVector<StringRef, SmallVector<unsigned>> byTarget;
      for (unsigned idx : members)
        byTarget[launchOps[idx].getKernelLeafName()].push_back(idx);

      for (auto &[targetName, sites] : byTarget) {
        auto target = symTable.lookup<cir::OffloadFuncOp>(targetName);
        if (!target || target.isExternal())
          continue;
        std::string cloneName = (targetName + "$scalarspec" + sig).str();
        auto clone = dyn_cast_or_null<cir::OffloadFuncOp>(
            symTable.lookup(cloneName));
        if (!clone) {
          auto *cloneOp = target->clone();
          clone = cast<cir::OffloadFuncOp>(cloneOp);
          SymbolTable::setSymbolName(clone, cloneName);
          applySpecialization(ctx, clone, specKey);
          symTable.insert(clone);
          clone->moveAfter(target);
          LLVM_DEBUG(llvm::dbgs() << "OffloadSpecializeScalarArgs: per-site "
                                  << cloneName << "\n");
        }
        for (unsigned idx : sites) {
          cir::OffloadKernelLaunchOp launch = launchOps[idx];
          StringRef launchModName =
              launch.getKernelAttr().getRootReference().getValue();
          if (launchModName != offloadMod.getSymName()) {
            auto launchMod =
                module.lookupSymbol<cir::OffloadModuleOp>(launchModName);
            if (launchMod && !launchMod.lookupSymbol(cloneName)) {
              if (auto origDecl =
                      launchMod.lookupSymbol<cir::OffloadFuncOp>(targetName)) {
                auto *declClone = origDecl->clone();
                SymbolTable::setSymbolName(declClone, cloneName);
                OpBuilder b(ctx);
                b.setInsertionPointToEnd(&launchMod.getBody().front());
                b.insert(declClone);
              }
            }
          }
          launch.setKernelAttr(SymbolRefAttr::get(
              ctx, launchModName, {FlatSymbolRefAttr::get(ctx, cloneName)}));
          changed = true;
        }
      }
    }
    return changed;
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
