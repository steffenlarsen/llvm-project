//===- InlineLaunchPaths.cpp - Inline the calls that lead to a launch -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Programming models layer the launch behind dispatch functions: Kokkos builds
// the launch geometry in `ParallelFor::execute`, hands it by reference through
// `hip_parallel_launch` and its invoker, and only there calls the stub. Every
// fact the offload passes read from a launch site -- geometry, arguments --
// is then a parameter of the function the launch sits in.
//
// Inlining the calls on the way to a launch puts the launch back next to the
// values it uses. Only those calls are inlined: a call is inlined when its
// callee, possibly through further calls, reaches a launch. Everything else
// stays a call, which bounds the growth to the dispatch chains themselves.
//
// A `<<<>>>` through a kernel pointer the function was handed -- a launch
// helper templated on the kernel's type -- counts as a launch here, although it
// names no kernel yet: inlining the helper into its callers is what makes the
// pointer known, for ResolveIndirectCalls to turn the call into a launch.
//
// The device side has the same shape: a kernel is often only a call to the
// functor or driver that does the work, and its index arithmetic, guards and
// grid-stride loops sit one or more calls down, with their bounds read through
// accessors. There a call is inlined when its callee reaches a read of a launch
// index -- blockIdx, threadIdx, blockDim, gridDim -- and a trivial accessor is
// inlined into any function on such a path, so that the bounds it hands out are
// visible too. The index reads themselves stay calls: they are what the
// offload passes recognise.
//
// Inlining leaves the out-of-line copy of each function behind. Their launches
// would remain in the module with their geometry and arguments still
// parameters, and make every pass that has to see all of a kernel's launches
// give up. The copies nothing uses any more are erased, where their linkage
// allows it.
//
// What may be inlined at all is the CIR inliner interface's decision.
//
//===----------------------------------------------------------------------===//

#include "mlir/Analysis/CallGraph.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Transforms/Inliner.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/DeviceIndex.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/KernelBindingTable.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"

namespace mlir {
#define GEN_PASS_DECL_OFFLOADINLINELAUNCHPATHS
#define GEN_PASS_DEF_OFFLOADINLINELAUNCHPATHS
#include "clang/CIR/Dialect/Passes.h.inc"
} // namespace mlir

using namespace mlir;
using namespace cir;

