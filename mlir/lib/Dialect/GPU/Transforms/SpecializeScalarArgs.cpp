//===- SpecializeScalarArgs.cpp - Specialize scalar kernel arguments ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// For each gpu.func kernel, inspects all gpu.launch_func call sites and
// attempts to resolve each scalar (non-pointer) operand to a compile-time
// constant.  When ALL launch sites agree on the same constant value for a
// parameter, a specialised clone is created where that block argument is
// RAUW'd with the constant.  The original kernel is always preserved.
//
// This pass exploits CIR's single-source property: host launch sites and
// device kernel bodies are co-visible, so a value that is constant at every
// launch site (but typed as runtime in the kernel signature) can be baked in.
//
// Only values arriving through the launch-argument channel are specialised.
// Constants already visible in the kernel body (#define, constexpr) are NOT
// touched — the backend already constant-folds them.
//
// Run after TightenLaunchBounds and PropagatePointerFacts, before
// GpuSplitSingleSourcePass.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/GPU/Transforms/Passes.h"

#include "mlir/Dialect/GPU/IR/GPUDialect.h"
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

#define DEBUG_TYPE "gpu-specialize-scalar-args"

namespace mlir {

#define GEN_PASS_DEF_GPUSPECIALIZESCALARARGSPASS
#include "mlir/Dialect/GPU/Transforms/Passes.h.inc"

} // namespace mlir

