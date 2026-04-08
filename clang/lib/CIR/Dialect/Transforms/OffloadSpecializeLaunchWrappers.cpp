//===- OffloadSpecializeLaunchWrappers.cpp - Specialize launch wrappers ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A common CUDA/HIP idiom wraps the launch in a helper that takes the kernel
// as a parameter:
//
//   template <typename Kernel, typename... Args>
//   void launch(Kernel kernel, dim3 g, dim3 b, Args &&...args) {
//     kernel<<<g, b>>>(std::forward<Args>(args)...);
//   }
//
// Because `Kernel` deduces to a plain function-pointer type, every kernel
// sharing a signature instantiates the *same* specialization, so inside the
// wrapper the callee is genuinely a runtime value.  Only the call site knows
// which kernel it is, and the launch stays invisible to the offload passes.
//
// This pass clones such a wrapper once per distinct kernel it is called with
// and pins the kernel parameter to that constant inside the clone.
// OffloadDevirtualizeLaunches, which runs immediately after, then turns the
// now-constant callee into a cir.offload.kernel_launch.
//
// Specialization is preferred to a general inliner: CIR does not implement
// DialectInlinerInterface, and the transformation needed here is narrow
// enough that cloning a launch wrapper is both smaller and easier to bound.
//
// Call sites that do not pass a constant kernel keep calling the original,
// which is left untouched.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRAttrs.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/CIRTransformUtils.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormatVariadic.h"

#include <map>

#define DEBUG_TYPE "cir-offload-specialize-launch-wrappers"

using namespace mlir;

