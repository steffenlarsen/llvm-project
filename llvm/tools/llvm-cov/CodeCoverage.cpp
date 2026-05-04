//===- CodeCoverage.cpp - Coverage tool based on profiling instrumentation-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The 'CodeCoverageTool' class implements a command line tool to analyze and
// report coverage information using the profiling instrumentation and code
// coverage mapping.
//
//===----------------------------------------------------------------------===//

#include "CoverageExporterJson.h"
#include "CoverageExporterLcov.h"
#include "CoverageFilters.h"
#include "CoverageReport.h"
#include "CoverageSummaryInfo.h"
#include "CoverageViewOptions.h"
#include "RenderingSupport.h"
#include "SourceCoverageView.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Debuginfod/BuildIDFetcher.h"
#include "llvm/Debuginfod/Debuginfod.h"
#include "llvm/HTTP/HTTPClient.h"
#include "llvm/Object/BuildID.h"
#include "llvm/ProfileData/Coverage/CoverageMapping.h"
#include "llvm/ProfileData/InstrProfReader.h"
#include "llvm/Support/BoolOrDefault.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/SpecialCaseList.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Triple.h"

#include <functional>
#include <map>
#include <optional>
#include <system_error>

using namespace llvm;
using namespace coverage;

void exportCoverageDataToJson(const coverage::CoverageMapping &CoverageMapping,
                              const CoverageViewOptions &Options,
                              raw_ostream &OS);

// ---------------------------------------------------------------------------
// File-scope clv2 option descriptors
// ---------------------------------------------------------------------------

// Common options (from run())
inline constexpr clv2::OptionInfo<std::string> CC_CovFilenameOpt{
    "", "Covered executable or object file.", clv2::Positional{}};

inline constexpr clv2::ListOptionInfo<std::string> CC_CovFilenamesOpt{
    "object", "Coverage executable or object file"};

inline constexpr clv2::OptionInfo<bool> CC_DebugDumpObjsOpt{
    "dump-collected-objects", "Show the collected coverage object files",
    clv2::Hidden};

inline constexpr clv2::ListOptionInfo<std::string> CC_InputSourceFilesOpt{
    "sources", "<Source files>", clv2::Positional{}};

inline constexpr clv2::OptionInfo<bool> CC_DebugDumpPathsOpt{
    "dump-collected-paths", "Show the collected paths to source files",
    clv2::Hidden};

inline constexpr clv2::OptionInfo<std::string> CC_PGOFilenameOpt{
    "instr-profile",
    "File with the profile data obtained after an instrumented run",
    clv2::Init{""}};

inline constexpr clv2::OptionInfo<bool> CC_EmptyProfileOpt{
    "empty-profile",
    "Use a synthetic profile with no data to generate baseline coverage"};

inline constexpr clv2::ListOptionInfo<std::string> CC_ArchesOpt{
    "arch", "architectures of the coverage mapping binaries"};

inline constexpr clv2::OptionInfo<bool> CC_DebugDumpOpt{
    "dump", "Show internal debug dump"};

inline constexpr clv2::ListOptionInfo<std::string> CC_DebugFileDirOpt{
    "debug-file-directory",
    "Directories to search for object files by build ID"};

inline constexpr clv2::OptionInfo<bool> CC_DebuginfodOpt{
    "debuginfod", "Use debuginfod to look up object files from profile"};

constexpr clv2::EnumVal<CoverageViewOptions::OutputFormat> CC_FormatVals[] = {
    {"text", CoverageViewOptions::OutputFormat::Text, "Text output"},
    {"html", CoverageViewOptions::OutputFormat::HTML, "HTML output"},
    {"lcov", CoverageViewOptions::OutputFormat::Lcov, "lcov tracefile output"},
};
inline constexpr auto CC_FormatOpt =
    clv2::makeEnumOption<CoverageViewOptions::OutputFormat>(
        "format", "Output format for line-based coverage reports",
        CC_FormatVals, clv2::Init{CoverageViewOptions::OutputFormat::Text});

inline constexpr clv2::ListOptionInfo<std::string> CC_PathRemapsOpt{
    "path-equivalence",
    "<from>,<to> Map coverage data paths to local source file paths"};

// Filtering category and options
inline constexpr clv2::OptionCategory CC_FilteringCat{
    "Function filtering options"};

inline constexpr clv2::ListOptionInfo<std::string> CC_NameFiltersOpt{
    "name", "Show code coverage only for functions with the given name",
    clv2::cat(CC_FilteringCat)};

inline constexpr clv2::ListOptionInfo<std::string> CC_NameFilterFilesOpt{
    "name-allowlist",
    "Show code coverage only for functions listed in the given file",
    clv2::cat(CC_FilteringCat)};

inline constexpr clv2::ListOptionInfo<std::string> CC_NameRegexFiltersOpt{
    "name-regex",
    "Show code coverage only for functions that match the given regular "
    "expression",
    clv2::cat(CC_FilteringCat)};

inline constexpr clv2::ListOptionInfo<std::string> CC_IgnoreFilenameRegexOpt{
    "ignore-filename-regex",
    "Skip source code files with file paths that match the given regular "
    "expression",
    clv2::cat(CC_FilteringCat)};

inline constexpr clv2::ListOptionInfo<std::string> CC_IncludeFilenameRegexOpt{
    "include-filename-regex",
    "Only include source code files with file paths that match the given "
    "regular expression",
    clv2::cat(CC_FilteringCat)};

inline constexpr clv2::OptionInfo<double> CC_RegionCoverageLtOpt{
    "region-coverage-lt",
    "Show code coverage only for functions with region coverage less than the "
    "given threshold",
    clv2::cat(CC_FilteringCat)};

inline constexpr clv2::OptionInfo<double> CC_RegionCoverageGtOpt{
    "region-coverage-gt",
    "Show code coverage only for functions with region coverage greater than "
    "the given threshold",
    clv2::cat(CC_FilteringCat)};

inline constexpr clv2::OptionInfo<double> CC_LineCoverageLtOpt{
    "line-coverage-lt",
    "Show code coverage only for functions with line coverage less than the "
    "given threshold",
    clv2::cat(CC_FilteringCat)};

inline constexpr clv2::OptionInfo<double> CC_LineCoverageGtOpt{
    "line-coverage-gt",
    "Show code coverage only for functions with line coverage greater than the "
    "given threshold",
    clv2::cat(CC_FilteringCat)};

inline constexpr clv2::OptionInfo<bool> CC_UseColorOpt{
    "use-color", "Emit colored output (default=autodetect)"};

inline constexpr clv2::ListOptionInfo<std::string> CC_DemanglerOpt{
    "Xdemangler", "<demangler-path>|<demangler-option>"};

inline constexpr clv2::OptionInfo<bool> CC_RegionSummaryOpt{
    "show-region-summary", "Show region statistics in summary table",
    clv2::Init{true}};

inline constexpr clv2::OptionInfo<bool> CC_FunctionSummaryOpt{
    "show-function-summary", "Show function statistics in summary table",
    clv2::Init{true}};

inline constexpr clv2::OptionInfo<bool> CC_BranchSummaryOpt{
    "show-branch-summary", "Show branch condition statistics in summary table",
    clv2::Init{true}};

inline constexpr clv2::OptionInfo<bool> CC_MCDCSummaryOpt{
    "show-mcdc-summary", "Show MCDC statistics in summary table"};

inline constexpr clv2::OptionInfo<bool> CC_InstantiationSummaryOpt{
    "show-instantiation-summary",
    "Show instantiation statistics in summary table"};

