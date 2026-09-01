//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Similar to MLIR/LLVM's "opt" tools but also deals with analysis and custom
// arguments. TODO: this is basically a copy from MlirOptMain.cpp, but capable
// of module emission as specified by the user.
//
//===----------------------------------------------------------------------===//

#include "mlir/Conversion/ReconcileUnrealizedCasts/ReconcileUnrealizedCasts.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/OpenMP/Transforms/Passes.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassOptions.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Tools/mlir-opt/MlirOptMain.h"
#include "mlir/Transforms/Passes.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "clang/CIR/Dialect/IR/CIRInlinerInterface.h"
#include "clang/CIR/Dialect/Passes.h"
#include "clang/CIR/InitAllDialects.h"
#include "clang/CIR/Passes.h"

#ifdef CLANG_INCLUDE_TESTS
namespace cir::test {
void registerTestCIRAliasAnalysisPass();
void registerPrintKernelBindingsPass();
void registerPrintDeviceIndicesPass();
} // namespace cir::test
#endif

struct CIRToLLVMPipelineOptions
    : public mlir::PassPipelineOptions<CIRToLLVMPipelineOptions> {
  Option<bool> enableOpenMP{
      *this, "enable-openmp",
      llvm::cl::desc("Add OpenMP-specific CIR-to-LLVM lowering passes"),
      llvm::cl::init(false)};
};

int main(int argc, char **argv) {
  // TODO: register needed MLIR passes for CIR?
  mlir::DialectRegistry registry;
  cir::registerAllDialects(registry);

#ifdef CLANG_INCLUDE_TESTS
  cir::test::registerTestCIRAliasAnalysisPass();
  cir::test::registerPrintKernelBindingsPass();
  cir::test::registerPrintDeviceIndicesPass();
#endif
  registry.insert<mlir::memref::MemRefDialect, mlir::LLVM::LLVMDialect>();

  // Attach the CIR DialectInlinerInterface and register the LLVM dialect's
  // inliner-interface extension; shared with cir-offload-merge.
  cir::registerInlinerInterface(registry);

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createCIRCanonicalizePass();
  });
  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createCIRSimplifyPass();
  });

  mlir::PassPipelineRegistration<CIRToLLVMPipelineOptions> pipeline(
      "cir-to-llvm", "",
      [](mlir::OpPassManager &pm, const CIRToLLVMPipelineOptions &options) {
        cir::direct::populateCIRToLLVMPasses(pm, options.enableOpenMP);
      });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createCIRFlattenCFGPass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createCIREHABILoweringPass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createHoistAllocasPass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createGotoSolverPass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createCXXABILoweringPass();
  });
  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createCUDARegisterModulePass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createIdiomRecognizerPass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createCallConvLoweringPass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createOffloadDeadKernelEliminationPass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createOffloadKernelArgConstantPropagationPass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createOffloadLaunchBoundsPropagationPass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createOffloadPropagateBlockShapePass();
  });

  ::mlir::registerPass([]() -> std::unique_ptr<::mlir::Pass> {
    return mlir::createOffloadEliminateCoveredGuardsPass();
  });

  mlir::omp::registerOpenMPPasses();
  mlir::registerTransformsPasses();

  return mlir::asMainReturnCode(MlirOptMain(
      argc, argv, "Clang IR analysis and optimization tool\n", registry));
}
