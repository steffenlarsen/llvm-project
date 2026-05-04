//===- mlir-src-sharder.cpp - A tool for sharder generated source files ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Support/FileUtilities.h"
#include "mlir/Support/LLVM.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/ToolOutputFile.h"

using namespace mlir;
using namespace llvm::clv2;

/// Create a dependency file for `-d` option.
///
/// This functionality is generally only for the benefit of the build system,
/// and is modeled after the same option in TableGen.
static LogicalResult createDependencyFile(StringRef outputFilename,
                                          StringRef dependencyFile) {
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

  outputFile->os() << outputFilename << ":\n";
  outputFile->keep();
  return success();
}

inline constexpr OptionInfo<unsigned> opShardIndex{"op-shard-index",
                                                   "The current shard index"};

inline constexpr OptionInfo<std::string> inputFilename{"", "<input file>",
                                                       Positional{}, Init{"-"}};

inline constexpr OptionInfo<std::string> outputFilename{
    "o", "Output filename", value_desc("filename"), Init{"-"}};

inline constexpr ListOptionInfo<std::string> includeDirs{
    "I", "Directory of include files", value_desc("directory"), PrefixFormat};

inline constexpr OptionInfo<std::string> dependencyFilename{
    "d", "Dependency filename", value_desc("filename"), Init{""}};

inline constexpr OptionInfo<bool> writeIfChanged{
    "write-if-changed", "Only write to the output file if it changed"};

// `ResetCommandLineParser` at the above unregistered the "D" option
// of `llvm-tblgen`, which caused `TestOps.cpp` to fail due to
// "Unknnown command line argument '-D...`" when a macros name is
// present. The following is a workaround to re-register it again.
inline constexpr ListOptionInfo<std::string> macroNames{
    "D", "Name of the macro to be defined -- ignored by mlir-src-sharder",
    value_desc("macro name"), PrefixFormat};

static constexpr OptionsRegistry<&opShardIndex, &inputFilename, &outputFilename,
                                 &includeDirs, &dependencyFilename,
                                 &writeIfChanged, &macroNames>
    SharderReg;

int main(int argc, char **argv) {
  llvm::InitLLVM y(argc, argv);
  llvm::clv2::OptionParser P;
  P.add<&SharderReg>();
  auto OptsCtx = P.parse(argc, argv);
  auto *Opts = OptsCtx->getViewPtr<&SharderReg>();

  // Open the input file.
  std::string errorMessage;
  std::unique_ptr<llvm::MemoryBuffer> inputFile =
      openInputFile(Opts->get<&inputFilename>(), &errorMessage);
  if (!inputFile) {
    llvm::errs() << errorMessage << "\n";
    return 1;
  }

  // Write the output to a buffer.
  std::string outputStr;
  llvm::raw_string_ostream os(outputStr);
  os << "#define GET_OP_DEFS_" << Opts->get<&opShardIndex>() << "\n"
     << inputFile->getBuffer();

  // Determine whether we need to write the output file.
  bool shouldWriteOutput = true;
  if (Opts->get<&writeIfChanged>()) {
    // Only update the real output file if there are any differences. This
    // prevents recompilation of all the files depending on it if there aren't
    // any.
    if (auto existingOrErr = llvm::MemoryBuffer::getFile(
            Opts->get<&outputFilename>(), /*IsText=*/true))
      if (std::move(existingOrErr.get())->getBuffer() == outputStr)
        shouldWriteOutput = false;
  }

  // Populate the output file if necessary.
  if (shouldWriteOutput) {
    std::unique_ptr<llvm::ToolOutputFile> outputFile =
        openOutputFile(Opts->get<&outputFilename>(), &errorMessage);
    if (!outputFile) {
      llvm::errs() << errorMessage << "\n";
      return 1;
    }
    outputFile->os() << os.str();
    outputFile->keep();
  }

  // Always write the depfile, even if the main output hasn't changed. If it's
  // missing, Ninja considers the output dirty.
  if (!Opts->get<&dependencyFilename>().empty())
    if (failed(createDependencyFile(Opts->get<&outputFilename>(),
                                    Opts->get<&dependencyFilename>())))
      return 1;

  return 0;
}