inline constexpr clv2::OptionInfo<bool> CC_SummaryOnlyOpt{
    "summary-only", "Export only summary information for each source file"};

inline constexpr clv2::OptionInfo<unsigned> CC_NumThreadsOpt{
    "num-threads", "Number of merge threads to use (default: autodetect)",
    clv2::Init{0u}};

inline constexpr clv2::AliasInfo CC_NumThreadsAlias{"j", "num-threads"};

inline constexpr clv2::OptionInfo<std::string> CC_CompilationDirOpt{
    "compilation-dir",
    "Directory used as a base for relative coverage mapping paths",
    clv2::Init{""}};

inline constexpr clv2::OptionInfo<bool> CC_CheckBinaryIDsOpt{
    "check-binary-ids",
    "Fail if an object couldn't be found for a binary ID in the profile"};

// View options (from doShow())
inline constexpr clv2::OptionCategory CC_ViewCat{"Viewing options"};

inline constexpr clv2::OptionInfo<bool> CC_ShowLineCountsOpt{
    "show-line-counts", "Show the execution counts for each line",
    clv2::Init{true}, clv2::cat(CC_ViewCat)};

inline constexpr clv2::OptionInfo<bool> CC_ShowRegionsOpt{
    "show-regions", "Show the execution counts for each region",
    clv2::cat(CC_ViewCat)};

constexpr clv2::EnumVal<CoverageViewOptions::BranchOutputType>
    CC_ShowBranchesVals[] = {
        {"count", CoverageViewOptions::BranchOutputType::Count,
         "Show True/False counts"},
        {"percent", CoverageViewOptions::BranchOutputType::Percent,
         "Show True/False percent"},
};
inline constexpr auto CC_ShowBranchesOpt =
    clv2::makeEnumOption<CoverageViewOptions::BranchOutputType>(
        "show-branches", "Show coverage for branch conditions",
        CC_ShowBranchesVals,
        clv2::Init{CoverageViewOptions::BranchOutputType::Off},
        clv2::cat(CC_ViewCat));

inline constexpr clv2::OptionInfo<bool> CC_ShowMCDCOpt{
    "show-mcdc",
    "Show the MCDC Coverage for each applicable boolean expression",
    clv2::cat(CC_ViewCat)};

inline constexpr clv2::OptionInfo<bool> CC_ShowMCDCNonExecOpt{
    "show-mcdc-non-executed-vectors",
    "Show MC/DC test vectors that were not executed"};

inline constexpr clv2::OptionInfo<bool> CC_ShowBestLineRegionsOpt{
    "show-line-counts-or-regions",
    "Show the execution counts for each line, or the execution counts for "
    "each region on lines that have multiple regions",
    clv2::cat(CC_ViewCat)};

inline constexpr clv2::OptionInfo<bool> CC_ShowExpansionsOpt{
    "show-expansions", "Show expanded source regions", clv2::cat(CC_ViewCat)};

inline constexpr clv2::OptionInfo<bool> CC_ShowInstantiationsOpt{
    "show-instantiations", "Show function instantiations", clv2::Init{true},
    clv2::cat(CC_ViewCat)};

inline constexpr clv2::OptionInfo<bool> CC_ShowDirCoverageOpt{
    "show-directory-coverage", "Show directory coverage",
    clv2::cat(CC_ViewCat)};

inline constexpr clv2::OptionInfo<bool> CC_ShowCreatedTimeOpt{
    "show-created-time", "Show created time for each page.", clv2::Init{true},
    clv2::cat(CC_ViewCat)};

inline constexpr clv2::OptionInfo<std::string> CC_OutputDirOpt{
    "output-dir", "Directory in which coverage information is written out",
    clv2::Init{""}};

inline constexpr clv2::AliasInfo CC_OutputDirAlias{"o", "output-dir"};

inline constexpr clv2::OptionInfo<bool> CC_BinaryCountersOpt{
    "binary-counters",
    "Show binary counters (1/0) in lines and branches instead of integer "
    "execution counts",
    clv2::cat(CC_ViewCat)};

inline constexpr clv2::OptionInfo<unsigned> CC_TabSizeOpt{
    "tab-size",
    "Set tab expansion size for html coverage reports (default = 2)",
    clv2::Init{2u}};

inline constexpr clv2::OptionInfo<std::string> CC_ProjectTitleOpt{
    "project-title", "Set project title for the coverage report"};

inline constexpr clv2::OptionInfo<std::string> CC_CovWatermarkOpt{
    "coverage-watermark",
    "<high>,<low> value indicate thresholds for high and low"
    "coverage watermark"};

// Report options (from doReport())
inline constexpr clv2::OptionInfo<bool> CC_ShowFunctionsOpt{
    "show-functions", "Show coverage summaries for each function",
    clv2::Init{false}};

// Export options (from doExport())
inline constexpr clv2::OptionCategory CC_ExportCat{"Exporting options"};

inline constexpr clv2::OptionInfo<bool> CC_SkipExpansionsOpt{
    "skip-expansions", "Don't export expanded source regions",
    clv2::cat(CC_ExportCat)};

inline constexpr clv2::OptionInfo<bool> CC_SkipFunctionsOpt{
    "skip-functions", "Don't export per-function data",
    clv2::cat(CC_ExportCat)};

inline constexpr clv2::OptionInfo<bool> CC_SkipBranchesOpt{
    "skip-branches", "Don't export branch data (LCOV)",
    clv2::cat(CC_ExportCat)};

inline constexpr clv2::OptionInfo<bool> CC_UnifyInstantiationsOpt{
    "unify-instantiations", "Unify function instantiations", clv2::Init{true},
    clv2::cat(CC_ExportCat)};

// ---------------------------------------------------------------------------
// OptionsRegistry
// ---------------------------------------------------------------------------
static constexpr clv2::OptionsRegistry<
    &CC_CovFilenameOpt, &CC_CovFilenamesOpt, &CC_DebugDumpObjsOpt,
    &CC_InputSourceFilesOpt, &CC_DebugDumpPathsOpt, &CC_PGOFilenameOpt,
    &CC_EmptyProfileOpt, &CC_ArchesOpt, &CC_DebugDumpOpt, &CC_DebugFileDirOpt,
    &CC_DebuginfodOpt, &CC_FormatOpt, &CC_PathRemapsOpt, &CC_NameFiltersOpt,
    &CC_NameFilterFilesOpt, &CC_NameRegexFiltersOpt, &CC_IgnoreFilenameRegexOpt,
    &CC_IncludeFilenameRegexOpt, &CC_RegionCoverageLtOpt,
    &CC_RegionCoverageGtOpt, &CC_LineCoverageLtOpt, &CC_LineCoverageGtOpt,
    &CC_UseColorOpt, &CC_DemanglerOpt, &CC_RegionSummaryOpt,
    &CC_FunctionSummaryOpt, &CC_BranchSummaryOpt, &CC_MCDCSummaryOpt,
    &CC_InstantiationSummaryOpt, &CC_SummaryOnlyOpt, &CC_NumThreadsOpt,
    &CC_NumThreadsAlias, &CC_CompilationDirOpt, &CC_CheckBinaryIDsOpt,
    &CC_ShowLineCountsOpt, &CC_ShowRegionsOpt, &CC_ShowBranchesOpt,
    &CC_ShowMCDCOpt, &CC_ShowMCDCNonExecOpt, &CC_ShowBestLineRegionsOpt,
    &CC_ShowExpansionsOpt, &CC_ShowInstantiationsOpt, &CC_ShowDirCoverageOpt,
    &CC_ShowCreatedTimeOpt, &CC_OutputDirOpt, &CC_OutputDirAlias,
    &CC_BinaryCountersOpt, &CC_TabSizeOpt, &CC_ProjectTitleOpt,
    &CC_CovWatermarkOpt, &CC_ShowFunctionsOpt, &CC_SkipExpansionsOpt,
    &CC_SkipFunctionsOpt, &CC_SkipBranchesOpt, &CC_UnifyInstantiationsOpt>
    CC_ToolReg;