namespace {

// How many times the inliner revisits a call graph component. A launch path
// is usually a handful of calls deep; this only bounds the pathological case.
constexpr unsigned kMaxInliningIterations = 8;

// The most operations a function may hold and still count as a trivial
// accessor: a member read through `this`, give or take a cast.
constexpr unsigned kMaxAccessorOps = 8;

// Whether `call` launches a kernel, or would once its kernel pointer is known.
static bool isLaunch(cir::CallOp call, mlir::SymbolTableCollection &symbols) {
  return cir::getLaunchedKernel(call, symbols) ||
         cir::isCallUnderLaunchConfiguration(call);
}

// Whether `call` reads a launch index.
static bool isIndexRead(cir::CallOp call, mlir::SymbolTableCollection &) {
  return cir::matchDeviceIndex(call.getOperation()).has_value();
}

// Whether `fn` only hands out something it reads: a single block of a few
// operations, none of which calls or nests a region.
static bool isTrivialAccessor(cir::FuncOp fn) {
  if (fn.isDeclaration() || !fn.getBody().hasOneBlock())
    return false;
  mlir::Block &body = fn.getBody().front();
  if (body.getOperations().size() > kMaxAccessorOps)
    return false;
  return llvm::none_of(body, [](mlir::Operation &op) {
    return mlir::isa<cir::CallOp>(op) || op.getNumRegions() != 0;
  });
}

// The functions of `module` from which a call `isTarget` accepts is reachable
// through direct calls.
static llvm::SmallPtrSet<mlir::Operation *, 16> functionsReaching(
    mlir::ModuleOp module,
    llvm::function_ref<bool(cir::CallOp, mlir::SymbolTableCollection &)>
        isTarget) {
  mlir::SymbolTableCollection symbols;
  llvm::DenseMap<mlir::Operation *, llvm::SmallVector<mlir::Operation *, 4>>
      callers;
  llvm::SmallVector<mlir::Operation *> worklist;
  llvm::SmallPtrSet<mlir::Operation *, 16> reaching;

  module.walk([&](cir::CallOp call) {
    auto caller = call->getParentOfType<cir::FuncOp>();
    if (!caller)
      return;
    if (isTarget(call, symbols)) {
      if (reaching.insert(caller).second)
        worklist.push_back(caller);
      return;
    }
    if (cir::FuncOp callee = call.resolveCalleeInTable(symbols))
      callers[callee].push_back(caller);
  });

  while (!worklist.empty()) {
    mlir::Operation *fn = worklist.pop_back_val();
    for (mlir::Operation *caller : callers.lookup(fn))
      if (reaching.insert(caller).second)
        worklist.push_back(caller);
  }
  return reaching;
}

// Erase the functions named in `candidates` that nothing in `module` uses and
// that may be dropped when unused, then those that only they used, and so on.
static void eraseUnused(mlir::ModuleOp module,
                        llvm::ArrayRef<mlir::StringAttr> candidates) {
  std::optional<mlir::SymbolTable::UseRange> uses =
      mlir::SymbolTable::getSymbolUses(&module.getBodyRegion());
  if (!uses)
    return;
  llvm::DenseMap<mlir::StringAttr, unsigned> useCount;
  for (const mlir::SymbolTable::SymbolUse &use : *uses)
    ++useCount[use.getSymbolRef().getRootReference()];

  mlir::SymbolTable symbolTable(module);
  llvm::SmallVector<cir::FuncOp> worklist;
  for (mlir::StringAttr name : candidates)
    if (auto fn = symbolTable.lookup<cir::FuncOp>(name))
      worklist.push_back(fn);

  for (bool changed = true; changed;) {
    changed = false;
    for (cir::FuncOp &fn : worklist) {
      if (!fn || useCount.lookup(fn.getSymNameAttr()) ||
          !fn.canDiscardOnUseEmpty())
        continue;
      std::optional<mlir::SymbolTable::UseRange> own =
          mlir::SymbolTable::getSymbolUses(fn);
      if (!own)
        continue;
      for (const mlir::SymbolTable::SymbolUse &use : *own)
        --useCount[use.getSymbolRef().getRootReference()];
      symbolTable.erase(fn);
      fn = {};
      changed = true;
    }
  }
}

struct OffloadInlineLaunchPathsPass
    : public impl::OffloadInlineLaunchPathsBase<OffloadInlineLaunchPathsPass> {
  OffloadInlineLaunchPathsPass() = default;
  explicit OffloadInlineLaunchPathsPass(bool indexPaths) {
    this->indexPaths = indexPaths;
  }
  void runOnOperation() override;
};

void OffloadInlineLaunchPathsPass::runOnOperation() {
  mlir::ModuleOp module = getOperation();
  llvm::SmallPtrSet<mlir::Operation *, 16> reachingIndex;
  if (indexPaths)
    reachingIndex = functionsReaching(module, isIndexRead);
  llvm::SmallPtrSet<mlir::Operation *, 16> reaching =
      functionsReaching(module, isLaunch);
  reaching.insert(reachingIndex.begin(), reachingIndex.end());
  if (reaching.empty()) {
    markAllAnalysesPreserved();
    return;
  }

  // By name: the inliner erases private callees it inlined everywhere.
  llvm::SmallVector<mlir::StringAttr> pathFunctions;
  for (mlir::Operation *fn : reaching)
    pathFunctions.push_back(cast<cir::FuncOp>(fn).getSymNameAttr());

  auto isProfitable = [&](const mlir::Inliner::ResolvedCall &resolved) {
    if (cir::matchDeviceIndex(resolved.call))
      return false;
    mlir::Region *callable = resolved.targetNode->getCallableRegion();
    if (!callable)
      return false;
    mlir::Operation *callee = callable->getParentOp();
    if (reaching.contains(callee))
      return true;
    auto caller = resolved.call->getParentOfType<cir::FuncOp>();
    auto accessor = mlir::dyn_cast<cir::FuncOp>(callee);
    return caller && reachingIndex.contains(caller) && accessor &&
           isTrivialAccessor(accessor);
  };
  // No simplification runs between inlining iterations, so the pipeline
  // helper is never called.
  auto noPipeline = [](mlir::Pass &, mlir::OpPassManager &, mlir::Operation *) {
    return mlir::success();
  };
  mlir::InlinerConfig config;
  config.setMaxInliningIterations(kMaxInliningIterations);

  mlir::CallGraph &cg = getAnalysis<mlir::CallGraph>();
  mlir::Inliner inliner(module, cg, *this, getAnalysisManager(), noPipeline,
                        config, isProfitable);
  if (mlir::failed(inliner.doInlining())) {
    signalPassFailure();
    return;
  }
  eraseUnused(module, pathFunctions);
}

} // namespace

std::unique_ptr<Pass> mlir::createOffloadInlineLaunchPathsPass() {
  return std::make_unique<OffloadInlineLaunchPathsPass>();
}

std::unique_ptr<Pass>
mlir::createOffloadInlineLaunchPathsPass(bool indexPaths) {
  return std::make_unique<OffloadInlineLaunchPathsPass>(indexPaths);
}
