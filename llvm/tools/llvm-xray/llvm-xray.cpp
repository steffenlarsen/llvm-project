//===- llvm-xray.cpp: XRay Tool Main Program ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the main entry point for the suite of XRay tools. All
// additional functionality are implemented as subcommands.
//
//===----------------------------------------------------------------------===//
//
// Basic usage:
//
//   llvm-xray [options] <subcommand> [subcommand-specific options]
//
#include "xray-account.h"
#include "xray-converter.h"
#include "xray-graph.h"
#include "xray-stacks.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::xray;

// Forward declarations for handler functions in sub-files.
Error tryExtract();
Error tryFdrDump();
Error tryConvert();
Error tryAccount();
Error tryGraph();
Error tryGraphDiff();
Error tryStack();

// ============================================================================
// EnumVal arrays for enum options
// ============================================================================

static constexpr clv2::EnumVal<ConvertFormats> ConvertFormatsVals[] = {
    {"raw", ConvertFormats::BINARY, "output in binary"},
    {"yaml", ConvertFormats::YAML, "output in yaml"},
    {"trace_event", ConvertFormats::CHROME_TRACE_EVENT,
     "Output in chrome's trace event format. "
     "May be visualized with the Catapult trace viewer."},
};

static constexpr clv2::EnumVal<AccountOutputFormats>
    AccountOutputFormatsVals[] = {
        {"text", AccountOutputFormats::TEXT, "report stats in text"},
        {"csv", AccountOutputFormats::CSV, "report stats in csv"},
};

static constexpr clv2::EnumVal<SortField> SortFieldVals[] = {
    {"funcid", SortField::FUNCID, "function id"},
    {"count", SortField::COUNT, "function call counts"},
    {"min", SortField::MIN, "minimum function durations"},
    {"med", SortField::MED, "median function durations"},
    {"90p", SortField::PCT90, "90th percentile durations"},
    {"99p", SortField::PCT99, "99th percentile durations"},
    {"max", SortField::MAX, "maximum function durations"},
    {"sum", SortField::SUM, "sum of call durations"},
    {"func", SortField::FUNC, "function names"},
};

static constexpr clv2::EnumVal<SortDirection> SortDirectionVals[] = {
    {"asc", SortDirection::ASCENDING, "ascending"},
    {"dsc", SortDirection::DESCENDING, "descending"},
};

static constexpr clv2::EnumVal<GraphRenderer::StatType> StatTypeVals[] = {
    {"none", GraphRenderer::StatType::NONE, "Do not label"},
    {"count", GraphRenderer::StatType::COUNT, "function call counts"},
    {"min", GraphRenderer::StatType::MIN, "minimum function durations"},
    {"med", GraphRenderer::StatType::MED, "median function durations"},
    {"90p", GraphRenderer::StatType::PCT90, "90th percentile durations"},
    {"99p", GraphRenderer::StatType::PCT99, "99th percentile durations"},
    {"max", GraphRenderer::StatType::MAX, "maximum function durations"},
    {"sum", GraphRenderer::StatType::SUM, "sum of call durations"},
};

static constexpr clv2::EnumVal<StackOutputFormat> StackOutputFormatVals[] = {
    {"human", HUMAN, "Human readable output. Only valid without -all-stacks."},
    {"flame", FLAMETOOL,
     "Format consumable by Brendan Gregg's FlameGraph tool. "
     "Only valid with -all-stacks."},
};

static constexpr clv2::EnumVal<AggregationType> AggregationTypeVals[] = {
    {"time", AggregationType::TOTAL_TIME,
     "Capture the total time spent in an all invocations of a stack."},
    {"count", AggregationType::INVOCATION_COUNT,
     "Capture the number of times a stack was invoked. "
     "In flamegraph mode, this count also includes invocations of all "
     "callees."},
};

// ============================================================================
// Extract subcommand options
// ============================================================================

inline constexpr clv2::OptionInfo<std::string> ExtractInputOpt{
    "", "<input file>", clv2::Positional{}, clv2::Required};
inline constexpr clv2::OptionInfo<std::string> ExtractOutputOpt{
    "output", "output file; use '-' for stdout", clv2::Init{"-"}};
inline constexpr clv2::AliasInfo ExtractOutputAlias{"o", "output"};
inline constexpr clv2::OptionInfo<bool> ExtractSymbolizeOpt{
    "symbolize", "symbolize functions", clv2::Init{false}};
