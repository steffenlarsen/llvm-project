//===- PropagateBlockShape.cpp - Propagate block shape into kernel body ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// For each gpu.func kernel clone that has a known_block_size attribute (set by
// TightenLaunchBounds), this pass replaces gpu.block_dim ops in the body with
// arith.constant ops carrying the corresponding dimension value.
//
// This enables downstream canonicalization/folding to simplify block-relative
// guards (threadIdx.x == 15), prove fixed-size shared-tile bounds, and unroll
// blockDim-derived shuffle reduction loops.
//
// The pass does not create clones — it operates on clones already created by
// TightenLaunchBounds.  Kernels without known_block_size are untouched.
//
// Run after TightenLaunchBounds, before GpuSplitSingleSourcePass.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/GPU/Transforms/Passes.h"

#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/GPU/IR/GPUDialect.h"
#include "mlir/IR/BuiltinAttributes.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Pass/Pass.h"
#include "llvm/Support/Debug.h"

#define DEBUG_TYPE "gpu-propagate-block-shape"

namespace mlir {

#define GEN_PASS_DEF_GPUPROPAGATEBLOCKSHAPEPASS
#include "mlir/Dialect/GPU/Transforms/Passes.h.inc"

} // namespace mlir

using namespace mlir;
using namespace mlir::gpu;

namespace {

struct PropagateBlockShapePass
    : impl::GpuPropagateBlockShapePassBase<PropagateBlockShapePass> {

  using GpuPropagateBlockShapePassBase::GpuPropagateBlockShapePassBase;

  void runOnOperation() override {
    if (!enabled)
      return;

    ModuleOp module = getOperation();

    module.walk([&](gpu::GPUModuleOp gpuMod) {
      gpuMod.walk([&](gpu::GPUFuncOp fn) {
        if (!fn.isKernel())
          return;
        processKernel(fn);
      });
    });
  }

  void processKernel(gpu::GPUFuncOp kernel) {
    auto knownSize =
        kernel->getAttrOfType<DenseI32ArrayAttr>("known_block_size");
    if (!knownSize)
      return;

    auto dims = knownSize.asArrayRef();
    if (dims.size() < 3)
      return;

    int32_t bx = dims[0], by = dims[1], bz = dims[2];

    LLVM_DEBUG(llvm::dbgs() << "PropagateBlockShape: " << kernel.getName()
                            << " known_block_size=[" << bx << "," << by << ","
                            << bz << "]\n");

    SmallVector<gpu::BlockDimOp> blockDimOps;
    kernel.walk([&](gpu::BlockDimOp op) { blockDimOps.push_back(op); });

    if (blockDimOps.empty())
      return;

    OpBuilder builder(kernel.getContext());
    for (gpu::BlockDimOp dimOp : blockDimOps) {
      int32_t val;
      switch (dimOp.getDimension()) {
      case gpu::Dimension::x:
        val = bx;
        break;
      case gpu::Dimension::y:
        val = by;
        break;
      case gpu::Dimension::z:
        val = bz;
        break;
      }

      LLVM_DEBUG(llvm::dbgs() << "  replacing gpu.block_dim "
                              << stringifyDimension(dimOp.getDimension())
                              << " with " << val << "\n");

      builder.setInsertionPoint(dimOp);
      Value constant = builder.create<arith::ConstantIndexOp>(dimOp.getLoc(),
                                                              val);
      dimOp.getResult().replaceAllUsesWith(constant);
      dimOp.erase();
    }
  }
};

} // namespace
