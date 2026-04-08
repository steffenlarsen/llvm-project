//===- SplitSingleSource.cpp - Split GPU module into primary+deferred -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass splits a unified single-source GPU module into:
//   - Primary gpu.module: kernels with at least one observable launch site.
//   - Deferred gpu.module: kernels with no launch site (optional).
//
// The input module produced by CIRGen contains:
//   - Host-side cir.func / func.func ops with gpu.launch_func launch sites.
//   - A single gpu.module @offload_device_module containing gpu.func ops for
//     all device functions (__global__ and __device__) and cir.global ops for
//     device-qualified globals (identified by their addr_space attribute).
//
// After this pass all device global ops have been moved or replicated
// into the appropriate output gpu.modules.  gpu.launch_func ops in the host
// code already reference the primary module name and are NOT modified.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/GPU/Transforms/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/GPU/IR/GPUDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/RegionUtils.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"

namespace mlir {

#define GEN_PASS_DEF_GPUSPLITSINGLESOURCEPASS
#include "mlir/Dialect/GPU/Transforms/Passes.h.inc"

} // namespace mlir

using namespace mlir;
using namespace mlir::gpu;

namespace {

//===----------------------------------------------------------------------===//
// Dependency analysis
//===----------------------------------------------------------------------===//

/// Return true if op is a cir.global with a device-side addr_space attribute.
static bool isCIRGlobal(Operation *op) {
  return op->getName().getStringRef() == "cir.global";
}

/// Return the target address space value of a cir.global, or -1 if none.
/// AMDGPU: 1=device, 3=shared(LDS), 4=constant, 0=default/managed.
///
/// cir.global stores its address space as a TargetAddressSpaceAttr (from the
/// CIR dialect in the Clang tree).  We cannot include CIR headers here, so we
/// print the attr to string and parse the integer out of it.
/// Expected format: "#cir.target_address_space<target<N>>"
static int getCIRGlobalAddrSpace(Operation *op) {
  mlir::Attribute attr = op->getAttr("addr_space");
  if (!attr)
    return -1;
  std::string s;
  llvm::raw_string_ostream os(s);
  attr.print(os);
  // Find the inner integer after "target<".
  auto pos = s.find("target<");
  if (pos == std::string::npos)
    return -1;
  pos += 7; // length of "target<"
  auto end = s.find('>', pos);
  if (end == std::string::npos)
    return -1;
  int val = 0;
  if (llvm::StringRef(s).substr(pos, end - pos).getAsInteger(10, val))
    return -1;
  return val;
}

/// Return true if this device global cannot be safely replicated across
/// gpu.modules.  Replicating __device__, __constant__, or __managed__ globals
/// would create distinct device-side allocations with the same symbolic name,
/// breaking host-side symbol lookups (hipMemcpyToSymbol, etc.).
///
/// AMDGPU addr spaces: 1=device, 4=constant, 0=managed; 3=shared (LDS).
static bool isNonReplicableCIRGlobal(Operation *op) {
  int as = getCIRGlobalAddrSpace(op);
  return as == 1 || as == 4 || as == 0; // device / constant / managed
}

/// Build direct-dependency and reverse-dependency maps across all gpu.func
/// bodies in \p inputMod.
///
///  deps[fn]     = symbols (gpu.func / cir.global) that fn's body
///                 directly references.
///  revDeps[sym] = gpu.func names that directly reference sym.
static void
buildDepMap(gpu::GPUModuleOp inputMod,
            DenseMap<StringAttr, SmallVector<StringAttr>> &deps,
            DenseMap<StringAttr, SmallVector<StringAttr>> &revDeps) {
  inputMod.walk([&](gpu::GPUFuncOp fn) {
    auto uses = SymbolTable::getSymbolUses(fn.getOperation(), inputMod);
    if (!uses)
      return;
    SmallVector<StringAttr> &fnDeps = deps[fn.getNameAttr()];
    for (SymbolTable::SymbolUse use : *uses) {
      StringAttr ref = use.getSymbolRef().getRootReference();
      Operation *refOp = inputMod.lookupSymbol(ref);
      if (isa_and_nonnull<gpu::GPUFuncOp>(refOp) ||
          (refOp && isCIRGlobal(refOp))) {
        fnDeps.push_back(ref);
        revDeps[ref].push_back(fn.getNameAttr());
      }
    }
  });
}

/// Starting from the seed set \p seeds, walk forward through \p deps and
/// collect every symbol transitively reachable.  Seeds are included in
/// \p reachable.
static void
computeReachableFrom(const DenseMap<StringAttr, SmallVector<StringAttr>> &deps,
                     const DenseSet<StringAttr> &seeds,
                     DenseSet<StringAttr> &reachable) {
  SmallVector<StringAttr> worklist(seeds.begin(), seeds.end());
  reachable.insert(seeds.begin(), seeds.end());
  while (!worklist.empty()) {
    StringAttr sym = worklist.pop_back_val();
    auto it = deps.find(sym);
    if (it == deps.end())
      continue;
    for (StringAttr dep : it->second) {
      if (reachable.insert(dep).second)
        worklist.push_back(dep);
    }
  }
}

/// Compute two sets:
///
///  \p primaryKernels   — kernels that must reside in the primary gpu.module,
///                        either because they have an observable launch site,
///                        or because they transitively depend on a resource
///                        that cannot be replicated.
///
///  \p primaryOnlyHelpers — device helpers whose clone must reside only in the
///                          primary module (they transitively reference a
///                          non-replicable global).  Replicable helpers are
///                          NOT in this set and will be cloned into both.
static void computePrimarySet(ModuleOp module, gpu::GPUModuleOp inputMod,
                              DenseSet<StringAttr> &primaryKernels,
                              DenseSet<StringAttr> &primaryOnlyHelpers) {
  DenseMap<StringAttr, SmallVector<StringAttr>> deps;
  DenseMap<StringAttr, SmallVector<StringAttr>> revDeps;
  buildDepMap(inputMod, deps, revDeps);

  // Mark non-replicable globals.
  DenseSet<StringAttr> nonReplicableGlobals;
  inputMod.walk([&](Operation *op) {
    if (!isCIRGlobal(op))
      return;
    if (isNonReplicableCIRGlobal(op))
      if (auto sym = op->getAttrOfType<StringAttr>(
              mlir::SymbolTable::getSymbolAttrName()))
        nonReplicableGlobals.insert(sym);
  });

  // Classify device helpers: a helper is primary-only if it (transitively)
  // references a non-replicable global.
  SmallVector<StringAttr> worklist;
  inputMod.walk([&](gpu::GPUFuncOp fn) {
    if (fn.isKernel())
      return; // Skip kernels here.
    for (StringAttr dep : deps[fn.getNameAttr()])
      if (nonReplicableGlobals.count(dep)) {
        worklist.push_back(fn.getNameAttr());
        break;
      }
  });

  while (!worklist.empty()) {
    StringAttr item = worklist.pop_back_val();
    if (!primaryOnlyHelpers.insert(item).second)
      continue;
    for (StringAttr caller : revDeps[item]) {
      auto *callerFn = inputMod.lookupSymbol(caller);
      if (!callerFn)
        continue;
      auto callerGpuFn = dyn_cast<gpu::GPUFuncOp>(callerFn);
      if (!callerGpuFn || !callerGpuFn.isKernel())
        worklist.push_back(caller);
    }
  }

  // Seed primary kernels from gpu.launch_func callee nested symbol refs.
  // The callee is @offload_device_module::@kernelName — extract leaf ref.
  module.walk([&](gpu::LaunchFuncOp launch) {
    StringAttr kernelName = launch.getKernel().getLeafReference();
    primaryKernels.insert(kernelName);
  });

  // Pull unreferenced kernels into primary if they depend on a non-replicable
  // resource.
  bool changed = true;
  while (changed) {
    changed = false;
    inputMod.walk([&](gpu::GPUFuncOp fn) {
      if (!fn.isKernel())
        return;
      if (primaryKernels.count(fn.getNameAttr()))
        return;
      for (StringAttr dep : deps[fn.getNameAttr()]) {
        if (nonReplicableGlobals.count(dep) || primaryOnlyHelpers.count(dep)) {
          primaryKernels.insert(fn.getNameAttr());
          changed = true;
          return;
        }
      }
    });
  }
}

//===----------------------------------------------------------------------===//
// Pass
//===----------------------------------------------------------------------===//

struct SplitSingleSourcePass
    : impl::GpuSplitSingleSourcePassBase<SplitSingleSourcePass> {

  using GpuSplitSingleSourcePassBase::GpuSplitSingleSourcePassBase;

  void runOnOperation() override {
    ModuleOp module = getOperation();
    MLIRContext *ctx = module.getContext();
    OpBuilder builder(ctx);

    // Find the input gpu.module produced by CIRGen.
    gpu::GPUModuleOp inputMod =
        module.lookupSymbol<gpu::GPUModuleOp>("offload_device_module");
    if (!inputMod)
      return; // Nothing to split.

    // Collect gpu.func ops from the input module.
    SmallVector<gpu::GPUFuncOp> gpuFuncs;
    inputMod.walk([&](gpu::GPUFuncOp fn) { gpuFuncs.push_back(fn); });

    if (gpuFuncs.empty())
      return; // No device functions — nothing to do.

    // ------------------------------------------------------------------ //
    // Determine dead-kernel handling mode.
    // ------------------------------------------------------------------ //
    bool doNone = (deadKernelAction == "none");
    bool doDiscard = (deadKernelAction == "discard");
    bool doSplit = !doNone && !doDiscard; // "split" is the default

    // ------------------------------------------------------------------ //
    // Dependency analysis: compute which kernels and helpers go where.
    // ------------------------------------------------------------------ //
    DenseSet<StringAttr> primaryKernels;
    DenseSet<StringAttr> primaryOnlyHelpers;
    DenseSet<StringAttr> reachable; // Discard mode only

    if (doNone) {
      inputMod.walk([&](gpu::GPUFuncOp fn) {
        if (fn.isKernel())
          primaryKernels.insert(fn.getNameAttr());
      });
    } else if (doDiscard) {
      DenseMap<StringAttr, SmallVector<StringAttr>> deps, revDeps;
      buildDepMap(inputMod, deps, revDeps);
      module.walk([&](gpu::LaunchFuncOp launch) {
        primaryKernels.insert(launch.getKernel().getLeafReference());
      });
      computeReachableFrom(deps, primaryKernels, reachable);
    } else {
      computePrimarySet(module, inputMod, primaryKernels, primaryOnlyHelpers);
    }

    // ------------------------------------------------------------------ //
    // Step 0: Collect cir.global ops from the input gpu.module.
    // ------------------------------------------------------------------ //
    SmallVector<Operation *> globalVars;
    inputMod.walk([&](Operation *op) {
      if (isCIRGlobal(op))
        globalVars.push_back(op);
    });

    // ------------------------------------------------------------------ //
    // Step 1: Create the output gpu.module(s).
    //
    // The primary module always exists.  The deferred module is created only
    // in Split mode and erased at the end if it contains no kernels.
    // ------------------------------------------------------------------ //
    builder.setInsertionPointAfter(inputMod);
    auto gpuModulePrimary =
        gpu::GPUModuleOp::create(builder, inputMod.getLoc(), gpuModuleName);
    gpu::GPUModuleOp gpuModuleDeferred;
    if (doSplit)
      gpuModuleDeferred = gpu::GPUModuleOp::create(builder, inputMod.getLoc(),
                                                   deferredGpuModuleName);

    // ------------------------------------------------------------------ //
    // Step 1b: Move cir.global ops into the output gpu.module(s).
    //
    // Placement rules (AMDGPU addr spaces):
    //   - addr_space 1 (device), 4 (constant), 0 (managed): primary only.
    //   - addr_space 3 (shared/LDS): replicated into every gpu.module.
    //   - No addr_space / other: erase (no device representation needed).
    // ------------------------------------------------------------------ //
    for (Operation *gv : globalVars) {
      int as = getCIRGlobalAddrSpace(gv);
      if (as == 1 || as == 4 || as == 0) {
        // Non-replicable: move to primary only.
        gv->moveBefore(gpuModulePrimary.getBody(),
                       gpuModulePrimary.getBody()->begin());
      } else if (as == 3) {
        // Shared (LDS): replicate into deferred if it exists.
        if (doSplit && gpuModuleDeferred) {
          OpBuilder::InsertionGuard guard(builder);
          builder.setInsertionPointToStart(gpuModuleDeferred.getBody());
          builder.clone(*gv);
        }
        gv->moveBefore(gpuModulePrimary.getBody(),
                       gpuModulePrimary.getBody()->begin());
      } else {
        gv->erase();
      }
    }

    // ------------------------------------------------------------------ //
    // Step 1c: Clone external declarations from the outer module into the
    // output gpu.module(s).
    //
    // Device function bodies may reference external symbols declared in the
    // outer module (e.g. ROCm bitcode library functions).  gpu.module is
    // IsolatedFromAbove; after moving bodies in, lookupNearestSymbolFrom
    // stops at the gpu.module boundary.  Clone declarations so that calls
    // inside gpu.func bodies can resolve their callees.
    // ------------------------------------------------------------------ //
    DenseSet<StringAttr> referencedFromDevice;
    inputMod.walk([&](Operation *op) {
      if (auto calleeAttr = op->getAttrOfType<FlatSymbolRefAttr>("callee"))
        referencedFromDevice.insert(calleeAttr.getAttr());
      else if (auto calleeAttr = op->getAttrOfType<SymbolRefAttr>("callee"))
        referencedFromDevice.insert(calleeAttr.getRootReference());
      if (auto nameAttr = op->getAttrOfType<FlatSymbolRefAttr>("name"))
        referencedFromDevice.insert(nameAttr.getAttr());
      if (auto gnAttr = op->getAttrOfType<FlatSymbolRefAttr>("global_name"))
        referencedFromDevice.insert(gnAttr.getAttr());
    });

    auto cloneIntoGpuModule = [&](Operation *decl, gpu::GPUModuleOp gpuMod) {
      if (gpuMod.lookupSymbol(decl->getAttrOfType<StringAttr>(
              mlir::SymbolTable::getSymbolAttrName())))
        return;
      OpBuilder::InsertionGuard guard(builder);
      builder.setInsertionPointToStart(gpuMod.getBody());
      builder.clone(*decl);
    };

    for (StringAttr sym : referencedFromDevice) {
      if (inputMod.lookupSymbol(sym))
        continue; // Already in the input gpu.module.
      Operation *decl = module.lookupSymbol(sym);
      if (!decl)
        continue;
      bool isDeclaration =
          decl->getNumRegions() == 0 ||
          llvm::all_of(decl->getRegions(), [](Region &r) { return r.empty(); });
      if (!isDeclaration)
        continue;
      cloneIntoGpuModule(decl, gpuModulePrimary);
      if (doSplit && gpuModuleDeferred)
        cloneIntoGpuModule(decl, gpuModuleDeferred);
    }

    // ------------------------------------------------------------------ //
    // Step 2: Move each gpu.func into the appropriate output module.
    //
    // gpu.func ops are already in the correct form (gpu.func kernel for
    // __global__, plain gpu.func for __device__) — no lowering needed.
    // We just move (or clone for replication) them into the output modules.
    // ------------------------------------------------------------------ //
    for (gpu::GPUFuncOp fn : gpuFuncs) {
      bool isKernel = fn.isKernel();

      if (isKernel) {
        if (primaryKernels.count(fn.getNameAttr())) {
          fn->moveBefore(gpuModulePrimary.getBody(),
                         gpuModulePrimary.getBody()->end());
        } else if (doSplit) {
          fn->moveBefore(gpuModuleDeferred.getBody(),
                         gpuModuleDeferred.getBody()->end());
        } else {
          // Discard mode: dead kernel is dropped.
          fn.erase();
        }
      } else {
        // Device helper.
        if (doDiscard && !reachable.count(fn.getNameAttr())) {
          fn.erase();
          continue;
        }
        // Clone into deferred if replicable.
        if (doSplit && gpuModuleDeferred &&
            !primaryOnlyHelpers.count(fn.getNameAttr())) {
          OpBuilder::InsertionGuard guard(builder);
          builder.setInsertionPointToEnd(gpuModuleDeferred.getBody());
          builder.clone(*fn.getOperation());
        }
        fn->moveBefore(gpuModulePrimary.getBody(),
                       gpuModulePrimary.getBody()->end());
      }
    }

    // Erase the deferred module if it contains no kernels.
    if (doSplit && gpuModuleDeferred) {
      bool deferredHasKernel = false;
      gpuModuleDeferred.walk([&](gpu::GPUFuncOp f) {
        if (f.isKernel())
          deferredHasKernel = true;
      });
      if (!deferredHasKernel)
        gpuModuleDeferred.erase();
    }

    // ------------------------------------------------------------------ //
    // Step 3: Erase the now-empty input gpu.module.
    //
    // gpu.launch_func ops in the host code reference @offload_device_module::
    // @kernelName.  The primary output module is also named @offload_device_
    // module (gpuModuleName), so existing launch_func ops remain valid.
    // ------------------------------------------------------------------ //
    inputMod.erase();
  }
};

} // namespace
