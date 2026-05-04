//===- mlir-irdl-to-cpp.cpp - IRDL to C++ conversion tool -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a command line utility that translates an IRDL dialect definition
// into a C++ implementation to be included in MLIR.
//
//===----------------------------------------------------------------------===//

#include "mlir/Dialect/IRDL/IR/IRDL.h"
#include "mlir/IR/AsmState.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Support/FileUtilities.h"
#include "mlir/Support/ToolUtilities.h"
#include "mlir/Target/IRDLToCpp/IRDLToCpp.h"
#include "mlir/Tools/ParseUtilities.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"

using namespace mlir;
using namespace llvm::clv2;

static LogicalResult
processBuffer(llvm::raw_ostream &os,
              std::unique_ptr<llvm::MemoryBuffer> ownedBuffer,
              bool verifyDiagnostics, llvm::ThreadPoolInterface *threadPool) {
  // Tell sourceMgr about this buffer, which is what the parser will pick up.
  auto sourceMgr = std::make_shared<llvm::SourceMgr>();
  sourceMgr->AddNewSourceBuffer(std::move(ownedBuffer), SMLoc());

  DialectRegistry registry;
  registry.insert<irdl::IRDLDialect>();
  MLIRContext ctx(registry);

  ctx.printOpOnDiagnostic(!verifyDiagnostics);

  auto runTranslation = [&]() {
    ParserConfig parseConfig(&ctx);
    OwningOpRef<Operation *> op =
        parseSourceFileForTool(sourceMgr, parseConfig, true);
    if (!op)
      return failure();

    auto moduleOp = llvm::cast<ModuleOp>(*op);
    llvm::SmallVector<irdl::DialectOp> dialects{
        moduleOp.getOps<irdl::DialectOp>(),
    };

    return irdl::translateIRDLDialectToCpp(dialects, os);
  };

  if (!verifyDiagnostics) {
    // If no errors are expected, return translation result.
    SourceMgrDiagnosticHandler srcManagerHandler(*sourceMgr, &ctx);
    return runTranslation();
  }

  // If errors are expected, ignore translation result and check for
  // diagnostics.
  SourceMgrDiagnosticVerifierHandler srcManagerHandler(*sourceMgr, &ctx);
  (void)runTranslation();
  return srcManagerHandler.verify();
}

inline constexpr OptionInfo<std::string> inputFilename{"", "<input file>",
                                                       Positional{}, Init{"-"}};

inline constexpr OptionInfo<std::string> outputFilename{
    "o", "Output filename", value_desc("filename"), Init{"-"}};

inline constexpr OptionInfo<bool> verifyDiagnosticsOpt{
    "verify-diagnostics",
    "Check that emitted diagnostics match "
    "expected-* lines on the corresponding line",
    Init{false}};

inline constexpr OptionInfo<std::string> splitInputFileOpt{
    "split-input-file",
    "Split the input file into chunks using the given or "
    "default marker and process each chunk independently",
    ValueOptional, Init{""}};

static constexpr OptionsRegistry<&inputFilename, &outputFilename,
                                 &verifyDiagnosticsOpt, &splitInputFileOpt>
    IrdlToCppReg;

static LogicalResult translateIRDLToCpp(int argc, char **argv) {
  llvm::InitLLVM y(argc, argv);

  llvm::clv2::OptionParser P;
  P.add<&IrdlToCppReg>();
  llvm::RegisterCoreLLVMOptions(P);
  auto OptsCtx = P.parse(argc, argv, "mlir-irdl-to-cpp");
  auto *Opts = OptsCtx->getViewPtr<&IrdlToCppReg>();

  // If split-input-file was passed without a value, use the default marker.
  std::string splitInputFile = Opts->get<&splitInputFileOpt>();
  if (Opts->specified<&splitInputFileOpt>() && splitInputFile.empty())
    splitInputFile = std::string(kDefaultSplitMarker);

  bool verifyDiagnostics = Opts->get<&verifyDiagnosticsOpt>();

  std::string errorMessage;
  std::unique_ptr<llvm::MemoryBuffer> input =
      openInputFile(Opts->get<&inputFilename>(), &errorMessage);
  if (!input) {
    llvm::errs() << errorMessage << "\n";
    return failure();
  }

  std::unique_ptr<llvm::ToolOutputFile> output =
      openOutputFile(Opts->get<&outputFilename>(), &errorMessage);

  if (!output) {
    llvm::errs() << errorMessage << "\n";
    return failure();
  }

  auto chunkFn = [&](std::unique_ptr<llvm::MemoryBuffer> chunkBuffer,
                     raw_ostream &os) {
    return processBuffer(output->os(), std::move(chunkBuffer),
                         verifyDiagnostics, nullptr);
  };

  if (!splitInputFile.empty())
    return splitAndProcessBuffer(std::move(input), chunkFn, output->os(),
                                 splitInputFile, splitInputFile);

  if (failed(chunkFn(std::move(input), output->os())))
    return failure();

  if (!verifyDiagnostics)
    output->keep();

  return success();
}

int main(int argc, char **argv) {
  return failed(translateIRDLToCpp(argc, argv));
}
