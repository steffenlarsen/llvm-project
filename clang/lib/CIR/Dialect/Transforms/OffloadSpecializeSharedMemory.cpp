//===- OffloadSpecializeSharedMemory.cpp - Bake in dynamic shmem size -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// For each cir.offload.func kernel that uses cir.offload.dynamic_shared_memory,
// inspects all cir.offload.kernel_launch call sites and resolves the dynamic
// shared memory size to a compile-time constant.  When all launch sites (or up
// to --max-shmem-variants distinct groups) agree on the same constant size, the
// size is baked into the kernel clone as a static_shared_memory_bytes attribute.
//
// The lowering pass (ConvertCIROffloadToGPUPass) later converts the dynamic
// shared memory op to a statically-sized LDS global when this attribute is
// present, giving the backend full visibility into LDS usage.
//
// Must run after SpecializeScalarArgs and before ConvertCIROffloadToGPU.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/Dialect/Transforms/CIRTransformUtils.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FormatVariadic.h"

#define DEBUG_TYPE "cir-offload-specialize-shared-memory"

using namespace mlir;

namespace {

struct OffloadSpecializeSharedMemoryPass
    : public PassWrapper<OffloadSpecializeSharedMemoryPass,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(
      OffloadSpecializeSharedMemoryPass)

  OffloadSpecializeSharedMemoryPass() = default;
  OffloadSpecializeSharedMemoryPass(bool enabled, unsigned maxVariants)
      : passEnabled(enabled), maxShmemVariants(maxVariants) {}

  StringRef getArgument() const override {
    return "cir-offload-specialize-shared-memory";
  }
  StringRef getDescription() const override {
    return "Bake dynamic shared memory size into kernel clones when constant "
           "at all launch sites";
  }

  void runOnOperation() override {
    if (!passEnabled)
      return;

    ModuleOp module = getOperation();

    module.walk([&](cir::OffloadModuleOp offloadMod) {
      if (offloadMod.getBody().empty())
        return;
      SymbolTable symTable(offloadMod);
      SmallVector<cir::OffloadFuncOp> kernels;
      offloadMod.walk([&](cir::OffloadFuncOp fn) {
        if (fn.isKernel() && !fn.isExternal())
          kernels.push_back(fn);
      });

      for (auto kernel : kernels)
        processKernel(module, symTable, kernel);
    });
  }

  void processKernel(ModuleOp module, SymbolTable &symTable,
                     cir::OffloadFuncOp kernel) {
    // Only process kernels that use dynamic shared memory.
    bool usesDynShmem = false;
    kernel.walk([&](cir::OffloadDynamicSharedMemoryOp) {
      usesDynShmem = true;
    });
    if (!usesDynShmem)
      return;

    // Gather launch ops targeting this kernel.
    SmallVector<cir::OffloadKernelLaunchOp> launchOps;
    module.walk([&](cir::OffloadKernelLaunchOp op) {
      if (op.getKernel().getLeafReference() == kernel.getSymName())
        launchOps.push_back(op);
    });

    if (launchOps.empty())
      return;

    // Resolve shmem size at each launch site and group by value.
    llvm::DenseMap<int64_t, SmallVector<cir::OffloadKernelLaunchOp>> sizeGroups;
    bool hasUnresolvable = false;

    for (auto launch : launchOps) {
      Value shmemVal = launch.getDynamicSharedMemorySize();
      if (!shmemVal) {
        // No dynamic shmem at this launch — treat as size 0 (skip).
        continue;
      }
      auto resolved = cir::tryResolveToConstant(shmemVal);
      if (!resolved) {
        hasUnresolvable = true;
        break;
      }
      if (*resolved > 0)
        sizeGroups[*resolved].push_back(launch);
    }

    if (hasUnresolvable)
      return;

    // Check threshold: number of distinct sizes must be <= maxShmemVariants.
    if (sizeGroups.size() > maxShmemVariants)
      return;

    if (sizeGroups.empty())
      return;

    // For the common case (1 distinct size), annotate the existing kernel
    // in-place.  For multiple sizes, we'd need to clone — but with the
    // default threshold of 1, this only fires when all sites agree.
    MLIRContext *ctx = module.getContext();

    for (auto &[shmemSize, launches] : sizeGroups) {
      cir::OffloadFuncOp target = kernel;

      // If there's more than one group, we need clones for each size.
      if (sizeGroups.size() > 1) {
        std::string cloneName =
            llvm::formatv("{0}$shmem{1}", kernel.getSymName(), shmemSize)
                .str();
        auto *cloneOp = kernel->clone();
        auto clone = cast<cir::OffloadFuncOp>(cloneOp);
        SymbolTable::setSymbolName(clone, cloneName);
        symTable.insert(clone);
        clone->moveAfter(kernel);
        target = clone;

        // Redirect launches in this group to the clone.
        StringRef kernelModName =
            cast<cir::OffloadModuleOp>(kernel->getParentOp()).getSymName();
        for (auto launch : launches) {
          StringRef launchModName =
              launch.getKernel().getRootReference().getValue();
          if (launchModName != kernelModName) {
            auto launchMod =
                module.lookupSymbol<cir::OffloadModuleOp>(launchModName);
            if (launchMod && !launchMod.lookupSymbol(cloneName)) {
              auto origDecl = launchMod.lookupSymbol<cir::OffloadFuncOp>(
                  kernel.getSymName());
              if (origDecl) {
                auto *declClone = origDecl->clone();
                SymbolTable::setSymbolName(declClone, cloneName);
                OpBuilder builder(ctx);
                builder.setInsertionPointToEnd(&launchMod.getBody().back());
                builder.insert(declClone);
              }
            }
          }
          launch.setKernelAttr(SymbolRefAttr::get(
              ctx, launchModName,
              {FlatSymbolRefAttr::get(ctx, cloneName)}));
        }
      }

      // Set the static shared memory size attribute.
      target->setAttr("static_shared_memory_bytes",
                      IntegerAttr::get(IntegerType::get(ctx, 32), shmemSize));

      // Clear the dynamic shmem operand on the launches so the runtime
      // doesn't double-allocate.
      for (auto launch : launches) {
        auto mutable_shmem = launch.getDynamicSharedMemorySizeMutable();
        mutable_shmem.clear();
      }

      LLVM_DEBUG(llvm::dbgs()
                 << "OffloadSpecializeSharedMemory: "
                 << target.getSymName() << " = " << shmemSize << " bytes\n");
    }
  }

  bool passEnabled = true;
  unsigned maxShmemVariants = 1;
};

} // namespace

std::unique_ptr<mlir::Pass>
mlir::createOffloadSpecializeSharedMemoryPass(bool enabled,
                                              unsigned maxVariants) {
  return std::make_unique<OffloadSpecializeSharedMemoryPass>(enabled,
                                                             maxVariants);
}
