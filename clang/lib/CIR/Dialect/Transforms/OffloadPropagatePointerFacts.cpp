//===- OffloadPropagatePointerFacts.cpp - Propagate pointer alignment etc. ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// For each cir.offload.func kernel, this pass inspects all
// cir.offload.kernel_launch call sites and traces pointer operands back to
// their host allocation roots (hipMalloc, hipMallocManaged, etc.) to infer:
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
// This pass must run after TightenLaunchBounds and before the offload->GPU
// lowering pass.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/CIRTransformUtils.h"

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

#define DEBUG_TYPE "cir-offload-propagate-pointer-facts"

using namespace mlir;

namespace {

//===----------------------------------------------------------------------===//
// Allocation fact analysis
//===----------------------------------------------------------------------===//

enum class AllocKind { HipMalloc, HipMallocManaged, HipHostMalloc, Unknown };

struct AllocCallResult {
  AllocKind kind = AllocKind::Unknown;
  mlir::Value sizeValue; // hipMalloc size operand (2nd arg)
};

struct AllocationFact {
  Value root;
  int64_t baseAlign = 0;
  int64_t byteOffset = 0;
  bool offsetKnown = true;
  AllocKind kind = AllocKind::Unknown;
  mlir::Value sizeValue; // from hipMalloc

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

/// Return true if this op transparently forwards a pointer from operand 0
/// to its result without changing the allocation identity.
static bool isPointerTransparent(Operation *op) {
  if (!op || op->getNumResults() != 1)
    return false;
  return isa<cir::CastOp, cir::PtrStrideOp, cir::GetMemberOp,
             cir::BaseClassAddrOp, cir::GetElementOp,
             UnrealizedConversionCastOp>(op);
}

/// Strip pointer-type casts that don't change pointer identity.
/// Does NOT strip GetMemberOp/BaseClassAddrOp/GetElementOp — those change
/// which storage location the pointer refers to.
static Value stripPointerCasts(Value v) {
  while (auto *defOp = v.getDefiningOp()) {
    if ((isa<cir::CastOp, cir::PtrStrideOp, UnrealizedConversionCastOp>(
             defOp)) &&
        defOp->getNumOperands() >= 1 && defOp->getNumResults() == 1) {
      v = defOp->getOperand(0);
      continue;
    }
    break;
  }
  return v;
}

/// Given a cir.alloca value, check if hipMalloc (or friends) writes to it.
/// hipMalloc takes a void** as first arg -- the alloca holding the device ptr
/// is cast to void** and passed.  We look for:
///   %cast = cir.cast(%alloca) : ptr<ptr<T>> -> ptr<ptr<void>>
///   cir.call @hipMalloc(%cast, %size)
static AllocCallResult findAllocCallForAlloca(Value alloca) {
  for (OpOperand &use : alloca.getUses()) {
    Operation *userOp = use.getOwner();

    // Follow casts of the alloca.
    if (isa<cir::CastOp>(userOp) && userOp->getNumOperands() == 1 &&
        userOp->getNumResults() == 1) {
      auto result = findAllocCallForAlloca(userOp->getResult(0));
      if (result.kind != AllocKind::Unknown)
        return result;
      continue;
    }

    // Check if this is a cir.call to hipMalloc and the alloca (or its cast)
    // is the first argument.
    if (!isa<cir::CallOp>(userOp))
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

    // Capture the size argument (2nd operand) if available.
    mlir::Value sizeVal;
    if (userOp->getNumOperands() >= 2)
      sizeVal = userOp->getOperand(1);

    if (callee.contains("hipMalloc") && !callee.contains("Managed") &&
        !callee.contains("Host"))
      return {AllocKind::HipMalloc, sizeVal};
    if (callee.contains("hipMallocManaged"))
      return {AllocKind::HipMallocManaged, sizeVal};
    if (callee.contains("hipHostMalloc"))
      return {AllocKind::HipHostMalloc, sizeVal};
    if (callee.contains("hipExtMallocWithFlags"))
      return {AllocKind::HipMalloc, sizeVal};
  }
  return {};
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

// Forward declaration.
static AllocationFact tryTracePointerToAllocation(Value v,
                                                  unsigned depth = 0);

/// Try to resolve a storage location (the address operand of a LoadOp) to an
/// allocation fact.  Handles direct allocas, struct-member loads
/// (GetMemberOp → alloca), and single-store forwarding.
static AllocationFact resolveStorageLocation(Value addr, unsigned depth) {
  AllocationFact fact;
  addr = stripPointerCasts(addr);

  Operation *addrDef = addr.getDefiningOp();
  if (!addrDef)
    return fact;

  // Direct alloca — check if hipMalloc wrote to it.
  if (isa<cir::AllocaOp>(addrDef)) {
    auto allocResult = findAllocCallForAlloca(addr);
    if (allocResult.kind != AllocKind::Unknown) {
      fact.root = addr;
      fact.baseAlign = alignForKind(allocResult.kind);
      fact.kind = allocResult.kind;
      fact.sizeValue = allocResult.sizeValue;
      return fact;
    }

    // Not a recognized alloc — try following through a unique store.
    Operation *uniqueStore = nullptr;
    for (OpOperand &use : addr.getUses()) {
      Operation *userOp = use.getOwner();
      if (!isa<cir::StoreOp>(userOp) || userOp->getNumOperands() < 2 ||
          userOp->getOperand(1) != addr)
        continue;
      if (uniqueStore)
        return fact; // multiple stores — give up
      uniqueStore = userOp;
    }
    if (uniqueStore)
      return tryTracePointerToAllocation(uniqueStore->getOperand(0),
                                         depth + 1);
    return fact;
  }

  // GetMemberOp / GetElementOp / BaseClassAddrOp — the load address is a
  // struct member or derived pointer.  The member itself might have hipMalloc
  // writing to it (e.g., hipMalloc(&s.member, size)).  Check the member
  // address directly first, then fall back to the struct base.
  if (isa<cir::GetMemberOp, cir::BaseClassAddrOp, cir::GetElementOp>(
          addrDef)) {
    // Check if hipMalloc writes to this member address.
    auto allocResult = findAllocCallForAlloca(addr);
    if (allocResult.kind != AllocKind::Unknown) {
      fact.root = addr;
      fact.baseAlign = alignForKind(allocResult.kind);
      fact.kind = allocResult.kind;
      fact.sizeValue = allocResult.sizeValue;
      return fact;
    }
    // Try single-store forwarding on the member address.
    Operation *uniqueStore = nullptr;
    for (OpOperand &use : addr.getUses()) {
      Operation *userOp = use.getOwner();
      if (!isa<cir::StoreOp>(userOp) || userOp->getNumOperands() < 2 ||
          userOp->getOperand(1) != addr)
        continue;
      if (uniqueStore)
        return fact;
      uniqueStore = userOp;
    }
    if (uniqueStore)
      return tryTracePointerToAllocation(uniqueStore->getOperand(0),
                                         depth + 1);
    // Fall back to tracing the struct base.
    if (addrDef->getNumOperands() >= 1)
      return resolveStorageLocation(addrDef->getOperand(0), depth);
    return fact;
  }

  // GetGlobalOp — global variable; check hipMalloc.
  if (isa<cir::GetGlobalOp>(addrDef)) {
    auto allocResult = findAllocCallForAlloca(addr);
    if (allocResult.kind != AllocKind::Unknown) {
      fact.root = addr;
      fact.baseAlign = alignForKind(allocResult.kind);
      fact.kind = allocResult.kind;
      fact.sizeValue = allocResult.sizeValue;
    }
    return fact;
  }

  return fact;
}

/// Trace a launch pointer operand backward through CIR to find its allocation.
/// Uses a generic loop that peels transparent ops, then dispatches on terminals.
static AllocationFact tryTracePointerToAllocation(Value v, unsigned depth) {
  AllocationFact fact;
  if (depth > 8)
    return fact;

  // Peel all transparent (pointer-forwarding) ops.
  v = stripPointerCasts(v);

  Operation *defOp = v.getDefiningOp();

  // Block argument — try interprocedural: if the enclosing function has a
  // single call site, recurse into the caller's corresponding argument.
  if (!defOp) {
    auto blockArg = dyn_cast<BlockArgument>(v);
    if (!blockArg)
      return fact;
    auto parentFunc = dyn_cast_or_null<cir::FuncOp>(
        blockArg.getOwner()->getParentOp());
    if (!parentFunc)
      return fact;
    unsigned argIdx = blockArg.getArgNumber();
    // Find a unique caller.
    Operation *uniqueCaller = nullptr;
    auto *symbolTableOp = parentFunc->getParentOp();
    if (!symbolTableOp)
      return fact;
    symbolTableOp->walk([&](cir::CallOp call) {
      auto calleeRef = call.getCalleeAttr();
      if (!calleeRef || calleeRef.getValue() != parentFunc.getSymName())
        return;
      if (uniqueCaller) {
        uniqueCaller = nullptr; // multiple callers — give up
        return;
      }
      uniqueCaller = call;
    });
    if (uniqueCaller && argIdx < uniqueCaller->getNumOperands())
      return tryTracePointerToAllocation(uniqueCaller->getOperand(argIdx),
                                         depth + 1);
    return fact;
  }

  // LoadOp — the pointer was loaded from a storage location.
  if (isa<cir::LoadOp>(defOp) && defOp->getNumOperands() >= 1)
    return resolveStorageLocation(defOp->getOperand(0), depth);

  // AllocaOp — check if hipMalloc wrote to it (shouldn't normally reach
  // here since allocas produce ptr-to-ptr, not ptr, but handle defensively).
  if (isa<cir::AllocaOp>(defOp)) {
    auto allocResult = findAllocCallForAlloca(v);
    if (allocResult.kind != AllocKind::Unknown) {
      fact.root = v;
      fact.baseAlign = alignForKind(allocResult.kind);
      fact.kind = allocResult.kind;
      fact.sizeValue = allocResult.sizeValue;
    }
    return fact;
  }

  // GetGlobalOp — global variable storage.  Check if hipMalloc wrote to it.
  if (isa<cir::GetGlobalOp>(defOp)) {
    auto allocResult = findAllocCallForAlloca(v);
    if (allocResult.kind != AllocKind::Unknown) {
      fact.root = v;
      fact.baseAlign = alignForKind(allocResult.kind);
      fact.kind = allocResult.kind;
      fact.sizeValue = allocResult.sizeValue;
    }
    return fact;
  }

  return fact;
}

//===----------------------------------------------------------------------===//
// Read/write analysis on cir.offload.func body
//===----------------------------------------------------------------------===//

enum class AccessMode { None, ReadOnly, WriteOnly, ReadWrite };

/// Scan all uses of a block argument in the offload.func body to determine
/// whether it is only loaded, only stored, or both.
static AccessMode analyzeArgAccess(cir::OffloadFuncOp func, unsigned argIdx) {
  if (argIdx >= func.getNumArguments())
    return AccessMode::ReadWrite;

  BlockArgument arg = func.getArgument(argIdx);
  bool hasRead = false;
  bool hasWrite = false;

  SmallVector<Value, 8> worklist;
  llvm::SmallPtrSet<Value, 16> visited;
  worklist.push_back(arg);

  while (!worklist.empty()) {
    Value current = worklist.pop_back_val();
    if (!visited.insert(current).second)
      continue;
    for (OpOperand &use : current.getUses()) {
      Operation *user = use.getOwner();

      if (isa<cir::LoadOp>(user)) {
        // If loading from the pointer arg (it's the address operand),
        // that's a read. If the load result is a pointer that will be
        // stored/used elsewhere, follow through.
        hasRead = true;
      } else if (isa<cir::StoreOp>(user)) {
        // cir.store %val, %addr: operand 0 = val, operand 1 = addr.
        if (user->getNumOperands() >= 2 && use.getOperandNumber() >= 1) {
          // Our pointer is the address — this is a write through it.
          hasWrite = true;
        } else {
          // Our pointer is the VALUE being stored (operand 0).
          // This is the initial arg → alloca store. Follow the alloca's
          // loads to see how the pointer is actually used.
          Value storeAddr = user->getOperand(1);
          for (OpOperand &allocaUse : storeAddr.getUses()) {
            Operation *allocaUser = allocaUse.getOwner();
            if (isa<cir::LoadOp>(allocaUser)) {
              for (Value result : allocaUser->getResults())
                worklist.push_back(result);
            }
          }
        }
      } else if (isa<cir::CastOp>(user) ||
                 isa<UnrealizedConversionCastOp>(user) ||
                 isa<cir::PtrStrideOp>(user)) {
        // Pointer arithmetic / casts -- follow through.
        for (Value result : user->getResults())
          worklist.push_back(result);
      } else if (isa<cir::CallOp>(user) ||
                 isa<cir::OffloadKernelLaunchOp>(user)) {
        // Passed to a call -- assume read+write conservatively.
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
// Size expression decomposition
//===----------------------------------------------------------------------===//

struct SizeDecomp {
  Value countValue;
  int64_t elemBytes = 0;
};

static Value stripCasts(Value v) {
  while (v) {
    if (auto castOp = v.getDefiningOp<cir::CastOp>()) {
      v = castOp.getSrc();
      continue;
    }
    if (auto castOp = v.getDefiningOp<UnrealizedConversionCastOp>()) {
      if (castOp.getNumOperands() == 1) {
        v = castOp.getOperand(0);
        continue;
      }
    }
    break;
  }
  return v;
}

static std::optional<SizeDecomp> decomposeSize(Value sizeValue) {
  if (!sizeValue)
    return std::nullopt;

  Value v = stripCasts(sizeValue);
  if (!v)
    return std::nullopt;

  Operation *defOp = v.getDefiningOp();
  if (!defOp)
    return std::nullopt;

  StringRef opName = defOp->getName().getStringRef();

  if (opName == "cir.mul" && defOp->getNumOperands() == 2) {
    Value lhs = defOp->getOperand(0);
    Value rhs = defOp->getOperand(1);
    auto lhsConst = cir::tryResolveToConstant(lhs);
    auto rhsConst = cir::tryResolveToConstant(rhs);

    if (rhsConst && *rhsConst > 0)
      return SizeDecomp{stripCasts(lhs), *rhsConst};
    if (lhsConst && *lhsConst > 0)
      return SizeDecomp{stripCasts(rhs), *lhsConst};
  }

  if (opName == "cir.shift" && defOp->getNumOperands() == 2) {
    auto shiftAmt = cir::tryResolveToConstant(defOp->getOperand(1));
    if (shiftAmt && *shiftAmt > 0 && *shiftAmt < 32)
      return SizeDecomp{stripCasts(defOp->getOperand(0)),
                        int64_t(1) << *shiftAmt};
  }

  return SizeDecomp{v, 1};
}

//===----------------------------------------------------------------------===//
// Per-parameter fact set for a kernel
//===----------------------------------------------------------------------===//

struct ParamFacts {
  int64_t align = 0;
  int64_t dereferenceable = 0;
  bool noalias = false;
  bool nonnull = false;
  bool nocapture = false;
  AccessMode access = AccessMode::ReadWrite;
};

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct OffloadPropagatePointerFactsPass
    : public PassWrapper<OffloadPropagatePointerFactsPass,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      OffloadPropagatePointerFactsPass)

  OffloadPropagatePointerFactsPass() = default;
  OffloadPropagatePointerFactsPass(bool enabled) : passEnabled(enabled) {}

  StringRef getArgument() const override {
    return "cir-offload-propagate-pointer-facts";
  }
  StringRef getDescription() const override {
    return "Propagate pointer alignment, noalias, and readonly into offload "
           "kernel arguments";
  }

  void runOnOperation() override {
    if (!passEnabled)
      return;

    ModuleOp module = getOperation();

    // Process each cir.offload.module.
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
    // Gather all launch ops targeting this kernel or its clones.
    SmallVector<cir::OffloadKernelLaunchOp> launchOps;
    StringRef kernelName = kernel.getSymName();
    module.walk([&](cir::OffloadKernelLaunchOp op) {
      StringRef leaf = op.getKernelLeafName();
      if (leaf == kernelName || leaf.starts_with((kernelName + "$").str()))
        launchOps.push_back(op);
    });

    if (launchOps.empty())
      return;

    // Identify which arguments are pointer-typed.
    unsigned numArgs = kernel.getNumArguments();
    SmallVector<bool> isPointerArg(numArgs, false);
    for (unsigned i = 0; i < numArgs; ++i) {
      Type argTy = kernel.getArgumentTypes()[i];
      if (isa<cir::PointerType>(argTy))
        isPointerArg[i] = true;
    }

    // For each launch site, trace each pointer operand to its allocation.
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

      if (allKnown && minAlign > 0)
        metFacts[i].align = minAlign;

      // Meet noalias: for param i, it's noalias if at every site, param i's
      // root is distinct from every other pointer param's root.
      {
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

      // Meet nonnull: all sites must trace to a known allocation.
      {
        bool allNonnull = true;
        for (auto &sf : siteFacts) {
          if (sf.facts[i].kind == AllocKind::Unknown) {
            allNonnull = false;
            break;
          }
        }
        metFacts[i].nonnull = allNonnull;
      }

      // Meet dereferenceable: minimum allocation size across all sites.
      {
        int64_t minDeref = INT64_MAX;
        bool allHaveSize = true;
        for (auto &sf : siteFacts) {
          if (!sf.facts[i].sizeValue) {
            allHaveSize = false;
            break;
          }
          auto sizeConst = cir::tryResolveToConstant(sf.facts[i].sizeValue);
          if (!sizeConst) {
            allHaveSize = false;
            break;
          }
          int64_t deref = *sizeConst - sf.facts[i].byteOffset;
          if (deref <= 0) {
            allHaveSize = false;
            break;
          }
          minDeref = std::min(minDeref, deref);
        }
        if (allHaveSize && minDeref > 0)
          metFacts[i].dereferenceable = minDeref;
      }

      // nocapture: kernel-side analysis — the pointer arg is never stored
      // as a value (never escapes). This is almost always true for GPU
      // kernels. We already walk uses in analyzeArgAccess — reuse that
      // info. A conservative default: set nocapture for all known-alloc
      // pointer args (GPU kernels never capture pointer args to globals).
      // Set nocapture if we know the allocation (nonnull implies known alloc).
      metFacts[i].nocapture = metFacts[i].nonnull;
    }

    // Detect buffer-size-arg correlations: for each pointer arg with a
    // known allocation size, check if the size decomposes into
    // countValue * elemBytes where countValue traces to the same SSA
    // origin as some scalar kernel arg.
    SmallVector<std::pair<unsigned, unsigned>> bufferSizeArgs;
    for (unsigned i = 0; i < numArgs; ++i) {
      if (!isPointerArg[i])
        continue;
      // Check across all launch sites — use the first site's sizeValue
      // (all sites should agree on the structure if not the exact value).
      Value sizeValue;
      for (auto &sf : siteFacts) {
        if (sf.facts[i].sizeValue) {
          sizeValue = sf.facts[i].sizeValue;
          break;
        }
      }
      if (!sizeValue)
        continue;

      auto decomp = decomposeSize(sizeValue);
      if (!decomp || !decomp->countValue)
        continue;

      // Match the count value against kernel launch operands.
      // First try traceToKernelArgIndex (exact SSA terminal match).
      // Fallback: if the count value is a load from an alloca, match
      // launch operands that also load from the same alloca address.
      for (auto launch : launchOps) {
        std::optional<unsigned> countArgIdx =
            cir::traceToKernelArgIndex(decomp->countValue, launch);

        if (!countArgIdx) {
          // Fallback: compare alloca addresses for load-from-alloca pattern.
          Value countAddr;
          Value cv = stripCasts(decomp->countValue);
          if (auto loadOp = cv.getDefiningOp<cir::LoadOp>())
            countAddr = stripCasts(loadOp.getAddr());

          if (countAddr) {
            auto kernelOps = launch.getKernelOperands();
            for (unsigned j = 0; j < kernelOps.size(); ++j) {
              Value kv = stripCasts(kernelOps[j]);
              if (auto kLoad = kv.getDefiningOp<cir::LoadOp>()) {
                if (stripCasts(kLoad.getAddr()) == countAddr) {
                  countArgIdx = j;
                  break;
                }
              }
            }
          }
        }

        if (countArgIdx && *countArgIdx < numArgs &&
            !isPointerArg[*countArgIdx]) {
          bufferSizeArgs.push_back({i, *countArgIdx});
          LLVM_DEBUG(llvm::dbgs()
                     << "  buffer-size-arg: ptr arg " << i << " has count in arg "
                     << *countArgIdx << " (elemBytes=" << decomp->elemBytes
                     << ")\n");
          break;
        }
      }
    }

    // Detect nontemporal store candidates: writeonly args whose allocation
    // root is only copied D2H (or freed) after the kernel, never read on
    // device again.
    SmallVector<unsigned> nontemporalStoreArgs;
    for (unsigned i = 0; i < numArgs; ++i) {
      if (!isPointerArg[i])
        continue;
      if (metFacts[i].access != AccessMode::WriteOnly)
        continue;

      // Check all launch sites — the pattern must hold at every site.
      bool safeAtAllSites = true;
      for (auto launch : launchOps) {
        // Get the allocation root for this arg at this launch site.
        mlir::Value launchArg = launch.getKernelOperands()[i];
        auto fact = tryTracePointerToAllocation(launchArg);
        if (!fact.root) {
          safeAtAllSites = false;
          break;
        }

        // Walk all ops in the enclosing function after the launch.
        // After FlattenCFG, the hipMemcpy may be in a successor block.
        bool foundSafeUse = false;
        bool foundUnsafeUse = false;

        // Find the enclosing function-like op.
        Operation *enclosingFunc = launch->getParentOp();
        while (enclosingFunc && !isa<cir::FuncOp>(enclosingFunc))
          enclosingFunc = enclosingFunc->getParentOp();
        if (!enclosingFunc) {
          safeAtAllSites = false;
          break;
        }

        bool pastLaunch = false;
        enclosingFunc->walk([&](Operation *op) {
          if (foundSafeUse || foundUnsafeUse)
            return;
          if (op == launch.getOperation()) {
            pastLaunch = true;
            return;
          }
          if (!pastLaunch)
            return;

          auto call = dyn_cast<cir::CallOp>(op);
          if (call) {
            auto calleeAttr = call.getCalleeAttr();
            if (!calleeAttr)
              return;
            StringRef callee = calleeAttr.getValue();

            if (callee.contains("hipMemcpy") && !callee.contains("Symbol")) {
              if (call.getNumOperands() >= 4) {
                auto srcFact =
                    tryTracePointerToAllocation(call.getOperand(1));
                if (srcFact.root && srcFact.root == fact.root) {
                  auto dir = cir::tryResolveToConstant(call.getOperand(3));
                  if (dir && *dir == 2)
                    foundSafeUse = true;
                  else
                    foundUnsafeUse = true;
                  return;
                }
              }
            }

            if (callee.contains("hipFree") && call.getNumOperands() >= 1) {
              auto freeFact =
                  tryTracePointerToAllocation(call.getOperand(0));
              if (freeFact.root && freeFact.root == fact.root) {
                foundSafeUse = true;
                return;
              }
            }
          }

          if (auto nextLaunch = dyn_cast<cir::OffloadKernelLaunchOp>(op)) {
            for (auto operand : nextLaunch.getKernelOperands()) {
              auto opFact = tryTracePointerToAllocation(operand);
              if (opFact.root && opFact.root == fact.root) {
                foundUnsafeUse = true;
                return;
              }
            }
          }
        });

        if (foundUnsafeUse || !foundSafeUse) {
          safeAtAllSites = false;
          break;
        }
      }

      if (safeAtAllSites) {
        nontemporalStoreArgs.push_back(i);
        LLVM_DEBUG(llvm::dbgs()
                   << "  nontemporal-store-arg: arg " << i << "\n");
      }
    }

    // Check if we have any facts worth attaching.
    bool hasFacts = !bufferSizeArgs.empty() || !nontemporalStoreArgs.empty();
    int64_t minAlignBytes = 16; // minimum alignment to bother annotating
    for (unsigned i = 0; i < numArgs; ++i) {
      if (!isPointerArg[i])
        continue;
      if ((metFacts[i].align >= minAlignBytes) || metFacts[i].noalias ||
          metFacts[i].nonnull || metFacts[i].nocapture ||
          metFacts[i].dereferenceable > 0 ||
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
      StringRef leafName = launch.getKernelLeafName();
      if (!leafName.contains("$max")) {
        allOnClones = false;
        break;
      }
    }

    if (allOnClones) {
      // Annotate the existing clone(s) in-place.
      llvm::DenseMap<StringRef, cir::OffloadFuncOp> cloneMap;
      for (auto launch : launchOps) {
        StringRef leafName = launch.getKernelLeafName();
        if (cloneMap.count(leafName))
          continue;
        if (auto clone = symTable.lookup<cir::OffloadFuncOp>(leafName))
          cloneMap[leafName] = clone;
      }
      for (auto &[name, clone] : cloneMap)
        applyFactsToFunc(ctx, clone, metFacts, isPointerArg, bufferSizeArgs, nontemporalStoreArgs);
    } else {
      // Handle launches on existing clones.
      for (auto launch : launchOps) {
        StringRef leafName = launch.getKernelLeafName();
        if (!leafName.contains("$max"))
          continue;
        if (auto clone = symTable.lookup<cir::OffloadFuncOp>(leafName))
          applyFactsToFunc(ctx, clone, metFacts, isPointerArg, bufferSizeArgs, nontemporalStoreArgs);
      }

      // Create clone for launches still on the original.
      std::string cloneName =
          llvm::formatv("{0}$ptrfacts", kernel.getSymName()).str();

      auto *cloneOp = kernel->clone();
      auto clone = cast<cir::OffloadFuncOp>(cloneOp);
      SymbolTable::setSymbolName(clone, cloneName);

      applyFactsToFunc(ctx, clone, metFacts, isPointerArg, bufferSizeArgs, nontemporalStoreArgs);

      symTable.insert(clone);
      clone->moveAfter(kernel);

      // Redirect launches on the original to the clone.
      StringRef offloadModName = offloadMod.getSymName();
      for (auto launch : launchOps) {
        StringRef leafName = launch.getKernelLeafName();
        if (leafName.contains("$max"))
          continue; // already handled

        StringRef launchModName =
            launch.getKernelAttr().getRootReference().getValue();

        if (launchModName != offloadModName) {
          auto launchMod =
              module.lookupSymbol<cir::OffloadModuleOp>(launchModName);
          if (launchMod && !launchMod.lookupSymbol(cloneName)) {
            auto origDecl =
                launchMod.lookupSymbol<cir::OffloadFuncOp>(kernel.getSymName());
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

    LLVM_DEBUG({
      llvm::dbgs() << "OffloadPropagatePointerFacts: " << kernel.getSymName()
                   << "\n";
      for (unsigned i = 0; i < numArgs; ++i) {
        if (!isPointerArg[i])
          continue;
        llvm::dbgs() << "  arg " << i << ": align=" << metFacts[i].align
                     << " noalias=" << metFacts[i].noalias
                     << " nonnull=" << metFacts[i].nonnull
                     << " nocapture=" << metFacts[i].nocapture
                     << " deref=" << metFacts[i].dereferenceable
                     << " access=" << static_cast<int>(metFacts[i].access)
                     << "\n";
      }
    });
  }

  void applyFactsToFunc(
      MLIRContext *ctx, cir::OffloadFuncOp func, ArrayRef<ParamFacts> facts,
      ArrayRef<bool> isPointerArg,
      ArrayRef<std::pair<unsigned, unsigned>> bufferSizeArgs = {},
      ArrayRef<unsigned> nontemporalStoreArgs = {}) {
    for (unsigned i = 0; i < facts.size(); ++i) {
      if (!isPointerArg[i])
        continue;

      if (facts[i].align > 0) {
        func.setArgAttr(i, LLVM::LLVMDialect::getAlignAttrName(),
                        IntegerAttr::get(IntegerType::get(ctx, 64),
                                         facts[i].align));
      }

      if (facts[i].noalias) {
        func.setArgAttr(i, LLVM::LLVMDialect::getNoAliasAttrName(),
                        UnitAttr::get(ctx));
      }

      if (facts[i].nonnull) {
        func.setArgAttr(i, LLVM::LLVMDialect::getNonNullAttrName(),
                        UnitAttr::get(ctx));
      }

      if (facts[i].nocapture) {
        func.setArgAttr(i, LLVM::LLVMDialect::getNoCaptureAttrName(),
                        UnitAttr::get(ctx));
      }

      if (facts[i].dereferenceable > 0) {
        func.setArgAttr(
            i, LLVM::LLVMDialect::getDereferenceableAttrName(),
            IntegerAttr::get(IntegerType::get(ctx, 64),
                             facts[i].dereferenceable));
      }

      if (facts[i].access == AccessMode::ReadOnly) {
        func.setArgAttr(i, LLVM::LLVMDialect::getReadonlyAttrName(),
                        UnitAttr::get(ctx));
      } else if (facts[i].access == AccessMode::WriteOnly) {
        func.setArgAttr(i, LLVM::LLVMDialect::getWriteOnlyAttrName(),
                        UnitAttr::get(ctx));
      }
    }

    if (!bufferSizeArgs.empty()) {
      SmallVector<int32_t> flat;
      for (auto &[ptrIdx, countIdx] : bufferSizeArgs) {
        flat.push_back(static_cast<int32_t>(ptrIdx));
        flat.push_back(static_cast<int32_t>(countIdx));
      }
      func->setAttr("buffer_size_args",
                     DenseI32ArrayAttr::get(ctx, flat));
    }

    if (!nontemporalStoreArgs.empty()) {
      SmallVector<int32_t> indices(nontemporalStoreArgs.begin(),
                                   nontemporalStoreArgs.end());
      func->setAttr("nontemporal_store_args",
                     DenseI32ArrayAttr::get(ctx, indices));
    }
  }

  bool passEnabled = true;
};

} // namespace

std::unique_ptr<mlir::Pass>
mlir::createOffloadPropagatePointerFactsPass(bool enabled) {
  return std::make_unique<OffloadPropagatePointerFactsPass>(enabled);
}