namespace {
/// The implementation of the coverage tool.
class CodeCoverageTool {
public:
  enum Command {
    /// The show command.
    Show,
    /// The report command.
    Report,
    /// The export command.
    Export
  };

  int run(Command Cmd, int argc, const char **argv);

private:
  /// Print the error message to the error output stream.
  void error(const Twine &Message, StringRef Whence = "");

  /// Print the warning message to the error output stream.
  void warning(const Twine &Message, StringRef Whence = "");

  /// Convert \p Path into an absolute path and append it to the list
  /// of collected paths.
  void addCollectedPath(const std::string &Path);

  /// If \p Path is a regular file, collect the path. If it's a
  /// directory, recursively collect all of the paths within the directory.
  void collectPaths(const std::string &Path);

  /// Check if the two given files are the same file.
  bool isEquivalentFile(StringRef FilePath1, StringRef FilePath2);

  /// Retrieve a file status with a cache.
  std::optional<sys::fs::file_status> getFileStatus(StringRef FilePath);

  /// Return a memory buffer for the given source file.
  ErrorOr<const MemoryBuffer &> getSourceFile(StringRef SourceFile);

  /// Create source views for the expansions of the view.
  void attachExpansionSubViews(SourceCoverageView &View,
                               ArrayRef<ExpansionRecord> Expansions,
                               const CoverageMapping &Coverage);

  /// Create source views for the branches of the view.
  void attachBranchSubViews(SourceCoverageView &View,
                            ArrayRef<CountedRegion> Branches);

  /// Create source views for the MCDC records.
  void attachMCDCSubViews(SourceCoverageView &View,
                          ArrayRef<MCDCRecord> MCDCRecords);

  /// Create the source view of a particular function.
  std::unique_ptr<SourceCoverageView>
  createFunctionView(const FunctionRecord &Function,
                     const CoverageMapping &Coverage);

  /// Create the main source view of a particular source file.
  std::unique_ptr<SourceCoverageView>
  createSourceFileView(StringRef SourceFile, const CoverageMapping &Coverage);

  /// Load the coverage mapping data. Return nullptr if an error occurred.
  std::unique_ptr<CoverageMapping> load();

  /// Create a mapping from files in the Coverage data to local copies
  /// (path-equivalence).
  void remapPathNames(const CoverageMapping &Coverage);

  /// Remove input source files which aren't mapped by \p Coverage.
  void removeUnmappedInputs(const CoverageMapping &Coverage);

  /// If a demangler is available, demangle all symbol names.
  void demangleSymbols(const CoverageMapping &Coverage);

  /// Write out a source file view to the filesystem.
  void writeSourceFileView(StringRef SourceFile, CoverageMapping *Coverage,
                           CoveragePrinter *Printer, bool ShowFilenames);

  int doShow();

  int doReport(bool ShowFunctionSummaries);

  int doExport();

  std::vector<StringRef> ObjectFilenames;
  CoverageViewOptions ViewOpts;
  CoverageFiltersMatchAll Filters;
  CoverageFilters FilenameFilters;

  /// True if InputSourceFiles are provided.
  bool HadSourceFiles = false;

  /// The path to the indexed profile.
  std::optional<std::string> PGOFilename;

  /// A list of input source files.
  std::vector<std::string> SourceFiles;

  /// In -path-equivalence mode, this maps the absolute paths from the coverage
  /// mapping data to the input source files.
  StringMap<std::string> RemappedFilenames;

  /// The coverage data path to be remapped from, and the source path to be
  /// remapped to, when using -path-equivalence.
  std::optional<std::vector<std::pair<std::string, std::string>>>
      PathRemappings;

  /// File status cache used when finding the same file.
  StringMap<std::optional<sys::fs::file_status>> FileStatusCache;

  /// The architecture the coverage mapping data targets.
  std::vector<StringRef> CoverageArches;

  /// A cache for demangled symbols.
  DemangleCache DC;

  /// A lock which guards printing to stderr.
  std::mutex ErrsLock;

  /// A container for input source file buffers.
  std::mutex LoadedSourceFilesLock;
  std::vector<std::pair<std::string, std::unique_ptr<MemoryBuffer>>>
      LoadedSourceFiles;

  /// Allowlist from -name-allowlist to be used for filtering.
  std::unique_ptr<SpecialCaseList> NameAllowlist;

  std::unique_ptr<object::BuildIDFetcher> BIDFetcher;

  bool CheckBinaryIDs;
};
}

static std::string getErrorString(const Twine &Message, StringRef Whence,
                                  bool Warning) {
  std::string Str = (Warning ? "warning" : "error");
  Str += ": ";
  if (!Whence.empty())
    Str += Whence.str() + ": ";
  Str += Message.str() + "\n";
  return Str;
}

void CodeCoverageTool::error(const Twine &Message, StringRef Whence) {
  std::unique_lock<std::mutex> Guard{ErrsLock};
  ViewOpts.colored_ostream(errs(), raw_ostream::RED)
      << getErrorString(Message, Whence, false);
}

void CodeCoverageTool::warning(const Twine &Message, StringRef Whence) {
  std::unique_lock<std::mutex> Guard{ErrsLock};
  ViewOpts.colored_ostream(errs(), raw_ostream::RED)
      << getErrorString(Message, Whence, true);
}

void CodeCoverageTool::addCollectedPath(const std::string &Path) {
  SmallString<128> EffectivePath(Path);
  if (std::error_code EC = sys::fs::make_absolute(EffectivePath)) {
    error(EC.message(), Path);
    return;
  }
  sys::path::remove_dots(EffectivePath, /*remove_dot_dot=*/true);
  if (!FilenameFilters.matchesFilename(EffectivePath))
    SourceFiles.emplace_back(EffectivePath.str());
  HadSourceFiles = !SourceFiles.empty();
}

void CodeCoverageTool::collectPaths(const std::string &Path) {
  llvm::sys::fs::file_status Status;
  llvm::sys::fs::status(Path, Status);
  if (!llvm::sys::fs::exists(Status)) {
    if (PathRemappings)
      addCollectedPath(Path);
    else
      warning("Source file doesn't exist, proceeded by ignoring it.", Path);
    return;
  }

  if (llvm::sys::fs::is_regular_file(Status)) {
    addCollectedPath(Path);
    return;
  }

  if (llvm::sys::fs::is_directory(Status)) {
    std::error_code EC;
    for (llvm::sys::fs::recursive_directory_iterator F(Path, EC), E;
         F != E; F.increment(EC)) {

      auto Status = F->status();
      if (!Status) {
        warning(Status.getError().message(), F->path());
        continue;
      }

      if (Status->type() == llvm::sys::fs::file_type::regular_file)
        addCollectedPath(F->path());
    }
  }
}

