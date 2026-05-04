//===- gcov.cpp - GCOV compatible LLVM coverage tool ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// llvm-cov is a command line tools to analyze and report coverage information.
//
//===----------------------------------------------------------------------===//

#include "llvm/ProfileData/GCOV.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include <system_error>
using namespace llvm;

inline constexpr clv2::ListOptionInfo<std::string> GcovSourceFilesOpt{
    "", "SOURCEFILE", clv2::Positional{}, clv2::OneOrMore};

inline constexpr clv2::OptionInfo<bool> GcovAllBlocksOpt{
    "a", "Display all basic blocks", clv2::Grouping, clv2::Init{false}};
inline constexpr clv2::AliasInfo GcovAllBlocksAlias{"all-blocks", "a"};

inline constexpr clv2::OptionInfo<bool> GcovBranchProbOpt{
    "b", "Display branch probabilities", clv2::Grouping, clv2::Init{false}};
inline constexpr clv2::AliasInfo GcovBranchProbAlias{"branch-probabilities",
                                                     "b"};

inline constexpr clv2::OptionInfo<bool> GcovBranchCountOpt{
    "c", "Display branch counts instead of percentages (requires -b)",
    clv2::Grouping, clv2::Init{false}};
inline constexpr clv2::AliasInfo GcovBranchCountAlias{"branch-counts", "c"};

inline constexpr clv2::OptionInfo<bool> GcovLongNamesOpt{
    "l", "Prefix filenames with the main file", clv2::Grouping,
    clv2::Init{false}};
inline constexpr clv2::AliasInfo GcovLongNamesAlias{"long-file-names", "l"};

inline constexpr clv2::OptionInfo<bool> GcovFuncSummaryOpt{
    "f", "Show coverage for each function", clv2::Grouping, clv2::Init{false}};
inline constexpr clv2::AliasInfo GcovFuncSummaryAlias{"function-summaries",
                                                      "f"};

inline constexpr clv2::OptionInfo<bool> GcovIntermediateOpt{
    "intermediate-format", "Output .gcov in intermediate text format",
    clv2::Init{false}};
inline constexpr clv2::OptionInfo<bool> GcovIntermediateShortOpt{
    "i", "Alias for --intermediate-format", clv2::Grouping};

inline constexpr clv2::OptionInfo<bool> GcovDemangleOpt{
    "demangled-names", "Demangle function names", clv2::Init{false}};
inline constexpr clv2::OptionInfo<bool> GcovDemangleShortOpt{
    "m", "Alias for --demangled-names", clv2::Grouping};

inline constexpr clv2::OptionInfo<bool> GcovNoOutputOpt{
    "n", "Do not output any .gcov files", clv2::Grouping, clv2::Init{false}};
inline constexpr clv2::AliasInfo GcovNoOutputAlias{"no-output", "n"};

inline constexpr clv2::OptionInfo<std::string> GcovObjectDirOpt{
    "o", "Find objects in DIR or based on FILE's path",
    clv2::value_desc("DIR|FILE"), clv2::Init{""}};
inline constexpr clv2::AliasInfo GcovObjectDirAliasA{"object-directory", "o"};
inline constexpr clv2::AliasInfo GcovObjectDirAliasB{"object-file", "o"};

inline constexpr clv2::OptionInfo<bool> GcovPreservePathsOpt{
    "p", "Preserve path components", clv2::Grouping, clv2::Init{false}};
inline constexpr clv2::AliasInfo GcovPreservePathsAlias{"preserve-paths", "p"};

inline constexpr clv2::OptionInfo<bool> GcovRelativeOnlyOpt{
    "r",
    "Only dump files with relative paths or absolute paths with the "
    "prefix specified by -s",
    clv2::Grouping};
inline constexpr clv2::AliasInfo GcovRelativeOnlyAlias{"relative-only", "r"};

inline constexpr clv2::OptionInfo<std::string> GcovSourcePrefixOpt{
    "s", "Source prefix to elide"};
inline constexpr clv2::AliasInfo GcovSourcePrefixAlias{"source-prefix", "s"};

inline constexpr clv2::OptionInfo<bool> GcovUseStdoutOpt{
    "t", "Print to stdout", clv2::Grouping, clv2::Init{false}};
inline constexpr clv2::AliasInfo GcovUseStdoutAlias{"stdout", "t"};

inline constexpr clv2::OptionInfo<bool> GcovUncondBranchOpt{
    "u", "Display unconditional branch info (requires -b)", clv2::Grouping,
    clv2::Init{false}};
inline constexpr clv2::AliasInfo GcovUncondBranchAlias{"unconditional-branches",
                                                       "u"};

inline constexpr clv2::OptionInfo<bool> GcovHashFilenamesOpt{
    "x", "Hash long pathnames", clv2::Grouping, clv2::Init{false}};
inline constexpr clv2::AliasInfo GcovHashFilenamesAlias{"hash-filenames", "x"};

inline constexpr clv2::OptionCategory GcovDebugCat{
    "Internal and debugging options"};
inline constexpr clv2::OptionInfo<bool> GcovDumpOpt{
    "dump", "Dump the gcov file to stderr", clv2::Init{false},
    clv2::cat(GcovDebugCat)};
inline constexpr clv2::OptionInfo<std::string> GcovInputGCNOOpt{
    "gcno", "Override inferred gcno file", clv2::Init{""},
    clv2::cat(GcovDebugCat)};
inline constexpr clv2::OptionInfo<std::string> GcovInputGCDAOpt{
    "gcda", "Override inferred gcda file", clv2::Init{""},
    clv2::cat(GcovDebugCat)};

