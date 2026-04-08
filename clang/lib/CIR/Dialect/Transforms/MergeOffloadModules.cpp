//===- MergeOffloadModules.cpp - Merge host+device CIR modules ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass reads one or more device CIR files produced by device cc1
// invocations and merges their kernel functions into the host module.
//
// Each --device-cir entry has the form ``<arch>:<path>`` or bare ``<path>``:
//   - With arch (e.g. ``gfx90a:/tmp/dev.cir``): the device functions are
//     merged into ``gpu.module @offload_device_module_<arch>`` in the host.
//   - Bare path (no arch, legacy mode): merged into the single
//     ``gpu.module @offload_device_module``.
//
// Two device CIR formats are handled per entry:
//
// Format A (compile-only / single-cc1 mode):
//   The device cc1 also ran with -clangir-offload, so it already emitted
//   gpu.func ops inside a gpu.module @offload_device_module.
//
// Format B (two-pass / amdgcn target cc1 without -clangir-offload):
//   The device cc1 emits a plain ModuleOp with cir.func ops.
//   Kernel functions carry "cir.amdgpu-flat-work-group-size".
//
// After merging all entries this pass verifies that every gpu.launch_func in
// the host resolves to a gpu.func kernel somewhere in the merged modules.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/GPU/IR/GPUDialect.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/IRMapping.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/Pass.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

namespace mlir {

#define GEN_PASS_DECL_MERGEOFFLOADMODULES
#define GEN_PASS_DEF_MERGEOFFLOADMODULES
#include "clang/CIR/Dialect/Passes.h.inc"

} // namespace mlir

using namespace mlir;