namespace {

constexpr llvm::StringRef kPushCallConfiguration = "__hipPushCallConfiguration";

/// Upper bound on clones created, so a pathological input cannot blow up
/// compile time or code size.
constexpr unsigned kMaxSpecializations = 512;

/// Fixed-point iteration bound for nested wrappers.
constexpr unsigned kMaxRounds = 4;

/// Most distinct kernels a single call site may dispatch between.  Beyond a
/// couple the guard chain costs more than the optimization gains.
///
/// Held at 1, so guarded multi-candidate dispatch is off.  A value of 2 would
/// additionally cover call sites that choose between kernel variants differing
/// in one template parameter, which are otherwise invisible to every offload
/// pass; it is disabled because that coverage is not yet exploited by anything
/// downstream, and it materially grows the device module.
///
/// Raising it has a second-order hazard worth understanding first.
/// Specializing lets the downstream passes clone the kernel, and the clone
/// becomes another caller of device helpers the original called exclusively.
/// A helper that has lost its `always_inline` is then subject to the cost
/// model, where the extra caller can flip the decision, and a kernel sitting
/// just under the AMDGPU 256-VGPR occupancy cliff crosses it.
constexpr unsigned kMaxCandidates = 1;

/// Marks the fallback call left behind by a guarded dispatch.  Without it the
/// fallback -- which still holds the original multi-candidate argument --
/// would be re-expanded on every round.
constexpr llvm::StringRef kDispatchedAttr = "cir.offload.dispatched";

/// Infix marking a helper specialized on constant scalar arguments.  Also
/// stops such a clone from being specialized again on later rounds, which it
/// otherwise would be: its own call sites now pass the very constants it was
/// built for.
constexpr llvm::StringRef kConstArgsInfix = "$cargs";

static Value stripCasts(Value v) {
  while (auto cast = v.getDefiningOp<cir::CastOp>()) {
    if (cast.getKind() != cir::CastKind::bitcast)
      break;
    v = cast.getSrc();
  }
  return v;
}

/// Collect the set of kernel stubs \p v can hold, or return false if that set
/// cannot be bounded.
///
/// Handles a direct cir.get_global, and a load from a slot whose every store
/// is a get_global.  The latter covers the common
///
///   kernel_t k;
///   if (cond) k = kernelA; else k = kernelB;
///
/// shape, where the value is not a single constant but is still drawn from a
/// small, statically known set.
static bool collectCandidates(Value v, const llvm::StringSet<> &stubNames,
                              SmallVectorImpl<llvm::StringRef> &out,
                              unsigned maxCandidates) {
  v = stripCasts(v);

  if (auto get = v.getDefiningOp<cir::GetGlobalOp>()) {
    if (!stubNames.contains(get.getName()))
      return false;
    out.push_back(get.getName());
    return true;
  }

  auto load = v.getDefiningOp<cir::LoadOp>();
  if (!load)
    return false;
  auto alloca = stripCasts(load.getAddr()).getDefiningOp<cir::AllocaOp>();
  if (!alloca)
    return false;

  llvm::SmallSetVector<llvm::StringRef, 4> seen;
  for (Operation *user : alloca.getResult().getUsers()) {
    if (auto store = dyn_cast<cir::StoreOp>(user)) {
      if (store.getValue() == alloca.getResult())
        return false; // slot address escapes
      auto get =
          stripCasts(store.getValue()).getDefiningOp<cir::GetGlobalOp>();
      if (!get || !stubNames.contains(get.getName()))
        return false;
      seen.insert(get.getName());
      continue;
    }
    if (isa<cir::LoadOp>(user))
      continue;
    return false;
  }
  if (seen.empty() || seen.size() > maxCandidates)
    return false;

  out.assign(seen.begin(), seen.end());
  return true;
}

/// If \p callee is (transitively) the value of one of \p fn's parameters,
/// return that parameter's index.
///
/// CIRGen spills parameters into an entry-block alloca and reloads them, so
/// the shape is: load <- slot <- single store of a block argument.
static std::optional<unsigned> traceCalleeToParam(cir::FuncOp fn, Value callee) {
  callee = stripCasts(callee);

  auto asBlockArg = [&](Value v) -> std::optional<unsigned> {
    auto arg = dyn_cast<BlockArgument>(v);
    if (!arg || arg.getOwner() != &fn.getBody().front())
      return std::nullopt;
    return arg.getArgNumber();
  };

  if (auto direct = asBlockArg(callee))
    return direct;

  auto load = callee.getDefiningOp<cir::LoadOp>();
  if (!load)
    return std::nullopt;
  auto alloca = stripCasts(load.getAddr()).getDefiningOp<cir::AllocaOp>();
  if (!alloca)
    return std::nullopt;

  cir::StoreOp uniqueStore;
  for (Operation *user : alloca.getResult().getUsers()) {
    if (auto store = dyn_cast<cir::StoreOp>(user)) {
      if (store.getValue() == alloca.getResult())
        return std::nullopt; // slot address escapes
      if (uniqueStore)
        return std::nullopt; // more than one reaching definition
      uniqueStore = store;
      continue;
    }
    if (isa<cir::LoadOp>(user))
      continue;
    return std::nullopt;
  }
  if (!uniqueStore)
    return std::nullopt;

  return asBlockArg(stripCasts(uniqueStore.getValue()));
}

/// Collect the entry-block parameters \p v depends on.
///
/// A launch argument is often derived rather than forwarded -- `ne02*ne03` is
/// passed where `ne02` and `ne03` are the parameters -- so asking only whether
/// an operand *is* a parameter misses the values worth pinning.
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

