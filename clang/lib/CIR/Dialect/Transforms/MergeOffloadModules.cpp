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
//     merged into ``cir.offload.module @offload_device_module_<arch>``.
//   - Bare path (no arch): the arch is read from the device module's
//     ``offload.target`` attribute or its inner OffloadModuleOp's ``target``
//     attribute.  If found, functions are merged into
//     ``@offload_device_module_<arch>``; otherwise into
//     ``@offload_device_module``.
//
// Two device CIR formats are handled per entry:
//
// Format A (compile-only / single-cc1 mode):
//   The device cc1 also ran with -clangir-offload, so it already emitted
//   cir.offload.func ops inside a cir.offload.module @offload_device_module.
//
// Format B (two-pass / amdgcn target cc1 without -clangir-offload):
//   The device cc1 emits a plain ModuleOp with cir.func ops.
//   Kernel functions carry "cir.amdgpu-flat-work-group-size".
//
// After merging all entries this pass verifies that every
// cir.offload.kernel_launch in the host resolves to a cir.offload.func kernel
// somewhere in the merged modules.
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/AttrTypeSubElements.h"
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

static constexpr llvm::StringLiteral deviceAnonPrefix("device_anon");
static constexpr llvm::StringLiteral hostAnonPrefix("anon");

static bool isDeviceAnonType(mlir::Type ty) {
  if (auto rt = mlir::dyn_cast<cir::RecordType>(ty))
    if (auto name = rt.getName())
      return name.getValue().starts_with(deviceAnonPrefix);
  return false;
}

static bool isHostAnonType(mlir::Type ty) {
  if (auto rt = mlir::dyn_cast<cir::RecordType>(ty))
    if (auto name = rt.getName())
      return name.getValue().starts_with(hostAnonPrefix) &&
             !name.getValue().starts_with(deviceAnonPrefix);
  return false;
}

/// Recursively collect all cir::RecordType instances from a type,
/// including those nested inside struct/union members and pointer pointees.
static void collectRecordTypes(mlir::Type ty,
                               llvm::SmallVectorImpl<cir::RecordType> &out,
                               llvm::DenseSet<mlir::Type> &visited) {
  if (!visited.insert(ty).second)
    return;
  if (auto rt = mlir::dyn_cast<cir::RecordType>(ty)) {
    out.push_back(rt);
    for (auto member : rt.getMembers())
      collectRecordTypes(member, out, visited);
  } else if (auto pt = mlir::dyn_cast<cir::PointerType>(ty)) {
    collectRecordTypes(pt.getPointee(), out, visited);
  }
}

static void collectAllRecordTypes(ModuleOp mod,
                                  llvm::SmallVectorImpl<cir::RecordType> &out) {
  llvm::DenseSet<mlir::Type> visited;
  mod.walk([&](Operation *op) {
    for (auto ty : op->getResultTypes())
      collectRecordTypes(ty, out, visited);
    for (auto &region : op->getRegions())
      for (auto &block : region)
        for (auto arg : block.getArguments())
          collectRecordTypes(arg.getType(), out, visited);
  });
}

