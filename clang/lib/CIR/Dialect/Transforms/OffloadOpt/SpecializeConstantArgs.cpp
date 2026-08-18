//===- SpecializeConstantArgs.cpp - Pin constant args of launch helpers ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Launch arguments are routinely literals one call frame *above* the launch. A
// thin "contiguous" entry point forwards 1 for the higher dimensions into a
// general helper that does the launch:
//
//   void foo_cuda(const float *x, int ne0, int ne1, int ne2) {
//     foo<<<g, b>>>(x, ne0, ne1, ne2);          // ne1, ne2 are parameters
//   }
//   void foo_contiguous(const float *x, int n) { foo_cuda(x, n, 1, 1); }
//
// The kernel then indexes with extents that are in fact 1, and neither the
// helper nor the kernel can see it: at the launch site `ne1` is a block
// argument, so KernelArgConstantPropagation records a non-constant and gives
// up.
//
// This clones the helper once per distinct set of constants its callers pass
// and pins them in the clone, which puts the literals *at* the launch site
// where the existing specialisation can bake them into the kernel. It is the
// same clone-and-pin SpecializeLaunchWrappers performs for a kernel argument,
// with an integer constant in place of a stub address, and it needs no guard:
// the values are compile-time constants, so the retargeted callers are exactly
// the ones the clone is valid for and every other caller keeps the original.
//
// Only parameters that actually reach a launch are pinned. Pinning the rest
// would multiply clones without changing any generated kernel.
//
// When every caller ends up on a clone the original helper is left with no
// uses, and it is erased -- not merely as cleanup. Its launch site would
// otherwise remain in the module passing a runtime value, and the unanimity
// rule in KernelArgConstantPropagation would see the clones and the abandoned
// original disagree and pin nothing, which is exactly what this pass exists to
// avoid.
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/CIRTransformUtils.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelBindingTable.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelCloning.h"

#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormatVariadic.h"

#include <map>

#define DEBUG_TYPE "cir-offload-specialize-constant-args"

