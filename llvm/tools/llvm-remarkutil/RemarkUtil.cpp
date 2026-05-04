//===--------- llvm-remarkutil/RemarkUtil.cpp -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// Utility for remark files.
//===----------------------------------------------------------------------===//

#include "RemarkCounter.h"
#include "RemarkUtilHelpers.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SupportOptionsOptInfos.h"

using namespace llvm;
using namespace llvm::remarkutil;
ExitOnError ExitOnErr;

static constexpr clv2::OptionCategory RemarkUtilCategory{
    "llvm-remarkutil Options"};

//===----------------------------------------------------------------------===//
// Handler forward declarations
//===----------------------------------------------------------------------===//

namespace yaml2bitstream {
Error tryYAML2Bitstream();
} // namespace yaml2bitstream

namespace bitstream2yaml {
Error tryBitstream2YAML();
} // namespace bitstream2yaml

namespace instructioncount {
Error tryInstructionCount();
} // namespace instructioncount

namespace annotationcount {
Error tryAnnotationCount();
} // namespace annotationcount

Error collectRemarks();

Error tryFilter();

namespace instructionmix {
Error tryInstructionMix();
} // namespace instructionmix

Error trySizeDiff();

namespace summary {
Error trySummary();
} // namespace summary

//===----------------------------------------------------------------------===//
// Shared option descriptors
//===----------------------------------------------------------------------===//

inline constexpr clv2::OptionInfo<std::string> InputFileNameOpt{
    "", "<input file>", clv2::Positional{}, clv2::Init{"-"},
    clv2::cat(RemarkUtilCategory)};

inline constexpr clv2::OptionInfo<std::string> OutputFileNameOpt{
    "o", "Output", clv2::value_desc("filename"), clv2::Init{"-"},
    clv2::cat(RemarkUtilCategory)};

inline constexpr clv2::EnumVal<remarks::Format> InputFormatVals[] = {
    {"auto", remarks::Format::Auto, "Automatic detection (default)"},
    {"yaml", remarks::Format::YAML, "YAML"},
    {"bitstream", remarks::Format::Bitstream, "Bitstream"},
};
inline constexpr auto InputFormatOpt = clv2::makeEnumOption<remarks::Format>(
    "parser", "Input remark format to parse", InputFormatVals,
    clv2::Init{remarks::Format::Auto}, clv2::cat(RemarkUtilCategory));

inline constexpr clv2::EnumVal<remarks::Format> OutputFormatVals[] = {
    {"auto", remarks::Format::Auto,
     "Automatic detection based on output file extension or parser format "
     "(default)"},
    {"yaml", remarks::Format::YAML, "YAML"},
    {"bitstream", remarks::Format::Bitstream, "Bitstream"},
};
inline constexpr auto OutputFormatOpt = clv2::makeEnumOption<remarks::Format>(
    "serializer", "Output remark format to serialize", OutputFormatVals,
    clv2::Init{remarks::Format::Auto}, clv2::cat(RemarkUtilCategory));

inline constexpr clv2::OptionInfo<bool> UseDebugLocOpt{
    "use-debug-loc",
    "Add debug loc information when generating tables for functions. "
    "The loc is represented as (path:line number:column number)",
    clv2::cat(RemarkUtilCategory)};

//===----------------------------------------------------------------------===//
// Shared filter option descriptors (count + filter subcommands)
//===----------------------------------------------------------------------===//

inline constexpr clv2::OptionInfo<std::string> FunctionFilterOpt{
    "function", "Optional function name to filter collection by.",
    clv2::cat(RemarkUtilCategory)};
inline constexpr clv2::OptionInfo<std::string> FunctionFilterREOpt{
    "rfunction",
    "Optional function name to filter collection by "
    "(accepts regular expressions).",
    clv2::cat(RemarkUtilCategory)};
inline constexpr clv2::OptionInfo<std::string> RemarkNameFilterOpt{
    "remark-name", "Optional remark name to filter collection by.",
    clv2::cat(RemarkUtilCategory)};
inline constexpr clv2::OptionInfo<std::string> RemarkNameFilterREOpt{
    "rremark-name",
    "Optional remark name to filter collection by "
    "(accepts regular expressions).",
    clv2::cat(RemarkUtilCategory)};
