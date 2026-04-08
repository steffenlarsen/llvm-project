//===- OffloadPropagateBlockShape.cpp - Propagate block shape into kernels ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// For each cir.offload.func kernel clone that has a known_block_size attribute
// (set by TightenLaunchBounds), this pass replaces cir.offload.block_dim ops
// in the body with cir.const ops carrying the corresponding dimension value.
//
// Run after TightenLaunchBounds, before the offload→GPU lowering pass.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIROpsEnums.h"
#include "clang/CIR/Dialect/Passes.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "cir-offload-propagate-block-shape"

using namespace mlir;

namespace {

struct OffloadPropagateBlockShapePass
    : public PassWrapper<OffloadPropagateBlockShapePass,
                         OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(OffloadPropagateBlockShapePass)

  OffloadPropagateBlockShapePass() = default;
  OffloadPropagateBlockShapePass(bool enabled) : passEnabled(enabled) {}

  StringRef getArgument() const override {
    return "cir-offload-propagate-block-shape";
  }
  StringRef getDescription() const override {
    return "Propagate known block dimensions into offload kernel bodies";
  }

  void runOnOperation() override {
    if (!passEnabled)
      return;

    ModuleOp module = getOperation();

    module.walk([&](cir::OffloadModuleOp offloadMod) {
      if (offloadMod.getBody().empty())
        return;
      offloadMod.walk([&](cir::OffloadFuncOp fn) {
        if (!fn.isKernel())
          return;
        processKernel(fn);
      });
    });
  }

  void processKernel(cir::OffloadFuncOp kernel) {
    auto knownSize =
        kernel->getAttrOfType<DenseI32ArrayAttr>("known_block_size");
    if (!knownSize)
      return;

    auto dims = knownSize.asArrayRef();
    if (dims.size() < 3)
      return;

    int32_t bx = dims[0], by = dims[1], bz = dims[2];

    LLVM_DEBUG(llvm::dbgs() << "OffloadPropagateBlockShape: "
                            << kernel.getSymName()
                            << " known_block_size=[" << bx << "," << by << ","
                            << bz << "]\n");

    SmallVector<cir::OffloadBlockDimOp> blockDimOps;
    kernel.walk(
        [&](cir::OffloadBlockDimOp op) { blockDimOps.push_back(op); });

    if (blockDimOps.empty())
      return;

    OpBuilder builder(kernel.getContext());
    for (cir::OffloadBlockDimOp dimOp : blockDimOps) {
      int32_t val;
      switch (dimOp.getDimension()) {
      case cir::OffloadDimension::X:
        val = bx;
        break;
      case cir::OffloadDimension::Y:
        val = by;
        break;
      case cir::OffloadDimension::Z:
        val = bz;
        break;
      }

      LLVM_DEBUG(llvm::dbgs()
                 << "  replacing cir.offload.block_dim "
                 << cir::stringifyOffloadDimension(dimOp.getDimension())
                 << " with " << val << "\n");

      builder.setInsertionPoint(dimOp);
      auto resultTy = mlir::cast<cir::IntType>(dimOp.getType());
      auto constAttr = cir::IntAttr::get(resultTy, val);
      Value constant =
          cir::ConstantOp::create(builder, dimOp.getLoc(), resultTy, constAttr);
      dimOp.getResult().replaceAllUsesWith(constant);
      dimOp.erase();
    }
  }

  bool passEnabled = true;
};

} // namespace

std::unique_ptr<mlir::Pass>
mlir::createOffloadPropagateBlockShapePass(bool enabled) {
  return std::make_unique<OffloadPropagateBlockShapePass>(enabled);
}
