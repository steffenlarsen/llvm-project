//===-- llvm-debuginfo-analyzer.cpp - LLVM Debug info analysis utility ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program is a utility that displays the logical view for the debug
// information.
//
//===----------------------------------------------------------------------===//

#include "Options.h"
#include "llvm/DebugInfo/LogicalView/Core/LVOptions.h"
#include "llvm/DebugInfo/LogicalView/LVReaderHandler.h"
#include "llvm/Support/COM.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"

using namespace llvm;
using namespace logicalview;
using namespace cmdline;

/// Create formatted StringError object.
static StringRef ToolName = "llvm-debuginfo-analyzer";
template <typename... Ts>
static void error(std::error_code EC, char const *Fmt, const Ts &...Vals) {
  if (!EC)
    return;
  std::string Buffer;
  raw_string_ostream Stream(Buffer);
  Stream << format(Fmt, Vals...);
  WithColor::error(errs(), ToolName) << Buffer << "\n";
  exit(1);
}

static void error(Error EC) {
  if (!EC)
    return;
  handleAllErrors(std::move(EC), [&](const ErrorInfoBase &EI) {
    errs() << "\n";
    WithColor::error(errs(), ToolName) << EI.message() << ".\n";
    exit(1);
  });
}

/// If the input path is a .dSYM bundle (as created by the dsymutil tool),
/// replace it with individual entries for each of the object files inside the
/// bundle otherwise return the input path.
static std::vector<std::string> expandBundle(const std::string &InputPath) {
  std::vector<std::string> BundlePaths;
  SmallString<256> BundlePath(InputPath);
  // Normalize input path. This is necessary to accept `bundle.dSYM/`.
  sys::path::remove_dots(BundlePath);
  // Manually open up the bundle to avoid introducing additional dependencies.
  if (sys::fs::is_directory(BundlePath) &&
      sys::path::extension(BundlePath) == ".dSYM") {
    std::error_code EC;
    sys::path::append(BundlePath, "Contents", "Resources", "DWARF");
    for (sys::fs::directory_iterator Dir(BundlePath, EC), DirEnd;
         Dir != DirEnd && !EC; Dir.increment(EC)) {
      const std::string &Path = Dir->path();
      sys::fs::file_status Status;
      EC = sys::fs::status(Path, Status);
      error(EC, "%s", Path.c_str());
      switch (Status.type()) {
      case sys::fs::file_type::regular_file:
      case sys::fs::file_type::symlink_file:
      case sys::fs::file_type::type_unknown:
        BundlePaths.push_back(Path);
        break;
      default: /*ignore*/;
      }
    }
  }
  if (BundlePaths.empty())
    BundlePaths.push_back(InputPath);
  return BundlePaths;
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  // Initialize targets and assembly printers/parsers.
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  InitializeAllDisassemblers();

  llvm::sys::InitializeCOMRAII COM(llvm::sys::COMThreadingMode::MultiThreaded);

  static constexpr clv2::OptionsRegistry<
      &InputFilenamesOpt, &OutputFilenameOpt, &AttributeOptionsOpt,
      &CompareContextOpt, &CompareElementsOpt, &OutputFolderOpt,
      &OutputLevelOpt, &OutputOptionsOpt, &OutputSortOpt, &PrintOptionsOpt,
      &ReportOptionsOpt, &SelectIgnoreCaseOpt, &SelectUseRegexOpt,
      &SelectPatternsOpt, &SelectOffsetsOpt, &SelectElementsOpt,
      &SelectLinesOpt, &SelectScopesOpt, &SelectSymbolsOpt, &SelectTypesOpt,
      &WarningOptionsOpt, &InternalOptionsOpt>
      DebugInfoToolReg;
  clv2::OptionParser P;
  P.add<&DebugInfoToolReg>();
  RegisterAllLLVMOptions(P);
  P.hideUnrelatedOptions({&AttributeCategory, &CompareCategory, &OutputCategory,
                          &PrintCategory, &ReportCategory, &SelectCategory,
                          &WarningCategory, &InternalCategory});
  P.setExtraHelp("\nPass @FILE as argument to read options from FILE.\n");
  auto OptsCtx = P.parse(argc, argv,
                         "Printing a logical representation of low-level "
                         "debug information.\n");
  auto *Opts = OptsCtx->getViewPtr<&DebugInfoToolReg>();

  auto InputFilenames = Opts->get<&InputFilenamesOpt>();
  auto OutputFilename = Opts->get<&OutputFilenameOpt>();

  // Propagate parsed options into ReaderOptions.
  ReaderOptions.Compare.Context = Opts->get<&CompareContextOpt>();
  ReaderOptions.Output.Folder = Opts->get<&OutputFolderOpt>();
  ReaderOptions.Output.Level = Opts->get<&OutputLevelOpt>();
  ReaderOptions.Output.SortMode = Opts->get<&OutputSortOpt>();
  ReaderOptions.Select.IgnoreCase = Opts->get<&SelectIgnoreCaseOpt>();
  ReaderOptions.Select.UseRegex = Opts->get<&SelectUseRegexOpt>();

  // Propagate pattern lists.
  auto UpdatePattern = [&](auto &List, auto &Set, bool IgnoreCase,
                           bool UseRegex) {
    if (!List.empty())
      for (std::string &Pattern : List)
        Set.insert((IgnoreCase && !UseRegex) ? StringRef(Pattern).lower()
                                             : Pattern);
  };

  auto SelectPatterns = Opts->get<&SelectPatternsOpt>();
  UpdatePattern(SelectPatterns, ReaderOptions.Select.Generic,
                ReaderOptions.Select.IgnoreCase, ReaderOptions.Select.UseRegex);

  auto UpdateSet = [&](auto &List, auto &Set) {
    std::copy(List.begin(), List.end(), std::inserter(Set, Set.begin()));
  };

  auto AttributeOptions = Opts->get<&AttributeOptionsOpt>();
  auto PrintOptions = Opts->get<&PrintOptionsOpt>();
  auto OutputOptions = Opts->get<&OutputOptionsOpt>();
  auto ReportOptions = Opts->get<&ReportOptionsOpt>();
  auto WarningOptions = Opts->get<&WarningOptionsOpt>();
  auto InternalOptions = Opts->get<&InternalOptionsOpt>();
  auto SelectElements = Opts->get<&SelectElementsOpt>();
  auto SelectLines = Opts->get<&SelectLinesOpt>();
  auto SelectScopes = Opts->get<&SelectScopesOpt>();
  auto SelectSymbols = Opts->get<&SelectSymbolsOpt>();
  auto SelectTypes = Opts->get<&SelectTypesOpt>();
  auto SelectOffsets = Opts->get<&SelectOffsetsOpt>();
  auto CompareElements = Opts->get<&CompareElementsOpt>();

  UpdateSet(AttributeOptions, ReaderOptions.Attribute.Kinds);
  UpdateSet(PrintOptions, ReaderOptions.Print.Kinds);
  UpdateSet(OutputOptions, ReaderOptions.Output.Kinds);
  UpdateSet(ReportOptions, ReaderOptions.Report.Kinds);
  UpdateSet(WarningOptions, ReaderOptions.Warning.Kinds);
  UpdateSet(InternalOptions, ReaderOptions.Internal.Kinds);

  UpdateSet(SelectElements, ReaderOptions.Select.Elements);
  UpdateSet(SelectLines, ReaderOptions.Select.Lines);
  UpdateSet(SelectScopes, ReaderOptions.Select.Scopes);
  UpdateSet(SelectSymbols, ReaderOptions.Select.Symbols);
  UpdateSet(SelectTypes, ReaderOptions.Select.Types);
  UpdateSet(SelectOffsets, ReaderOptions.Select.Offsets);
  UpdateSet(CompareElements, ReaderOptions.Compare.Elements);

  ReaderOptions.resolveDependencies();

  std::error_code EC;
  ToolOutputFile OutputFile(OutputFilename, EC, sys::fs::OF_None);
  error(EC, "Unable to open output file %s", OutputFilename.c_str());
  // Don't remove output file if we exit with an error.
  OutputFile.keep();

  // Defaults to a.out if no filenames specified.
  if (InputFilenames.empty())
    InputFilenames.push_back("a.out");

  // Expand any .dSYM bundles to the individual object files contained therein.
  std::vector<std::string> Objects;
  for (const std::string &Filename : InputFilenames) {
    std::vector<std::string> Objs = expandBundle(Filename);
    llvm::append_range(Objects, Objs);
  }

  ScopedPrinter W(OutputFile.os());
  LVReaderHandler ReaderHandler(Objects, W, ReaderOptions);

  // Print the command line.
  if (options().getInternalCmdline()) {
    raw_ostream &Stream = W.getOStream();
    Stream << "\nCommand line:\n";
    for (int Index = 0; Index < argc; ++Index)
      Stream << "  " << argv[Index] << "\n";
    Stream << "\n";
  }

  // Create readers and perform requested tasks on them.
  if (Error Err = ReaderHandler.process())
    error(std::move(Err));

  return EXIT_SUCCESS;
}