  // A reload of a parameter spill: follow the value that was stored.
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

/// Specialize a launch helper on scalar arguments that its callers pass as
/// constants.
///
/// Kernel launch arguments are frequently literals one call frame above the
/// launch: a thin "contiguous" wrapper forwards 1 for the higher dimensions
/// into a general helper that does the launch.  The kernel then indexes with
/// runtime extents that are in fact 1, and neither the general helper nor the
/// kernel can see it.  Cloning the helper with those parameters pinned puts
/// the constants at the launch site, where the existing scalar-argument
/// specialization can bake them into the kernel.
///
/// This is the same clone-and-pin the kernel-pointer path above performs, with
/// an integer constant in place of a stub address.  No guard is needed: the
/// values are compile-time constants, so the specialized callers are exactly
/// the ones the clone is valid for, and every other caller keeps the original.
static bool specializeConstantScalarArgs(ModuleOp module,
                                         unsigned &created) {
  bool changed = false;
  MLIRContext *ctx = module.getContext();
  SymbolTable symTable(module);

  SmallVector<cir::FuncOp> helpers;
  module.walk([&](cir::FuncOp fn) {
    if (fn.isDeclaration() || fn.getSymName().contains(kConstArgsInfix))
      return;
    bool hasLaunch = false;
    fn.walk([&](cir::OffloadKernelLaunchOp) { hasLaunch = true; });
    if (hasLaunch)
      helpers.push_back(fn);
  });

  for (cir::FuncOp fn : helpers) {
    Block &entry = fn.getBody().front();

    // Only parameters that actually reach a launch are worth pinning; the
    // rest would multiply clones without changing any generated kernel.
    llvm::SmallSet<unsigned, 8> relevant;
    fn.walk([&](cir::OffloadKernelLaunchOp launch) {
      for (Value v : launch->getOperands()) {
        llvm::SmallPtrSet<void *, 32> visited;
        collectParamsReaching(fn, v, relevant, visited);
      }
    });
    // Only integers are pinnable here; anything else stays a parameter.
    llvm::SmallSet<unsigned, 8> intRelevant;
    for (unsigned idx : relevant)
      if (mlir::isa<cir::IntType>(entry.getArgument(idx).getType()))
        intRelevant.insert(idx);
    relevant = std::move(intRelevant);
    if (relevant.empty())
      continue;

    using ConstTuple = SmallVector<std::pair<unsigned, int64_t>, 4>;
    std::map<std::string, std::pair<ConstTuple, SmallVector<cir::CallOp>>>
        groups;

    std::optional<SymbolTable::UseRange> uses =
        symTable.getSymbolUses(fn.getOperation(), module);
    if (!uses)
      continue;
    for (SymbolTable::SymbolUse use : *uses) {
      auto call = dyn_cast<cir::CallOp>(use.getUser());
      if (!call || call.isIndirect())
        continue;
      ConstTuple tuple;
      std::string key;
      for (unsigned idx : relevant) {
        if (idx >= call.getArgOperands().size())
          continue;
        if (std::optional<int64_t> c =
                cir::tryResolveToConstant(call.getArgOperands()[idx])) {
          tuple.push_back({idx, *c});
          key += llvm::formatv("_{0}v{1}", idx, *c).str();
        }
      }
      if (tuple.empty())
        continue;
      llvm::sort(tuple, [](auto &a, auto &b) { return a.first < b.first; });
      groups[key].first = tuple;
      groups[key].second.push_back(call);
    }

    for (auto &[key, group] : groups) {
      if (created >= kMaxSpecializations)
        return changed;
      std::string cloneName =
          (fn.getSymName() + kConstArgsInfix + key).str();
      auto clone = dyn_cast_or_null<cir::FuncOp>(symTable.lookup(cloneName));
      if (!clone) {
        auto *cloneOp = fn->clone();
        clone = cast<cir::FuncOp>(cloneOp);
        SymbolTable::setSymbolName(clone, cloneName);
        clone.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
        SymbolTable::setSymbolVisibility(clone,
                                         SymbolTable::Visibility::Private);
        symTable.insert(clone);
        clone->moveAfter(fn);

        OpBuilder builder(ctx);
        Block &cloneEntry = clone.getBody().front();
        builder.setInsertionPointToStart(&cloneEntry);
        for (auto &[idx, value] : group.first) {
          BlockArgument arg = cloneEntry.getArgument(idx);
          auto intTy = mlir::dyn_cast<cir::IntType>(arg.getType());
          if (!intTy)
            continue;
          // The resolver hands back a sign-extended 64-bit value, so a wide
          // unsigned constant arrives looking negative.  Narrow it to the
          // parameter explicitly and require the round trip to be exact --
          // building the attribute from the raw value asserts inside APInt
          // when it does not fit.
          llvm::APInt wide(64, static_cast<uint64_t>(value), /*isSigned=*/true);
          llvm::APInt fitted = wide.trunc(intTy.getWidth());
          bool exact = intTy.isSigned() ? (fitted.sext(64) == wide)
                                        : (fitted.zext(64) == wide);
          if (!exact)
            continue;
          auto cst = cir::ConstantOp::create(builder, clone.getLoc(),
                                             cir::IntAttr::get(intTy, fitted));
          arg.replaceAllUsesWith(cst.getResult());
        }
        ++created;
        LLVM_DEBUG(llvm::dbgs()
                   << "OffloadSpecializeLaunchWrappers: " << cloneName << "\n");
      }

      for (cir::CallOp call : group.second) {
        call.setCalleeAttr(FlatSymbolRefAttr::get(ctx, cloneName));
        changed = true;
      }
    }
  }
  return changed;
}

struct OffloadSpecializeLaunchWrappersPass
    : public PassWrapper<OffloadSpecializeLaunchWrappersPass,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      OffloadSpecializeLaunchWrappersPass)

  OffloadSpecializeLaunchWrappersPass() = default;
  OffloadSpecializeLaunchWrappersPass(bool enabled) : passEnabled(enabled) {}

  StringRef getArgument() const override {
    return "cir-offload-specialize-launch-wrappers";
  }
  StringRef getDescription() const override {
    return "Clone kernel-launch wrappers per constant kernel argument";
  }

  void runOnOperation() override {
    if (!passEnabled)
      return;

    // Specializing an outer wrapper turns its kernel parameter into a
    // constant, which can expose an inner wrapper that was not specializable
    // before.  Launch helpers are routinely nested two levels deep, so
    // iterate to a fixed point rather than making a single pass.
    unsigned constArgClones = 0;
    for (unsigned round = 0; round < kMaxRounds; ++round) {
      bool changed = runOnce();
      // Pinning a constant can expose a launch whose arguments only now
      // resolve, and specializing a wrapper can expose the caller above it,
      // so both run to a shared fixed point.
      changed |= specializeConstantScalarArgs(getOperation(), constArgClones);
      if (!changed)
        break;
    }
  }

  /// Rewrite a call whose kernel argument is one of \p candidates into a
  /// chain of guarded calls, each passing a single constant kernel:
  ///
  ///   if (k == @A)      wrapper(@A, ...)
  ///   else if (k == @B) wrapper(@B, ...)
  ///   else              wrapper(k, ...)     // unchanged fallback
  ///
  /// This is indirect-call promotion: the guard makes it correct regardless
  /// of whether the candidate set is complete, and the retained fallback
  /// means a missed candidate costs nothing but the optimization.  The
  /// single-constant calls it produces are what the specializer can then
  /// clone for.
  ///
  /// Only valid for a void call -- with a result the branches would need a
  /// merge, and every launch wrapper returns void.
  bool emitGuardedDispatch(cir::CallOp call, unsigned paramIdx,
                           ArrayRef<llvm::StringRef> candidates) {
    assert(call.getNumResults() == 0 && "guarded dispatch needs a void call");
    MLIRContext *ctx = call.getContext();
    Location loc = call.getLoc();
    Value kernelArg = call.getArgOperands()[paramIdx];

    OpBuilder builder(call);
    auto boolTy = cir::BoolType::get(ctx);
    SmallVector<cir::IfOp> chain;

    // Build the chain outside-in, nesting each subsequent test in the else
    // region of the previous one.
    for (llvm::StringRef cand : candidates) {
      auto candPtr = cir::GetGlobalOp::create(builder, loc,
                                              kernelArg.getType(), cand);
      auto isCand = cir::CmpOp::create(builder, loc, boolTy,
                                       cir::CmpOpKind::eq, kernelArg,
                                       candPtr.getResult());
      // Empty callbacks: the regions are populated below, including their
      // cir.yield terminators.
      auto emptyBuilder = [](OpBuilder &, Location) {};
      auto ifOp = cir::IfOp::create(builder, loc, isCand.getResult(),
                                    /*withElseRegion=*/true, emptyBuilder,
                                    emptyBuilder);

      // Then: call the wrapper with this candidate pinned.
      {
        OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToStart(&ifOp.getThenRegion().front());
        auto pinned =
            cir::GetGlobalOp::create(builder, loc, kernelArg.getType(), cand);
        SmallVector<Value> args(call.getArgOperands());
        args[paramIdx] = pinned.getResult();
        cir::CallOp::create(builder, loc, call.getCalleeAttr(),
                            cir::VoidType::get(ctx), ValueRange(args));
        cir::YieldOp::create(builder, loc);
      }

      chain.push_back(ifOp);
      // Subsequent tests (and finally the fallback) go in the else region.
      builder.setInsertionPointToStart(&ifOp.getElseRegion().front());
    }

    // Innermost else: the original call, untouched, plus the terminator.
    call->moveBefore(builder.getInsertionBlock(),
                     builder.getInsertionPoint());
    builder.setInsertionPointAfter(call);
    cir::YieldOp::create(builder, loc);
    call->setAttr(kDispatchedAttr, UnitAttr::get(ctx));

    // Every enclosing else region holds only the next test in the chain and
    // still needs its own terminator.
    for (cir::IfOp ifOp : chain) {
      Block &elseBlock = ifOp.getElseRegion().front();
      if (elseBlock.empty() ||
          !elseBlock.back().hasTrait<OpTrait::IsTerminator>()) {
        OpBuilder::InsertionGuard guard(builder);
        builder.setInsertionPointToEnd(&elseBlock);
        cir::YieldOp::create(builder, loc);
      }
    }

    LLVM_DEBUG(llvm::dbgs()
               << "OffloadSpecializeLaunchWrappers: guarded dispatch over "
               << candidates.size() << " candidates\n");
    return true;
  }

  /// Returns true if anything changed.
  bool runOnce() {
    ModuleOp module = getOperation();
    MLIRContext *ctx = &getContext();
    bool changed = false;

    // Only stubs are interesting as constant callees.
    llvm::StringSet<> stubNames;
    module.walk([&](cir::FuncOp fn) {
      if (fn->hasAttr(cir::CUDAKernelNameAttr::getMnemonic()))
        stubNames.insert(fn.getSymName());
    });
    if (stubNames.empty())
      return false;

    // Wrapper -> index of the parameter that supplies the launched kernel.
    llvm::MapVector<cir::FuncOp, unsigned> wrappers;
    module.walk([&](cir::FuncOp fn) {
      if (fn.isDeclaration() || fn.getBody().empty())
        return;
      // Must contain a launch.
      bool hasLaunchConfig = false;
      fn.walk([&](cir::CallOp call) {
        std::optional<llvm::StringRef> callee = call.getCallee();
        if (callee && *callee == kPushCallConfiguration)
          hasLaunchConfig = true;
      });
      if (!hasLaunchConfig)
        return;

      std::optional<unsigned> paramIdx;
      bool ambiguous = false;
      fn.walk([&](cir::CallOp call) {
        if (!call.isIndirect())
          return;
        auto idx = traceCalleeToParam(fn, call.getIndirectCall());
        if (!idx) {
          ambiguous = true;
          return;
        }
        if (paramIdx && *paramIdx != *idx)
          ambiguous = true;
        paramIdx = idx;
      });
      if (ambiguous || !paramIdx)
        return;
      wrappers.insert({fn, *paramIdx});
    });

    // A wrapper need not contain the launch itself: it may just forward the
    // kernel to another wrapper, which is common when a library layers a
    // generic launch helper over a per-algorithm one.  Propagate the wrapper
    // property backwards to such forwarders until it stops growing.
    for (bool grew = true; grew;) {
      grew = false;
      SmallVector<std::pair<cir::FuncOp, unsigned>> found;
      for (auto &[callee, calleeParamIdx] : wrappers) {
        std::optional<SymbolTable::UseRange> uses =
            SymbolTable::getSymbolUses(callee.getOperation(), module);
        if (!uses)
          continue;
        for (SymbolTable::SymbolUse use : *uses) {
          auto call = dyn_cast<cir::CallOp>(use.getUser());
          if (!call || call.isIndirect())
            continue;
          if (calleeParamIdx >= call.getArgOperands().size())
            continue;
          auto caller = call->getParentOfType<cir::FuncOp>();
          if (!caller || caller.getBody().empty() || wrappers.count(caller))
            continue;
          // Is the forwarded kernel one of the caller's own parameters?
          if (auto idx = traceCalleeToParam(
                  caller, call.getArgOperands()[calleeParamIdx]))
            found.push_back({caller, *idx});
        }
      }
      for (auto &[fn, idx] : found)
        if (wrappers.insert({fn, idx}).second)
          grew = true;
    }

    if (wrappers.empty())
      return false;

    LLVM_DEBUG({
      llvm::dbgs() << "  round: wrappers =";
      for (auto &[w, i] : wrappers)
        llvm::dbgs() << " " << w.getSymName() << "[" << i << "]";
      llvm::dbgs() << "\n";
    });
    SymbolTable symTable(module);
    unsigned created = 0;

    for (auto &[wrapper, paramIdx] : wrappers) {
      // Group call sites by the kernel stub they pass.  A site whose kernel
      // is drawn from a small candidate set is first rewritten into a guarded
      // chain of single-candidate calls, which then group normally.
      llvm::MapVector<llvm::StringRef, SmallVector<cir::CallOp>> byStub;
      std::optional<SymbolTable::UseRange> uses =
          symTable.getSymbolUses(wrapper.getOperation(), module);
      if (!uses)
        continue;
      SmallVector<cir::CallOp> multiCandidateSites;
      for (SymbolTable::SymbolUse use : *uses) {
        auto call = dyn_cast<cir::CallOp>(use.getUser());
        if (!call || call.isIndirect())
          continue;
        if (paramIdx >= call.getArgOperands().size())
          continue;
        Value kernelArg = call.getArgOperands()[paramIdx];
        SmallVector<llvm::StringRef> candidates;
        if (!collectCandidates(kernelArg, stubNames, candidates,
                               kMaxCandidates))
          continue;
        // One reaching kernel -- specialize directly.  Several -- rewrite the
        // site into a guarded chain first; the resulting single-candidate
        // calls are picked up on a later round.
        if (candidates.size() == 1)
          byStub[candidates.front()].push_back(call);
        else if (!call->hasAttr(kDispatchedAttr) && call.getNumResults() == 0)
          multiCandidateSites.push_back(call);
      }

      // Rewriting a multi-candidate site produces new single-candidate calls;
      // they are picked up on the next round rather than mid-iteration.
      for (cir::CallOp call : multiCandidateSites) {
        SmallVector<llvm::StringRef> candidates;
        if (!collectCandidates(call.getArgOperands()[paramIdx], stubNames,
                               candidates, kMaxCandidates))
          continue;
        if (emitGuardedDispatch(call, paramIdx, candidates))
          changed = true;
      }

      for (auto &[stubName, calls] : byStub) {
        if (created >= kMaxSpecializations) {
          LLVM_DEBUG(llvm::dbgs()
                     << "OffloadSpecializeLaunchWrappers: clone budget "
                        "exhausted, leaving remaining wrappers alone\n");
          break;
        }

        std::string cloneName =
            llvm::formatv("{0}$launch${1}", wrapper.getSymName(), stubName)
                .str();
        auto clone = dyn_cast_or_null<cir::FuncOp>(symTable.lookup(cloneName));
        if (!clone) {
          auto *cloneOp = wrapper->clone();
          clone = cast<cir::FuncOp>(cloneOp);
          SymbolTable::setSymbolName(clone, cloneName);
          // The clone is an implementation detail of this TU.
          clone.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
          SymbolTable::setSymbolVisibility(clone,
                                           SymbolTable::Visibility::Private);
          symTable.insert(clone);
          clone->moveAfter(wrapper);

          // Pin the kernel parameter to the constant stub inside the clone.
          Block &entry = clone.getBody().front();
          OpBuilder builder(ctx);
          builder.setInsertionPointToStart(&entry);
          BlockArgument kernelArg = entry.getArgument(paramIdx);
          auto get = cir::GetGlobalOp::create(builder, clone.getLoc(),
                                              kernelArg.getType(), stubName);
          kernelArg.replaceAllUsesWith(get.getResult());
          ++created;

          LLVM_DEBUG(llvm::dbgs()
                     << "OffloadSpecializeLaunchWrappers: " << cloneName
                     << "\n");
        }

        for (cir::CallOp call : calls) {
          call.setCalleeAttr(FlatSymbolRefAttr::get(ctx, cloneName));
          changed = true;
        }
      }
    }
    return changed;
  }

  bool passEnabled = true;
};

} // namespace

std::unique_ptr<mlir::Pass>
mlir::createOffloadSpecializeLaunchWrappersPass(bool enabled) {
  return std::make_unique<OffloadSpecializeLaunchWrappersPass>(enabled);
}