namespace {

// Merge device functions from `deviceModule` into `hostGpuMod`.
// Format A: deviceModule contains gpu.module @offload_device_module — move
//           gpu.func ops directly.
// Format B: deviceModule contains raw cir.func ops — promote to gpu.func.
static void mergeDeviceModule(ModuleOp deviceModule,
                               gpu::GPUModuleOp hostGpuMod,
                               MLIRContext *ctx) {
  auto deviceGpuMod =
      deviceModule.lookupSymbol<gpu::GPUModuleOp>("offload_device_module");

  if (deviceGpuMod) {
    // Format A: move gpu.func (and other ops) from device gpu.module into host
    // gpu.module, skipping duplicates.
    SmallVector<Operation *> toMove;
    for (Operation &op : *deviceGpuMod.getBody()) {
      if (op.hasTrait<OpTrait::IsTerminator>())
        continue;
      auto symName = op.getAttrOfType<StringAttr>(
          mlir::SymbolTable::getSymbolAttrName());
      if (symName) {
        if (auto *existing =
                SymbolTable::lookupSymbolIn(hostGpuMod, symName)) {
          // Device version replaces host version.  The device cc1
          // compiles with the correct GPU target (e.g. __gfx90a__),
          // so architecture-guarded code (if constexpr, #ifdef) takes
          // the right branch.  The host version may have empty bodies
          // from failed constexpr checks on the host target.
          existing->erase();
        }
      }
      toMove.push_back(&op);
    }
    for (Operation *op : toMove)
      op->moveBefore(hostGpuMod.getBody(), hostGpuMod.getBody()->end());
  } else {
    // Format B: promote cir.func ops from device module → gpu.func in host
    // gpu.module.  Kernel functions carry "cir.amdgpu-flat-work-group-size".
    OpBuilder gpuBuilder(ctx);
    gpuBuilder.setInsertionPointToEnd(hostGpuMod.getBody());

    for (Operation &op : deviceModule.getBody()->getOperations()) {
      auto cirFn = dyn_cast<cir::FuncOp>(&op);
      if (!cirFn)
        continue;

      if (auto *existing =
              SymbolTable::lookupSymbolIn(hostGpuMod, cirFn.getSymName())) {
        // Device version replaces host version — see Format A comment.
        existing->erase();
      }

      // Skip declarations and functions with all-empty blocks (effectively
      // declarations from linked device bitcode).
      if (cirFn.isDeclaration() ||
          llvm::all_of(cirFn.getBody().getBlocks(),
                       [](mlir::Block &b) { return b.empty(); }))
        continue;

      bool isKernel = cirFn->hasAttr("cir.amdgpu-flat-work-group-size") ||
                      cirFn->hasAttr("gpu.kernel");

      auto cirFnTy = cirFn.getFunctionType();
      SmallVector<Type> argTypes(cirFnTy.getInputs().begin(),
                                 cirFnTy.getInputs().end());
      auto retTypes = cirFnTy.getReturnTypes();
      auto gpuFnTy = FunctionType::get(ctx, argTypes, retTypes);

      auto gpuFn = gpu::GPUFuncOp::create(gpuBuilder, cirFn.getLoc(),
                                          cirFn.getSymName(), gpuFnTy);

      if (isKernel) {
        gpuFn.setKernelAttr(UnitAttr::get(ctx));
        if (auto wgs = cirFn->getAttrOfType<StringAttr>(
                "cir.amdgpu-flat-work-group-size"))
          gpuFn->setAttr("rocdl.flat_work_group_size", wgs);
      }

      if (!cirFn.isDeclaration()) {
        Region &srcRegion = cirFn.getBody();
        Region &dstRegion = gpuFn.getBody();
        dstRegion.takeBody(srcRegion);

        // Only convert top-level cir.return (direct gpu.func body block
        // terminators). Nested cir.return inside cir.scope/cir.if/etc. must
        // stay as cir.return — gpu.return requires its immediate parent to
        // be gpu.func.
        for (mlir::Block &blk : gpuFn.getBody()) {
          if (auto ret = dyn_cast<cir::ReturnOp>(blk.getTerminator())) {
            OpBuilder rb(ret);
            gpu::ReturnOp::create(rb, ret.getLoc(), ret.getOperands());
            ret.erase();
          }
        }
      }

      for (unsigned i = 0, e = cirFn.getNumArguments(); i < e; ++i) {
        auto attrs = cirFn.getArgAttrDict(i);
        if (attrs)
          gpuFn.setArgAttrs(i, attrs);
      }

      for (NamedAttribute na : cirFn->getDiscardableAttrs()) {
        StringRef name = na.getName().strref();
        if (name == "cir.amdgpu-flat-work-group-size" ||
            name == "gpu.kernel" || name == "cir.extra_attrs")
          continue;
        gpuFn->setAttr(na.getName(), na.getValue());
      }
    }
  }
}

// Import declaration-only ops from the device outer module into the host outer
// module (extern symbol declarations needed by device code at lowering time).
static void importDeclarations(ModuleOp deviceModule, ModuleOp hostModule,
                               bool isFormatA) {
  OpBuilder builder(hostModule.getContext());
  builder.setInsertionPointToEnd(hostModule.getBody());
  for (Operation &op : deviceModule.getBody()->getOperations()) {
    if (isa<gpu::GPUModuleOp>(&op))
      continue;
    if (auto cirFn = dyn_cast<cir::FuncOp>(&op)) {
      if (!isFormatA && !cirFn.isDeclaration())
        continue;
    }
    bool isDecl =
        op.getNumRegions() == 0 ||
        llvm::all_of(op.getRegions(), [](Region &r) { return r.empty(); });
    if (!isDecl)
      continue;
    auto symName = op.getAttrOfType<StringAttr>(
        mlir::SymbolTable::getSymbolAttrName());
    if (!symName)
      continue;
    if (hostModule.lookupSymbol(symName.getValue()))
      continue;
    Operation *cloned = builder.clone(op);
    // Device cc1 compiles with -fvisibility=hidden -fapply-global-visibility-to-externs,
    // which sets HIDDEN/PROTECTED visibility on device-side declarations. Reset
    // to DEFAULT so these don't leak into the host ELF symbol table.
    if (auto cirFn = dyn_cast<cir::FuncOp>(cloned))
      cirFn.setGlobalVisibility(cir::VisibilityKind::Default);
    else if (auto cirGlobal = dyn_cast<cir::GlobalOp>(cloned))
      cirGlobal.setGlobalVisibility(cir::VisibilityKind::Default);
  }
}

struct MergeOffloadModulesPass
    : public impl::MergeOffloadModulesBase<MergeOffloadModulesPass> {

  using MergeOffloadModulesBase::MergeOffloadModulesBase;

  void runOnOperation() override {
    ModuleOp hostModule = getOperation();
    MLIRContext *ctx = hostModule.getContext();

    if (deviceCirFiles.empty()) {
      hostModule.emitError("offload-merge-modules: no device CIR file specified"
                           " (use --device-cir=<arch>:<path> or "
                           "--device-cir=<path>)");
      return signalPassFailure();
    }

    // Track all per-arch gpu.modules we create/find so we can verify launches.
    SmallVector<gpu::GPUModuleOp> mergedModules;

    for (const std::string &entry : deviceCirFiles) {
      //--------------------------------------------------------------------
      // Parse arch:path entry.
      //--------------------------------------------------------------------
      StringRef arch, path;
      StringRef entryRef(entry);
      if (entryRef.contains(':')) {
        auto [a, p] = entryRef.split(':');
        arch = a;
        path = p;
      } else {
        arch = "";
        path = entryRef;
      }

      //--------------------------------------------------------------------
      // Load the device module.
      //--------------------------------------------------------------------
      ParserConfig parserCfg(ctx);
      OwningOpRef<ModuleOp> deviceModuleRef =
          parseSourceFile<ModuleOp>(path, parserCfg);
      if (!deviceModuleRef) {
        hostModule.emitError("offload-merge-modules: failed to parse device "
                             "CIR file '")
            << path << "'";
        return signalPassFailure();
      }
      ModuleOp deviceModule = *deviceModuleRef;

      // For Format A (single-source/clangir-offload), the device CIR already
      // contains gpu.module @offload_device_module. Ignore the arch prefix and
      // always merge into @offload_device_module to match host gpu.launch_func.
      bool isFormatA = static_cast<bool>(
          deviceModule.lookupSymbol<gpu::GPUModuleOp>("offload_device_module"));
      std::string modName = (!arch.empty() && !isFormatA)
                                ? ("offload_device_module_" + arch).str()
                                : "offload_device_module";

      //--------------------------------------------------------------------
      // Find or create gpu.module @<modName> in the host.
      //--------------------------------------------------------------------
      auto hostGpuMod =
          hostModule.lookupSymbol<gpu::GPUModuleOp>(modName);
      if (!hostGpuMod) {
        OpBuilder b(ctx);
        b.setInsertionPointToEnd(hostModule.getBody());
        hostGpuMod =
            gpu::GPUModuleOp::create(b, hostModule.getLoc(), modName);
      }
      mergedModules.push_back(hostGpuMod);

      //--------------------------------------------------------------------
      // Merge device functions into the host gpu.module.
      //--------------------------------------------------------------------
      mergeDeviceModule(deviceModule, hostGpuMod, ctx);

      //--------------------------------------------------------------------
      // Import declarations from device outer module into host outer module.
      //--------------------------------------------------------------------
      importDeclarations(deviceModule, hostModule, isFormatA);

      //--------------------------------------------------------------------
      // Fill empty gpu.func declarations from host/device outer modules.
      //
      // The gpu.module may contain gpu.func declarations (empty bodies)
      // for device helper functions whose definitions live in the host
      // or device outer module as cir.func ops.  Clone those bodies so
      // the LLVM backend can inline them into kernels.
      //--------------------------------------------------------------------
      {
        SmallVector<gpu::GPUFuncOp> emptyFuncs;
        hostGpuMod.walk([&](gpu::GPUFuncOp fn) {
          if (fn.isDeclaration())
            emptyFuncs.push_back(fn);
        });

        for (gpu::GPUFuncOp emptyFn : emptyFuncs) {
          StringRef name = emptyFn.getName();

          // Look for a cir.func with body in device or host outer module.
          cir::FuncOp srcFn;
          if (auto fn = deviceModule.lookupSymbol<cir::FuncOp>(name))
            if (!fn.isDeclaration())
              srcFn = fn;
          if (!srcFn)
            if (auto fn = hostModule.lookupSymbol<cir::FuncOp>(name))
              if (!fn.isDeclaration())
                srcFn = fn;
          if (!srcFn)
            continue;

          // Clone the body from cir.func into gpu.func.
          Region &srcRegion = srcFn.getBody();
          Region &dstRegion = emptyFn.getBody();
          IRMapping mapping;
          srcRegion.cloneInto(&dstRegion, mapping);

          // Convert top-level cir.return → gpu.return.
          for (Block &blk : emptyFn.getBody()) {
            if (auto ret = dyn_cast<cir::ReturnOp>(blk.getTerminator())) {
              OpBuilder rb(ret);
              gpu::ReturnOp::create(rb, ret.getLoc(), ret.getOperands());
              ret.erase();
            }
          }

          // Copy attributes.
          for (NamedAttribute na : srcFn->getDiscardableAttrs()) {
            StringRef attrName = na.getName().strref();
            if (attrName == "cir.extra_attrs")
              continue;
            emptyFn->setAttr(na.getName(), na.getValue());
          }
        }
      }
    }

    //----------------------------------------------------------------------
    // Verify kernel launch targets.
    //
    // Each gpu.launch_func in the host must resolve to a gpu.func kernel in
    // at least one of the merged modules.  In single-arch mode (one module
    // named @offload_device_module), verification is the same as before.
    // In multi-arch mode, we check that EVERY per-arch module contains the
    // kernel so that any GPU can run it.
    //----------------------------------------------------------------------
    bool anyError = false;
    hostModule.walk([&](gpu::LaunchFuncOp launch) {
      StringRef kernelModule = launch.getKernelModuleName();
      StringRef kernelName = launch.getKernel().getLeafReference();

      // Find the target gpu.module by name (handles both @offload_device_module
      // in single-arch and @offload_device_module_<arch> in multi-arch).
      // In multi-arch mode, gpu.launch_func still references
      // @offload_device_module; we verify against all merged modules.
      bool foundInAny = false;
      bool missingInSome = false;
      for (gpu::GPUModuleOp gpuMod : mergedModules) {
        auto fn = dyn_cast_or_null<gpu::GPUFuncOp>(
            SymbolTable::lookupSymbolIn(gpuMod, kernelName));
        if (fn && fn.isKernel())
          foundInAny = true;
        else
          missingInSome = true;
      }

      if (!foundInAny) {
        launch.emitError("offload-merge-modules: kernel '")
            << kernelName
            << "' referenced by gpu.launch_func has no matching "
               "gpu.func kernel in any merged device module";
        anyError = true;
      } else if (missingInSome && mergedModules.size() > 1) {
        launch.emitWarning("offload-merge-modules: kernel '")
            << kernelName
            << "' is missing in at least one per-arch device module; "
               "compilation will fail for that architecture";
      }
      (void)kernelModule;
    });

    if (anyError)
      return signalPassFailure();
  }
};

} // namespace
