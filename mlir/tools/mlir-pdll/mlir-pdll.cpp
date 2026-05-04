//===- mlir-pdll.cpp - MLIR PDLL frontend -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/IR/BuiltinOps.h"
#include "mlir/Support/FileUtilities.h"
#include "mlir/Support/ToolUtilities.h"
#include "mlir/Tools/PDLL/AST/Context.h"
#include "mlir/Tools/PDLL/AST/Nodes.h"
#include "mlir/Tools/PDLL/CodeGen/CPPGen.h"
#include "mlir/Tools/PDLL/CodeGen/MLIRGen.h"
#include "mlir/Tools/PDLL/ODS/Context.h"
#include "mlir/Tools/PDLL/Parser/Parser.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <set>

using namespace mlir;
using namespace mlir::pdll;
using namespace llvm::clv2;

//===----------------------------------------------------------------------===//
// main
//===----------------------------------------------------------------------===//

/// The desired output type.
enum class OutputType {
  AST,
  MLIR,
  CPP,
};

static LogicalResult
processBuffer(raw_ostream &os, std::unique_ptr<llvm::MemoryBuffer> chunkBuffer,
              OutputType outputType, std::vector<std::string> &includeDirs,
              bool dumpODS, std::set<std::string> *includedFiles) {
  llvm::SourceMgr sourceMgr;
  sourceMgr.setIncludeDirs(includeDirs);
  sourceMgr.setVirtualFileSystem(llvm::vfs::getRealFileSystem());
  sourceMgr.AddNewSourceBuffer(std::move(chunkBuffer), SMLoc());

  // If we are dumping ODS information, also enable documentation to ensure the
  // summary and description information is imported as well.
  bool enableDocumentation = dumpODS;

  ods::Context odsContext;
  ast::Context astContext(odsContext);
  FailureOr<ast::Module *> module =
      parsePDLLAST(astContext, sourceMgr, enableDocumentation);
  if (failed(module))
    return failure();

  // Add the files that were included to the set.
  if (includedFiles) {
    for (unsigned i = 1, e = sourceMgr.getNumBuffers(); i < e; ++i) {
      includedFiles->insert(
          sourceMgr.getMemoryBuffer(i + 1)->getBufferIdentifier().str());
    }
  }

  // Print out the ODS information if requested.
  if (dumpODS)
    odsContext.print(llvm::errs());

  // Generate the output.
  if (outputType == OutputType::AST) {
    (*module)->print(os);
    return success();
  }

  MLIRContext mlirContext;
  OwningOpRef<ModuleOp> pdlModule =
      codegenPDLLToMLIR(&mlirContext, astContext, sourceMgr, **module);
  if (!pdlModule)
    return failure();

  if (outputType == OutputType::MLIR) {
    pdlModule->print(os, opPrintingFlags(*pdlModule).enableDebugInfo());
    return success();
  }
  codegenPDLLToCPP(**module, *pdlModule, os);
  return success();
}

/// Create a dependency file for `-d` option.
///
/// This functionality is generally only for the benefit of the build system,
/// and is modeled after the same option in TableGen.
static LogicalResult
createDependencyFile(StringRef outputFilename, StringRef dependencyFile,
                     std::set<std::string> &includedFiles) {
  if (outputFilename == "-") {
    llvm::errs() << "error: the option -d must be used together with -o\n";
    return failure();
  }

  std::string errorMessage;
  std::unique_ptr<llvm::ToolOutputFile> outputFile =
      openOutputFile(dependencyFile, &errorMessage);
  if (!outputFile) {
    llvm::errs() << errorMessage << "\n";
    return failure();
  }

  outputFile->os() << outputFilename << ":";
  for (const auto &includeFile : includedFiles)
    outputFile->os() << ' ' << includeFile;
  outputFile->os() << "\n";
  outputFile->keep();
  return success();
}

inline constexpr OptionInfo<std::string> inputFilenameOpt{
    "", "<input file>", Positional{}, Init{"-"}, value_desc("filename")};

inline constexpr OptionInfo<std::string> outputFilenameOpt{
    "o", "Output filename", value_desc("filename"), Init{"-"}};

inline constexpr ListOptionInfo<std::string> includeDirsOpt{
    "I", "Directory of include files", value_desc("directory"), PrefixFormat};

inline constexpr OptionInfo<bool> dumpODSOpt{
    "dump-ods", "Print out the parsed ODS information from the input file",
    Init{false}};

inline constexpr OptionInfo<std::string> inputSplitMarkerOpt{
    "split-input-file",
    "Split the input file into chunks using the given or "
    "default marker and process each chunk independently",
    ValueOptional, Init{""}};

inline constexpr OptionInfo<std::string> outputSplitMarkerOpt{
    "output-split-marker", "Split marker to use for merging the ouput",
    Init{"// -----"}};

inline constexpr EnumVal<OutputType> outputTypeVals[] = {
    {"ast", OutputType::AST, "generate the AST for the input file"},
    {"mlir", OutputType::MLIR, "generate the PDL MLIR for the input file"},
    {"cpp", OutputType::CPP,
     "generate a C++ source file containing the "
     "patterns for the input file"},
};