inline constexpr clv2::OptionInfo<std::string> PassNameFilterOpt{
    "pass-name", "Optional remark pass name to filter collection by.",
    clv2::cat(RemarkUtilCategory)};
inline constexpr clv2::OptionInfo<std::string> PassNameFilterREOpt{
    "rpass-name",
    "Optional remark pass name to filter collection by "
    "(accepts regular expressions).",
    clv2::cat(RemarkUtilCategory)};

inline constexpr clv2::EnumVal<remarks::Type> RemarkTypeVals[] = {
    {"unknown", remarks::Type::Unknown, "UNKNOWN"},
    {"passed", remarks::Type::Passed, "PASSED"},
    {"missed", remarks::Type::Missed, "MISSED"},
    {"analysis", remarks::Type::Analysis, "ANALYSIS"},
    {"analysis-fp-commute", remarks::Type::AnalysisFPCommute,
     "ANALYSIS_FP_COMMUTE"},
    {"analysis-aliasing", remarks::Type::AnalysisAliasing, "ANALYSIS_ALIASING"},
    {"failure", remarks::Type::Failure, "FAILURE"},
};
inline constexpr auto RemarkTypeFilterOpt = clv2::makeEnumOption<remarks::Type>(
    "remark-type", "Optional remark type to filter collection by.",
    RemarkTypeVals, clv2::cat(RemarkUtilCategory));

inline constexpr clv2::OptionInfo<std::string> FilterArgByOpt{
    "filter-arg-by", "Optional remark arg to filter collection by.",
    clv2::cat(RemarkUtilCategory)};
inline constexpr clv2::OptionInfo<std::string> FilterArgByREOpt{
    "rfilter-arg-by",
    "Optional remark arg to filter collection by "
    "(accepts regular expressions).",
    clv2::cat(RemarkUtilCategory)};

//===----------------------------------------------------------------------===//
// annotation-count specific
//===----------------------------------------------------------------------===//

inline constexpr clv2::OptionInfo<std::string> AnnotationTypeOpt{
    "annotation-type", "annotation-type remark to collect count for",
    clv2::cat(RemarkUtilCategory)};

//===----------------------------------------------------------------------===//
// count specific
//===----------------------------------------------------------------------===//

inline constexpr clv2::ListOptionInfo<std::string> CountKeysOpt{
    "args", "Specify remark argument/s to count by.",
    clv2::value_desc("arguments"), clv2::cat(RemarkUtilCategory)};
inline constexpr clv2::ListOptionInfo<std::string> CountRKeysOpt{
    "rargs",
    "Specify remark argument/s to count (accepts regular expressions).",
    clv2::value_desc("arguments"), clv2::cat(RemarkUtilCategory)};

inline constexpr clv2::EnumVal<remarks::CountBy> CountByVals[] = {
    {"remark-name", remarks::CountBy::REMARK,
     "Counts individual remarks based on how many of the remark exists."},
    {"arg", remarks::CountBy::ARGUMENT,
     "Counts based on the value each specified argument has. The argument "
     "has to have a number value to be considered."},
};
inline constexpr auto CountByOptDesc = clv2::makeEnumOption<remarks::CountBy>(
    "count-by", "Specify the property to collect remarks by.", CountByVals,
    clv2::Init{remarks::CountBy::REMARK}, clv2::cat(RemarkUtilCategory));

inline constexpr clv2::EnumVal<remarks::GroupBy> GroupByVals[] = {
    {"source", remarks::GroupBy::PER_SOURCE,
     "Display the count broken down by the filepath of each remark emitted. "
     "Requires remarks to have DebugLoc information."},
    {"function", remarks::GroupBy::PER_FUNCTION,
     "Breakdown the count by function name."},
    {"function-with-loc", remarks::GroupBy::PER_FUNCTION_WITH_DEBUG_LOC,
     "Breakdown the count by function name taking into consideration the "
     "filepath info from the DebugLoc of the remark."},
    {"total", remarks::GroupBy::TOTAL,
     "Output the total number corresponding to the count for the provided "
     "input file."},
};
inline constexpr auto GroupByOptDesc = clv2::makeEnumOption<remarks::GroupBy>(
    "group-by", "Specify the property to group remarks by.", GroupByVals,
    clv2::Init{remarks::GroupBy::PER_SOURCE}, clv2::cat(RemarkUtilCategory));