inline constexpr clv2::AliasInfo ExtractSymbolizeAlias{"s", "symbolize"};
inline constexpr clv2::OptionInfo<bool> ExtractDemangleOpt{
    "demangle", "demangle symbols (default)"};
inline constexpr clv2::OptionInfo<bool> ExtractNoDemangleOpt{
    "no-demangle", "don't demangle symbols"};

// ============================================================================
// FDR Dump subcommand options
// ============================================================================

inline constexpr clv2::OptionInfo<std::string> DumpInputOpt{
    "", "<xray fdr mode log>", clv2::Positional{}, clv2::Required};
inline constexpr clv2::OptionInfo<bool> DumpVerifyOpt{
    "verify", "verify structure of the log", clv2::Init{false}};

// ============================================================================
// Convert subcommand options
// ============================================================================

inline constexpr clv2::OptionInfo<std::string> ConvertInputOpt{
    "", "<xray log file>", clv2::Positional{}, clv2::Required};
inline constexpr auto ConvertOutputFormatOpt =
    clv2::makeEnumOption<ConvertFormats>("output-format", "output format",
                                         ConvertFormatsVals);
inline constexpr clv2::AliasInfo ConvertOutputFormatAlias{"f", "output-format"};
inline constexpr clv2::OptionInfo<std::string> ConvertOutputOpt{
    "output", "output file; use '-' for stdout", clv2::Init{"-"}};
inline constexpr clv2::AliasInfo ConvertOutputAlias{"o", "output"};
inline constexpr clv2::OptionInfo<bool> ConvertSymbolizeOpt{
    "symbolize", "symbolize function ids from the input log",
    clv2::Init{false}};
inline constexpr clv2::AliasInfo ConvertSymbolizeAlias{"y", "symbolize"};
inline constexpr clv2::OptionInfo<bool> ConvertNoDemangleOpt{
    "no-demangle",
    "determines whether to demangle function name "
    "when symbolizing function ids from the input log",
    clv2::Init{false}};
inline constexpr clv2::OptionInfo<bool> ConvertDemangleOpt{
    "demangle", "demangle symbols (default)"};
inline constexpr clv2::OptionInfo<std::string> ConvertInstrMapOpt{
    "instr_map",
    "binary with the instrumentation map, or a separate instrumentation map",
    clv2::Init{""}};
inline constexpr clv2::AliasInfo ConvertInstrMapAlias{"m", "instr_map"};
inline constexpr clv2::OptionInfo<bool> ConvertSortInputOpt{
    "sort", "determines whether to sort input log records by timestamp",
    clv2::Init{true}};
inline constexpr clv2::AliasInfo ConvertSortInputAlias{"s", "sort"};

// ============================================================================
// Account subcommand options
// ============================================================================

inline constexpr clv2::OptionInfo<std::string> AccountInputOpt{
    "", "<xray log file>", clv2::Positional{}, clv2::Required};
inline constexpr clv2::OptionInfo<bool> AccountKeepGoingOpt{
    "keep-going", "Keep going on errors encountered", clv2::Init{false}};
inline constexpr clv2::AliasInfo AccountKeepGoingAlias{"k", "keep-going"};
inline constexpr clv2::OptionInfo<bool> AccountRecursiveCallsOnlyOpt{
    "recursive-calls-only", "Only count the calls that are recursive",
    clv2::Init{false}};
inline constexpr clv2::OptionInfo<bool> AccountDeduceSiblingCallsOpt{
    "deduce-sibling-calls",
    "Deduce sibling calls when unrolling function call stacks",
    clv2::Init{false}};
inline constexpr clv2::AliasInfo AccountDeduceSiblingCallsAlias{
    "d", "deduce-sibling-calls"};
inline constexpr clv2::OptionInfo<std::string> AccountOutputOpt{
    "output", "output file; use '-' for stdout", clv2::Init{"-"}};
inline constexpr clv2::AliasInfo AccountOutputAlias{"o", "output"};
inline constexpr auto AccountOutputFormatOpt =
    clv2::makeEnumOption<AccountOutputFormats>("format", "output format",
                                               AccountOutputFormatsVals);
inline constexpr clv2::AliasInfo AccountOutputFormatAlias{"f", "format"};
inline constexpr auto AccountSortOutputOpt = clv2::makeEnumOption<SortField>(
    "sort", "sort output by this field", SortFieldVals,
    clv2::Init{SortField::FUNCID});
