//===- TestDeviceIndices.cpp - Print recognised device index reads --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Test pass that reports every device index read the recogniser finds, so it
// can be exercised through cir-opt + FileCheck without needing a client pass.
//
//===----------------------------------------------------------------------===//

#include "mlir/Pass/Pass.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/Transforms/OffloadOpt/DeviceIndex.h"
#include "llvm/Support/raw_ostream.h"

using namespace mlir;

namespace {
// Anchored on the module rather than on cir.func so the report is emitted in
// source order; a function pass would run the bodies concurrently and
// interleave their lines.
struct PrintDeviceIndicesPass
    : public PassWrapper<PrintDeviceIndicesPass, OperationPass<ModuleOp>> {
  MLIR_DEFINE_EXPLICIT_INTERNAL_INLINE_TYPE_ID(PrintDeviceIndicesPass)

  StringRef getArgument() const final { return "test-print-device-indices"; }
  StringRef getDescription() const final {
    return "Print the device index reads (blockIdx/threadIdx/blockDim/gridDim) "
           "recognised in each function.";
  }
  void runOnOperation() override {
    getOperation().walk([&](cir::FuncOp fn) {
      if (fn.isDeclaration())
        return;
      llvm::errs() << "function " << fn.getSymName() << "\n";
      fn.walk([&](mlir::Operation *op) {
        if (std::optional<cir::DeviceIndex> index = cir::matchDeviceIndex(op))
          llvm::errs() << "  " << cir::getDeviceIndexKindName(index->kind)
                       << "." << "xyz"[index->dim] << "\n";
      });
    });
  }
};
} // namespace

namespace cir {
namespace test {
void registerPrintDeviceIndicesPass() {
  PassRegistration<PrintDeviceIndicesPass>();
}
} // namespace test
} // namespace cir