std::optional<sys::fs::file_status>
CodeCoverageTool::getFileStatus(StringRef FilePath) {
  auto It = FileStatusCache.try_emplace(FilePath);
  auto &CachedStatus = It.first->getValue();
  if (!It.second)
    return CachedStatus;

  sys::fs::file_status Status;
  if (!sys::fs::status(FilePath, Status))
    CachedStatus = Status;
  return CachedStatus;
}

bool CodeCoverageTool::isEquivalentFile(StringRef FilePath1,
                                        StringRef FilePath2) {
  auto Status1 = getFileStatus(FilePath1);
  auto Status2 = getFileStatus(FilePath2);
  return Status1 && Status2 && sys::fs::equivalent(*Status1, *Status2);
}

ErrorOr<const MemoryBuffer &>
CodeCoverageTool::getSourceFile(StringRef SourceFile) {
  // If we've remapped filenames, look up the real location for this file.
  std::unique_lock<std::mutex> Guard{LoadedSourceFilesLock};
  if (!RemappedFilenames.empty()) {
    auto Loc = RemappedFilenames.find(SourceFile);
    if (Loc != RemappedFilenames.end())
      SourceFile = Loc->second;
  }
  for (const auto &Files : LoadedSourceFiles)
    if (isEquivalentFile(SourceFile, Files.first))
      return *Files.second;
  auto Buffer = MemoryBuffer::getFile(SourceFile);
  if (auto EC = Buffer.getError()) {
    error(EC.message(), SourceFile);
    return EC;
  }
  LoadedSourceFiles.emplace_back(std::string(SourceFile),
                                 std::move(Buffer.get()));
  return *LoadedSourceFiles.back().second;
}

void CodeCoverageTool::attachExpansionSubViews(
    SourceCoverageView &View, ArrayRef<ExpansionRecord> Expansions,
    const CoverageMapping &Coverage) {
  if (!ViewOpts.ShowExpandedRegions)
    return;
  for (const auto &Expansion : Expansions) {
    auto ExpansionCoverage = Coverage.getCoverageForExpansion(Expansion);
    if (ExpansionCoverage.empty())
      continue;
    auto SourceBuffer = getSourceFile(ExpansionCoverage.getFilename());
    if (!SourceBuffer)
      continue;

    auto SubViewBranches = ExpansionCoverage.getBranches();
    auto SubViewExpansions = ExpansionCoverage.getExpansions();
    auto SubView =
        SourceCoverageView::create(Expansion.Function.Name, SourceBuffer.get(),
                                   ViewOpts, std::move(ExpansionCoverage));
    attachExpansionSubViews(*SubView, SubViewExpansions, Coverage);
    attachBranchSubViews(*SubView, SubViewBranches);
    View.addExpansion(Expansion.Region, std::move(SubView));
  }
}

void CodeCoverageTool::attachBranchSubViews(SourceCoverageView &View,
                                            ArrayRef<CountedRegion> Branches) {
  if (!ViewOpts.ShowBranchCounts && !ViewOpts.ShowBranchPercents)
    return;

  const auto *NextBranch = Branches.begin();
  const auto *EndBranch = Branches.end();

  // Group branches that have the same line number into the same subview.
  while (NextBranch != EndBranch) {
    SmallVector<CountedRegion, 0> ViewBranches;
    unsigned CurrentLine = NextBranch->LineStart;
    while (NextBranch != EndBranch && CurrentLine == NextBranch->LineStart)
      ViewBranches.push_back(*NextBranch++);

    View.addBranch(CurrentLine, std::move(ViewBranches));
  }
}

void CodeCoverageTool::attachMCDCSubViews(SourceCoverageView &View,
                                          ArrayRef<MCDCRecord> MCDCRecords) {
  if (!ViewOpts.ShowMCDC)
    return;

  const auto *NextRecord = MCDCRecords.begin();
  const auto *EndRecord = MCDCRecords.end();

  // Group and process MCDC records that have the same line number into the
  // same subview.
  while (NextRecord != EndRecord) {
    SmallVector<MCDCRecord, 0> ViewMCDCRecords;
    unsigned CurrentLine = NextRecord->getDecisionRegion().LineEnd;
    while (NextRecord != EndRecord &&
           CurrentLine == NextRecord->getDecisionRegion().LineEnd)
      ViewMCDCRecords.push_back(*NextRecord++);

    View.addMCDCRecord(CurrentLine, std::move(ViewMCDCRecords));
  }
}

std::unique_ptr<SourceCoverageView>
CodeCoverageTool::createFunctionView(const FunctionRecord &Function,
                                     const CoverageMapping &Coverage) {
  auto FunctionCoverage = Coverage.getCoverageForFunction(Function);
  if (FunctionCoverage.empty())
    return nullptr;
  auto SourceBuffer = getSourceFile(FunctionCoverage.getFilename());
  if (!SourceBuffer)
    return nullptr;

  auto Branches = FunctionCoverage.getBranches();
  auto Expansions = FunctionCoverage.getExpansions();
  auto MCDCRecords = FunctionCoverage.getMCDCRecords();
  auto View = SourceCoverageView::create(DC.demangle(Function.Name),
                                         SourceBuffer.get(), ViewOpts,
                                         std::move(FunctionCoverage));
  attachExpansionSubViews(*View, Expansions, Coverage);
  attachBranchSubViews(*View, Branches);
  attachMCDCSubViews(*View, MCDCRecords);

  return View;
}

std::unique_ptr<SourceCoverageView>
CodeCoverageTool::createSourceFileView(StringRef SourceFile,
                                       const CoverageMapping &Coverage) {
  auto SourceBuffer = getSourceFile(SourceFile);
  if (!SourceBuffer)
    return nullptr;
  auto FileCoverage = Coverage.getCoverageForFile(SourceFile);
  if (FileCoverage.empty())
    return nullptr;

  auto Branches = FileCoverage.getBranches();
  auto Expansions = FileCoverage.getExpansions();
  auto MCDCRecords = FileCoverage.getMCDCRecords();
  auto View = SourceCoverageView::create(SourceFile, SourceBuffer.get(),
                                         ViewOpts, std::move(FileCoverage));
  attachExpansionSubViews(*View, Expansions, Coverage);
  attachBranchSubViews(*View, Branches);
  attachMCDCSubViews(*View, MCDCRecords);
  if (!ViewOpts.ShowFunctionInstantiations)
    return View;

  for (const auto &Group : Coverage.getInstantiationGroups(SourceFile)) {
    // Skip functions which have a single instantiation.
    if (Group.size() < 2)
      continue;

    for (const FunctionRecord *Function : Group.getInstantiations()) {
      std::unique_ptr<SourceCoverageView> SubView{nullptr};

      StringRef Funcname = DC.demangle(Function->Name);

      if (Function->ExecutionCount > 0) {
        auto SubViewCoverage = Coverage.getCoverageForFunction(*Function);
        auto SubViewExpansions = SubViewCoverage.getExpansions();
        auto SubViewBranches = SubViewCoverage.getBranches();
        auto SubViewMCDCRecords = SubViewCoverage.getMCDCRecords();
        SubView = SourceCoverageView::create(
            Funcname, SourceBuffer.get(), ViewOpts, std::move(SubViewCoverage));
        attachExpansionSubViews(*SubView, SubViewExpansions, Coverage);
        attachBranchSubViews(*SubView, SubViewBranches);
        attachMCDCSubViews(*SubView, SubViewMCDCRecords);
      }

      unsigned FileID = Function->CountedRegions.front().FileID;
      unsigned Line = 0;
      for (const auto &CR : Function->CountedRegions)
        if (CR.FileID == FileID)
          Line = std::max(CR.LineEnd, Line);
      View->addInstantiation(Funcname, Line, std::move(SubView));
    }
  }
  return View;
}