inline constexpr clv2::AliasInfo AccountSortOutputAlias{"s", "sort"};
inline constexpr auto AccountSortOrderOpt = clv2::makeEnumOption<SortDirection>(
    "sortorder", "sort ordering", SortDirectionVals,
    clv2::Init{SortDirection::ASCENDING});
inline constexpr clv2::AliasInfo AccountSortOrderAlias{"r", "sortorder"};
inline constexpr clv2::OptionInfo<int> AccountTopOpt{
    "top", "only show the top N results", clv2::Init{-1}};
inline constexpr clv2::AliasInfo AccountTopAlias{"p", "top"};
inline constexpr clv2::OptionInfo<std::string> AccountInstrMapOpt{
    "instr_map",
    "binary with the instrumentation map, or a separate instrumentation map",
    clv2::Init{""}};
inline constexpr clv2::AliasInfo AccountInstrMapAlias{"m", "instr_map"};

// ============================================================================
// Graph subcommand options
// ============================================================================

inline constexpr clv2::OptionInfo<std::string> GraphInputOpt{
    "", "<xray log file>", clv2::Positional{}, clv2::Required};
inline constexpr clv2::OptionInfo<bool> GraphKeepGoingOpt{
    "keep-going", "Keep going on errors encountered", clv2::Init{false}};
inline constexpr clv2::AliasInfo GraphKeepGoingAlias{"k", "keep-going"};
inline constexpr clv2::OptionInfo<std::string> GraphOutputOpt{
    "output", "output file; use '-' for stdout", clv2::Init{"-"}};
inline constexpr clv2::AliasInfo GraphOutputAlias{"o", "output"};
inline constexpr clv2::OptionInfo<std::string> GraphInstrMapOpt{
    "instr_map",
    "binary with the instrumrntation map, or a separate instrumentation map",
    clv2::Init{""}};
inline constexpr clv2::AliasInfo GraphInstrMapAlias{"m", "instr_map"};
inline constexpr clv2::OptionInfo<bool> GraphDeduceSiblingCallsOpt{
    "deduce-sibling-calls",
    "Deduce sibling calls when unrolling function call stacks",
    clv2::Init{false}};
inline constexpr clv2::AliasInfo GraphDeduceSiblingCallsAlias{
    "d", "deduce-sibling-calls"};
inline constexpr auto GraphEdgeLabelOpt =
    clv2::makeEnumOption<GraphRenderer::StatType>(
        "edge-label", "Output graphs with edges labeled with this field",
        StatTypeVals, clv2::Init{GraphRenderer::StatType::NONE});
inline constexpr clv2::AliasInfo GraphEdgeLabelAlias{"e", "edge-label"};
inline constexpr auto GraphVertexLabelOpt =
    clv2::makeEnumOption<GraphRenderer::StatType>(
        "vertex-label", "Output graphs with vertices labeled with this field",
        StatTypeVals, clv2::Init{GraphRenderer::StatType::NONE});
inline constexpr clv2::AliasInfo GraphVertexLabelAlias{"v", "vertex-label"};
inline constexpr auto GraphEdgeColorTypeOpt =
    clv2::makeEnumOption<GraphRenderer::StatType>(
        "color-edges",
        "Output graphs with edge colors determined by this field", StatTypeVals,
        clv2::Init{GraphRenderer::StatType::NONE});
inline constexpr clv2::AliasInfo GraphEdgeColorTypeAlias{"c", "color-edges"};
inline constexpr auto GraphVertexColorTypeOpt =
    clv2::makeEnumOption<GraphRenderer::StatType>(
        "color-vertices",
        "Output graphs with vertex colors determined by this field",
        StatTypeVals, clv2::Init{GraphRenderer::StatType::NONE});
inline constexpr clv2::AliasInfo GraphVertexColorTypeAlias{"b",
                                                           "color-vertices"};

// ============================================================================
// Graph-diff subcommand options
// ============================================================================

inline constexpr clv2::OptionInfo<std::string> GDInput1Opt{
    "", "<xray log file 1>", clv2::Positional{}, clv2::Required};
inline constexpr clv2::OptionInfo<std::string> GDInput2Opt{
    "", "<xray log file 2>", clv2::Positional{}, clv2::Required};
inline constexpr clv2::OptionInfo<bool> GDKeepGoingOpt{
    "keep-going", "Keep going on errors encountered", clv2::Init{false}};
