//===- tco.cpp - Tilikum Crossing Opt ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is to be like LLVM's opt program, only for FIR.  Such a program is
// required for roundtrip testing, etc.
//
//===----------------------------------------------------------------------===//

#include "flang/Common/FlangOptionsOptInfos.h"
#include "flang/Optimizer/CodeGen/CodeGen.h"
#include "flang/Optimizer/Dialect/Support/FIRContext.h"
#include "flang/Optimizer/Dialect/Support/KindMapping.h"
#include "flang/Optimizer/Support/DataLayout.h"
#include "flang/Optimizer/Support/InitFIR.h"
#include "flang/Optimizer/Support/InternalNames.h"
#include "flang/Optimizer/Transforms/Passes.h"
#include "flang/Support/FPMaxminBehavior.h"
#include "flang/Tools/CrossToolHelpers.h"
#include "mlir/IR/AsmState.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/MLIROptionsOptInfos.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::clv2;

//===----------------------------------------------------------------------===//
// tco-local option declarations
//===----------------------------------------------------------------------===//

inline constexpr OptionInfo<std::string> TCO_InputFilename{
    "", "<input file>", Positional{}, Init{"-"}};

inline constexpr OptionInfo<std::string> TCO_OutputFilename{
    "o", "Specify output filename", value_desc("filename"), Init{"-"}};

inline constexpr OptionInfo<bool> TCO_EmitFir{
    "emit-fir", "Parse and pretty-print the input", Init{false}};

inline constexpr OptionInfo<unsigned> TCO_OptLevel{
    "O", "Optimization level. [-O0, -O1, -O2, or -O3] (default = '-O2')",
    PrefixFormat, Init{2u}};

inline constexpr OptionInfo<std::string> TCO_TargetTriple{
    "target", "specify a target triple", Init{"native"}};

inline constexpr OptionInfo<std::string> TCO_TargetCPU{
    "target-cpu", "specify a target CPU", Init{""}};

inline constexpr OptionInfo<std::string> TCO_TuneCPU{
    "tune-cpu", "specify a tune CPU", Init{""}};

inline constexpr OptionInfo<std::string> TCO_TargetFeatures{
    "target-features", "specify the target features", Init{""}};

inline constexpr OptionInfo<bool> TCO_CodeGenLLVM{
    "code-gen-llvm", "Run only CodeGen passes and translate FIR to LLVM IR",
    Init{false}};

inline constexpr OptionInfo<bool> TCO_EmitFinalMLIR{
    "emit-final-mlir", "Only translate FIR to MLIR, do not lower to LLVM IR",
    Init{false}};

inline constexpr OptionInfo<bool> TCO_SimplifyMLIR{
    "simplify-mlir", "Run CSE and canonicalization on MLIR output",
    Init{false}};

// Enabled by default to accurately reflect -O2
inline constexpr OptionInfo<bool> TCO_EnableAliasAnalysis{
    "enable-aa", "Enable FIR alias analysis", Init{true}};

inline constexpr OptionInfo<bool> TCO_TestGeneratorMode{
    "test-gen", "-emit-final-mlir -simplify-mlir -enable-aa=false",
    Init{false}};

// FPMaxminBehavior enum option
inline constexpr EnumVal<Fortran::common::FPMaxminBehavior>
    FPMaxminBehaviorVals[] = {
        {"legacy", Fortran::common::FPMaxminBehavior::Legacy, "cmp+select"},
        {"portable", Fortran::common::FPMaxminBehavior::Portable,
         "cmp+select and arith.max/minnumf when nnan and nsz fast math flags "
         "are enabled"},
        {"extremum", Fortran::common::FPMaxminBehavior::Extremum,
         "arith.max/minimum"},
        {"extremenum", Fortran::common::FPMaxminBehavior::ExtremeNum,
         "arith.max/minnum"},
};

inline constexpr auto TCO_FPMaxminBehavior =
    makeEnumOption<Fortran::common::FPMaxminBehavior>(
        "ffp-maxmin-behavior",
        "Control max/min and [max|min][loc|val] behavior "
        "[legacy|portable|extremum|extremenum] (for future pass use)",
        FPMaxminBehaviorVals, Init{Fortran::common::FPMaxminBehavior::Legacy});

//===----------------------------------------------------------------------===//
// Tool registry
//===----------------------------------------------------------------------===//