//===----------------------------------------------------------------------===//
// filter specific
//===----------------------------------------------------------------------===//

inline constexpr clv2::ListOptionInfo<std::string> FilterInputFilesOpt{
    "", "<input file> [<input file> ...]", clv2::Positional{},
    clv2::cat(RemarkUtilCategory)};

inline constexpr clv2::OptionInfo<bool> FilterExcludeOpt{
    "exclude", "Keep all remarks except those matching the filter",
    clv2::cat(RemarkUtilCategory)};
inline constexpr clv2::OptionInfo<bool> FilterSortOpt{
    "sort", "Sort remarks (expensive!)", clv2::cat(RemarkUtilCategory)};
inline constexpr clv2::OptionInfo<bool> FilterDedupeOpt{
    "dedupe", "Deduplicate remarks (expensive!)",
    clv2::cat(RemarkUtilCategory)};

//===----------------------------------------------------------------------===//
// instruction-mix specific
//===----------------------------------------------------------------------===//

inline constexpr clv2::OptionInfo<std::string> IMixFunctionFilterOpt{
    "filter", "Optional function name to filter collection by",
    clv2::cat(RemarkUtilCategory)};
inline constexpr clv2::OptionInfo<std::string> IMixFunctionFilterREOpt{
    "rfilter",
    "Optional function name to filter collection by "
    "(accepts regular expressions)",
    clv2::cat(RemarkUtilCategory)};

inline constexpr clv2::EnumVal<instructionmix::ReportStyleOptions>
    IMixReportStyleVals[] = {
        {"human", instructionmix::human_output, "Human-readable format"},
        {"csv", instructionmix::csv_output, "CSV format"},
};
inline constexpr auto IMixReportStyleOpt =
    clv2::makeEnumOption<instructionmix::ReportStyleOptions>(
        "report_style", "Choose the report output format:", IMixReportStyleVals,
        clv2::Init{instructionmix::human_output},
        clv2::cat(RemarkUtilCategory));

//===----------------------------------------------------------------------===//
// size-diff specific
//===----------------------------------------------------------------------===//

inline constexpr clv2::OptionInfo<std::string> SDInputFileNameAOpt{
    "", "remarks_a", clv2::Positional{}, clv2::cat(RemarkUtilCategory)};
inline constexpr clv2::OptionInfo<std::string> SDInputFileNameBOpt{
    "", "remarks_b", clv2::Positional{}, clv2::cat(RemarkUtilCategory)};

inline constexpr clv2::EnumVal<sizediff::ReportStyleOptions>
    SDReportStyleVals[] = {
        {"human", sizediff::human_output, "Human-readable format"},
        {"json", sizediff::json_output, "JSON format"},
};
inline constexpr auto SDReportStyleOpt =
    clv2::makeEnumOption<sizediff::ReportStyleOptions>(
        "report_style", "Choose the report output format:", SDReportStyleVals,
        clv2::Init{sizediff::human_output}, clv2::cat(RemarkUtilCategory));

inline constexpr clv2::OptionInfo<bool> SDPrettyPrintOpt{
    "pretty", "Pretty-print JSON", clv2::cat(RemarkUtilCategory)};

//===----------------------------------------------------------------------===//
// summary specific
//===----------------------------------------------------------------------===//

inline constexpr clv2::EnumVal<summary::KeepMode> KeepModeVals[] = {
    {"none", summary::KeepMode::None, "Don't keep input remarks (default)"},
    {"used", summary::KeepMode::Used, "Keep only remarks used for summary"},
    {"all", summary::KeepMode::All, "Keep all input remarks"},
};
inline constexpr auto SummaryKeepOpt = clv2::makeEnumOption<summary::KeepMode>(
    "keep", "Keep input remarks in output", KeepModeVals,
    clv2::Init{summary::KeepMode::None}, clv2::cat(RemarkUtilCategory));

inline constexpr clv2::OptionInfo<bool> SummaryIgnoreMalformedOpt{
    "ignore-malformed", "Ignore remarks that fail to process", clv2::Hidden,
    clv2::cat(RemarkUtilCategory)};