static bool modifiedTimeGT(StringRef LHS, StringRef RHS) {
  sys::fs::file_status Status;
  if (sys::fs::status(LHS, Status))
    return false;
  auto LHSTime = Status.getLastModificationTime();
  if (sys::fs::status(RHS, Status))
    return false;
  auto RHSTime = Status.getLastModificationTime();
  return LHSTime > RHSTime;
}

std::unique_ptr<CoverageMapping> CodeCoverageTool::load() {
  if (PGOFilename) {
    for (StringRef ObjectFilename : ObjectFilenames)
      if (modifiedTimeGT(ObjectFilename, PGOFilename.value()))
        warning("profile data may be out of date - object is newer",
                ObjectFilename);
  }
  auto FS = vfs::getRealFileSystem();
  auto CoverageOrErr = CoverageMapping::load(
      ObjectFilenames, PGOFilename, *FS, CoverageArches,
      ViewOpts.CompilationDirectory, BIDFetcher.get(), CheckBinaryIDs);
  if (Error E = CoverageOrErr.takeError()) {
    error("failed to load coverage: " + toString(std::move(E)));
    return nullptr;
  }
  auto Coverage = std::move(CoverageOrErr.get());
  unsigned Mismatched = Coverage->getMismatchedCount();
  if (Mismatched) {
    warning(Twine(Mismatched) + " functions have mismatched data");

    if (ViewOpts.Debug) {
      for (const auto &HashMismatch : Coverage->getHashMismatches())
        errs() << "hash-mismatch: "
               << "No profile record found for '" << HashMismatch.first << "'"
               << " with hash = 0x" << Twine::utohexstr(HashMismatch.second)
               << '\n';
    }
  }

  remapPathNames(*Coverage);

  if (!SourceFiles.empty())
    removeUnmappedInputs(*Coverage);

  demangleSymbols(*Coverage);

  return Coverage;
}

void CodeCoverageTool::remapPathNames(const CoverageMapping &Coverage) {
  if (!PathRemappings)
    return;

  // Convert remapping paths to native paths with trailing separators.
  auto nativeWithTrailing = [](StringRef Path) -> std::string {
    if (Path.empty())
      return "";
    SmallString<128> NativePath;
    sys::path::native(Path, NativePath);
    sys::path::remove_dots(NativePath, true);
    if (!NativePath.empty() && !sys::path::is_separator(NativePath.back()))
      NativePath += sys::path::get_separator();
    return NativePath.c_str();
  };

  for (std::pair<std::string, std::string> &PathRemapping : *PathRemappings) {
    std::string RemapFrom = nativeWithTrailing(PathRemapping.first);
    std::string RemapTo = nativeWithTrailing(PathRemapping.second);

    // Create a mapping from coverage data file paths to local paths.
    for (StringRef Filename : Coverage.getUniqueSourceFiles()) {
      if (RemappedFilenames.count(Filename) == 1)
        continue;

      SmallString<128> NativeFilename;
      sys::path::native(Filename, NativeFilename);
      sys::path::remove_dots(NativeFilename, true);
      if (NativeFilename.starts_with(RemapFrom)) {
        RemappedFilenames[Filename] =
            RemapTo + NativeFilename.substr(RemapFrom.size()).str();
      }
    }
  }

  // Convert input files from local paths to coverage data file paths.
  StringMap<std::string> InvRemappedFilenames;
  for (const auto &RemappedFilename : RemappedFilenames)
    InvRemappedFilenames[RemappedFilename.getValue()] =
        std::string(RemappedFilename.getKey());

  for (std::string &Filename : SourceFiles) {
    SmallString<128> NativeFilename;
    sys::path::native(Filename, NativeFilename);
    auto CovFileName = InvRemappedFilenames.find(NativeFilename);
    if (CovFileName != InvRemappedFilenames.end())
      Filename = CovFileName->second;
  }
}

void CodeCoverageTool::removeUnmappedInputs(const CoverageMapping &Coverage) {
  std::vector<StringRef> CoveredFiles = Coverage.getUniqueSourceFiles();

  // The user may have specified source files which aren't in the coverage
  // mapping. Filter these files away.
  llvm::erase_if(SourceFiles, [&](const std::string &SF) {
    return !llvm::binary_search(CoveredFiles, SF);
  });
}

void CodeCoverageTool::demangleSymbols(const CoverageMapping &Coverage) {
  if (!ViewOpts.hasDemangler())
    return;

  // Pass function names to the demangler in a temporary file.
  int InputFD;
  SmallString<256> InputPath;
  std::error_code EC =
      sys::fs::createTemporaryFile("demangle-in", "list", InputFD, InputPath);
  if (EC) {
    error(InputPath, EC.message());
    return;
  }
  ToolOutputFile InputTOF{InputPath, InputFD};

  unsigned NumSymbols = 0;
  for (const auto &Function : Coverage.getCoveredFunctions()) {
    InputTOF.os() << Function.Name << '\n';
    ++NumSymbols;
  }
  InputTOF.os().close();

  // Use another temporary file to store the demangler's output.
  int OutputFD;
  SmallString<256> OutputPath;
  EC = sys::fs::createTemporaryFile("demangle-out", "list", OutputFD,
                                    OutputPath);
  if (EC) {
    error(OutputPath, EC.message());
    return;
  }
  ToolOutputFile OutputTOF{OutputPath, OutputFD};
  OutputTOF.os().close();

  // Invoke the demangler.
  std::vector<StringRef> ArgsV;
  ArgsV.reserve(ViewOpts.DemanglerOpts.size());
  llvm::append_range(ArgsV, ViewOpts.DemanglerOpts);
  std::optional<StringRef> Redirects[] = {
      InputPath.str(), OutputPath.str(), {""}};
  std::string ErrMsg;
  int RC =
      sys::ExecuteAndWait(ViewOpts.DemanglerOpts[0], ArgsV,
                          /*env=*/std::nullopt, Redirects, /*secondsToWait=*/0,
                          /*memoryLimit=*/0, &ErrMsg);
  if (RC) {
    error(ErrMsg, ViewOpts.DemanglerOpts[0]);
    return;
  }

  // Parse the demangler's output.
  auto BufOrError = MemoryBuffer::getFile(OutputPath);
  if (!BufOrError) {
    error(OutputPath, BufOrError.getError().message());
    return;
  }

  std::unique_ptr<MemoryBuffer> DemanglerBuf = std::move(*BufOrError);

  SmallVector<StringRef, 8> Symbols;
  StringRef DemanglerData = DemanglerBuf->getBuffer();
  DemanglerData.split(Symbols, '\n', /*MaxSplit=*/NumSymbols,
                      /*KeepEmpty=*/false);
  if (Symbols.size() != NumSymbols) {
    error("demangler did not provide expected number of symbols");
    return;
  }

  // Cache the demangled names.
  unsigned I = 0;
  for (const auto &Function : Coverage.getCoveredFunctions())
    // On Windows, lines in the demangler's output file end with "\r\n".
    // Splitting by '\n' keeps '\r's, so cut them now.
    DC.DemangledNames[Function.Name] = std::string(Symbols[I++].rtrim());
}