inline constexpr auto outputTypeOpt = makeEnumOption<OutputType>(
    "x", "The type of output desired", outputTypeVals, Init{OutputType::AST});

inline constexpr OptionInfo<std::string> dependencyFilenameOpt{
    "d", "Dependency filename", value_desc("filename"), Init{""}};

inline constexpr OptionInfo<bool> writeIfChangedOpt{
    "write-if-changed", "Only write to the output file if it changed"};

// `ResetCommandLineParser` at the above unregistered the "D" option
// of `llvm-tblgen`, which causes tblgen usage to fail due to
// "Unknnown command line argument '-D...`" when a macros name is
// present. The following is a workaround to re-register it again.
inline constexpr ListOptionInfo<std::string> macroNamesOpt{
    "D", "Name of the macro to be defined -- ignored by mlir-pdll",
    value_desc("macro name"), PrefixFormat};

static constexpr OptionsRegistry<
    &inputFilenameOpt, &outputFilenameOpt, &includeDirsOpt, &dumpODSOpt,
    &inputSplitMarkerOpt, &outputSplitMarkerOpt, &outputTypeOpt,
    &dependencyFilenameOpt, &writeIfChangedOpt, &macroNamesOpt>
    PdllReg;

int main(int argc, char **argv) {
  llvm::InitLLVM y(argc, argv);
  llvm::clv2::OptionParser P;
  P.add<&PdllReg>();
  llvm::RegisterCoreLLVMOptions(P);
  auto OptsCtx = P.parse(argc, argv, "PDLL Frontend");
  auto *Opts = OptsCtx->getViewPtr<&PdllReg>();

  // Handle split-input-file: if flag was passed without a value, use default.
  std::string inputSplitMarker = Opts->get<&inputSplitMarkerOpt>();
  if (Opts->specified<&inputSplitMarkerOpt>() && inputSplitMarker.empty())
    inputSplitMarker = std::string(kDefaultSplitMarker);

  // Set up the input file.
  std::string errorMessage;
  std::unique_ptr<llvm::MemoryBuffer> inputFile =
      openInputFile(Opts->get<&inputFilenameOpt>(), &errorMessage);
  if (!inputFile) {
    llvm::errs() << errorMessage << "\n";
    return 1;
  }

  // If we are creating a dependency file, we'll also need to track what files
  // get included during processing.
  std::set<std::string> includedFilesStorage;
  std::set<std::string> *includedFiles = nullptr;
  if (!Opts->get<&dependencyFilenameOpt>().empty())
    includedFiles = &includedFilesStorage;

  // Copy include dirs to a mutable vector.
  auto &includeVec = Opts->get<&includeDirsOpt>();
  std::vector<std::string> includeDirs(includeVec.begin(), includeVec.end());

  OutputType outputType = Opts->get<&outputTypeOpt>();
  bool dumpODS = Opts->get<&dumpODSOpt>();

  // The split-input-file mode is a very specific mode that slices the file
  // up into small pieces and checks each independently.
  std::string outputStr;
  llvm::raw_string_ostream outputStrOS(outputStr);
  auto processFn = [&](std::unique_ptr<llvm::MemoryBuffer> chunkBuffer,
                       raw_ostream &os) {
    // Split does not guarantee null-termination. Make a copy of the buffer to
    // ensure null-termination.
    if (!chunkBuffer->getBuffer().ends_with('\0')) {
      chunkBuffer = llvm::MemoryBuffer::getMemBufferCopy(
          chunkBuffer->getBuffer(), chunkBuffer->getBufferIdentifier());
    }
    return processBuffer(os, std::move(chunkBuffer), outputType, includeDirs,
                         dumpODS, includedFiles);
  };
  if (failed(splitAndProcessBuffer(std::move(inputFile), processFn, outputStrOS,
                                   inputSplitMarker,
                                   Opts->get<&outputSplitMarkerOpt>())))
    return 1;

  // Write the output.
  bool shouldWriteOutput = true;
  if (Opts->get<&writeIfChangedOpt>()) {
    // Only update the real output file if there are any differences. This
    // prevents recompilation of all the files depending on it if there aren't
    // any.
    if (auto existingOrErr = llvm::MemoryBuffer::getFile(
            Opts->get<&outputFilenameOpt>(), /*IsText=*/true))
      if (std::move(existingOrErr.get())->getBuffer() == outputStr)
        shouldWriteOutput = false;
  }

  // Populate the output file if necessary.
  if (shouldWriteOutput) {
    std::unique_ptr<llvm::ToolOutputFile> outputFile =
        openOutputFile(Opts->get<&outputFilenameOpt>(), &errorMessage);
    if (!outputFile) {
      llvm::errs() << errorMessage << "\n";
      return 1;
    }
    outputFile->os() << outputStr;
    outputFile->keep();
  }

  // Always write the depfile, even if the main output hasn't changed. If it's
  // missing, Ninja considers the output dirty.
  if (!Opts->get<&dependencyFilenameOpt>().empty()) {
    if (failed(createDependencyFile(Opts->get<&outputFilenameOpt>(),
                                    Opts->get<&dependencyFilenameOpt>(),
                                    includedFilesStorage)))
      return 1;
  }

  return 0;
}
