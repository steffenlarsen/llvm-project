//===- toyc.cpp - The Toy Compiler ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the entry point for the Toy compiler.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/Func/Extensions/AllExtensions.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/IR/Diagnostics.h"
#include "toy/AST.h"
#include "toy/Dialect.h"
#include "toy/Lexer.h"
#include "toy/MLIRGen.h"
#include "toy/Parser.h"
#include "toy/Passes.h"

#include "mlir/Dialect/Affine/Transforms/Passes.h"
#include "mlir/IR/AsmState.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Verifier.h"
#include "mlir/InitAllDialects.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <string>
#include <system_error>
#include <utility>

using namespace toy;
using namespace llvm;

namespace {
enum InputType { Toy, MLIR };
enum Action { None, DumpAST, DumpMLIR, DumpMLIRAffine };
} // namespace

static constexpr clv2::OptionInfo<std::string> inputFilenameOpt{
    "", "<input toy file>", clv2::Positional{}, clv2::Init{"-"}};

static constexpr clv2::EnumVal<InputType> inputTypeVals[] = {
    {"toy", Toy, "load the input file as a Toy source."},
    {"mlir", MLIR, "load the input file as an MLIR file"},
};
static constexpr auto inputTypeOpt = clv2::makeEnumOption<InputType>(
    "x", "Decided the kind of output desired", inputTypeVals);

static constexpr clv2::EnumVal<Action> emitActionVals[] = {
    {"ast", DumpAST, "output the AST dump"},
    {"mlir", DumpMLIR, "output the MLIR dump"},
    {"mlir-affine", DumpMLIRAffine,
     "output the MLIR dump after affine lowering"},
};
static constexpr auto emitActionOpt = clv2::makeEnumOption<Action>(
    "emit", "Select the kind of output desired", emitActionVals);

static constexpr clv2::OptionInfo<bool> enableOptOpt{"opt",
                                                     "Enable optimizations"};

static constexpr clv2::OptionsRegistry<&inputFilenameOpt, &inputTypeOpt,
                                       &emitActionOpt, &enableOptOpt>
    ToyReg;

// These are populated from parsed options in main().
static std::string inputFilename;
static InputType inputType;
static Action emitAction;
static bool enableOpt;

/// Returns a Toy AST resulting from parsing the file or a nullptr on error.
static std::unique_ptr<toy::ModuleAST>
parseInputFile(llvm::StringRef filename) {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> fileOrErr =
      llvm::MemoryBuffer::getFileOrSTDIN(filename);
  if (std::error_code ec = fileOrErr.getError()) {
    llvm::errs() << "Could not open input file: " << ec.message() << "\n";
    return nullptr;
  }
  auto buffer = fileOrErr.get()->getBuffer();
  LexerBuffer lexer(buffer.begin(), buffer.end(), std::string(filename));
  Parser parser(lexer);
  return parser.parseModule();
}

static int loadMLIR(llvm::SourceMgr &sourceMgr, mlir::MLIRContext &context,
                    mlir::OwningOpRef<mlir::ModuleOp> &module) {
  // Handle '.toy' input to the compiler.
  if (inputType != InputType::MLIR &&
      !llvm::StringRef(inputFilename).ends_with(".mlir")) {
    auto moduleAST = parseInputFile(inputFilename);
    if (!moduleAST)
      return 6;
    module = mlirGen(context, *moduleAST);
    return !module ? 1 : 0;
  }

  // Otherwise, the input is '.mlir'.
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> fileOrErr =
      llvm::MemoryBuffer::getFileOrSTDIN(inputFilename);
  if (std::error_code ec = fileOrErr.getError()) {
    llvm::errs() << "Could not open input file: " << ec.message() << "\n";
    return -1;
  }

  // Parse the input mlir.
  sourceMgr.AddNewSourceBuffer(std::move(*fileOrErr), llvm::SMLoc());
  module = mlir::parseSourceFile<mlir::ModuleOp>(sourceMgr, &context);
  if (!module) {
    llvm::errs() << "Error can't load file " << inputFilename << "\n";
    return 3;
  }
  return 0;
}

static int dumpMLIR() {
  mlir::DialectRegistry registry;
  mlir::func::registerAllExtensions(registry);

  mlir::MLIRContext context(registry);
  // Load our Dialect in this MLIR Context.
  context.getOrLoadDialect<mlir::toy::ToyDialect>();

  mlir::OwningOpRef<mlir::ModuleOp> module;
  llvm::SourceMgr sourceMgr;
  mlir::SourceMgrDiagnosticHandler sourceMgrHandler(sourceMgr, &context);
  if (int error = loadMLIR(sourceMgr, context, module))
    return error;

  mlir::PassManager pm(module.get()->getName());
  // Apply any generic pass manager command line options and run the pipeline.
  if (mlir::failed(mlir::applyPassManagerCLOptions(pm)))
    return 4;

  // Check to see what granularity of MLIR we are compiling to.
  bool isLoweringToAffine = emitAction >= Action::DumpMLIRAffine;

  if (enableOpt || isLoweringToAffine) {
    // Inline all functions into main and then delete them.
    pm.addPass(mlir::createInlinerPass());

    // Now that there is only one function, we can infer the shapes of each of
    // the operations.
    mlir::OpPassManager &optPM = pm.nest<mlir::toy::FuncOp>();
    optPM.addPass(mlir::toy::createShapeInferencePass());
    optPM.addPass(mlir::createCanonicalizerPass());
    optPM.addPass(mlir::createCSEPass());
  }

  if (isLoweringToAffine) {
    // Partially lower the toy dialect.
    pm.addPass(mlir::toy::createLowerToAffinePass());

    // Add a few cleanups post lowering.
    mlir::OpPassManager &optPM = pm.nest<mlir::func::FuncOp>();
    optPM.addPass(mlir::createCanonicalizerPass());
    optPM.addPass(mlir::createCSEPass());

    // Add optimizations if enabled.
    if (enableOpt) {
      optPM.addPass(mlir::affine::createLoopFusionPass());
      optPM.addPass(mlir::affine::createAffineScalarReplacementPass());
    }
  }

  if (mlir::failed(pm.run(*module)))
    return 4;

  module->dump();
  return 0;
}

static int dumpAST() {
  if (inputType == InputType::MLIR) {
    llvm::errs() << "Can't dump a Toy AST when the input is MLIR\n";
    return 5;
  }

  auto moduleAST = parseInputFile(inputFilename);
  if (!moduleAST)
    return 1;

  dump(*moduleAST);
  return 0;
}

int main(int argc, char **argv) {
  // Register any command line options.
  mlir::registerAsmPrinterCLOptions();
  mlir::registerMLIRContextCLOptions();

  llvm::clv2::OptionParser P;
  P.add<&ToyReg>();
  RegisterAllLLVMOptions(P);
  mlir::registerPassManagerCLOptions(P);
  auto OptsCtx = P.parse(argc, argv, "toy compiler\n");
  auto *Opts = OptsCtx->getViewPtr<&ToyReg>();

  inputFilename = std::string(Opts->get<&inputFilenameOpt>());
  inputType = Opts->get<&inputTypeOpt>();
  emitAction = Opts->get<&emitActionOpt>();
  enableOpt = Opts->get<&enableOptOpt>();

  switch (emitAction) {
  case Action::DumpAST:
    return dumpAST();
  case Action::DumpMLIR:
  case Action::DumpMLIRAffine:
    return dumpMLIR();
  default:
    llvm::errs() << "No action specified (parsing only?), use -emit=<action>\n";
  }

  return 0;
}