void CodeCoverageTool::writeSourceFileView(StringRef SourceFile,
                                           CoverageMapping *Coverage,
                                           CoveragePrinter *Printer,
                                           bool ShowFilenames) {
  auto View = createSourceFileView(SourceFile, *Coverage);
  if (!View) {
    warning("The file '" + SourceFile + "' isn't covered.");
    return;
  }

  auto OSOrErr = Printer->createViewFile(SourceFile, /*InToplevel=*/false);
  if (Error E = OSOrErr.takeError()) {
    error("could not create view file!", toString(std::move(E)));
    return;
  }
  auto OS = std::move(OSOrErr.get());

  View->print(*OS.get(), /*Wholefile=*/true,
              /*ShowSourceName=*/ShowFilenames,
              /*ShowTitle=*/ViewOpts.hasOutputDirectory());
  Printer->closeViewFile(std::move(OS));
}

int CodeCoverageTool::run(Command Cmd, int argc, const char **argv) {
  clv2::OptionParser P;
  P.add<&CC_ToolReg>();
  RegisterAllLLVMOptions(P);
  auto OptsCtx = P.parse(argc, argv, "LLVM code coverage tool\n");
  auto *S = OptsCtx->getViewPtr<&CC_ToolReg>();

  // --- Common setup (formerly in commandLineParser lambda) ---

  ViewOpts.Debug = S->get<&CC_DebugDumpOpt>();

  // Handle debuginfod: runtime default if not specified on command line.
  bool UseDebuginfod = S->specified<&CC_DebuginfodOpt>()
                           ? S->get<&CC_DebuginfodOpt>()
                           : canUseDebuginfod();

  const auto &DebugFileDirRef = S->get<&CC_DebugFileDirOpt>();
  std::vector<std::string> DebugFileDirVec(DebugFileDirRef.begin(),
                                           DebugFileDirRef.end());
  if (UseDebuginfod) {
    HTTPClient::initialize();
    BIDFetcher = std::make_unique<DebuginfodFetcher>(DebugFileDirVec);
  } else {
    BIDFetcher = std::make_unique<object::BuildIDFetcher>(DebugFileDirVec);
  }
  this->CheckBinaryIDs = S->get<&CC_CheckBinaryIDsOpt>();

  const auto &PGOFilenameVal = S->get<&CC_PGOFilenameOpt>();
  bool EmptyProfile = S->get<&CC_EmptyProfileOpt>();
  if (!PGOFilenameVal.empty() == EmptyProfile) {
    error("exactly one of -instr-profile and -empty-profile must be specified");
    return 1;
  }
  if (!PGOFilenameVal.empty()) {
    this->PGOFilename = std::make_optional(PGOFilenameVal);
  }

  const auto &CovFilenameVal = S->get<&CC_CovFilenameOpt>();
  if (!CovFilenameVal.empty())
    ObjectFilenames.emplace_back(CovFilenameVal);
  for (const auto &Filename : S->get<&CC_CovFilenamesOpt>())
    ObjectFilenames.emplace_back(Filename);
  if (ObjectFilenames.empty() && !UseDebuginfod && DebugFileDirRef.empty()) {
    errs() << "No filenames specified!\n";
    ::exit(1);
  }

  if (S->get<&CC_DebugDumpObjsOpt>()) {
    for (StringRef OF : ObjectFilenames)
      outs() << OF << '\n';
    ::exit(0);
  }

  ViewOpts.Format = S->get<&CC_FormatOpt>();
  cl::boolOrDefault UseColor = cl::boolOrDefault::BOU_UNSET;
  if (S->specified<&CC_UseColorOpt>())
    UseColor = S->get<&CC_UseColorOpt>() ? cl::boolOrDefault::BOU_TRUE
                                         : cl::boolOrDefault::BOU_FALSE;
  switch (ViewOpts.Format) {
  case CoverageViewOptions::OutputFormat::Text:
    ViewOpts.Colors = UseColor == cl::boolOrDefault::BOU_UNSET
                          ? sys::Process::StandardOutHasColors()
                          : UseColor == cl::boolOrDefault::BOU_TRUE;
    break;
  case CoverageViewOptions::OutputFormat::HTML:
    if (UseColor == cl::boolOrDefault::BOU_FALSE)
      errs() << "Color output cannot be disabled when generating html.\n";
    ViewOpts.Colors = true;
    break;
  case CoverageViewOptions::OutputFormat::Lcov:
    if (UseColor == cl::boolOrDefault::BOU_TRUE)
      errs() << "Color output cannot be enabled when generating lcov.\n";
    ViewOpts.Colors = false;
    break;
  }

  const auto &PathRemapsRef = S->get<&CC_PathRemapsOpt>();
  if (!PathRemapsRef.empty()) {
    std::vector<std::pair<std::string, std::string>> Remappings;

    for (const auto &PathRemap : PathRemapsRef) {
      auto EquivPair = StringRef(PathRemap).split(',');
      if (EquivPair.first.empty() || EquivPair.second.empty()) {
        error("invalid argument '" + PathRemap +
                  "', must be in format 'from,to'",
              "-path-equivalence");
        return 1;
      }

      Remappings.push_back(
          {std::string(EquivPair.first), std::string(EquivPair.second)});
    }

    PathRemappings = Remappings;
  }

  // If a demangler is supplied, check if it exists and register it.
  const auto &DemanglerOptsRef = S->get<&CC_DemanglerOpt>();
  if (!DemanglerOptsRef.empty()) {
    auto DemanglerPathOrErr = sys::findProgramByName(DemanglerOptsRef[0]);
    if (!DemanglerPathOrErr) {
      error("could not find the demangler!",
            DemanglerPathOrErr.getError().message());
      return 1;
    }
    ViewOpts.DemanglerOpts.assign(DemanglerOptsRef.begin(),
                                  DemanglerOptsRef.end());
    ViewOpts.DemanglerOpts[0] = *DemanglerPathOrErr;
  }

  // Read in -name-allowlist files.
  const auto &NameFilterFilesRef = S->get<&CC_NameFilterFilesOpt>();
  if (!NameFilterFilesRef.empty()) {
    std::string SpecialCaseListErr;
    std::vector<std::string> NameFilterFilesVec(NameFilterFilesRef.begin(),
                                                NameFilterFilesRef.end());
    NameAllowlist = SpecialCaseList::create(
        NameFilterFilesVec, *vfs::getRealFileSystem(), SpecialCaseListErr);
    if (!NameAllowlist)
      error(SpecialCaseListErr);
  }

  // Create the function filters
  const auto &NameFiltersRef = S->get<&CC_NameFiltersOpt>();
  const auto &NameRegexFiltersRef = S->get<&CC_NameRegexFiltersOpt>();
  if (!NameFiltersRef.empty() || NameAllowlist ||
      !NameRegexFiltersRef.empty()) {
    auto NameFilterer = std::make_unique<CoverageFilters>();
    for (const auto &Name : NameFiltersRef)
      NameFilterer->push_back(std::make_unique<NameCoverageFilter>(Name));
    if (NameAllowlist && !NameFilterFilesRef.empty())
      NameFilterer->push_back(
          std::make_unique<NameAllowlistCoverageFilter>(*NameAllowlist));
    for (const auto &Regex : NameRegexFiltersRef)
      NameFilterer->push_back(std::make_unique<NameRegexCoverageFilter>(Regex));
    Filters.push_back(std::move(NameFilterer));
  }

  if (S->specified<&CC_RegionCoverageLtOpt>() ||
      S->specified<&CC_RegionCoverageGtOpt>() ||
      S->specified<&CC_LineCoverageLtOpt>() ||
      S->specified<&CC_LineCoverageGtOpt>()) {
    auto StatFilterer = std::make_unique<CoverageFilters>();
    if (S->specified<&CC_RegionCoverageLtOpt>())
      StatFilterer->push_back(std::make_unique<RegionCoverageFilter>(
          RegionCoverageFilter::LessThan, S->get<&CC_RegionCoverageLtOpt>()));
    if (S->specified<&CC_RegionCoverageGtOpt>())
      StatFilterer->push_back(std::make_unique<RegionCoverageFilter>(
          RegionCoverageFilter::GreaterThan,
          S->get<&CC_RegionCoverageGtOpt>()));
    if (S->specified<&CC_LineCoverageLtOpt>())
      StatFilterer->push_back(std::make_unique<LineCoverageFilter>(
          LineCoverageFilter::LessThan, S->get<&CC_LineCoverageLtOpt>()));
    if (S->specified<&CC_LineCoverageGtOpt>())
      StatFilterer->push_back(std::make_unique<LineCoverageFilter>(
          RegionCoverageFilter::GreaterThan, S->get<&CC_LineCoverageGtOpt>()));
    Filters.push_back(std::move(StatFilterer));
  }

  // Create the ignore filename filters.
  for (const auto &RE : S->get<&CC_IgnoreFilenameRegexOpt>())
    FilenameFilters.push_back(std::make_unique<NameRegexCoverageFilter>(RE));

  for (const auto &RE : S->get<&CC_IncludeFilenameRegexOpt>())
    FilenameFilters.push_back(std::make_unique<NameRegexCoverageFilter>(
        RE, NameRegexCoverageFilter::FilterType::Include));

  const auto &ArchesRef = S->get<&CC_ArchesOpt>();
  if (!ArchesRef.empty()) {
    for (const auto &Arch : ArchesRef) {
      if (Triple(Arch).getArch() == llvm::Triple::ArchType::UnknownArch) {
        error("unknown architecture: " + Arch);
        return 1;
      }
      CoverageArches.emplace_back(Arch);
    }
    if (CoverageArches.size() != 1 &&
        CoverageArches.size() != ObjectFilenames.size()) {
      error("number of architectures doesn't match the number of objects");
      return 1;
    }
  }

  // FilenameFilters are applied even when InputSourceFiles specified.
  for (const auto &File : S->get<&CC_InputSourceFilesOpt>())
    collectPaths(File);

  if (S->get<&CC_DebugDumpPathsOpt>()) {
    for (const std::string &SF : SourceFiles)
      outs() << SF << '\n';
    ::exit(0);
  }

  ViewOpts.ShowMCDCSummary = S->get<&CC_MCDCSummaryOpt>();
  ViewOpts.ShowBranchSummary = S->get<&CC_BranchSummaryOpt>();
  ViewOpts.ShowRegionSummary = S->get<&CC_RegionSummaryOpt>();
  ViewOpts.ShowFunctionSummary = S->get<&CC_FunctionSummaryOpt>();
  ViewOpts.ShowInstantiationSummary = S->get<&CC_InstantiationSummaryOpt>();
  ViewOpts.ExportSummaryOnly = S->get<&CC_SummaryOnlyOpt>();
  ViewOpts.NumThreads = S->get<&CC_NumThreadsOpt>();
  ViewOpts.CompilationDirectory = S->get<&CC_CompilationDirOpt>();

  // --- Command-specific setup ---
  switch (Cmd) {
  case Show: {
    if (ViewOpts.Format == CoverageViewOptions::OutputFormat::Lcov) {
      error("lcov format should be used with 'llvm-cov export'.");
      return 1;
    }

    ViewOpts.HighCovWatermark = 100.0;
    ViewOpts.LowCovWatermark = 80.0;
    const auto &CovWatermark = S->get<&CC_CovWatermarkOpt>();
    if (!CovWatermark.empty()) {
      auto WaterMarkPair = StringRef(CovWatermark).split(',');
      if (WaterMarkPair.first.empty() || WaterMarkPair.second.empty()) {
        error("invalid argument '" + CovWatermark +
                  "', must be in format 'high,low'",
              "-coverage-watermark");
        return 1;
      }

      char *EndPointer = nullptr;
      ViewOpts.HighCovWatermark =
          strtod(WaterMarkPair.first.begin(), &EndPointer);
      if (EndPointer != WaterMarkPair.first.end()) {
        error("invalid number '" + WaterMarkPair.first +
                  "', invalid value for 'high'",
              "-coverage-watermark");
        return 1;
      }

      ViewOpts.LowCovWatermark =
          strtod(WaterMarkPair.second.begin(), &EndPointer);
      if (EndPointer != WaterMarkPair.second.end()) {
        error("invalid number '" + WaterMarkPair.second +
                  "', invalid value for 'low'",
              "-coverage-watermark");
        return 1;
      }

      if (ViewOpts.HighCovWatermark > 100 || ViewOpts.LowCovWatermark < 0 ||
          ViewOpts.HighCovWatermark <= ViewOpts.LowCovWatermark) {
        error("invalid number range '" + CovWatermark +
                  "', must be both high and low should be between 0-100, and "
                  "high "
                  "> low",
              "-coverage-watermark");
        return 1;
      }
    }

    ViewOpts.ShowLineNumbers = true;
    ViewOpts.ShowLineStats = S->specified<&CC_ShowLineCountsOpt>() ||
                             !S->get<&CC_ShowRegionsOpt>() ||
                             S->get<&CC_ShowBestLineRegionsOpt>();
    ViewOpts.ShowRegionMarkers =
        S->get<&CC_ShowRegionsOpt>() || S->get<&CC_ShowBestLineRegionsOpt>();
    ViewOpts.ShowExpandedRegions = S->get<&CC_ShowExpansionsOpt>();
    ViewOpts.ShowBranchCounts = S->get<&CC_ShowBranchesOpt>() ==
                                CoverageViewOptions::BranchOutputType::Count;
    ViewOpts.ShowMCDC = S->get<&CC_ShowMCDCOpt>();
    ViewOpts.ShowMCDCNonExecutedVectors = S->get<&CC_ShowMCDCNonExecOpt>();
    ViewOpts.ShowBranchPercents =
        S->get<&CC_ShowBranchesOpt>() ==
        CoverageViewOptions::BranchOutputType::Percent;
    ViewOpts.ShowFunctionInstantiations = S->get<&CC_ShowInstantiationsOpt>();
    ViewOpts.ShowDirectoryCoverage = S->get<&CC_ShowDirCoverageOpt>();
    ViewOpts.ShowOutputDirectory = S->get<&CC_OutputDirOpt>();
    ViewOpts.BinaryCounters = S->get<&CC_BinaryCountersOpt>();
    ViewOpts.TabSize = S->get<&CC_TabSizeOpt>();
    ViewOpts.ProjectTitle = S->get<&CC_ProjectTitleOpt>();

    if (ViewOpts.hasOutputDirectory()) {
      if (auto E = sys::fs::create_directories(ViewOpts.ShowOutputDirectory)) {
        error("could not create output directory!", E.message());
        return 1;
      }
    }

    if (PGOFilename) {
      sys::fs::file_status Status;
      if (std::error_code EC = sys::fs::status(PGOFilename.value(), Status)) {
        error("could not read profile data!" + EC.message(),
              PGOFilename.value());
        return 1;
      }

      if (S->get<&CC_ShowCreatedTimeOpt>()) {
        auto ModifiedTime = Status.getLastModificationTime();
        std::string ModifiedTimeStr = to_string(ModifiedTime);
        size_t found = ModifiedTimeStr.rfind(':');
        ViewOpts.CreatedTimeStr =
            (found != std::string::npos)
                ? "Created: " + ModifiedTimeStr.substr(0, found)
                : "Created: " + ModifiedTimeStr;
      }
    }

    return doShow();
  }

  case Report: {
    if (ViewOpts.Format == CoverageViewOptions::OutputFormat::HTML) {
      error("HTML output for summary reports is not yet supported.");
      return 1;
    } else if (ViewOpts.Format == CoverageViewOptions::OutputFormat::Lcov) {
      error("lcov format should be used with 'llvm-cov export'.");
      return 1;
    }

    if (PGOFilename) {
      sys::fs::file_status Status;
      if (std::error_code EC = sys::fs::status(PGOFilename.value(), Status)) {
        error("could not read profile data!" + EC.message(),
              PGOFilename.value());
        return 1;
      }
    }

    return doReport(S->get<&CC_ShowFunctionsOpt>());
  }

  case Export: {
    ViewOpts.SkipExpansions = S->get<&CC_SkipExpansionsOpt>();
    ViewOpts.SkipFunctions = S->get<&CC_SkipFunctionsOpt>();
    ViewOpts.SkipBranches = S->get<&CC_SkipBranchesOpt>();
    ViewOpts.UnifyFunctionInstantiations = S->get<&CC_UnifyInstantiationsOpt>();
    ViewOpts.ShowMCDCNonExecutedVectors = S->get<&CC_ShowMCDCNonExecOpt>();

    if (ViewOpts.Format != CoverageViewOptions::OutputFormat::Text &&
        ViewOpts.Format != CoverageViewOptions::OutputFormat::Lcov) {
      error("coverage data can only be exported as textual JSON or an "
            "lcov tracefile.");
      return 1;
    }

    if (PGOFilename) {
      sys::fs::file_status Status;
      if (std::error_code EC = sys::fs::status(PGOFilename.value(), Status)) {
        error("could not read profile data!" + EC.message(),
              PGOFilename.value());
        return 1;
      }
    }

    return doExport();
  }
  }
  return 0;
}