inline constexpr OptionsRegistry<
    &TCO_InputFilename, &TCO_OutputFilename, &TCO_EmitFir, &TCO_OptLevel,
    &TCO_TargetTriple, &TCO_TargetCPU, &TCO_TuneCPU, &TCO_TargetFeatures,
    &TCO_CodeGenLLVM, &TCO_EmitFinalMLIR, &TCO_SimplifyMLIR,
    &TCO_EnableAliasAnalysis, &TCO_TestGeneratorMode, &TCO_FPMaxminBehavior>
    TCOToolReg;

using TCOToolOpts = decltype(TCOToolReg)::ParsedOptionsT;

#include "flang/Optimizer/Passes/Pipelines.h"

static void printModule(mlir::ModuleOp mod, raw_ostream &output) {
  output << mod << '\n';
}

static std::optional<llvm::OptimizationLevel>
getOptimizationLevel(unsigned level) {
  switch (level) {
  default:
    return std::nullopt;
  case 0:
    return llvm::OptimizationLevel::O0;
  case 1:
    return llvm::OptimizationLevel::O1;
  case 2:
    return llvm::OptimizationLevel::O2;
  case 3:
    return llvm::OptimizationLevel::O3;
  }
}

// compile a .fir file
static llvm::LogicalResult
compileFIR(const mlir::PassPipelineCLParser &passPipeline,
           const OptionsContext &OptsCtx) {
  const auto *TCOOpts = OptsCtx.getViewPtr<&TCOToolReg>();

  const std::string &inputFilename = TCOOpts->get<&TCO_InputFilename>();
  const std::string &outputFilename = TCOOpts->get<&TCO_OutputFilename>();
  bool emitFir = TCOOpts->get<&TCO_EmitFir>();
  unsigned optLevel = TCOOpts->get<&TCO_OptLevel>();
  const std::string &targetTriple = TCOOpts->get<&TCO_TargetTriple>();
  const std::string &targetCPU = TCOOpts->get<&TCO_TargetCPU>();
  const std::string &tuneCPU = TCOOpts->get<&TCO_TuneCPU>();
  const std::string &targetFeatures = TCOOpts->get<&TCO_TargetFeatures>();
  bool codeGenLLVM = TCOOpts->get<&TCO_CodeGenLLVM>();
  bool emitFinalMLIR = TCOOpts->get<&TCO_EmitFinalMLIR>();
  bool simplifyMLIR = TCOOpts->get<&TCO_SimplifyMLIR>();
  bool enableAliasAnalysis = TCOOpts->get<&TCO_EnableAliasAnalysis>();
  bool testGeneratorMode = TCOOpts->get<&TCO_TestGeneratorMode>();
  auto fpMaxminBehavior = TCOOpts->get<&TCO_FPMaxminBehavior>();

  // check that there is a file to load
  ErrorOr<std::unique_ptr<MemoryBuffer>> fileOrErr =
      MemoryBuffer::getFileOrSTDIN(inputFilename);

  if (std::error_code EC = fileOrErr.getError()) {
    errs() << "Could not open file: " << EC.message() << '\n';
    return mlir::failure();
  }

  // load the file into a module
  SourceMgr sourceMgr;
  sourceMgr.AddNewSourceBuffer(std::move(*fileOrErr), SMLoc());
  mlir::DialectRegistry registry;
  fir::support::registerDialects(registry);
  fir::support::addFIRExtensions(registry);
  mlir::MLIRContext context(OptsCtx, registry);
  fir::support::loadDialects(context);
  fir::support::registerLLVMTranslation(context);
  auto owningRef = mlir::parseSourceFile<mlir::ModuleOp>(sourceMgr, &context);

  if (!owningRef) {
    errs() << "Error can't load file " << inputFilename << '\n';
    return mlir::failure();
  }
  if (mlir::failed(owningRef->verifyInvariants())) {
    errs() << "Error verifying FIR module\n";
    return mlir::failure();
  }

  std::error_code ec;
  ToolOutputFile out(outputFilename, ec, sys::fs::OF_None);

  // run passes
  fir::KindMapping kindMap{&context};
  fir::setTargetTriple(*owningRef, targetTriple);
  fir::setKindMapping(*owningRef, kindMap);
  fir::setTargetCPU(*owningRef, targetCPU);
  fir::setTuneCPU(*owningRef, tuneCPU);
  fir::setTargetFeatures(*owningRef, targetFeatures);
  // tco is a testing tool, so it will happily use the target independent
  // data layout if none is on the module.
  fir::support::setMLIRDataLayoutFromAttributes(*owningRef,
                                                /*allowDefaultLayout=*/true);
  mlir::PassManager pm((*owningRef)->getName(),
                       mlir::OpPassManager::Nesting::Implicit);
  pm.enableVerifier(/*verifyPasses=*/true);
  (void)mlir::applyPassManagerCLOptions(pm);
  if (emitFir) {
    // parse the input and pretty-print it back out
    // -emit-fir intentionally disables all the passes
  } else if (passPipeline.hasAnyOccurrences()) {
    auto errorHandler = [&](const Twine &msg) {
      mlir::emitError(mlir::UnknownLoc::get(pm.getContext())) << msg;
      return mlir::failure();
    };
    if (mlir::failed(passPipeline.addToPipeline(pm, errorHandler)))
      return mlir::failure();
  } else {
    std::optional<llvm::OptimizationLevel> level =
        getOptimizationLevel(optLevel);
    if (!level) {
      errs() << "Error invalid optimization level\n";
      return mlir::failure();
    }
    MLIRToLLVMPassPipelineConfig config(*level);
    config.fpMaxminBehavior = fpMaxminBehavior;
    // TODO: config.StackArrays should be set here?
    config.EnableOpenMP = true;  // assume the input contains OpenMP
    config.AliasAnalysis = enableAliasAnalysis && !testGeneratorMode;
    config.LoopVersioning = optLevel > 2;
    if (codeGenLLVM) {
      // Run only CodeGen passes.
      fir::createDefaultFIRCodeGenPassPipeline(pm, config);
    } else {
      // Run tco with O2 by default.
      fir::registerDefaultInlinerPass(config);
      fir::createMLIRToLLVMPassPipeline(pm, config);
    }
    if (simplifyMLIR || testGeneratorMode) {
      pm.addPass(mlir::createCanonicalizerPass());
      pm.addPass(mlir::createCSEPass());
    }
    if (!emitFinalMLIR && !testGeneratorMode)
      fir::addLLVMDialectToLLVMPass(pm, out.os());
  }

  // run the pass manager
  if (mlir::succeeded(pm.run(*owningRef))) {
    // passes ran successfully, so keep the output
    if ((emitFir || passPipeline.hasAnyOccurrences() || emitFinalMLIR ||
         testGeneratorMode) &&
        !codeGenLLVM)
      printModule(*owningRef, out.os());
    out.keep();
    return mlir::success();
  }

  // pass manager failed
  printModule(*owningRef, errs());
  errs() << "\n\nFAILED: " << inputFilename << '\n';
  return mlir::failure();
}