inline constexpr clv2::AliasInfo GDKeepGoingAlias{"k", "keep-going"};
inline constexpr clv2::OptionInfo<bool> GDKeepGoing1Opt{
    "keep-going-1", "Keep going on errors encountered in trace 1",
    clv2::Init{false}};
inline constexpr clv2::AliasInfo GDKeepGoing1Alias{"k1", "keep-going-1"};
inline constexpr clv2::OptionInfo<bool> GDKeepGoing2Opt{
    "keep-going-2", "Keep going on errors encountered in trace 2",
    clv2::Init{false}};
inline constexpr clv2::AliasInfo GDKeepGoing2Alias{"k2", "keep-going-2"};
inline constexpr clv2::OptionInfo<std::string> GDInstrMapOpt{
    "instr-map",
    "binary with the instrumentation map, or a separate instrumentation map "
    "for graph",
    clv2::Init{""}};
inline constexpr clv2::AliasInfo GDInstrMapAlias{"m", "instr-map"};
inline constexpr clv2::OptionInfo<std::string> GDInstrMap1Opt{
    "instr-map-1",
    "binary with the instrumentation map, or a separate instrumentation map "
    "for graph 1",
    clv2::Init{""}};
inline constexpr clv2::AliasInfo GDInstrMap1Alias{"m1", "instr-map-1"};
inline constexpr clv2::OptionInfo<std::string> GDInstrMap2Opt{
    "instr-map-2",
    "binary with the instrumentation map, or a separate instrumentation map "
    "for graph 2",
    clv2::Init{""}};
inline constexpr clv2::AliasInfo GDInstrMap2Alias{"m2", "instr-map-2"};
inline constexpr clv2::OptionInfo<bool> GDDeduceSiblingCallsOpt{
    "deduce-sibling-calls",
    "Deduce sibling calls when unrolling function call stacks",
    clv2::Init{false}};
inline constexpr clv2::AliasInfo GDDeduceSiblingCallsAlias{
    "d", "deduce-sibling-calls"};
inline constexpr clv2::OptionInfo<bool> GDDeduceSiblingCalls1Opt{
    "deduce-sibling-calls-1",
    "Deduce sibling calls when unrolling function call stacks",
    clv2::Init{false}};
inline constexpr clv2::AliasInfo GDDeduceSiblingCalls1Alias{
    "d1", "deduce-sibling-calls-1"};
inline constexpr clv2::OptionInfo<bool> GDDeduceSiblingCalls2Opt{
    "deduce-sibling-calls-2",
    "Deduce sibling calls when unrolling function call stacks",
    clv2::Init{false}};
inline constexpr clv2::AliasInfo GDDeduceSiblingCalls2Alias{
    "d2", "deduce-sibling-calls-2"};
inline constexpr auto GDEdgeLabelOpt =
    clv2::makeEnumOption<GraphRenderer::StatType>(
        "edge-label", "Output graphs with edges labeled with this field",
        StatTypeVals, clv2::Init{GraphRenderer::StatType::NONE});
inline constexpr clv2::AliasInfo GDEdgeLabelAlias{"e", "edge-label"};
inline constexpr auto GDEdgeColorOpt =
    clv2::makeEnumOption<GraphRenderer::StatType>(
        "edge-color", "Output graphs with edges colored by this field",
        StatTypeVals, clv2::Init{GraphRenderer::StatType::NONE});
inline constexpr clv2::AliasInfo GDEdgeColorAlias{"c", "edge-color"};
inline constexpr auto GDVertexLabelOpt =
    clv2::makeEnumOption<GraphRenderer::StatType>(
        "vertex-label", "Output graphs with vertices labeled with this field",
        StatTypeVals, clv2::Init{GraphRenderer::StatType::NONE});
inline constexpr clv2::AliasInfo GDVertexLabelAlias{"v", "vertex-label"};
inline constexpr auto GDVertexColorOpt =
    clv2::makeEnumOption<GraphRenderer::StatType>(
        "vertex-color", "Output graphs with vertices colored by this field",
        StatTypeVals, clv2::Init{GraphRenderer::StatType::NONE});
inline constexpr clv2::AliasInfo GDVertexColorAlias{"b", "vertex-color"};
inline constexpr clv2::OptionInfo<int> GDVertexLabelTruncOpt{
    "vertex-label-trun", "What length to truncate vertex labels to ",
    clv2::Init{40}};
inline constexpr clv2::AliasInfo GDVertexLabelTruncAlias{"t",
                                                         "vertex-label-trun"};
