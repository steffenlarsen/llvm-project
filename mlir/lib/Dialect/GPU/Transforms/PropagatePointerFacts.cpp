//===- PropagatePointerFacts.cpp - Propagate pointer alignment & noalias --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// For each gpu.func with the kernel attribute, this pass inspects all
// gpu.launch_func call sites and traces pointer operands back to their host
// allocation roots (hipMalloc, hipMallocManaged, etc.) to infer:
//
//   1. Pointer alignment (hipMalloc guarantees >= 256-byte alignment)
//   2. noalias: distinct allocation roots => non-aliasing pointers
//   3. readonly/writeonly: from scanning uses of the block argument
//
// These facts are attached to a cloned kernel (or to existing $maxN clones
// from TightenLaunchBounds), and all visible launches are redirected to the
// annotated clone.  The original kernel is always preserved for runtime API
// callers (hipModuleGetFunction).
//
// This pass must run after TightenLaunchBounds and before
// GpuSplitSingleSourcePass.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/GPU/Transforms/Passes.h"

#include "mlir/Dialect/GPU/IR/GPUDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <numeric>

#define DEBUG_TYPE "gpu-propagate-pointer-facts"

namespace mlir {

#define GEN_PASS_DEF_GPUPROPAGATEPOINTERFACTSPASS
#include "mlir/Dialect/GPU/Transforms/Passes.h.inc"

} // namespace mlir

using namespace mlir;
using namespace mlir::gpu;

namespace {

//===----------------------------------------------------------------------===//
// Allocation fact analysis
//===----------------------------------------------------------------------===//

enum class AllocKind { HipMalloc, HipMallocManaged, HipHostMalloc, Unknown };

struct AllocationFact {
  Value root;
  int64_t baseAlign = 0;
  int64_t byteOffset = 0;
  bool offsetKnown = true;
  AllocKind kind = AllocKind::Unknown;

