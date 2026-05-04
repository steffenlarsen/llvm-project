//===- SSAFAnalyzer.cpp - SSAF Analyzer Tool ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the SSAF analyzer tool that runs whole-program
//  analyses over an LUSummary and writes the resulting WPASuite to an
//  output file.
//
//===----------------------------------------------------------------------===//

#include "clang/ScalableStaticAnalysis/Core/EntityLinker/LUSummary.h"
#include "clang/ScalableStaticAnalysis/Core/WholeProgramAnalysis/AnalysisDriver.h"
#include "clang/ScalableStaticAnalysis/Core/WholeProgramAnalysis/AnalysisName.h"
#include "clang/ScalableStaticAnalysis/SSAFForceLinker.h" // IWYU pragma: keep
#include "clang/ScalableStaticAnalysis/Tool/Utils.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/InitLLVM.h"
#include <memory>
#include <string>

using namespace llvm;
using namespace clang::ssaf;

namespace {

//===----------------------------------------------------------------------===//
// Command-Line Options
//===----------------------------------------------------------------------===//

clv2::OptionCategory SsafAnalyzerCategory("clang-ssaf-analyzer options");

inline constexpr clv2::OptionInfo<std::string> SAInputPathOpt{
    "", "<input file>", clv2::Positional{}, clv2::Required,
    clv2::cat(SsafAnalyzerCategory)};

inline constexpr clv2::OptionInfo<std::string> SAOutputPathOpt{
    "o", "Output file path", clv2::Required, clv2::value_desc("path"),
    clv2::cat(SsafAnalyzerCategory)};

inline constexpr clv2::ListOptionInfo<std::string> SAAnalysisNamesOpt{
    "a", "Analysis name to run", clv2::value_desc("name"),
    clv2::cat(SsafAnalyzerCategory)};

inline constexpr clv2::AliasInfo SAAnalysisNamesAlias{"analysis", "a",
                                                      "Alias for -a"};

inline constexpr clv2::ListOptionInfo<std::string> SALoadPluginsOpt{
    "load", "Load a plugin shared library", clv2::value_desc("path"),
    clv2::cat(SsafAnalyzerCategory)};

inline constexpr clv2::AliasInfo SALoadPluginsAlias{"l", "load",
                                                    "Alias for --load"};

inline constexpr clv2::OptionsRegistry<
    &SAInputPathOpt, &SAOutputPathOpt, &SAAnalysisNamesOpt,
    &SAAnalysisNamesAlias, &SALoadPluginsOpt, &SALoadPluginsAlias>
    SsafAnalyzerOptsReg;

} // namespace

struct SsafAnalyzerOptions {
  std::string InputPath;
  std::string OutputPath;
  std::vector<std::string> AnalysisNames;
  std::vector<std::string> LoadPlugins;
};

static void
applySsafAnalyzerOpts(const decltype(SsafAnalyzerOptsReg)::ParsedOptionsT &Opts,
                      SsafAnalyzerOptions &ToolOpts) {
  ToolOpts.InputPath = Opts.get<&SAInputPathOpt>();
  ToolOpts.OutputPath = Opts.get<&SAOutputPathOpt>();
  ToolOpts.AnalysisNames = Opts.get<&SAAnalysisNamesOpt>();
  ToolOpts.LoadPlugins = Opts.get<&SALoadPluginsOpt>();
}

namespace {

//===----------------------------------------------------------------------===//
// Input Validation
//===----------------------------------------------------------------------===//

struct AnalyzerInput {
  FormatFile InputFile;
  FormatFile OutputFile;
  llvm::SmallVector<AnalysisName> Names;
};

AnalyzerInput validate(const SsafAnalyzerOptions &ToolOpts) {
  AnalyzerInput AI;

  // Validate the input path.
  AI.InputFile = FormatFile::fromInputPath(ToolOpts.InputPath);

  // Validate the output path.
  AI.OutputFile = FormatFile::fromOutputPath(ToolOpts.OutputPath);

  // Build and validate analysis names.
  for (const auto &Name : ToolOpts.AnalysisNames) {
    if (Name.empty()) {
      fail("analysis name must not be empty");
    }
    AI.Names.push_back(AnalysisName(Name));
  }

  return AI;
}

//===----------------------------------------------------------------------===//
// Analysis Pipeline
//===----------------------------------------------------------------------===//

void analyze(const AnalyzerInput &AI) {
  // Read the LUSummary.
  auto ExpectedLU = AI.InputFile.Format->readLUSummary(AI.InputFile.Path);
  if (!ExpectedLU) {
    fail(ExpectedLU.takeError());
  }

  // Run analyses. If specific names were given, run only those;
  // otherwise run all registered analyses.
  AnalysisDriver Driver(std::make_unique<LUSummary>(std::move(*ExpectedLU)));
  auto ExpectedSuite =
      AI.Names.empty() ? std::move(Driver).run() : Driver.run(AI.Names);
  if (!ExpectedSuite) {
    fail(ExpectedSuite.takeError());
  }

  // Write the WPASuite.
  if (auto Err = AI.OutputFile.Format->writeWPASuite(*ExpectedSuite,
                                                     AI.OutputFile.Path)) {
    fail(std::move(Err));
  }
}

} // namespace

//===----------------------------------------------------------------------===//
// Driver
//===----------------------------------------------------------------------===//

int main(int argc, const char **argv) {
  llvm::StringRef ToolHeading = "SSAF Analyzer";

  InitLLVM X(argc, argv);
  auto OptsCtx =
      initTool(argc, argv, "0.1", SsafAnalyzerCategory, ToolHeading,
               [](clv2::OptionParser &P) { P.add<&SsafAnalyzerOptsReg>(); });

  SsafAnalyzerOptions ToolOpts;
  applySsafAnalyzerOpts(*OptsCtx->getViewPtr<&SsafAnalyzerOptsReg>(), ToolOpts);

  loadPlugins(ToolOpts.LoadPlugins);

  AnalyzerInput AI = validate(ToolOpts);
  analyze(AI);

  return 0;
}
