//===- PropagatePointerFacts.cpp - Pointer facts into kernel args ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A kernel's pointer parameters arrive as plain pointers: nothing in the
// device module says how they are aligned, whether two of them can overlap, or
// whether the kernel only reads through one. The host knows all three, because
// it allocated the memory and wrote the launch. This pass reads the launch
// sites and states the facts on the kernel.
//
//   align       every HIP allocator returns at least 256-byte aligned memory
//   noalias     two parameters that come from different allocations at every
//               launch cannot overlap
//   readonly    the kernel body only loads through the parameter
//   writeonly   ... only stores through it
//
// Unlike the string attributes some earlier passes emitted, these are real LLVM
// parameter attributes with consumers -- alias analysis, the vectoriser, and
// load/store widening all read them.
//
// Deliberately not ported from the staging implementation: `nonnull`,
// `dereferenceable`, and the `nontemporal-store-args` passthrough. The first
// two are unsound on a failed allocation, and the third is a string nothing in
// llvm/ reads.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/CIRTransformUtils.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelBindingTable.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelCloning.h"

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"

namespace mlir {
#define GEN_PASS_DEF_OFFLOADPROPAGATEPOINTERFACTS
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

using namespace mlir;
using namespace cir;

namespace {

// Every HIP allocator returns memory aligned to at least this. hipMalloc's
// contract is 256 bytes; the managed and host variants are no weaker.
constexpr int64_t kHipAllocAlign = 256;

// Whether `callee` is a HIP allocator that writes the new pointer through its
// first argument.
static bool isHipAllocator(llvm::StringRef callee) {
  return callee == "hipMalloc" || callee == "hipMallocManaged" ||
         callee == "hipHostMalloc" || callee == "hipExtMallocWithFlags";
}

/// Whether some HIP allocator wrote the pointer held in `slot`.
///
/// The allocators take the destination by address -- `hipMalloc(&p, n)` -- so
/// the slot appears as the call's first operand, possibly behind casts.
static bool filledByHipAllocator(mlir::Value slot) {
  llvm::SmallVector<mlir::Value, 4> worklist{slot};
  llvm::SmallPtrSet<void *, 8> seen;

  while (!worklist.empty()) {
    mlir::Value v = worklist.pop_back_val();
    if (!v || !seen.insert(v.getAsOpaquePointer()).second)
      continue;
    for (mlir::Operation *user : v.getUsers()) {
      if (auto cast = mlir::dyn_cast<cir::CastOp>(user)) {
        worklist.push_back(cast.getResult());
        continue;
      }
      auto call = mlir::dyn_cast<cir::CallOp>(user);
      if (!call || call.isIndirect())
        continue;
      std::optional<llvm::StringRef> callee = call.getCallee();
      if (!callee || !isHipAllocator(*callee))
        continue;
      mlir::OperandRange args = call.getArgOperands();
      if (!args.empty() && args[0] == v)
        return true;
    }
  }
  return false;
}

/// The allocation a launch argument refers to, or null.
///
/// Returns the slot the pointer was loaded out of, which stands in for the
/// allocation: two parameters loaded from different slots, each filled by its
/// own allocator call, cannot be the same buffer.
///
/// `traceValueOrigin` stops at `cir.alloca` and does not walk `cir.ptr_stride`,
/// so a launch of `base + k` traces to Unknown and yields no fact -- which is
/// what we want, since the offset would invalidate the alignment.
static mlir::Value allocationRoot(mlir::Value arg) {
  cir::ValueTraceResult trace = cir::traceValueOrigin(arg);
  if (trace.kind != cir::ValueTraceResult::Alloca || !trace.terminal)
    return {};
  return filledByHipAllocator(trace.terminal) ? trace.terminal : mlir::Value{};
}

enum class AccessMode { Unused, ReadOnly, WriteOnly, ReadWrite };

/// How the kernel body reaches memory through parameter `argIdx`.
///
/// Conservative by construction: anything not recognised as a load, a store, or
/// pointer arithmetic is treated as both a read and a write, so an unhandled op
/// can only cost the optimisation, never state something false.
static AccessMode analyzeArgAccess(cir::FuncOp kernel, unsigned argIdx) {
  if (kernel.isDeclaration() || argIdx >= kernel.getNumArguments())
    return AccessMode::ReadWrite;

  bool read = false, wrote = false;
  llvm::SmallVector<mlir::Value, 8> worklist{kernel.getArgument(argIdx)};
  llvm::SmallPtrSet<void *, 16> seen;

  while (!worklist.empty()) {
    mlir::Value v = worklist.pop_back_val();
    if (!seen.insert(v.getAsOpaquePointer()).second)
      continue;
    for (mlir::OpOperand &use : v.getUses()) {
      mlir::Operation *user = use.getOwner();

      if (mlir::isa<cir::LoadOp>(user)) {
        read = true;
        continue;
      }
      if (auto store = mlir::dyn_cast<cir::StoreOp>(user)) {
        // Storing *through* the pointer writes the buffer; storing the pointer
        // itself into a slot is CIRGen spilling the parameter, so follow the
        // reloads to find how it is really used.
        if (store.getAddr() == v) {
          wrote = true;
          continue;
        }
        for (mlir::Operation *slotUser : store.getAddr().getUsers())
          if (auto load = mlir::dyn_cast<cir::LoadOp>(slotUser))
            worklist.push_back(load.getResult());
        continue;
      }
      if (mlir::isa<cir::CastOp, cir::PtrStrideOp>(user)) {
        for (mlir::Value result : user->getResults())
          worklist.push_back(result);
        continue;
      }
      // Handed to something we did not model -- assume the worst.
      read = wrote = true;
    }
  }

  if (read && wrote)
    return AccessMode::ReadWrite;
  if (read)
    return AccessMode::ReadOnly;
  if (wrote)
    return AccessMode::WriteOnly;
  return AccessMode::Unused;
}

/// What every launch site agrees is true of one pointer parameter.
struct ParamFacts {
  bool aligned = false;
  bool noalias = false;
  AccessMode access = AccessMode::ReadWrite;

