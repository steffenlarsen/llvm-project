//===- mlir-reduce.cpp - The MLIR reducer ---------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the general framework of the MLIR reducer tool. It
// parses the command line arguments, parses the initial MLIR test case and sets
// up the testing environment. It  outputs the most reduced test case variant
// after executing the reduction passes.
//
//===----------------------------------------------------------------------===//

#include "mlir/Tools/mlir-reduce/MlirReduceMain.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Reducer/Passes.h"
#include "mlir/Support/FileUtilities.h"
#include "mlir/Support/ToolUtilities.h"
#include "mlir/Tools/ParseUtilities.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"

using namespace mlir;
using namespace llvm::clv2;

//===----------------------------------------------------------------------===//
// Command-line options
//===----------------------------------------------------------------------===//

static constexpr OptionInfo<std::string> reduceInputOpt{"", "<input file>",
                                                        Positional{}};
static constexpr OptionInfo<std::string> reduceOutputOpt{"o", "Output filename",
                                                         Init{"-"}};
static constexpr OptionInfo<bool> reduceNoImplicitModuleOpt{
    "no-implicit-module",
    "Disable implicit addition of a top-level module op during parsing"};
static constexpr OptionInfo<bool> reduceAllowUnregisteredOpt{
    "allow-unregistered-dialect",
    "Allow operation with no registered dialects"};
static constexpr OptionInfo<std::string> reduceSplitInputFileOpt{
    "split-input-file",
    "Split the input file into chunks and process each independently",
    ValueOptional};

static constexpr OptionsRegistry<
    &reduceInputOpt, &reduceOutputOpt, &reduceNoImplicitModuleOpt,
    &reduceAllowUnregisteredOpt, &reduceSplitInputFileOpt>
    MlirReduceReg;

LogicalResult mlir::mlirReduceMain(int argc, char **argv,
                                   MLIRContext &context) {

  llvm::InitLLVM y(argc, argv);

  registerReducerPasses();

  PassPipelineCLParser parser("", "Reduction Passes to Run");
  llvm::clv2::OptionParser P;
  P.add<&MlirReduceReg>();
  llvm::RegisterCoreLLVMOptions(P);
  parser.registerWith(P);
  auto OptsCtx = P.parse(argc, argv, "MLIR test case reduction tool.\n");
  auto *Opts = OptsCtx->getViewPtr<&MlirReduceReg>();

  bool allowUnregisteredDialects = Opts->get<&reduceAllowUnregisteredOpt>();
  if (allowUnregisteredDialects)
    context.allowUnregisteredDialects();

  std::string errorMessage;

  auto output = openOutputFile(Opts->get<&reduceOutputOpt>(), &errorMessage);
  if (!output)
    return failure();

  std::unique_ptr<llvm::MemoryBuffer> input =
      openInputFile(Opts->get<&reduceInputOpt>(), &errorMessage);
  if (!input) {
    llvm::errs() << errorMessage << "\n";
    return failure();
  }

  auto errorHandler = [&](const Twine &msg) {
    return emitError(UnknownLoc::get(&context)) << msg;
  };

  auto chunkFn = [&](std::unique_ptr<llvm::MemoryBuffer> chunkBuffer,
                     raw_ostream &os) {
    auto sourceMgr = std::make_shared<llvm::SourceMgr>();
    sourceMgr->AddNewSourceBuffer(std::move(chunkBuffer), SMLoc());
    OwningOpRef<Operation *> opRef = parseSourceFileForTool(
        sourceMgr, &context, !Opts->get<&reduceNoImplicitModuleOpt>());
    if (!opRef)
      return failure();
    // Reduction pass pipeline.
    PassManager pm(&context, opRef.get()->getName().getStringRef());
    if (failed(parser.addToPipeline(pm, errorHandler)))
      return failure();

    OwningOpRef<Operation *> op = opRef.get()->clone();

    if (failed(pm.run(op.get())))
      return failure();
    op.get()->print(output->os());
    output->keep();
    return success();
  };

  if (Opts->specified<&reduceSplitInputFileOpt>()) {
    auto marker = Opts->get<&reduceSplitInputFileOpt>();
    std::string splitMarker =
        marker.empty() ? kDefaultSplitMarker : std::string(marker);
    return splitAndProcessBuffer(std::move(input), chunkFn, output->os(),
                                 splitMarker, splitMarker);
  }

  return chunkFn(std::move(input), output->os());
}
