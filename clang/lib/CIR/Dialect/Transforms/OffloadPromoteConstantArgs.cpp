//===- OffloadPromoteConstantArgs.cpp - Promote scalars to constant mem ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Phase 2 of constant memory promotion. Consumes cir.offload.memcpy_to_symbol
// ops (emitted by Phase 1 before loops) and loop_invariant_args annotations
// on launches. Creates __constant__ device globals, kernel clones that read
// from constant memory, and lowers the memcpy ops to hipMemcpyToSymbol calls.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"

#include "mlir/IR/Builders.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cir-offload-promote-constant-args"

using namespace mlir;

namespace {

struct OffloadPromoteConstantArgsPass
    : public PassWrapper<OffloadPromoteConstantArgsPass,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(OffloadPromoteConstantArgsPass)

  OffloadPromoteConstantArgsPass() = default;
  OffloadPromoteConstantArgsPass(bool enabled) : passEnabled(enabled) {}

  StringRef getArgument() const override {
    return "cir-offload-promote-constant-args";
  }
  StringRef getDescription() const override {
    return "Promote loop-invariant scalar kernel args to constant memory";
  }

  void runOnOperation() override {
    if (!passEnabled)
      return;

    ModuleOp module = getOperation();
    MLIRContext *ctx = module.getContext();

    // Collect all memcpy_to_symbol ops.
    SmallVector<cir::OffloadMemcpyToSymbolOp> memcpyOps;
    module.walk([&](cir::OffloadMemcpyToSymbolOp op) {
      memcpyOps.push_back(op);
    });

    if (memcpyOps.empty()) {
      // Still clean up stale annotations.
      module.walk([&](cir::OffloadKernelLaunchOp launch) {
        launch->removeAttr("loop_invariant_args");
      });
      return;
    }

    // Find offload modules.
    SmallVector<cir::OffloadModuleOp> offloadMods;
    module.walk([&](cir::OffloadModuleOp mod) {
      if (!mod.getBody().empty())
        offloadMods.push_back(mod);
    });

    for (auto memcpyOp : memcpyOps)
      processMemcpyOp(ctx, module, offloadMods, memcpyOp);

    // Clean up remaining annotations.
    module.walk([&](cir::OffloadKernelLaunchOp launch) {
      launch->removeAttr("loop_invariant_args");
    });
  }