inline constexpr clv2::OptionInfo<std::string> GDOutputOpt{
    "output", "output file; use '-' for stdout", clv2::Init{"-"}};
inline constexpr clv2::AliasInfo GDOutputAlias{"o", "output"};

// ============================================================================
// Stack subcommand options
// ============================================================================

inline constexpr clv2::ListOptionInfo<std::string> StackInputsOpt{
    "", "<xray trace>", clv2::Positional{}, clv2::OneOrMore};
inline constexpr clv2::OptionInfo<bool> StackKeepGoingOpt{
    "keep-going", "Keep going on errors encountered", clv2::Init{false}};
inline constexpr clv2::AliasInfo StackKeepGoingAlias{"k", "keep-going"};
inline constexpr clv2::OptionInfo<std::string> StacksInstrMapOpt{
    "instr_map",
    "instrumentation map used to identify function ids. "
    "Currently supports elf file instrumentation maps.",
    clv2::Init{""}};
inline constexpr clv2::AliasInfo StacksInstrMapAlias{"m", "instr_map"};
inline constexpr clv2::OptionInfo<bool> SeparateThreadStacksOpt{
    "per-thread-stacks", "Report top stacks within each thread id",
    clv2::Init{false}};
inline constexpr clv2::OptionInfo<bool> AggregateThreadsOpt{
    "aggregate-threads", "Aggregate stack times across threads",
    clv2::Init{false}};
inline constexpr clv2::OptionInfo<bool> DumpAllStacksOpt{
    "all-stacks",
    "Dump sum of timings for all stacks. "
    "By default separates stacks per-thread.",
    clv2::Init{false}};
inline constexpr clv2::AliasInfo DumpAllStacksAlias{"all", "all-stacks"};
inline constexpr auto StacksOutputFormatOpt =
    clv2::makeEnumOption<StackOutputFormat>(
        "stack-format",
        "The format that output stacks should be "
        "output in. Only applies with all-stacks.",
        StackOutputFormatVals, clv2::Init{HUMAN});
inline constexpr auto RequestedAggregationOpt =
    clv2::makeEnumOption<AggregationType>(
        "aggregation-type", "The type of aggregation to do on call stacks.",
        AggregationTypeVals, clv2::Init{AggregationType::TOTAL_TIME});

// ============================================================================
// SubCommandInfo declarations
// ============================================================================

inline constexpr clv2::SubCommandInfo<
    &ExtractInputOpt, &ExtractOutputOpt, &ExtractOutputAlias,
    &ExtractSymbolizeOpt, &ExtractSymbolizeAlias, &ExtractDemangleOpt,
    &ExtractNoDemangleOpt>
    ExtractCmd{"extract", "Extract instrumentation maps"};

inline constexpr clv2::SubCommandInfo<&DumpInputOpt, &DumpVerifyOpt> DumpCmd{
    "fdr-dump", "FDR Trace Dump"};

inline constexpr clv2::SubCommandInfo<
    &ConvertInputOpt, &ConvertOutputFormatOpt, &ConvertOutputFormatAlias,
    &ConvertOutputOpt, &ConvertOutputAlias, &ConvertSymbolizeOpt,
    &ConvertSymbolizeAlias, &ConvertNoDemangleOpt, &ConvertDemangleOpt,
    &ConvertInstrMapOpt, &ConvertInstrMapAlias, &ConvertSortInputOpt,
    &ConvertSortInputAlias>
    ConvertCmd{"convert", "Trace Format Conversion"};

inline constexpr clv2::SubCommandInfo<
    &AccountInputOpt, &AccountKeepGoingOpt, &AccountKeepGoingAlias,
    &AccountRecursiveCallsOnlyOpt, &AccountDeduceSiblingCallsOpt,
    &AccountDeduceSiblingCallsAlias, &AccountOutputOpt, &AccountOutputAlias,
    &AccountOutputFormatOpt, &AccountOutputFormatAlias, &AccountSortOutputOpt,
    &AccountSortOutputAlias, &AccountSortOrderOpt, &AccountSortOrderAlias,
    &AccountTopOpt, &AccountTopAlias, &AccountInstrMapOpt,
    &AccountInstrMapAlias>
    AccountCmd{"account", "Function call accounting"};