inline constexpr clv2::OptionInfo<bool> SummaryInlineCalleesOpt{
    "inline-callees", "Summarize per-callee inling statistics",
    clv2::cat(RemarkUtilCategory)};

//===----------------------------------------------------------------------===//
// SubCommand descriptors
//===----------------------------------------------------------------------===//

inline constexpr clv2::SubCommandInfo<&InputFileNameOpt, &OutputFileNameOpt>
    YAML2BitstreamCmd{"yaml2bitstream",
                      "Convert YAML remarks to bitstream remarks"};

inline constexpr clv2::SubCommandInfo<&InputFileNameOpt, &OutputFileNameOpt>
    Bitstream2YAMLCmd{"bitstream2yaml",
                      "Convert bitstream remarks to YAML remarks"};

inline constexpr clv2::SubCommandInfo<&InputFormatOpt, &InputFileNameOpt,
                                      &OutputFileNameOpt, &UseDebugLocOpt>
    InstructionCountCmd{"instruction-count",
                        "Function instruction count information (requires "
                        "asm-printer remarks)"};

inline constexpr clv2::SubCommandInfo<&InputFormatOpt, &AnnotationTypeOpt,
                                      &InputFileNameOpt, &OutputFileNameOpt,
                                      &UseDebugLocOpt>
    AnnotationCountCmd{
        "annotation-count",
        "Collect count information from annotation remarks (uses "
        "AnnotationRemarksPass)"};

inline constexpr clv2::SubCommandInfo<
    &InputFormatOpt, &InputFileNameOpt, &OutputFileNameOpt, &FunctionFilterOpt,
    &FunctionFilterREOpt, &RemarkNameFilterOpt, &RemarkNameFilterREOpt,
    &PassNameFilterOpt, &PassNameFilterREOpt, &RemarkTypeFilterOpt,
    &FilterArgByOpt, &FilterArgByREOpt, &CountKeysOpt, &CountRKeysOpt,
    &CountByOptDesc, &GroupByOptDesc>
    CountCmd{"count", "Collect remarks based on specified criteria."};

inline constexpr clv2::SubCommandInfo<
    &InputFormatOpt, &OutputFormatOpt, &OutputFileNameOpt, &FunctionFilterOpt,
    &FunctionFilterREOpt, &RemarkNameFilterOpt, &RemarkNameFilterREOpt,
    &PassNameFilterOpt, &PassNameFilterREOpt, &RemarkTypeFilterOpt,
    &FilterArgByOpt, &FilterArgByREOpt, &FilterInputFilesOpt, &FilterExcludeOpt,
    &FilterSortOpt, &FilterDedupeOpt>
    FilterCmd{"filter",
              "Filter remarks based on specified criteria. "
              "Can be used to merge multiple remark files.\n"
              "Multiple input files are processed in argument order and their "
              "outputs are combined into a single output file."};

inline constexpr clv2::SubCommandInfo<
    &InputFormatOpt, &InputFileNameOpt, &OutputFileNameOpt,
    &IMixFunctionFilterOpt, &IMixFunctionFilterREOpt, &IMixReportStyleOpt>
    InstructionMixCmd{"instruction-mix",
                      "Instruction Mix (requires asm-printer remarks)"};

inline constexpr clv2::SubCommandInfo<&InputFormatOpt, &SDInputFileNameAOpt,
                                      &SDInputFileNameBOpt, &OutputFileNameOpt,
                                      &SDReportStyleOpt, &SDPrettyPrintOpt>
    SizeDiffCmd{"size-diff", "Diff instruction count and stack size remarks "
                             "between two remark files"};

inline constexpr clv2::SubCommandInfo<
    &InputFormatOpt, &OutputFormatOpt, &InputFileNameOpt, &OutputFileNameOpt,
    &SummaryKeepOpt, &SummaryIgnoreMalformedOpt, &SummaryInlineCalleesOpt>
    SummaryCmd{"summary", "Summarize remarks using different strategies."};

//===----------------------------------------------------------------------===//
// Registries
//===----------------------------------------------------------------------===//

static constexpr clv2::OptionsRegistry<
    &YAML2BitstreamCmd, &Bitstream2YAMLCmd, &InstructionCountCmd,
    &AnnotationCountCmd, &CountCmd, &FilterCmd, &InstructionMixCmd,
    &SizeDiffCmd, &SummaryCmd>
    RemarkUtilToolReg;