namespace mlir {
#define GEN_PASS_DEF_OFFLOADSPECIALIZECONSTANTARGS
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

using namespace mlir;
using namespace cir;

namespace {

// Helpers are layered -- a per-datatype entry point over a generic one -- and
// pinning the outer constants is what makes the inner call site constant.
// Iterate, but bounded.
constexpr unsigned kMaxRounds = 4;

// Upper bound on clones, so a pathological input cannot blow up compile time
// or code size.
constexpr unsigned kMaxSpecializations = 512;

// Infix marking a clone. Also stops a clone being specialised again on a later
// round: its parameters are already pinned, so it would match itself forever.
constexpr llvm::StringRef kConstArgsInfix = ".cargs.";

static Value stripCasts(Value v) {
  while (auto cast = v.getDefiningOp<cir::CastOp>()) {
    if (cast.getKind() != cir::CastKind::bitcast)
      break;
    v = cast.getSrc();
  }
  return v;
}

/// Add to \p out the index of every entry-block parameter of \p fn that \p v
/// is computed from.
///
/// The multi-target sibling of `cir::traceToKernelArgIndex`: a launch operand
/// is often an expression over several parameters, and pinning is worthwhile
/// for each of them.
static void collectParamsReaching(cir::FuncOp fn, Value v,
                                  llvm::SmallSet<unsigned, 8> &out,
                                  llvm::SmallPtrSetImpl<void *> &visited,
                                  unsigned depth = 0) {
  if (!v || depth > 8 || !visited.insert(v.getAsOpaquePointer()).second)
    return;
  v = stripCasts(v);

  if (auto arg = dyn_cast<BlockArgument>(v)) {
    if (arg.getOwner() == &fn.getBody().front())
      out.insert(arg.getArgNumber());
    return;
  }

  Operation *def = v.getDefiningOp();
  if (!def)
    return;

  // A reload of the slot CIRGen spilled the parameter into: follow what was
  // stored, not the address.
  if (auto load = dyn_cast<cir::LoadOp>(def)) {
    auto alloca = stripCasts(load.getAddr()).getDefiningOp<cir::AllocaOp>();
    if (!alloca)
      return;
    for (Operation *user : alloca.getResult().getUsers())
      if (auto store = dyn_cast<cir::StoreOp>(user))
        if (store.getValue() != alloca.getResult())
          collectParamsReaching(fn, store.getValue(), out, visited, depth + 1);
    return;
  }

  for (Value operand : def->getOperands())
    collectParamsReaching(fn, operand, out, visited, depth + 1);
}

/// Whether \p fn is a helper whose constants are worth pinning: it launches
/// something, and it is not itself a device stub or an earlier clone.
static bool isLaunchHelper(cir::FuncOp fn) {
  if (fn.isDeclaration() || fn.getBody().empty())
    return false;
  if (fn.getSymName().contains(kConstArgsInfix))
    return false;
  // A stub *is* the launch; its own parameters are the kernel's, and pinning
  // them here would fight KernelArgConstantPropagation rather than feed it.
  if (fn->getAttrOfType<cir::CUDAKernelNameAttr>(
          cir::CUDAKernelNameAttr::getMnemonic()))
    return false;

  bool launches = false;
  fn.walk([&](cir::CallOp call) {
    if (cir::getLaunchedKernel(call))
      launches = true;
  });
  return launches;
}

/// The integer parameters of \p fn that reach one of its launches.
static llvm::SmallSet<unsigned, 8> pinnableParams(cir::FuncOp fn) {
  Block &entry = fn.getBody().front();
  llvm::SmallSet<unsigned, 8> reaching;
  fn.walk([&](cir::CallOp call) {
    if (!cir::getLaunchedKernel(call))
      return;
    for (Value arg : call.getArgOperands()) {
      llvm::SmallPtrSet<void *, 32> visited;
      collectParamsReaching(fn, arg, reaching, visited);
    }
  });

  llvm::SmallSet<unsigned, 8> pinnable;
  for (unsigned idx : reaching)
    if (mlir::isa<cir::IntType>(entry.getArgument(idx).getType()))
      pinnable.insert(idx);
  return pinnable;
}

/// The attribute pinning \p value into a parameter of type \p intTy, or null
/// when it does not fit.
///
/// The resolver hands back a sign-extended 64-bit value, so a wide unsigned
/// constant arrives looking negative. Narrow explicitly and require the round
/// trip to be exact rather than letting the attribute builder truncate.
static cir::IntAttr fittedIntAttr(cir::IntType intTy, int64_t value) {
  llvm::APInt wide(64, static_cast<uint64_t>(value), /*isSigned=*/true);
  llvm::APInt fitted = wide.trunc(intTy.getWidth());
  bool exact =
      intTy.isSigned() ? (fitted.sext(64) == wide) : (fitted.zext(64) == wide);
  return exact ? cir::IntAttr::get(intTy, fitted) : cir::IntAttr();
}

struct OffloadSpecializeConstantArgsPass
    : public impl::OffloadSpecializeConstantArgsBase<
          OffloadSpecializeConstantArgsPass> {
  void runOnOperation() override;

private:
  bool runOnce(ModuleOp host, unsigned &created);
};

bool OffloadSpecializeConstantArgsPass::runOnce(ModuleOp host,
                                                unsigned &created) {
  SmallVector<cir::FuncOp> helpers;
  host.walk([&](cir::FuncOp fn) {
    if (isLaunchHelper(fn))
      helpers.push_back(fn);
  });
  if (helpers.empty())
    return false;

  SymbolTable symTable(host);
  bool changed = false;
  SmallVector<cir::FuncOp> abandoned;

  for (cir::FuncOp helper : helpers) {
    llvm::SmallSet<unsigned, 8> pinnable = pinnableParams(helper);
    if (pinnable.empty())
      continue;

    // Call sites grouped by the constants they pass. Sites that resolve a
    // different subset of the parameters land in different groups, which is
    // what makes pinning each group sound.
    using ConstTuple = SmallVector<std::pair<unsigned, int64_t>, 4>;
    struct ConstGroup {
      ConstTuple constants;
      SmallVector<cir::CallOp> sites;
    };
    // Keyed by the constant signature; std::map keeps clone naming
    // deterministic without needing a DenseMapInfo for std::string.
    std::map<std::string, ConstGroup> groups;

    std::optional<SymbolTable::UseRange> uses =
        symTable.getSymbolUses(helper.getOperation(), host);
    if (!uses)
      continue;
    unsigned callSites = 0;
    for (SymbolTable::SymbolUse use : *uses) {
      auto call = dyn_cast<cir::CallOp>(use.getUser());
      if (!call || call.isIndirect())
        continue;
      ++callSites;

      ConstTuple constants;
      std::string key;
      for (unsigned idx : pinnable) {
        if (idx >= call.getArgOperands().size())
          continue;
        if (std::optional<int64_t> c =
                cir::tryResolveToConstant(call.getArgOperands()[idx]))
          constants.push_back({idx, *c});
      }
      if (constants.empty())
        continue;
      llvm::sort(constants, [](auto &a, auto &b) { return a.first < b.first; });
      for (auto &[idx, value] : constants)
        key += llvm::formatv("_{0}v{1}", idx, value).str();

      ConstGroup &group = groups[key];
      group.constants = constants;
      group.sites.push_back(call);
    }

    unsigned retargeted = 0;
    // Clones go after the helper, each after the one before it, so they read
    // in the order they were created rather than reversed.
    Operation *insertAfter = helper.getOperation();
    for (auto &[key, group] : groups) {
      if (created >= kMaxSpecializations) {
        LLVM_DEBUG(llvm::dbgs() << "  specialization cap reached, stopping\n");
        return changed;
      }

      std::string cloneName =
          (helper.getSymName() + kConstArgsInfix + key).str();
      if (host.lookupSymbol(cloneName))
        continue;

      auto clone = cast<cir::FuncOp>(helper->clone());
      clone.setSymName(cloneName);
      // The clone serves only the sites retargeted at it below; it must not
      // keep the original's linkage and become reachable with other values.
      clone.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
      clone.setSymVisibility("private");
      // Tell the specialising passes downstream that this clone's launches are
      // a group they may treat apart from the helper's other callers.
      clone->setAttr(cir::kConstArgsCloneAttr,
                     UnitAttr::get(clone.getContext()));
      symTable.insert(clone, Block::iterator(insertAfter->getNextNode()));
      insertAfter = clone.getOperation();

      Block &cloneEntry = clone.getBody().front();
      OpBuilder builder(&cloneEntry, cloneEntry.begin());
      unsigned pinned = 0;
      for (auto &[idx, value] : group.constants) {
        BlockArgument arg = cloneEntry.getArgument(idx);
        auto intTy = mlir::dyn_cast<cir::IntType>(arg.getType());
        if (!intTy)
          continue;
        cir::IntAttr attr = fittedIntAttr(intTy, value);
        if (!attr)
          continue;
        auto constant = cir::ConstantOp::create(builder, clone.getLoc(), attr);
        arg.replaceAllUsesWith(constant.getResult());
        ++pinned;
      }

      // Nothing was pinnable after all, so the clone is a plain copy. Drop it
      // rather than leave a duplicate behind.
      if (!pinned) {
        clone.erase();
        continue;
      }

      for (cir::CallOp site : group.sites)
        site.setCalleeAttr(mlir::FlatSymbolRefAttr::get(clone));
      retargeted += group.sites.size();

      ++created;
      changed = true;
      LLVM_DEBUG(llvm::dbgs()
                 << "  " << helper.getSymName() << " -> " << cloneName << " ("
                 << group.sites.size() << " sites, " << pinned << " pinned)\n");
    }

    // Every caller moved to a clone. Leaving the original behind would leave a
    // launch site passing a runtime value, which is enough to defeat the
    // unanimity rule downstream -- see the file comment.
    if (retargeted && retargeted == callSites)
      abandoned.push_back(helper);
  }

  for (cir::FuncOp helper : abandoned) {
    if (helper.isPublic())
      continue;
    std::optional<SymbolTable::UseRange> uses =
        symTable.getSymbolUses(helper.getOperation(), host);
    if (!uses || !uses->empty())
      continue;
    LLVM_DEBUG(llvm::dbgs()
               << "  erasing abandoned " << helper.getSymName() << "\n");
    symTable.erase(helper);
    changed = true;
  }

  return changed;
}

void OffloadSpecializeConstantArgsPass::runOnOperation() {
  cir::OffloadContainerOp container = getOperation();
  ModuleOp host = container.getHostModule();
  if (!host)
    return;

  unsigned created = 0;
  bool changed = false;
  for (unsigned round = 0; round < kMaxRounds; ++round) {
    if (!runOnce(host, created))
      break;
    changed = true;
  }

  if (!changed)
    markAllAnalysesPreserved();
}

} // namespace

std::unique_ptr<Pass> mlir::createOffloadSpecializeConstantArgsPass() {
  return std::make_unique<OffloadSpecializeConstantArgsPass>();
}