/// Remap device_anon.* types in the device module to structurally-equivalent
/// anon.* types from the host module. Device and host cc1 may number anonymous
/// types differently, so "device_anon.1" and "anon.3" can refer to the same
/// struct/union. The device_anon prefix ensures they parse into the host
/// MLIRContext without silently aliasing to host types with different members.
static void remapDeviceAnonTypes(ModuleOp hostModule, ModuleOp deviceModule) {
  llvm::SmallVector<cir::RecordType> hostTypes, deviceTypes;
  collectAllRecordTypes(hostModule, hostTypes);
  collectAllRecordTypes(deviceModule, deviceTypes);

  // Build host map: members → type (for anonymous types only).
  llvm::SmallVector<std::pair<llvm::SmallVector<mlir::Type>, mlir::Type>>
      hostAnonTypes;
  for (auto rt : hostTypes) {
    if (!isHostAnonType(rt) || rt.getMembers().empty())
      continue;
    llvm::SmallVector<mlir::Type> members(rt.getMembers());
    hostAnonTypes.emplace_back(std::move(members), mlir::Type(rt));
  }

  // Build remap table: device type → host type by structural match.
  llvm::DenseMap<mlir::Type, mlir::Type> remapTable;
  for (auto rt : deviceTypes) {
    if (!isDeviceAnonType(rt) || rt.getMembers().empty())
      continue;
    auto deviceMembers = rt.getMembers();
    for (auto &[hostMembers, hostTy] : hostAnonTypes) {
      if (hostMembers.size() != deviceMembers.size())
        continue;
      if (std::equal(deviceMembers.begin(), deviceMembers.end(),
                     hostMembers.begin())) {
        remapTable[mlir::Type(rt)] = hostTy;
        break;
      }
    }
  }

  if (remapTable.empty())
    return;

  // Recursive type replacement since CIR types don't implement
  // SubElementTypeInterface and the AttrTypeReplacer can't recurse
  // into CIR pointer/func types automatically.
  std::function<mlir::Type(mlir::Type)> remapType;
  remapType = [&](mlir::Type ty) -> mlir::Type {
    auto it = remapTable.find(ty);
    if (it != remapTable.end())
      return it->second;
    if (auto pt = mlir::dyn_cast<cir::PointerType>(ty)) {
      auto newPointee = remapType(pt.getPointee());
      if (newPointee != pt.getPointee())
        return cir::PointerType::get(ty.getContext(), newPointee,
                                     pt.getAddrSpace());
    } else if (auto ft = mlir::dyn_cast<cir::FuncType>(ty)) {
      bool changed = false;
      llvm::SmallVector<mlir::Type> newInputs;
      for (auto input : ft.getInputs()) {
        auto mapped = remapType(input);
        newInputs.push_back(mapped);
        if (mapped != input)
          changed = true;
      }
      mlir::Type newRet = ft.getReturnType();
      if (newRet) {
        auto mapped = remapType(newRet);
        if (mapped != newRet) {
          newRet = mapped;
          changed = true;
        }
      }
      if (changed)
        return cir::FuncType::get(newInputs, newRet, ft.isVarArg());
    }
    return ty;
  };

  // Apply the remapping to all types in the device module.
  AttrTypeReplacer replacer;
  replacer.addReplacement([&](mlir::Type ty) -> std::optional<mlir::Type> {
    auto newTy = remapType(ty);
    if (newTy != ty)
      return newTy;
    return std::nullopt;
  });
  replacer.recursivelyReplaceElementsIn(deviceModule.getOperation(),
                                        /*replaceAttrs=*/true,
                                        /*replaceLocs=*/false,
                                        /*replaceTypes=*/true);
}