static constexpr clv2::OptionsRegistry<
    &GcovSourceFilesOpt, &GcovAllBlocksOpt, &GcovAllBlocksAlias,
    &GcovBranchProbOpt, &GcovBranchProbAlias, &GcovBranchCountOpt,
    &GcovBranchCountAlias, &GcovLongNamesOpt, &GcovLongNamesAlias,
    &GcovFuncSummaryOpt, &GcovFuncSummaryAlias, &GcovIntermediateOpt,
    &GcovIntermediateShortOpt, &GcovDemangleOpt, &GcovDemangleShortOpt,
    &GcovNoOutputOpt, &GcovNoOutputAlias, &GcovObjectDirOpt,
    &GcovObjectDirAliasA, &GcovObjectDirAliasB, &GcovPreservePathsOpt,
    &GcovPreservePathsAlias, &GcovRelativeOnlyOpt, &GcovRelativeOnlyAlias,
    &GcovSourcePrefixOpt, &GcovSourcePrefixAlias, &GcovUseStdoutOpt,
    &GcovUseStdoutAlias, &GcovUncondBranchOpt, &GcovUncondBranchAlias,
    &GcovHashFilenamesOpt, &GcovHashFilenamesAlias, &GcovDumpOpt,
    &GcovInputGCDAOpt, &GcovInputGCNOOpt>
    GcovToolReg;

static void reportCoverage(StringRef SourceFile, StringRef ObjectDir,
                           const std::string &InputGCNO,
                           const std::string &InputGCDA, bool DumpGCOV,
                           const GCOV::Options &Options) {
  SmallString<128> CoverageFileStem(ObjectDir);
  if (CoverageFileStem.empty()) {
    // If no directory was specified with -o, look next to the source file.
    CoverageFileStem = sys::path::parent_path(SourceFile);
    sys::path::append(CoverageFileStem, sys::path::stem(SourceFile));
  } else if (sys::fs::is_directory(ObjectDir))
    // A directory name was given. Use it and the source file name.
    sys::path::append(CoverageFileStem, sys::path::stem(SourceFile));
  else
    // A file was given. Ignore the source file and look next to this file.
    sys::path::replace_extension(CoverageFileStem, "");

  std::string GCNO =
      InputGCNO.empty() ? std::string(CoverageFileStem) + ".gcno" : InputGCNO;
  std::string GCDA =
      InputGCDA.empty() ? std::string(CoverageFileStem) + ".gcda" : InputGCDA;
  GCOVFile GF;

  // Open .gcda and .gcda without requiring a NUL terminator. The concurrent
  // modification may nullify the NUL terminator condition.
  ErrorOr<std::unique_ptr<MemoryBuffer>> GCNO_Buff =
      MemoryBuffer::getFileOrSTDIN(GCNO, /*IsText=*/false,
                                   /*RequiresNullTerminator=*/false);
  if (std::error_code EC = GCNO_Buff.getError()) {
    errs() << GCNO << ": " << EC.message() << "\n";
    return;
  }
  GCOVBuffer GCNO_GB(GCNO_Buff.get().get());
  if (!GF.readGCNO(GCNO_GB)) {
    errs() << "Invalid .gcno File!\n";
    return;
  }

  ErrorOr<std::unique_ptr<MemoryBuffer>> GCDA_Buff =
      MemoryBuffer::getFileOrSTDIN(GCDA, /*IsText=*/false,
                                   /*RequiresNullTerminator=*/false);
  if (std::error_code EC = GCDA_Buff.getError()) {
    if (EC != errc::no_such_file_or_directory) {
      errs() << GCDA << ": " << EC.message() << "\n";
      return;
    }
    // Clear the filename to make it clear we didn't read anything.
    GCDA = "-";
  } else {
    GCOVBuffer gcda_buf(GCDA_Buff.get().get());
    if (!gcda_buf.readGCDAFormat())
      errs() << GCDA << ":not a gcov data file\n";
    else if (!GF.readGCDA(gcda_buf))
      errs() << "Invalid .gcda File!\n";
  }

  if (DumpGCOV)
    GF.print(errs());

  gcovOneInput(Options, SourceFile, GCNO, GCDA, GF);
}

int gcovMain(int argc, const char *argv[]) {
  clv2::OptionParser P;
  P.add<&GcovToolReg>();
  RegisterAllLLVMOptions(P);
  auto OptsCtx = P.parse(argc, argv, "LLVM code coverage tool\n");
  auto *S = OptsCtx->getViewPtr<&GcovToolReg>();

  bool Intermediate =
      S->get<&GcovIntermediateOpt>() || S->get<&GcovIntermediateShortOpt>();
  bool Demangle = S->get<&GcovDemangleOpt>() || S->get<&GcovDemangleShortOpt>();

  GCOV::Options Options(
      S->get<&GcovAllBlocksOpt>(), S->get<&GcovBranchProbOpt>(),
      S->get<&GcovBranchCountOpt>(), S->get<&GcovFuncSummaryOpt>(),
      S->get<&GcovPreservePathsOpt>(), S->get<&GcovUncondBranchOpt>(),
      Intermediate, S->get<&GcovLongNamesOpt>(), Demangle,
      S->get<&GcovNoOutputOpt>(), S->get<&GcovRelativeOnlyOpt>(),
      S->get<&GcovUseStdoutOpt>(), S->get<&GcovHashFilenamesOpt>(),
      S->get<&GcovSourcePrefixOpt>());

  for (const auto &SourceFile : S->get<&GcovSourceFilesOpt>())
    reportCoverage(SourceFile, S->get<&GcovObjectDirOpt>(),
                   S->get<&GcovInputGCNOOpt>(), S->get<&GcovInputGCDAOpt>(),
                   S->get<&GcovDumpOpt>(), Options);
  return 0;
}