//===----------------------------------------------------------------------===//
// Extern declarations for subcommand-specific globals in sub-files
//===----------------------------------------------------------------------===//

// RemarkCount.cpp
extern std::string AnnotationTypeToCollect;

// RemarkCounter.cpp
extern std::vector<std::string> Keys;
extern std::vector<std::string> RKeys;
extern remarks::CountBy CountByVal;
extern remarks::GroupBy GroupByVal;

// RemarkFilter.cpp
extern std::vector<std::string> FilterInputFileNames;
extern bool ExcludeOptVal;
extern bool SortOptVal;
extern bool DedupeOptVal;

// RemarkInstructionMix.cpp
namespace instructionmix {
extern std::string FunctionFilter;
extern std::string FunctionFilterRE;
extern ReportStyleOptions ReportStyle;
} // namespace instructionmix

// RemarkSizeDiff.cpp
extern std::string InputFileNameA;
extern std::string InputFileNameB;
extern sizediff::ReportStyleOptions SDReportStyle;
extern bool PrettyPrint;

// RemarkSummary.cpp
namespace summary {
extern KeepMode KeepInputVal;
extern bool IgnoreMalformedVal;
extern bool EnableInlineSummaryVal;
extern bool EnableInlineSummarySpecified;
} // namespace summary

//===----------------------------------------------------------------------===//
// Helper to extract shared filter globals
//===----------------------------------------------------------------------===//

template <typename SubOptsT>
static void extractFilterGlobals(const SubOptsT &S) {
  FunctionOpt = S.template get<&FunctionFilterOpt>();
  FunctionOptRE = S.template get<&FunctionFilterREOpt>();
  RemarkNameOpt = S.template get<&RemarkNameFilterOpt>();
  RemarkNameOptRE = S.template get<&RemarkNameFilterREOpt>();
  PassNameOpt = S.template get<&PassNameFilterOpt>();
  PassNameOptRE = S.template get<&PassNameFilterREOpt>();
  RemarkFilterArgByOpt = S.template get<&FilterArgByOpt>();
  RemarkArgFilterOptRE = S.template get<&FilterArgByREOpt>();
  if (S.template specified<&RemarkTypeFilterOpt>())
    RemarkTypeFilter = S.template get<&RemarkTypeFilterOpt>();
  else
    RemarkTypeFilter = std::nullopt;
}

//===----------------------------------------------------------------------===//
// main
//===----------------------------------------------------------------------===//