using namespace mlir;
using namespace mlir::gpu;

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
  if (!defOp || defOp->getName().getStringRef() != "cir.const" ||
      defOp->getNumResults() != 1)
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
///   - Strip casts: cir.cast, unrealized_conversion_cast, arith.index_castui
///   - cir.load → cir.alloca → unique cir.store → follow stored value
///   - Interprocedural: function parameter → trace through all callers
static Attribute tryResolveScalarToConstant(Value v, int depth = 0) {
  // Fast path: standard arith integer constant.
  APInt directConst;
  if (matchPattern(v, m_ConstantInt(&directConst))) {
    auto intTy = cast<IntegerType>(v.getType());
    return IntegerAttr::get(intTy, directConst);
  }

  Operation *defOp = v.getDefiningOp();
  if (!defOp) {
    // v is a block argument — possibly a function parameter.
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
      StringRef opName = op->getName().getStringRef();
      bool isCall = (opName == "cir.call" || opName == "func.call" ||
                     opName == "cir.try_call");
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

  StringRef opName = defOp->getName().getStringRef();

  // Strip type-cast wrappers.
  if (opName == "arith.index_castui" || opName == "arith.index_cast" ||
      opName == "builtin.unrealized_conversion_cast" ||
      opName.starts_with("cir.cast") || opName == "cir.unary") {
    if (defOp->getNumOperands() == 1)
      return tryResolveScalarToConstant(defOp->getOperand(0), depth);
    return {};
  }

  // cir.load — value comes from memory.
  if (opName == "cir.load" && defOp->getNumOperands() >= 1) {
    Value ptrVal = defOp->getOperand(0);
    Operation *ptrDefOp = ptrVal.getDefiningOp();
    if (!ptrDefOp)
      return {};

    if (ptrDefOp->getName().getStringRef() == "cir.alloca") {
      Operation *uniqueStore = nullptr;
      for (OpOperand &use : ptrVal.getUses()) {
        Operation *userOp = use.getOwner();
        if (userOp->getName().getStringRef() != "cir.store" ||
            userOp->getNumOperands() < 2 || userOp->getOperand(1) != ptrVal)
          continue;
        if (uniqueStore)
          return {}; // multiple stores — give up
        uniqueStore = userOp;
      }
      if (uniqueStore)
        return tryResolveScalarToConstant(uniqueStore->getOperand(0), depth);
    }
    return {};
  }

  return {};
}

/// Check if a type is a pointer type (CIR ptr or LLVM ptr or memref).
static bool isPointerType(Type ty) {
  StringRef tyName = ty.getAbstractType().getName();
  return tyName.contains("ptr") || tyName.contains("memref");
}

struct SpecEntry {
  unsigned paramIndex;
  Attribute constantAttr;
};

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct SpecializeScalarArgsPass
    : impl::GpuSpecializeScalarArgsPassBase<SpecializeScalarArgsPass> {

  using GpuSpecializeScalarArgsPassBase::GpuSpecializeScalarArgsPassBase;

  void runOnOperation() override {
    if (!enabled)
      return;

    ModuleOp module = getOperation();

    module.walk([&](gpu::GPUModuleOp gpuModule) {
      SymbolTable symTable(gpuModule);
      SmallVector<gpu::GPUFuncOp> kernels;
      for (auto func : gpuModule.getOps<gpu::GPUFuncOp>()) {
        if (func.isKernel() && !func.isExternal())
          kernels.push_back(func);
      }
      for (auto kernel : kernels)
        processKernel(module.getContext(), module, symTable, gpuModule, kernel);
    });
  }

  void processKernel(MLIRContext *ctx, ModuleOp module, SymbolTable &symTable,
                     gpu::GPUModuleOp gpuModule, gpu::GPUFuncOp kernel) {
    // Gather all launch ops targeting this kernel.
    SmallVector<gpu::LaunchFuncOp> launchOps;
    module.walk([&](gpu::LaunchFuncOp op) {
      if (op.getKernel().getLeafReference() == kernel.getName())
        launchOps.push_back(op);
    });

    LLVM_DEBUG(llvm::dbgs() << "[SpecScalar] kernel: " << kernel.getName()
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

      // Skip float specialization if disabled.
      if (!specializeFloats) {
        std::string tyName;
        llvm::raw_string_ostream os(tyName);
        kernel.getArgumentTypes()[i].print(os);
        if (tyName.find("float") != std::string::npos ||
            tyName.find("double") != std::string::npos ||
            tyName.find("fp") != std::string::npos)
          continue;
      }

      specKey.push_back({i, first});
    }

    LLVM_DEBUG(llvm::dbgs() << "[SpecScalar] specKey size: " << specKey.size()
                          << "\n");
    if (specKey.empty())
      return;

    LLVM_DEBUG({
      llvm::dbgs() << "SpecializeScalarArgs: " << kernel.getName()
                   << " — specializing " << specKey.size() << " params:\n";
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
      llvm::DenseMap<StringRef, gpu::GPUFuncOp> cloneMap;
      for (auto launch : launchOps) {
        StringRef leafName = launch.getKernel().getLeafReference().getValue();
        if (cloneMap.count(leafName))
          continue;
        if (auto clone = symTable.lookup<gpu::GPUFuncOp>(leafName))
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
        if (auto clone = symTable.lookup<gpu::GPUFuncOp>(leafName))
          applySpecialization(ctx, clone, specKey);
      }

      // Create a $scalarspec clone for launches on the original.
      std::string cloneName =
          llvm::formatv("{0}$scalarspec", kernel.getName()).str();

      auto *cloneOp = kernel->clone();
      auto clone = cast<gpu::GPUFuncOp>(cloneOp);
      SymbolTable::setSymbolName(clone, cloneName);

      applySpecialization(ctx, clone, specKey);

      symTable.insert(clone);
      clone->moveAfter(kernel);

      // Redirect launches on the original to the clone.
      StringRef kernelModName = gpuModule.getName();
      for (auto launch : launchOps) {
        StringRef leafName = launch.getKernel().getLeafReference().getValue();
        if (leafName.contains('$'))
          continue; // already on a clone

        StringRef launchModName =
            launch.getKernel().getRootReference().getValue();

        if (launchModName != kernelModName) {
          auto launchMod =
              module.lookupSymbol<gpu::GPUModuleOp>(launchModName);
          if (launchMod && !launchMod.lookupSymbol(cloneName)) {
            auto origDecl =
                launchMod.lookupSymbol<gpu::GPUFuncOp>(kernel.getName());
            if (origDecl) {
              auto *declClone = origDecl->clone();
              SymbolTable::setSymbolName(declClone, cloneName);
              OpBuilder builder(ctx);
              builder.setInsertionPointToEnd(launchMod.getBody());
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

  void applySpecialization(MLIRContext *ctx, gpu::GPUFuncOp func,
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
      // When `tryResolveScalarToConstant` strips a narrowing cast (e.g.
      // cir.cast double→float), it returns the pre-cast value whose embedded
      // type differs from `argTy`.  Creating a cir.const with mismatched type
      // and value produces an invalid op that crashes during CIR→LLVM lowering
      // (FloatAttr semantics mismatch).  Skip such parameters safely.
      if (auto typedAttr = dyn_cast<TypedAttr>(entry.constantAttr)) {
        if (typedAttr.getType() != argTy) {
          LLVM_DEBUG(llvm::dbgs()
                     << "  SKIP arg " << entry.paramIndex << ": attr type "
                     << typedAttr.getType() << " != argTy " << argTy << "\n");
          continue;
        }
      }

      // Create a cir.const op with the constant attribute.
      OperationState state(func.getLoc(), "cir.const");
      state.addTypes({argTy});
      state.addAttribute("value", entry.constantAttr);
      Operation *constOp = builder.create(state);

      arg.replaceAllUsesWith(constOp->getResult(0));

      LLVM_DEBUG(llvm::dbgs() << "  RAUW arg " << entry.paramIndex << " in "
                               << func.getName() << " with ";
                 entry.constantAttr.print(llvm::dbgs());
                 llvm::dbgs() << "\n");
    }
  }
};

} // namespace