  void processMemcpyOp(MLIRContext *ctx, ModuleOp module,
                        ArrayRef<cir::OffloadModuleOp> offloadMods,
                        cir::OffloadMemcpyToSymbolOp memcpyOp) {
    StringRef kernelName =
        memcpyOp.getKernel().getLeafReference().getValue();
    ArrayRef<int32_t> argIndices = memcpyOp.getArgIndices();
    auto values = memcpyOp.getValues();

    if (argIndices.empty() || values.empty()) {
      memcpyOp.erase();
      return;
    }

    // Find the kernel definition in an offload module.
    cir::OffloadFuncOp kernel;
    cir::OffloadModuleOp kernelMod;
    for (auto mod : offloadMods) {
      // The kernel name at Phase 1 time may not have $-suffixes.
      // Search for exact match or any $-suffixed variant.
      mod.walk([&](cir::OffloadFuncOp fn) {
        if (!fn.isKernel() || fn.isExternal())
          return;
        StringRef fnName = fn.getSymName();
        if (fnName == kernelName || fnName.starts_with(
                (kernelName + "$").str())) {
          kernel = fn;
          kernelMod = mod;
        }
      });
      if (kernel)
        break;
    }

    if (!kernel || !kernelMod) {
      memcpyOp.erase();
      return;
    }

    // Resolve actual kernel name (may have $max256 etc).
    StringRef actualKernelName = kernel.getSymName();
    SymbolTable symTable(kernelMod);

    // Validate arg indices against the actual kernel.
    SmallVector<unsigned> validIndices;
    SmallVector<mlir::Value> validValues;
    for (auto [i, idx] : llvm::enumerate(argIndices)) {
      if ((unsigned)idx >= kernel.getNumArguments())
        continue;
      if (isa<cir::PointerType>(kernel.getArgument(idx).getType()))
        continue;
      if (i >= values.size())
        continue;
      validIndices.push_back(idx);
      validValues.push_back(values[i]);
    }

    if (validIndices.empty()) {
      memcpyOp.erase();
      return;
    }

    LLVM_DEBUG(llvm::dbgs() << "PromoteConstantArgs: " << actualKernelName
                            << " promoting " << validIndices.size()
                            << " args\n");

    // --- Device side: create constant globals and kernel clone ---

    auto addrSpaceAttr = cir::TargetAddressSpaceAttr::get(ctx, 4);
    OpBuilder modBuilder(ctx);
    modBuilder.setInsertionPointToStart(&kernelMod.getBody().front());

    SmallVector<std::string> globalNames;
    for (unsigned idx : validIndices) {
      Type argTy = kernel.getArgument(idx).getType();
      std::string globalName =
          (actualKernelName + "$constarg" + llvm::Twine(idx)).str();
      globalNames.push_back(globalName);

      if (!symTable.lookup(globalName)) {
        auto constGlobal = cir::GlobalOp::create(
            modBuilder, kernel.getLoc(), globalName, argTy);
        constGlobal.setAddrSpaceAttr(addrSpaceAttr);
        constGlobal.setConstant(false);
        constGlobal.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
        // Zero initializer so the HSA loader allocates storage.
        if (isa<cir::DoubleType>(argTy) || isa<cir::SingleType>(argTy))
          constGlobal.setInitialValueAttr(cir::FPAttr::getZero(argTy));
        else if (isa<cir::IntType>(argTy))
          constGlobal.setInitialValueAttr(cir::IntAttr::get(argTy, 0));
        symTable.insert(constGlobal);
      }
    }

    // Host-side shadow globals.
    {
      OpBuilder topBuilder(ctx);
      topBuilder.setInsertionPointToStart(module.getBody());
      for (auto [i, idx] : llvm::enumerate(validIndices)) {
        Type argTy = kernel.getArgument(idx).getType();
        if (!module.lookupSymbol(globalNames[i])) {
          auto hostGlobal = cir::GlobalOp::create(
              topBuilder, kernel.getLoc(), globalNames[i], argTy);
          hostGlobal.setLinkage(cir::GlobalLinkageKind::InternalLinkage);
        }
      }
    }

    // Clone the kernel.
    std::string cloneName = (actualKernelName + "$constargs").str();
    if (!symTable.lookup(cloneName)) {
      auto *cloneOp = kernel->clone();
      auto clone = cast<cir::OffloadFuncOp>(cloneOp);
      SymbolTable::setSymbolName(clone, cloneName);
      symTable.insert(clone);
      clone->moveAfter(kernel);

      // Replace promoted args with loads from constant globals.
      if (!clone.getBody().empty()) {
        OpBuilder builder(ctx);
        builder.setInsertionPointToStart(&clone.getBody().front());

        for (auto [i, idx] : llvm::enumerate(validIndices)) {
          BlockArgument arg = clone.getArgument(idx);
          Type argTy = arg.getType();
          auto ptrTy = cir::PointerType::get(argTy);

          auto globalAddr = cir::GetGlobalOp::create(
              builder, clone.getLoc(), ptrTy, globalNames[i]);
          auto loadedVal = cir::LoadOp::create(
              builder, clone.getLoc(), globalAddr.getResult());

          arg.replaceAllUsesWith(loadedVal);
        }
      }
    }

    // --- Host side: lower memcpy_to_symbol op ---

    OpBuilder hostBuilder(ctx);
    hostBuilder.setInsertionPoint(memcpyOp);
    mlir::Location loc = memcpyOp.getLoc();

    auto voidPtrTy = cir::PointerType::get(cir::VoidType::get(ctx));
    auto u64Ty = cir::IntType::get(ctx, 64, /*isSigned=*/false);
    auto s32Ty = cir::IntType::get(ctx, 32, /*isSigned=*/true);

    // Ensure hipMemcpyToSymbol declaration exists.
    if (!module.lookupSymbol("hipMemcpyToSymbol")) {
      auto memcpyFnTy = cir::FuncType::get(
          {voidPtrTy, voidPtrTy, u64Ty, u64Ty, s32Ty}, s32Ty);
      OpBuilder declBuilder(ctx);
      declBuilder.setInsertionPointToEnd(module.getBody());
      auto fnDecl = cir::FuncOp::create(
          declBuilder, loc, "hipMemcpyToSymbol", memcpyFnTy);
      fnDecl.setLinkage(cir::GlobalLinkageKind::ExternalLinkage);
    }

    for (auto [i, idx] : llvm::enumerate(validIndices)) {
      Type argTy = kernel.getArgument(idx).getType();
      auto ptrTy = cir::PointerType::get(argTy);
      Value argVal = validValues[i];

      // Store value to host shadow global.
      auto globalAddr = cir::GetGlobalOp::create(
          hostBuilder, loc, ptrTy, globalNames[i]);
      cir::StoreOp::create(hostBuilder, loc, argVal, globalAddr);

      // hipMemcpyToSymbol(symbol, src, size, 0, hipMemcpyHostToDevice)
      auto symbolPtr = cir::CastOp::create(
          hostBuilder, loc, voidPtrTy, cir::CastKind::bitcast,
          globalAddr.getResult());
      auto srcPtr = cir::CastOp::create(
          hostBuilder, loc, voidPtrTy, cir::CastKind::bitcast,
          globalAddr.getResult());

      uint64_t sizeBytes = 8;
      if (isa<cir::IntType>(argTy))
        sizeBytes = cast<cir::IntType>(argTy).getWidth() / 8;
      else if (isa<cir::SingleType>(argTy))
        sizeBytes = 4;

      auto sizeVal = cir::ConstantOp::create(
          hostBuilder, loc, cir::IntAttr::get(u64Ty, sizeBytes));
      auto offsetVal = cir::ConstantOp::create(
          hostBuilder, loc, cir::IntAttr::get(u64Ty, 0));
      auto kindVal = cir::ConstantOp::create(
          hostBuilder, loc, cir::IntAttr::get(s32Ty, 1));

      cir::CallOp::create(
          hostBuilder, loc,
          mlir::FlatSymbolRefAttr::get(ctx, "hipMemcpyToSymbol"),
          s32Ty,
          mlir::ValueRange{symbolPtr, srcPtr, sizeVal, offsetVal, kindVal});
    }

    // Erase the memcpy_to_symbol op (replaced by the calls above).
    memcpyOp.erase();

    // --- Redirect launches to the clone ---

    StringRef offloadModName = kernelMod.getSymName();
    SmallVector<cir::OffloadKernelLaunchOp> launches;
    module.walk([&](cir::OffloadKernelLaunchOp launch) {
      StringRef leafName = launch.getKernelLeafName();
      if (leafName == actualKernelName)
        launches.push_back(launch);
    });

    for (auto launch : launches) {
      StringRef launchModName =
          launch.getKernelAttr().getRootReference().getValue();
      if (launchModName != offloadModName) {
        auto launchMod =
            module.lookupSymbol<cir::OffloadModuleOp>(launchModName);
        if (launchMod && !launchMod.lookupSymbol(cloneName)) {
          auto origDecl =
              launchMod.lookupSymbol<cir::OffloadFuncOp>(actualKernelName);
          if (origDecl) {
            auto *declClone = origDecl->clone();
            SymbolTable::setSymbolName(declClone, cloneName);
            OpBuilder declBuilder(ctx);
            declBuilder.setInsertionPointToEnd(
                &launchMod.getBody().front());
            declBuilder.insert(declClone);
          }
        }
      }

      launch.setKernelAttr(SymbolRefAttr::get(
          ctx, launchModName,
          {FlatSymbolRefAttr::get(ctx, cloneName)}));
      launch->removeAttr("loop_invariant_args");
    }
  }

  bool passEnabled = true;
};

} // namespace

std::unique_ptr<Pass>
mlir::createOffloadPromoteConstantArgsPass(bool enabled) {
  return std::make_unique<OffloadPromoteConstantArgsPass>(enabled);
}