int main(int argc, char *argv[]) {
  InitLLVM X(argc, argv);
  clv2::OptionParser P;
  P.add<&RemarkUtilToolReg>();
  RegisterCoreLLVMOptions(P);
  auto OptsCtx = P.parse(argc, argv, "Remark file utilities\n");
  auto *Opts = OptsCtx->getViewPtr<&RemarkUtilToolReg>();
  ExitOnErr.setBanner(std::string(argv[0]) + ": error: ");

  if (Opts->isActive<&YAML2BitstreamCmd>()) {
    auto &S = Opts->getSubOptions<&YAML2BitstreamCmd>();
    InputFileName = S.get<&InputFileNameOpt>();
    OutputFileName = S.get<&OutputFileNameOpt>();
    ExitOnErr(yaml2bitstream::tryYAML2Bitstream());
    return 0;
  }

  if (Opts->isActive<&Bitstream2YAMLCmd>()) {
    auto &S = Opts->getSubOptions<&Bitstream2YAMLCmd>();
    InputFileName = S.get<&InputFileNameOpt>();
    OutputFileName = S.get<&OutputFileNameOpt>();
    ExitOnErr(bitstream2yaml::tryBitstream2YAML());
    return 0;
  }

  if (Opts->isActive<&InstructionCountCmd>()) {
    auto &S = Opts->getSubOptions<&InstructionCountCmd>();
    InputFormat = S.get<&InputFormatOpt>();
    InputFileName = S.get<&InputFileNameOpt>();
    OutputFileName = S.get<&OutputFileNameOpt>();
    UseDebugLoc = S.get<&UseDebugLocOpt>();
    ExitOnErr(instructioncount::tryInstructionCount());
    return 0;
  }

  if (Opts->isActive<&AnnotationCountCmd>()) {
    auto &S = Opts->getSubOptions<&AnnotationCountCmd>();
    InputFormat = S.get<&InputFormatOpt>();
    AnnotationTypeToCollect = S.get<&AnnotationTypeOpt>();
    InputFileName = S.get<&InputFileNameOpt>();
    OutputFileName = S.get<&OutputFileNameOpt>();
    UseDebugLoc = S.get<&UseDebugLocOpt>();
    ExitOnErr(annotationcount::tryAnnotationCount());
    return 0;
  }

  if (Opts->isActive<&CountCmd>()) {
    auto &S = Opts->getSubOptions<&CountCmd>();
    InputFormat = S.get<&InputFormatOpt>();
    InputFileName = S.get<&InputFileNameOpt>();
    OutputFileName = S.get<&OutputFileNameOpt>();
    extractFilterGlobals(S);
    Keys = S.get<&CountKeysOpt>();
    RKeys = S.get<&CountRKeysOpt>();
    CountByVal = S.get<&CountByOptDesc>();
    GroupByVal = S.get<&GroupByOptDesc>();
    ExitOnErr(collectRemarks());
    return 0;
  }

  if (Opts->isActive<&FilterCmd>()) {
    auto &S = Opts->getSubOptions<&FilterCmd>();
    InputFormat = S.get<&InputFormatOpt>();
    OutputFormat = S.get<&OutputFormatOpt>();
    OutputFileName = S.get<&OutputFileNameOpt>();
    extractFilterGlobals(S);
    FilterInputFileNames = S.get<&FilterInputFilesOpt>();
    if (FilterInputFileNames.empty())
      FilterInputFileNames.push_back("-");
    ExcludeOptVal = S.get<&FilterExcludeOpt>();
    SortOptVal = S.get<&FilterSortOpt>();
    DedupeOptVal = S.get<&FilterDedupeOpt>();
    ExitOnErr(tryFilter());
    return 0;
  }

  if (Opts->isActive<&InstructionMixCmd>()) {
    auto &S = Opts->getSubOptions<&InstructionMixCmd>();
    InputFormat = S.get<&InputFormatOpt>();
    InputFileName = S.get<&InputFileNameOpt>();
    OutputFileName = S.get<&OutputFileNameOpt>();
    instructionmix::FunctionFilter = S.get<&IMixFunctionFilterOpt>();
    instructionmix::FunctionFilterRE = S.get<&IMixFunctionFilterREOpt>();
    instructionmix::ReportStyle = S.get<&IMixReportStyleOpt>();
    ExitOnErr(instructionmix::tryInstructionMix());
    return 0;
  }

  if (Opts->isActive<&SizeDiffCmd>()) {
    auto &S = Opts->getSubOptions<&SizeDiffCmd>();
    InputFormat = S.get<&InputFormatOpt>();
    InputFileNameA = S.get<&SDInputFileNameAOpt>();
    InputFileNameB = S.get<&SDInputFileNameBOpt>();
    OutputFileName = S.get<&OutputFileNameOpt>();
    SDReportStyle = S.get<&SDReportStyleOpt>();
    PrettyPrint = S.get<&SDPrettyPrintOpt>();
    ExitOnErr(trySizeDiff());
    return 0;
  }

  if (Opts->isActive<&SummaryCmd>()) {
    auto &S = Opts->getSubOptions<&SummaryCmd>();
    InputFormat = S.get<&InputFormatOpt>();
    OutputFormat = S.get<&OutputFormatOpt>();
    InputFileName = S.get<&InputFileNameOpt>();
    OutputFileName = S.get<&OutputFileNameOpt>();
    summary::KeepInputVal = S.get<&SummaryKeepOpt>();
    summary::IgnoreMalformedVal = S.get<&SummaryIgnoreMalformedOpt>();
    summary::EnableInlineSummaryVal = S.get<&SummaryInlineCalleesOpt>();
    summary::EnableInlineSummarySpecified =
        S.specified<&SummaryInlineCalleesOpt>();
    ExitOnErr(summary::trySummary());
    return 0;
  }

  ExitOnErr(make_error<StringError>(
      "Please specify a subcommand. (See -help for options)",
      inconvertibleErrorCode()));
  return 1;
}