  bool worthStating() const {
    return aligned || noalias || access == AccessMode::ReadOnly ||
           access == AccessMode::WriteOnly;
  }
};

struct OffloadPropagatePointerFactsPass
    : public impl::OffloadPropagatePointerFactsBase<
          OffloadPropagatePointerFactsPass> {
  void runOnOperation() override;
};

void OffloadPropagatePointerFactsPass::runOnOperation() {
  cir::OffloadContainerOp container = getOperation();
  cir::KernelBindingTable &table = getAnalysis<cir::KernelBindingTable>();
  mlir::MLIRContext *ctx = &getContext();
  mlir::UnitAttr unit = mlir::UnitAttr::get(ctx);
  bool changed = false;

  for (const auto &entry : table) {
    const cir::KernelBinding &binding = entry.second;
    cir::FuncOp stub = binding.hostStub;
    llvm::ArrayRef<cir::LaunchSite> sites = binding.launchSites;
    if (sites.empty() || !stub || binding.deviceKernels.empty())
      continue;

    unsigned numArgs = stub.getNumArguments();
    llvm::SmallVector<bool> isPointer(numArgs, false);
    for (unsigned i = 0; i != numArgs; ++i)
      isPointer[i] = mlir::isa<cir::PointerType>(stub.getArgument(i).getType());

    // The allocation each pointer parameter came from, per site. A null entry
    // means "not traceable here", which blocks every fact for that parameter.
    llvm::SmallVector<llvm::SmallVector<mlir::Value>> roots(sites.size());
    for (auto [s, site] : llvm::enumerate(sites)) {
      roots[s].assign(numArgs, mlir::Value{});
      for (unsigned i = 0; i != numArgs; ++i)
        if (isPointer[i])
          roots[s][i] = allocationRoot(site.getArg(i));
    }

    llvm::SmallVector<ParamFacts> facts(numArgs);
    bool anything = false;
    for (unsigned i = 0; i != numArgs; ++i) {
      if (!isPointer[i])
        continue;

      // Traceable to a HIP allocation at *every* site: the alignment holds on
      // every path that reaches the kernel.
      bool allTraced =
          llvm::all_of(llvm::seq<size_t>(0, sites.size()), [&](size_t s) {
            return static_cast<bool>(roots[s][i]);
          });
      facts[i].aligned = allTraced;

      // Distinct from every other traced pointer parameter, at every site.
      // A site where some other pointer is untraceable is not evidence of
      // distinctness, so it blocks the fact.
      facts[i].noalias =
          allTraced &&
          llvm::all_of(llvm::seq<size_t>(0, sites.size()), [&](size_t s) {
            for (unsigned j = 0; j != numArgs; ++j) {
              if (j == i || !isPointer[j])
                continue;
              if (!roots[s][j] || roots[s][j] == roots[s][i])
                return false;
            }
            return true;
          });

      facts[i].access = analyzeArgAccess(binding.deviceKernels.front(), i);

      // Most of what this pass can derive, ggml already declares: a
      // `__restrict__` parameter reaches us with `llvm.noalias` and a const one
      // with `llvm.readonly` already set. Re-deriving those would clone the
      // kernel for no change at all, so only keep a fact the argument does not
      // already carry.
      cir::FuncOp probe = binding.deviceKernels.front();
      auto already = [&](llvm::StringRef name) {
        return i < probe.getNumArguments() && probe.getArgAttr(i, name);
      };
      if (already(mlir::LLVM::LLVMDialect::getAlignAttrName()))
        facts[i].aligned = false;
      if (already(mlir::LLVM::LLVMDialect::getNoAliasAttrName()))
        facts[i].noalias = false;
      if ((facts[i].access == AccessMode::ReadOnly &&
           already(mlir::LLVM::LLVMDialect::getReadonlyAttrName())) ||
          (facts[i].access == AccessMode::WriteOnly &&
           already(mlir::LLVM::LLVMDialect::getWriteOnlyAttrName())))
        facts[i].access = AccessMode::ReadWrite;

      anything |= facts[i].worthStating();
    }
    if (!anything)
      continue;

    // Stating a fact rewrites the kernel, so it must not reach a launch we did
    // not see. Specialise a copy unless every launch is accounted for.
    cir::SpecializationTarget target = cir::getSpecializationTarget(
        container, entry.first, binding, ".ptrfacts", sites);
    if (!target)
      continue;
    changed |= target.cloned;

    for (cir::FuncOp kernel : target.deviceKernels) {
      if (kernel.isDeclaration())
        continue;
      for (unsigned i = 0,
                    e = std::min<unsigned>(numArgs, kernel.getNumArguments());
           i != e; ++i) {
        if (!isPointer[i] || !facts[i].worthStating())
          continue;
        if (facts[i].aligned)
          kernel.setArgAttr(
              i, mlir::LLVM::LLVMDialect::getAlignAttrName(),
              mlir::IntegerAttr::get(mlir::IntegerType::get(ctx, 64),
                                     kHipAllocAlign));
        if (facts[i].noalias)
          kernel.setArgAttr(i, mlir::LLVM::LLVMDialect::getNoAliasAttrName(),
                            unit);
        if (facts[i].access == AccessMode::ReadOnly)
          kernel.setArgAttr(i, mlir::LLVM::LLVMDialect::getReadonlyAttrName(),
                            unit);
        else if (facts[i].access == AccessMode::WriteOnly)
          kernel.setArgAttr(i, mlir::LLVM::LLVMDialect::getWriteOnlyAttrName(),
                            unit);
        changed = true;
      }
    }
  }

  if (!changed)
    markAllAnalysesPreserved();
}

} // namespace

std::unique_ptr<Pass> mlir::createOffloadPropagatePointerFactsPass() {
  return std::make_unique<OffloadPropagatePointerFactsPass>();
}