inline constexpr clv2::SubCommandInfo<
    &GraphInputOpt, &GraphKeepGoingOpt, &GraphKeepGoingAlias, &GraphOutputOpt,
    &GraphOutputAlias, &GraphInstrMapOpt, &GraphInstrMapAlias,
    &GraphDeduceSiblingCallsOpt, &GraphDeduceSiblingCallsAlias,
    &GraphEdgeLabelOpt, &GraphEdgeLabelAlias, &GraphVertexLabelOpt,
    &GraphVertexLabelAlias, &GraphEdgeColorTypeOpt, &GraphEdgeColorTypeAlias,
    &GraphVertexColorTypeOpt, &GraphVertexColorTypeAlias>
    GraphCmd{"graph", "Generate function-call graph"};

inline constexpr clv2::SubCommandInfo<
    &GDInput1Opt, &GDInput2Opt, &GDKeepGoingOpt, &GDKeepGoingAlias,
    &GDKeepGoing1Opt, &GDKeepGoing1Alias, &GDKeepGoing2Opt, &GDKeepGoing2Alias,
    &GDInstrMapOpt, &GDInstrMapAlias, &GDInstrMap1Opt, &GDInstrMap1Alias,
    &GDInstrMap2Opt, &GDInstrMap2Alias, &GDDeduceSiblingCallsOpt,
    &GDDeduceSiblingCallsAlias, &GDDeduceSiblingCalls1Opt,
    &GDDeduceSiblingCalls1Alias, &GDDeduceSiblingCalls2Opt,
    &GDDeduceSiblingCalls2Alias, &GDEdgeLabelOpt, &GDEdgeLabelAlias,
    &GDEdgeColorOpt, &GDEdgeColorAlias, &GDVertexLabelOpt, &GDVertexLabelAlias,
    &GDVertexColorOpt, &GDVertexColorAlias, &GDVertexLabelTruncOpt,
    &GDVertexLabelTruncAlias, &GDOutputOpt, &GDOutputAlias>
    GraphDiffCmd{"graph-diff", "Generate diff of function-call graphs"};

inline constexpr clv2::SubCommandInfo<
    &StackInputsOpt, &StackKeepGoingOpt, &StackKeepGoingAlias,
    &StacksInstrMapOpt, &StacksInstrMapAlias, &SeparateThreadStacksOpt,
    &AggregateThreadsOpt, &DumpAllStacksOpt, &DumpAllStacksAlias,
    &StacksOutputFormatOpt, &RequestedAggregationOpt>
    StackCmd{"stack", "Call stack accounting"};

// ============================================================================
// OptionsRegistry
// ============================================================================

static constexpr clv2::OptionsRegistry<&ExtractCmd, &DumpCmd, &ConvertCmd,
                                       &AccountCmd, &GraphCmd, &GraphDiffCmd,
                                       &StackCmd>
    XRayToolReg;

// ============================================================================
// Extern declarations for globals in sub-files
// ============================================================================

// xray-extract.cpp
extern std::string ExtractInputVal;
extern std::string ExtractOutputVal;
extern bool ExtractSymbolizeVal;
extern bool ExtractNoDemangleVal;

// xray-fdr-dump.cpp
extern std::string DumpInputVal;
extern bool DumpVerifyVal;

// xray-converter.cpp
extern std::string ConvertInputVal;
extern ConvertFormats ConvertOutputFormatVal;
extern std::string ConvertOutputVal;
extern bool ConvertSymbolizeVal;
extern bool ConvertNoDemangleVal;
extern std::string ConvertInstrMapVal;
extern bool ConvertSortInputVal;

// xray-account.cpp
extern std::string AccountInputVal;
extern bool AccountKeepGoingVal;
extern bool AccountRecursiveCallsOnlyVal;
extern bool AccountDeduceSiblingCallsVal;
extern std::string AccountOutputVal;
extern AccountOutputFormats AccountOutputFormatVal;
extern SortField AccountSortOutputVal;
extern SortDirection AccountSortOrderVal;
extern int AccountTopVal;
extern std::string AccountInstrMapVal;

// xray-graph.cpp
extern std::string GraphInputVal;
extern bool GraphKeepGoingVal;
extern std::string GraphOutputVal;
extern std::string GraphInstrMapVal;
extern bool GraphDeduceSiblingCallsVal;
extern GraphRenderer::StatType GraphEdgeLabelVal;
extern GraphRenderer::StatType GraphVertexLabelVal;
extern GraphRenderer::StatType GraphEdgeColorTypeVal;
extern GraphRenderer::StatType GraphVertexColorTypeVal;