int CodeCoverageTool::doShow() {
  auto Coverage = load();
  if (!Coverage)
    return 1;

  auto Printer = CoveragePrinter::create(ViewOpts);

  if (SourceFiles.empty() && !HadSourceFiles)
    // Get the source files from the function coverage mapping.
    for (StringRef Filename : Coverage->getUniqueSourceFiles()) {
      if (!FilenameFilters.matchesFilename(Filename))
        SourceFiles.push_back(std::string(Filename));
    }

  // Create an index out of the source files.
  if (ViewOpts.hasOutputDirectory()) {
    if (Error E = Printer->createIndexFile(SourceFiles, *Coverage, Filters)) {
      error("could not create index file!", toString(std::move(E)));
      return 1;
    }
  }

  if (!Filters.empty()) {
    // Build the map of filenames to functions.
    std::map<llvm::StringRef, std::vector<const FunctionRecord *>>
        FilenameFunctionMap;
    for (const auto &SourceFile : SourceFiles)
      for (const auto &Function : Coverage->getCoveredFunctions(SourceFile))
        if (Filters.matches(*Coverage, Function))
          FilenameFunctionMap[SourceFile].push_back(&Function);

    // Only print filter matching functions for each file.
    for (const auto &FileFunc : FilenameFunctionMap) {
      StringRef File = FileFunc.first;
      const auto &Functions = FileFunc.second;

      auto OSOrErr = Printer->createViewFile(File, /*InToplevel=*/false);
      if (Error E = OSOrErr.takeError()) {
        error("could not create view file!", toString(std::move(E)));
        return 1;
      }
      auto OS = std::move(OSOrErr.get());

      bool ShowTitle = ViewOpts.hasOutputDirectory();
      for (const auto *Function : Functions) {
        auto FunctionView = createFunctionView(*Function, *Coverage);
        if (!FunctionView) {
          warning("Could not read coverage for '" + Function->Name + "'.");
          continue;
        }
        FunctionView->print(*OS.get(), /*WholeFile=*/false,
                            /*ShowSourceName=*/true, ShowTitle);
        ShowTitle = false;
      }

      Printer->closeViewFile(std::move(OS));
    }
    return 0;
  }

  // Show files
  bool ShowFilenames =
      (SourceFiles.size() != 1) || ViewOpts.hasOutputDirectory() ||
      (ViewOpts.Format == CoverageViewOptions::OutputFormat::HTML);

  ThreadPoolStrategy S = hardware_concurrency(ViewOpts.NumThreads);
  if (ViewOpts.NumThreads == 0) {
    // If NumThreads is not specified, create one thread for each input, up to
    // the number of hardware cores.
    S = heavyweight_hardware_concurrency(SourceFiles.size());
    S.Limit = true;
  }

  if (!ViewOpts.hasOutputDirectory() || S.ThreadsRequested == 1) {
    for (const std::string &SourceFile : SourceFiles)
      writeSourceFileView(SourceFile, Coverage.get(), Printer.get(),
                          ShowFilenames);
  } else {
    // In -output-dir mode, it's safe to use multiple threads to print files.
    DefaultThreadPool Pool(S);
    for (const std::string &SourceFile : SourceFiles)
      Pool.async(&CodeCoverageTool::writeSourceFileView, this, SourceFile,
                 Coverage.get(), Printer.get(), ShowFilenames);
    Pool.wait();
  }

  return 0;
}