namespace {

// Merge device functions from `deviceModule` into `hostOffloadMod`.
// Format A: deviceModule contains cir.offload.module @offload_device_module —
//           move cir.offload.func ops directly.
// Format B: deviceModule contains raw cir.func ops — promote to
//           cir.offload.func.
static void mergeDeviceModule(ModuleOp deviceModule,
                               cir::OffloadModuleOp hostOffloadMod,
                               MLIRContext *ctx) {
  auto deviceOffloadMod =
      deviceModule.lookupSymbol<cir::OffloadModuleOp>("offload_device_module");

  if (deviceOffloadMod) {
    // Format A: move ops from device cir.offload.module into host
    // cir.offload.module, skipping duplicates.
    SmallVector<Operation *> toMove;
    for (Operation &op : deviceOffloadMod.getBody().front()) {
      if (op.hasTrait<OpTrait::IsTerminator>())
        continue;
      auto symName = op.getAttrOfType<StringAttr>(
          mlir::SymbolTable::getSymbolAttrName());
      if (symName) {
        if (auto *existing =
                SymbolTable::lookupSymbolIn(hostOffloadMod, symName)) {
          existing->erase();
        }
      }
      toMove.push_back(&op);
    }
    Block *hostBody = &hostOffloadMod.getBody().front();
    for (Operation *op : toMove)
      op->moveBefore(hostBody, hostBody->end());
  } else {
    // Format B: promote cir.func ops from device module → cir.offload.func in
    // host cir.offload.module.
    OpBuilder builder(ctx);
    builder.setInsertionPointToEnd(&hostOffloadMod.getBody().front());

    for (Operation &op : deviceModule.getBody()->getOperations()) {
      auto cirFn = dyn_cast<cir::FuncOp>(&op);
      if (!cirFn)
        continue;

      if (cirFn.isDeclaration() ||
          llvm::all_of(cirFn.getBody().getBlocks(),
                       [](mlir::Block &b) { return b.empty(); }))
        continue;

      if (auto *existing =
              SymbolTable::lookupSymbolIn(hostOffloadMod, cirFn.getSymName())) {
        existing->erase();
      }

      bool isKernel = cirFn->hasAttr("cir.amdgpu-flat-work-group-size") ||
                      cirFn->hasAttr("gpu.kernel");

      cir::FuncType cirFnTy = cirFn.getFunctionType();

      auto offloadFn = cir::OffloadFuncOp::create(
          builder, cirFn.getLoc(), cirFn.getSymName(), cirFnTy, isKernel);

      if (isKernel) {
        if (auto wgs = cirFn->getAttrOfType<StringAttr>(
                "cir.amdgpu-flat-work-group-size"))
          offloadFn->setAttr("rocdl.flat_work_group_size", wgs);
      }

      // Inherent attributes are not part of getDiscardableAttrs() below, so
      // the inline kind has to be carried across by hand.  Device code leans
      // on __forceinline__ heavily; dropping it silently hands a decision the
      // programmer already made back to the inliner's cost model, where an
      // extra caller is enough to change the answer.
      if (auto inlineKind = cirFn.getInlineKindAttr())
        offloadFn.setInlineKindAttr(inlineKind);

      if (!cirFn.isDeclaration()) {
        offloadFn.getBody().takeBody(cirFn.getBody());
        // cir.return is the terminator for cir.offload.func — no conversion
        // needed (unlike the gpu.func path which required gpu.return).
      }

      for (unsigned i = 0, e = cirFn.getNumArguments(); i < e; ++i) {
        auto attrs = cirFn.getArgAttrDict(i);
        if (attrs)
          offloadFn.setArgAttrs(i, attrs);
      }

      for (NamedAttribute na : cirFn->getDiscardableAttrs()) {
        StringRef name = na.getName().strref();
        if (name == "cir.amdgpu-flat-work-group-size" ||
            name == "gpu.kernel" || name == "cir.extra_attrs")
          continue;
        offloadFn->setAttr(na.getName(), na.getValue());
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
    if (isa<cir::OffloadModuleOp>(&op))
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

    SmallVector<cir::OffloadModuleOp> mergedModules;

    for (const std::string &entry : deviceCirFiles) {
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

      ParserConfig parserCfg(ctx, /*verifyAfterParse=*/false);
      OwningOpRef<ModuleOp> deviceModuleRef =
          parseSourceFile<ModuleOp>(path, parserCfg);
      if (!deviceModuleRef) {
        hostModule.emitError("offload-merge-modules: failed to parse device "
                             "CIR file '")
            << path << "'";
        return signalPassFailure();
      }
      ModuleOp deviceModule = *deviceModuleRef;

      remapDeviceAnonTypes(hostModule, deviceModule);

      // If no arch was given on the command line, try reading it from the
      // device module's offload.target attribute (set by device cc1).
      if (arch.empty()) {
        if (auto targets = deviceModule->getAttrOfType<ArrayAttr>(
                "offload.target")) {
          if (!targets.empty())
            if (auto strAttr = dyn_cast<StringAttr>(targets[0]))
              arch = strAttr.getValue();
        }
      }

      bool isFormatA = false;
      if (auto devOffloadMod = deviceModule.lookupSymbol<cir::OffloadModuleOp>(
              "offload_device_module")) {
        isFormatA = true;
        if (arch.empty()) {
          if (auto target = devOffloadMod.getTarget())
            arch = *target;
        }
      }

      std::string modName = !arch.empty()
                                ? ("offload_device_module_" + arch).str()
                                : "offload_device_module";

      auto hostOffloadMod =
          hostModule.lookupSymbol<cir::OffloadModuleOp>(modName);
      if (!hostOffloadMod) {
        OpBuilder b(ctx);
        b.setInsertionPointToEnd(hostModule.getBody());
        hostOffloadMod = cir::OffloadModuleOp::create(
            b, hostModule.getLoc(), modName, /*sym_visibility=*/StringAttr{},
            /*target=*/arch.empty() ? StringAttr{}
                                    : StringAttr::get(ctx, arch));
        hostOffloadMod.getBody().emplaceBlock();
      } else if (!hostOffloadMod.getTarget() && !arch.empty()) {
        hostOffloadMod.setTargetAttr(StringAttr::get(ctx, arch));
      }
      mergedModules.push_back(hostOffloadMod);

      mergeDeviceModule(deviceModule, hostOffloadMod, ctx);
      importDeclarations(deviceModule, hostModule, isFormatA);

      // Fill empty cir.offload.func declarations from host/device outer modules.
      {
        SmallVector<cir::OffloadFuncOp> emptyFuncs;
        hostOffloadMod.walk([&](cir::OffloadFuncOp fn) {
          if (fn.isExternal())
            emptyFuncs.push_back(fn);
        });

        for (cir::OffloadFuncOp emptyFn : emptyFuncs) {
          StringRef name = emptyFn.getSymName();

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

          Region &srcRegion = srcFn.getBody();
          Region &dstRegion = emptyFn.getBody();
          IRMapping mapping;
          srcRegion.cloneInto(&dstRegion, mapping);

          for (NamedAttribute na : srcFn->getDiscardableAttrs()) {
            StringRef attrName = na.getName().strref();
            if (attrName == "cir.extra_attrs")
              continue;
            emptyFn->setAttr(na.getName(), na.getValue());
          }
        }
      }
    }

    // Verify kernel launch targets.
    bool anyError = false;
    hostModule.walk([&](cir::OffloadKernelLaunchOp launch) {
      StringRef kernelName = launch.getKernelLeafName();

      bool foundInAny = false;
      bool missingInSome = false;
      for (cir::OffloadModuleOp offloadMod : mergedModules) {
        auto fn = dyn_cast_or_null<cir::OffloadFuncOp>(
            SymbolTable::lookupSymbolIn(offloadMod, kernelName));
        if (fn && fn.isKernel())
          foundInAny = true;
        else
          missingInSome = true;
      }

      if (!foundInAny) {
        // The kernel is defined in another translation unit (cross-TU).
        // This is valid for multi-file HIP builds — the HIP runtime
        // resolves the kernel by name at link time via __hipRegisterFunction.
        // Emit a warning instead of an error.
        launch.emitWarning("offload-merge-modules: kernel '")
            << kernelName
            << "' not found in this TU's device code; expected in another "
               "object file (cross-TU kernel reference)";
      } else if (missingInSome && mergedModules.size() > 1) {
        launch.emitWarning("offload-merge-modules: kernel '")
            << kernelName
            << "' is missing in at least one per-arch device module; "
               "compilation will fail for that architecture";
      }
    });

    if (anyError)
      return signalPassFailure();

    // Add per-arch module refs to kernel launches.  The first ref
    // (@offload_device_module::@kernel from codegen) is kept for
    // SplitSingleSource compatibility.  Per-arch refs are appended
    // so the lowering can map kernels to specific arch modules.
    if (mergedModules.size() > 0) {
      hostModule.walk([&](cir::OffloadKernelLaunchOp launch) {
        StringRef leafName = launch.getKernelLeafName();
        SmallVector<mlir::Attribute> newRefs;
        // Keep the existing base ref as the first entry.
        newRefs.push_back(launch.getKernelAttr());
        for (cir::OffloadModuleOp mod : mergedModules) {
          if (mod.getSymName() == "offload_device_module")
            continue;
          newRefs.push_back(mlir::SymbolRefAttr::get(
              ctx, mod.getSymName(),
              {mlir::FlatSymbolRefAttr::get(ctx, leafName)}));
        }
        launch.setKernelsAttr(mlir::ArrayAttr::get(ctx, newRefs));
      });
    }
  }
};

} // namespace