// xray-graph-diff.cpp
extern std::string GraphDiffInput1Val;
extern std::string GraphDiffInput2Val;
extern bool GraphDiffKeepGoing1Val;
extern bool GraphDiffKeepGoing2Val;
extern std::string GraphDiffInstrMap1Val;
extern std::string GraphDiffInstrMap2Val;
extern bool GraphDiffDeduceSiblingCalls1Val;
extern bool GraphDiffDeduceSiblingCalls2Val;
extern GraphRenderer::StatType GraphDiffEdgeLabelVal;
extern GraphRenderer::StatType GraphDiffEdgeColorVal;
extern GraphRenderer::StatType GraphDiffVertexLabelVal;
extern GraphRenderer::StatType GraphDiffVertexColorVal;
extern int GraphDiffVertexLabelTruncVal;
extern std::string GraphDiffOutputVal;

// xray-stacks.cpp
extern std::vector<std::string> StackInputsVal;
extern bool StackKeepGoingVal;
extern std::string StacksInstrMapVal;
extern bool SeparateThreadStacksVal;
extern bool AggregateThreadsVal;
extern bool DumpAllStacksVal;
extern StackOutputFormat StacksOutputFormatVal;
extern AggregationType RequestedAggregationVal;

// ============================================================================
// main
// ============================================================================