int main(int argc, char **argv) {
  [[maybe_unused]] InitLLVM y(argc, argv);
  fir::support::registerMLIRPassesForFortranTools();
  fir::registerOptCodeGenPasses();
  fir::registerOptTransformPasses();
  mlir::registerMLIRContextCLOptions();
  mlir::PassPipelineCLParser passPipe("", "Compiler passes to run");

  // Build the OptionParser with tco-local, flang library, MLIR, and LLVM
  // options.
  clv2::OptionParser P;
  P.add<&TCOToolReg>();
  P.add<&FlangOptsReg>();
  P.add<&clv2::MLIROptsReg>();
  RegisterCoreLLVMOptions(P);
  P.enableGlobalDynamicEntries();
  mlir::registerPassManagerCLOptions(P);
  passPipe.registerWith(P);

  auto OptsCtx = P.parse(argc, argv, "Tilikum Crossing Optimizer\n");
  if (!OptsCtx)
    return 1;

  // Disable the ExternalNameConversion pass by default until all the tests have
  // been updated to pass with it enabled. Override via mutable view.
  if (auto *FlangOpts = OptsCtx->getViewPtr<&FlangOptsReg>()) {
    if (!FlangOpts->specified<&FLANG_DisableExternalNameInterop>())
      FlangOpts->get<&FLANG_DisableExternalNameInterop>() = true;
  }

  return mlir::failed(compileFIR(passPipe, *OptsCtx));
}