  int64_t effectiveAlign() const {
    if (baseAlign == 0)
      return 0;
    if (!offsetKnown)
      return 0;
    if (byteOffset == 0)
      return baseAlign;
    return std::gcd(baseAlign, std::abs(byteOffset));
  }
};

/// Strip pointer-type cast wrappers: cir.cast, unrealized_conversion_cast.
static Value stripPointerCasts(Value v) {
  while (auto *defOp = v.getDefiningOp()) {
    StringRef opName = defOp->getName().getStringRef();
    if ((opName.starts_with("cir.cast") || opName == "cir.unary" ||
         opName == "builtin.unrealized_conversion_cast") &&
        defOp->getNumOperands() == 1 && defOp->getNumResults() == 1) {
      v = defOp->getOperand(0);
      continue;
    }
    break;
  }
  return v;
}

/// Given a cir.alloca value, check if hipMalloc (or friends) writes to it.
/// hipMalloc takes a void** as first arg — the alloca holding the device ptr
/// is cast to void** and passed.  We look for:
///   %cast = cir.cast(%alloca) : ptr<ptr<T>> -> ptr<ptr<void>>
///   cir.call @hipMalloc(%cast, %size)
static AllocKind findAllocCallForAlloca(Value alloca) {
  for (OpOperand &use : alloca.getUses()) {
    Operation *userOp = use.getOwner();
    StringRef opName = userOp->getName().getStringRef();

    // Follow casts of the alloca.
    if (opName.starts_with("cir.cast") && userOp->getNumOperands() == 1 &&
        userOp->getNumResults() == 1) {
      AllocKind result = findAllocCallForAlloca(userOp->getResult(0));
      if (result != AllocKind::Unknown)
        return result;
      continue;
    }

    // Check if this is a cir.call to hipMalloc and the alloca (or its cast)
    // is the first argument.
    if (opName != "cir.call")
      continue;

    // Find the callee symbol.
    FlatSymbolRefAttr calleeAttr;
    for (NamedAttribute na : userOp->getAttrDictionary()) {
      if (auto sym = dyn_cast<FlatSymbolRefAttr>(na.getValue())) {
        calleeAttr = sym;
        break;
      }
    }
    if (!calleeAttr)
      continue;

    StringRef callee = calleeAttr.getValue();

    // The alloca (or its cast) must be the first operand (the void** out param).
    if (userOp->getNumOperands() < 1 ||
        stripPointerCasts(userOp->getOperand(0)) != stripPointerCasts(alloca))
      continue;

    if (callee.contains("hipMalloc") && !callee.contains("Managed") &&
        !callee.contains("Host"))
      return AllocKind::HipMalloc;
    if (callee.contains("hipMallocManaged"))
      return AllocKind::HipMallocManaged;
    if (callee.contains("hipHostMalloc"))
      return AllocKind::HipHostMalloc;
    if (callee.contains("hipExtMallocWithFlags"))
      return AllocKind::HipMalloc;
  }
  return AllocKind::Unknown;
}

static int64_t alignForKind(AllocKind kind) {
  switch (kind) {
  case AllocKind::HipMalloc:
  case AllocKind::HipMallocManaged:
  case AllocKind::HipHostMalloc:
    return 256;
  case AllocKind::Unknown:
    return 0;
  }
  return 0;
}

/// Trace a launch pointer operand backward through CIR to find its allocation.
static AllocationFact tryTracePointerToAllocation(Value v) {
  AllocationFact fact;
  v = stripPointerCasts(v);

  Operation *defOp = v.getDefiningOp();
  if (!defOp)
    return fact;

  StringRef opName = defOp->getName().getStringRef();

  // cir.load — the pointer was loaded from an alloca slot.
  if (opName == "cir.load" && defOp->getNumOperands() >= 1) {
    Value ptrVal = stripPointerCasts(defOp->getOperand(0));
    Operation *ptrDefOp = ptrVal.getDefiningOp();
    if (!ptrDefOp)
      return fact;

    if (ptrDefOp->getName().getStringRef() == "cir.alloca") {
      // This is a load from an alloca — check if hipMalloc wrote to it.
      AllocKind kind = findAllocCallForAlloca(ptrVal);
      if (kind != AllocKind::Unknown) {
        fact.root = ptrVal;
        fact.baseAlign = alignForKind(kind);
        fact.kind = kind;
        return fact;
      }

      // Not a recognized alloc — try following through a unique store.
      Operation *uniqueStore = nullptr;
      for (OpOperand &use : ptrVal.getUses()) {
        Operation *userOp = use.getOwner();
        if (userOp->getName().getStringRef() != "cir.store" ||
            userOp->getNumOperands() < 2 || userOp->getOperand(1) != ptrVal)
          continue;
        if (uniqueStore)
          return fact; // multiple stores — give up
        uniqueStore = userOp;
      }
      if (uniqueStore)
        return tryTracePointerToAllocation(uniqueStore->getOperand(0));
    }

    return fact;
  }

  // cir.ptr_stride — pointer arithmetic. Track the offset.
  // CIR ptr_stride works in element units, and we can't reliably determine
  // the element byte size from the CIR type at this level.  Conservatively
  // mark the offset as unknown but preserve the allocation root.
  if (opName == "cir.ptr_stride" && defOp->getNumOperands() >= 2) {
    AllocationFact baseFact =
        tryTracePointerToAllocation(defOp->getOperand(0));
    if (baseFact.kind == AllocKind::Unknown)
      return baseFact;
    baseFact.offsetKnown = false;
    return baseFact;
  }

  return fact;
}

//===----------------------------------------------------------------------===//
// Read/write analysis on gpu.func body
//===----------------------------------------------------------------------===//

enum class AccessMode { None, ReadOnly, WriteOnly, ReadWrite };

/// Scan all uses of a block argument in the gpu.func body to determine
/// whether it is only loaded, only stored, or both.
static AccessMode analyzeArgAccess(gpu::GPUFuncOp func, unsigned argIdx) {
  if (argIdx >= func.getNumArguments())
    return AccessMode::ReadWrite;

  BlockArgument arg = func.getArgument(argIdx);
  bool hasRead = false;
  bool hasWrite = false;

  SmallVector<Value, 8> worklist;
  worklist.push_back(arg);

  while (!worklist.empty()) {
    Value current = worklist.pop_back_val();
    for (OpOperand &use : current.getUses()) {
      Operation *user = use.getOwner();
      StringRef name = user->getName().getStringRef();

      if (name == "cir.load" || name == "memref.load" ||
          name == "llvm.load") {
        hasRead = true;
      } else if (name == "cir.store" || name == "memref.store" ||
                 name == "llvm.store") {
        // For stores, check if our value is the address (operand 1 for
        // cir.store: cir.store %val, %addr) or the stored value.
        if (user->getNumOperands() >= 2 && use.getOperandNumber() >= 1) {
          hasWrite = true;
        } else {
          // Stored value flows somewhere — conservative.
          hasRead = true;
          hasWrite = true;
        }
      } else if (name.starts_with("cir.cast") ||
                 name == "builtin.unrealized_conversion_cast" ||
                 name == "cir.ptr_stride") {
        // Pointer arithmetic / casts — follow through.
        for (Value result : user->getResults())
          worklist.push_back(result);
      } else if (name == "gpu.launch_func" || name == "cir.call" ||
                 name == "func.call" || name == "llvm.call") {
        // Passed to a call — assume read+write conservatively.
        hasRead = true;
        hasWrite = true;
      }
      // Other ops: ignore (conservative for casts, GEPs that don't
      // themselves load/store).
    }
  }

  if (hasRead && hasWrite)
    return AccessMode::ReadWrite;
  if (hasRead)
    return AccessMode::ReadOnly;
  if (hasWrite)
    return AccessMode::WriteOnly;
  return AccessMode::None;
}

//===----------------------------------------------------------------------===//
// Per-parameter fact set for a kernel
//===----------------------------------------------------------------------===//

struct ParamFacts {
  int64_t align = 0;
  bool noalias = false;
  AccessMode access = AccessMode::ReadWrite;
};

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct PropagatePointerFactsPass
    : impl::GpuPropagatePointerFactsPassBase<PropagatePointerFactsPass> {

  using GpuPropagatePointerFactsPassBase::
      GpuPropagatePointerFactsPassBase;

  void runOnOperation() override {
    if (!enabled)
      return;

    ModuleOp module = getOperation();

    // Process each gpu.module.
    module.walk([&](gpu::GPUModuleOp gpuModule) {
      SymbolTable symTable(gpuModule);
      SmallVector<gpu::GPUFuncOp> kernels;
      for (auto func : gpuModule.getOps<gpu::GPUFuncOp>()) {
        if (func.isKernel() && !func.isExternal())
          kernels.push_back(func);
      }
      for (auto kernel : kernels)
        processKernel(module.getContext(), module, symTable, gpuModule,
                      kernel);
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

    if (launchOps.empty())
      return;

    // Identify which arguments are pointer-typed.
    unsigned numArgs = kernel.getNumArguments();
    SmallVector<bool> isPointerArg(numArgs, false);
    for (unsigned i = 0; i < numArgs; ++i) {
      Type argTy = kernel.getArgumentTypes()[i];
      StringRef tyName = argTy.getAbstractType().getName();
      if (tyName.contains("ptr"))
        isPointerArg[i] = true;
    }

    // For each launch site, trace each pointer operand to its allocation.
    // The kernel operands in a gpu.launch_func start after the 6 grid/block
    // dims.  The getKernelOperands() accessor gives us just the kernel args.
    struct PerSiteFacts {
      SmallVector<AllocationFact> facts; // indexed by kernel arg position
    };
    SmallVector<PerSiteFacts> siteFacts;

    for (auto launch : launchOps) {
      PerSiteFacts sf;
      auto kernelOperands = launch.getKernelOperands();
      sf.facts.resize(numArgs);

      for (unsigned i = 0; i < numArgs && i < kernelOperands.size(); ++i) {
        if (!isPointerArg[i])
          continue;
        sf.facts[i] = tryTracePointerToAllocation(kernelOperands[i]);
      }
      siteFacts.push_back(std::move(sf));
    }

    // Compute conservative meet across all sites.
    SmallVector<ParamFacts> metFacts(numArgs);
    for (unsigned i = 0; i < numArgs; ++i) {
      if (!isPointerArg[i])
        continue;

      // Meet alignment: minimum across all sites.
      int64_t minAlign = INT64_MAX;
      bool allKnown = true;
      for (auto &sf : siteFacts) {
        int64_t ea = sf.facts[i].effectiveAlign();
        if (ea == 0) {
          allKnown = false;
          break;
        }
        minAlign = std::min(minAlign, ea);
      }

      if (allKnown && minAlign > 0 && inferAlign)
        metFacts[i].align = minAlign;

      // Meet noalias: for param i, it's noalias if at every site, param i's
      // root is distinct from every other pointer param's root.
      if (inferNoalias) {
        bool canNoalias = true;
        for (auto &sf : siteFacts) {
          if (sf.facts[i].kind == AllocKind::Unknown) {
            canNoalias = false;
            break;
          }
          // Check distinctness against all other pointer params at this site.
          for (unsigned j = 0; j < numArgs; ++j) {
            if (j == i || !isPointerArg[j])
              continue;
            if (sf.facts[j].kind == AllocKind::Unknown) {
              canNoalias = false;
              break;
            }
            // Same root → not distinct (unless disjoint ranges, which we
            // don't implement yet).
            if (sf.facts[i].root && sf.facts[j].root &&
                sf.facts[i].root == sf.facts[j].root) {
              canNoalias = false;
              break;
            }
          }
          if (!canNoalias)
            break;
        }
        metFacts[i].noalias = canNoalias;
      }

      // Analyze read/write from the kernel body.
      metFacts[i].access = analyzeArgAccess(kernel, i);
    }

    // Check if we have any facts worth attaching.
    bool hasFacts = false;
    for (unsigned i = 0; i < numArgs; ++i) {
      if (!isPointerArg[i])
        continue;
      if ((metFacts[i].align >= minAlignBytes) || metFacts[i].noalias ||
          metFacts[i].access == AccessMode::ReadOnly ||
          metFacts[i].access == AccessMode::WriteOnly) {
        hasFacts = true;
        break;
      }
    }

    if (!hasFacts)
      return;

    // Determine if we should annotate existing $maxN clones in-place
    // (from TightenLaunchBounds) or create a new $ptrfacts clone.

    // Check if ALL launches already target a $max clone.
    bool allOnClones = true;
    for (auto launch : launchOps) {
      StringRef leafName = launch.getKernel().getLeafReference().getValue();
      if (!leafName.contains("$max")) {
        allOnClones = false;
        break;
      }
    }

    if (allOnClones) {
      // Annotate the existing clone(s) in-place. Group launches by their
      // current target clone and annotate each.
      llvm::DenseMap<StringRef, gpu::GPUFuncOp> cloneMap;
      for (auto launch : launchOps) {
        StringRef leafName = launch.getKernel().getLeafReference().getValue();
        if (cloneMap.count(leafName))
          continue;
        if (auto clone = symTable.lookup<gpu::GPUFuncOp>(leafName))
          cloneMap[leafName] = clone;
      }
      for (auto &[name, clone] : cloneMap)
        applyFactsToFunc(ctx, clone, metFacts, isPointerArg);
    } else {
      // Create a $ptrfacts clone for launches on the original kernel,
      // and annotate existing $max clones in-place for the rest.

      // Handle launches on existing clones.
      for (auto launch : launchOps) {
        StringRef leafName = launch.getKernel().getLeafReference().getValue();
        if (!leafName.contains("$max"))
          continue;
        if (auto clone = symTable.lookup<gpu::GPUFuncOp>(leafName))
          applyFactsToFunc(ctx, clone, metFacts, isPointerArg);
      }

      // Create clone for launches still on the original.
      std::string cloneName =
          llvm::formatv("{0}$ptrfacts", kernel.getName()).str();

      auto *cloneOp = kernel->clone();
      auto clone = cast<gpu::GPUFuncOp>(cloneOp);
      SymbolTable::setSymbolName(clone, cloneName);

      applyFactsToFunc(ctx, clone, metFacts, isPointerArg);

      symTable.insert(clone);
      clone->moveAfter(kernel);

      // Redirect launches on the original to the clone.
      StringRef kernelModName = gpuModule.getName();
      for (auto launch : launchOps) {
        StringRef leafName = launch.getKernel().getLeafReference().getValue();
        if (leafName.contains("$max"))
          continue; // already handled

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

    LLVM_DEBUG({
      llvm::dbgs() << "PropagatePointerFacts: " << kernel.getName() << "\n";
      for (unsigned i = 0; i < numArgs; ++i) {
        if (!isPointerArg[i])
          continue;
        llvm::dbgs() << "  arg " << i << ": align=" << metFacts[i].align
                     << " noalias=" << metFacts[i].noalias
                     << " access=" << static_cast<int>(metFacts[i].access)
                     << "\n";
      }
    });
  }

  void applyFactsToFunc(MLIRContext *ctx, gpu::GPUFuncOp func,
                         ArrayRef<ParamFacts> facts,
                         ArrayRef<bool> isPointerArg) {
    for (unsigned i = 0; i < facts.size(); ++i) {
      if (!isPointerArg[i])
        continue;

      if (facts[i].align >= minAlignBytes && inferAlign) {
        func.setArgAttr(i, LLVM::LLVMDialect::getAlignAttrName(),
                        IntegerAttr::get(IntegerType::get(ctx, 64),
                                         facts[i].align));
      }

      if (facts[i].noalias && inferNoalias) {
        func.setArgAttr(i, LLVM::LLVMDialect::getNoAliasAttrName(),
                        UnitAttr::get(ctx));
      }

      if (facts[i].access == AccessMode::ReadOnly) {
        func.setArgAttr(i, LLVM::LLVMDialect::getReadonlyAttrName(),
                        UnitAttr::get(ctx));
      } else if (facts[i].access == AccessMode::WriteOnly) {
        func.setArgAttr(i, LLVM::LLVMDialect::getWriteOnlyAttrName(),
                        UnitAttr::get(ctx));
      }
    }
  }
};

} // namespace