int CodeCoverageTool::doReport(bool ShowFunctionSummaries) {
  auto Coverage = load();
  if (!Coverage)
    return 1;

  CoverageReport Report(ViewOpts, *Coverage);
  if (!ShowFunctionSummaries) {
    if (SourceFiles.empty())
      Report.renderFileReports(llvm::outs(), FilenameFilters);
    else
      Report.renderFileReports(llvm::outs(), SourceFiles);
  } else {
    if (SourceFiles.empty()) {
      error("source files must be specified when -show-functions=true is "
            "specified");
      return 1;
    }

    Report.renderFunctionReports(SourceFiles, DC, llvm::outs());
  }
  return 0;
}

int CodeCoverageTool::doExport() {
  auto Coverage = load();
  if (!Coverage) {
    error("could not load coverage information");
    return 1;
  }

  std::unique_ptr<CoverageExporter> Exporter;

  switch (ViewOpts.Format) {
  case CoverageViewOptions::OutputFormat::Text:
    Exporter =
        std::make_unique<CoverageExporterJson>(*Coverage, ViewOpts, outs());
    break;
  case CoverageViewOptions::OutputFormat::HTML:
    // Unreachable because we should have gracefully terminated with an error
    // above.
    llvm_unreachable("Export in HTML is not supported!");
  case CoverageViewOptions::OutputFormat::Lcov:
    Exporter =
        std::make_unique<CoverageExporterLcov>(*Coverage, ViewOpts, outs());
    break;
  }

  if (SourceFiles.empty())
    Exporter->renderRoot(FilenameFilters);
  else
    Exporter->renderRoot(SourceFiles);

  return 0;
}

int showMain(int argc, const char *argv[]) {
  CodeCoverageTool Tool;
  return Tool.run(CodeCoverageTool::Show, argc, argv);
}

int reportMain(int argc, const char *argv[]) {
  CodeCoverageTool Tool;
  return Tool.run(CodeCoverageTool::Report, argc, argv);
}

int exportMain(int argc, const char *argv[]) {
  CodeCoverageTool Tool;
  return Tool.run(CodeCoverageTool::Export, argc, argv);
}