int main(int argc, char *argv[]) {
  clv2::OptionParser P;
  P.add<&XRayToolReg>();
  RegisterCoreLLVMOptions(P);
  P.showOptions({"disable-auto-upgrade-debug-info", "disable-i2p-p2i-opt",
                 "elide-all-zero-branch-weights"});
  auto OptsCtx = P.parse(argc, argv,
                         "XRay Tools\n\n"
                         "  This program consolidates multiple XRay trace "
                         "processing tools for convenient access.\n");
  auto *Opts = OptsCtx->getViewPtr<&XRayToolReg>();

  if (Opts->isActive<&ExtractCmd>()) {
    auto &S = Opts->getSubOptions<&ExtractCmd>();
    ExtractInputVal = S.get<&ExtractInputOpt>();
    ExtractOutputVal = S.get<&ExtractOutputOpt>();
    ExtractSymbolizeVal = S.get<&ExtractSymbolizeOpt>();
    ExtractNoDemangleVal =
        S.position<&ExtractNoDemangleOpt>() > S.position<&ExtractDemangleOpt>();
    ExitOnError("llvm-xray: ")(tryExtract());
    return 0;
  }

  if (Opts->isActive<&DumpCmd>()) {
    auto &S = Opts->getSubOptions<&DumpCmd>();
    DumpInputVal = S.get<&DumpInputOpt>();
    DumpVerifyVal = S.get<&DumpVerifyOpt>();
    ExitOnError("llvm-xray: ")(tryFdrDump());
    return 0;
  }

  if (Opts->isActive<&ConvertCmd>()) {
    auto &S = Opts->getSubOptions<&ConvertCmd>();
    ConvertInputVal = S.get<&ConvertInputOpt>();
    ConvertOutputFormatVal = S.get<&ConvertOutputFormatOpt>();
    ConvertOutputVal = S.get<&ConvertOutputOpt>();
    ConvertSymbolizeVal = S.get<&ConvertSymbolizeOpt>();
    ConvertNoDemangleVal =
        S.position<&ConvertNoDemangleOpt>() > S.position<&ConvertDemangleOpt>();
    ConvertInstrMapVal = S.get<&ConvertInstrMapOpt>();
    ConvertSortInputVal = S.get<&ConvertSortInputOpt>();
    ExitOnError("llvm-xray: ")(tryConvert());
    return 0;
  }

  if (Opts->isActive<&AccountCmd>()) {
    auto &S = Opts->getSubOptions<&AccountCmd>();
    AccountInputVal = S.get<&AccountInputOpt>();
    AccountKeepGoingVal = S.get<&AccountKeepGoingOpt>();
    AccountRecursiveCallsOnlyVal = S.get<&AccountRecursiveCallsOnlyOpt>();
    AccountDeduceSiblingCallsVal = S.get<&AccountDeduceSiblingCallsOpt>();
    AccountOutputVal = S.get<&AccountOutputOpt>();
    AccountOutputFormatVal = S.get<&AccountOutputFormatOpt>();
    AccountSortOutputVal = S.get<&AccountSortOutputOpt>();
    AccountSortOrderVal = S.get<&AccountSortOrderOpt>();
    AccountTopVal = S.get<&AccountTopOpt>();
    AccountInstrMapVal = S.get<&AccountInstrMapOpt>();
    ExitOnError("llvm-xray: ")(tryAccount());
    return 0;
  }

  if (Opts->isActive<&GraphCmd>()) {
    auto &S = Opts->getSubOptions<&GraphCmd>();
    GraphInputVal = S.get<&GraphInputOpt>();
    GraphKeepGoingVal = S.get<&GraphKeepGoingOpt>();
    GraphOutputVal = S.get<&GraphOutputOpt>();
    GraphInstrMapVal = S.get<&GraphInstrMapOpt>();
    GraphDeduceSiblingCallsVal = S.get<&GraphDeduceSiblingCallsOpt>();
    GraphEdgeLabelVal = S.get<&GraphEdgeLabelOpt>();
    GraphVertexLabelVal = S.get<&GraphVertexLabelOpt>();
    GraphEdgeColorTypeVal = S.get<&GraphEdgeColorTypeOpt>();
    GraphVertexColorTypeVal = S.get<&GraphVertexColorTypeOpt>();
    ExitOnError("llvm-xray: ")(tryGraph());
    return 0;
  }

  if (Opts->isActive<&GraphDiffCmd>()) {
    auto &S = Opts->getSubOptions<&GraphDiffCmd>();
    GraphDiffInput1Val = S.get<&GDInput1Opt>();
    GraphDiffInput2Val = S.get<&GDInput2Opt>();
    // For the numbered variants, use the specific value if specified,
    // otherwise fall back to the general value.
    auto KeepGoing = S.get<&GDKeepGoingOpt>();
    GraphDiffKeepGoing1Val =
        S.specified<&GDKeepGoing1Opt>() ? S.get<&GDKeepGoing1Opt>() : KeepGoing;
    GraphDiffKeepGoing2Val =
        S.specified<&GDKeepGoing2Opt>() ? S.get<&GDKeepGoing2Opt>() : KeepGoing;
    auto InstrMap = S.get<&GDInstrMapOpt>();
    GraphDiffInstrMap1Val =
        S.specified<&GDInstrMap1Opt>() ? S.get<&GDInstrMap1Opt>() : InstrMap;
    GraphDiffInstrMap2Val =
        S.specified<&GDInstrMap2Opt>() ? S.get<&GDInstrMap2Opt>() : InstrMap;
    auto DeduceSibCalls = S.get<&GDDeduceSiblingCallsOpt>();
    GraphDiffDeduceSiblingCalls1Val = S.specified<&GDDeduceSiblingCalls1Opt>()
                                          ? S.get<&GDDeduceSiblingCalls1Opt>()
                                          : DeduceSibCalls;
    GraphDiffDeduceSiblingCalls2Val = S.specified<&GDDeduceSiblingCalls2Opt>()
                                          ? S.get<&GDDeduceSiblingCalls2Opt>()
                                          : DeduceSibCalls;
    GraphDiffEdgeLabelVal = S.get<&GDEdgeLabelOpt>();
    GraphDiffEdgeColorVal = S.get<&GDEdgeColorOpt>();
    GraphDiffVertexLabelVal = S.get<&GDVertexLabelOpt>();
    GraphDiffVertexColorVal = S.get<&GDVertexColorOpt>();
    GraphDiffVertexLabelTruncVal = S.get<&GDVertexLabelTruncOpt>();
    GraphDiffOutputVal = S.get<&GDOutputOpt>();
    ExitOnError("llvm-xray: ")(tryGraphDiff());
    return 0;
  }

  if (Opts->isActive<&StackCmd>()) {
    auto &S = Opts->getSubOptions<&StackCmd>();
    StackInputsVal = S.get<&StackInputsOpt>();
    StackKeepGoingVal = S.get<&StackKeepGoingOpt>();
    StacksInstrMapVal = S.get<&StacksInstrMapOpt>();
    SeparateThreadStacksVal = S.get<&SeparateThreadStacksOpt>();
    AggregateThreadsVal = S.get<&AggregateThreadsOpt>();
    DumpAllStacksVal = S.get<&DumpAllStacksOpt>();
    StacksOutputFormatVal = S.get<&StacksOutputFormatOpt>();
    RequestedAggregationVal = S.get<&RequestedAggregationOpt>();
    ExitOnError("llvm-xray: ")(tryStack());
    return 0;
  }

  outs() << "OVERVIEW: XRay Tools\n\n"
            "  This program consolidates multiple XRay trace "
            "processing tools for convenient access.\n";
  return 0;
}
