//===-- Options.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines command line options used by llvm-debuginfo-analyzer.
//
//===----------------------------------------------------------------------===//

#ifndef OPTIONS_H
#define OPTIONS_H

#include "llvm/DebugInfo/LogicalView/Core/LVLine.h"
#include "llvm/DebugInfo/LogicalView/Core/LVOptions.h"
#include "llvm/DebugInfo/LogicalView/Core/LVScope.h"
#include "llvm/DebugInfo/LogicalView/Core/LVSymbol.h"
#include "llvm/DebugInfo/LogicalView/Core/LVType.h"
#include "llvm/Support/CommandLineV2.h"

namespace llvm {
namespace logicalview {
namespace cmdline {

//===----------------------------------------------------------------------===//
// Option categories
//===----------------------------------------------------------------------===//
inline constexpr clv2::OptionCategory
    AttributeCategory("Attribute Options",
                      "These control extra attributes that are "
                      "added when the element is printed.");
inline constexpr clv2::OptionCategory
    CompareCategory("Compare Options", "These control the view comparison.");
inline constexpr clv2::OptionCategory
    OutputCategory("Output Options", "These control the output generated.");
inline constexpr clv2::OptionCategory
    PrintCategory("Print Options", "These control which elements are printed.");
inline constexpr clv2::OptionCategory
    ReportCategory("Report Options",
                   "These control how the elements are printed.");
inline constexpr clv2::OptionCategory
    SelectCategory("Select Options",
                   "These control which elements are selected.");
inline constexpr clv2::OptionCategory
    WarningCategory("Warning Options", "These control the generated warnings.");
inline constexpr clv2::OptionCategory
    InternalCategory("Internal Options",
                     "Internal traces and extra debugging code.");

//===----------------------------------------------------------------------===//
// EnumVal tables (defined in Options.cpp)
//===----------------------------------------------------------------------===//
extern const clv2::EnumVal<LVAttributeKind> AttributeKindVals[];
extern const clv2::EnumVal<LVCompareKind> CompareKindVals[];
extern const clv2::EnumVal<LVOutputKind> OutputKindVals[];
extern const clv2::EnumVal<LVSortMode> SortModeVals[];
extern const clv2::EnumVal<LVPrintKind> PrintKindVals[];
extern const clv2::EnumVal<LVReportKind> ReportKindVals[];
extern const clv2::EnumVal<LVElementKind> ElementKindVals[];
extern const clv2::EnumVal<LVLineKind> LineKindVals[];
extern const clv2::EnumVal<LVScopeKind> ScopeKindVals[];
extern const clv2::EnumVal<LVSymbolKind> SymbolKindVals[];
extern const clv2::EnumVal<LVTypeKind> TypeKindVals[];
extern const clv2::EnumVal<LVWarningKind> WarningKindVals[];
extern const clv2::EnumVal<LVInternalKind> InternalKindVals[];

// Sizes of enum tables (defined in Options.cpp)
inline constexpr std::size_t NumAttributeKindVals = 37;
inline constexpr std::size_t NumCompareKindVals = 5;
inline constexpr std::size_t NumOutputKindVals = 4;
inline constexpr std::size_t NumSortModeVals = 6;
inline constexpr std::size_t NumPrintKindVals = 10;
inline constexpr std::size_t NumReportKindVals = 5;
inline constexpr std::size_t NumElementKindVals = 3;
inline constexpr std::size_t NumLineKindVals = 10;
inline constexpr std::size_t NumScopeKindVals = 24;
inline constexpr std::size_t NumSymbolKindVals = 7;
inline constexpr std::size_t NumTypeKindVals = 19;
inline constexpr std::size_t NumWarningKindVals = 5;
inline constexpr std::size_t NumInternalKindVals = 6;

//===----------------------------------------------------------------------===//
// Option descriptors
//===----------------------------------------------------------------------===//

// Input/Output
inline constexpr clv2::ListOptionInfo<std::string> InputFilenamesOpt{
    "", "<input object files or .dSYM bundles>", clv2::Positional{}};
inline constexpr clv2::OptionInfo<std::string> OutputFilenameOpt{
    "output-file",
    "Redirect output to the specified file.",
    clv2::cat(OutputCategory),
    clv2::Hidden,
    clv2::value_desc("filename"),
    clv2::Init<const char *>{"-"}};

// Attribute options
inline constexpr clv2::ListOptionInfo<LVAttributeKind> AttributeOptionsOpt{
    "attribute",
    "Element attributes.",
    clv2::ValuesRef<LVAttributeKind>(AttributeKindVals, NumAttributeKindVals),
    clv2::cat(AttributeCategory),
    clv2::Hidden,
    clv2::CommaSeparated};

// Compare options
inline constexpr clv2::OptionInfo<bool> CompareContextOpt{
    "compare-context", "Add the view as compare context.",
    clv2::cat(CompareCategory), clv2::Hidden};
inline constexpr clv2::ListOptionInfo<LVCompareKind> CompareElementsOpt{
    "compare",
    "Elements to compare.",
    clv2::ValuesRef<LVCompareKind>(CompareKindVals, NumCompareKindVals),
    clv2::cat(CompareCategory),
    clv2::Hidden,
    clv2::CommaSeparated};

// Output options
inline constexpr clv2::OptionInfo<std::string> OutputFolderOpt{
    "output-folder", "Folder name for view splitting.",
    clv2::cat(OutputCategory), clv2::value_desc("pathname"), clv2::Hidden};
inline constexpr clv2::OptionInfo<unsigned> OutputLevelOpt{
    "output-level",
    "Only print to a depth of N elements.",
    clv2::cat(OutputCategory),
    clv2::value_desc("N"),
    clv2::Hidden,
    clv2::Init{~0u}};
inline constexpr clv2::ListOptionInfo<LVOutputKind> OutputOptionsOpt{
    "output",
    "Outputs for view.",
    clv2::ValuesRef<LVOutputKind>(OutputKindVals, NumOutputKindVals),
    clv2::cat(OutputCategory),
    clv2::Hidden,
    clv2::CommaSeparated};
inline constexpr clv2::OptionInfo<LVSortMode> OutputSortOpt{
    "output-sort",
    "Primary key when ordering logical view (default: line).",
    clv2::ValuesRef<LVSortMode>(SortModeVals, NumSortModeVals),
    clv2::cat(OutputCategory),
    clv2::Hidden,
    clv2::Init{LVSortMode::Line}};

// Print options
inline constexpr clv2::ListOptionInfo<LVPrintKind> PrintOptionsOpt{
    "print", "Element to print.",
    clv2::ValuesRef<LVPrintKind>(PrintKindVals, NumPrintKindVals),
    clv2::cat(PrintCategory), clv2::CommaSeparated};

// Report options
inline constexpr clv2::ListOptionInfo<LVReportKind> ReportOptionsOpt{
    "report",
    "Reports layout used for print, compare and select.",
    clv2::ValuesRef<LVReportKind>(ReportKindVals, NumReportKindVals),
    clv2::cat(ReportCategory),
    clv2::Hidden,
    clv2::CommaSeparated};

// Select options
inline constexpr clv2::OptionInfo<bool> SelectIgnoreCaseOpt{
    "select-nocase", "Ignore case distinctions when searching.",
    clv2::cat(SelectCategory), clv2::Hidden};
inline constexpr clv2::OptionInfo<bool> SelectUseRegexOpt{
    "select-regex",
    "Treat any <pattern> strings as regular expressions when "
    "selecting instead of just as an exact string match.",
    clv2::cat(SelectCategory), clv2::Hidden};
inline constexpr clv2::ListOptionInfo<std::string> SelectPatternsOpt{
    "select",
    "Search elements matching the given pattern.",
    clv2::cat(SelectCategory),
    clv2::Hidden,
    clv2::value_desc("pattern"),
    clv2::CommaSeparated};
inline constexpr clv2::ListOptionInfo<uint64_t> SelectOffsetsOpt{
    "select-offsets", "Offset element to print.", clv2::cat(SelectCategory),
    clv2::Hidden,     clv2::value_desc("offset"), clv2::CommaSeparated};
inline constexpr clv2::ListOptionInfo<LVElementKind> SelectElementsOpt{
    "select-elements",
    "Conditions to use when printing elements.",
    clv2::ValuesRef<LVElementKind>(ElementKindVals, NumElementKindVals),
    clv2::cat(SelectCategory),
    clv2::Hidden,
    clv2::CommaSeparated};
inline constexpr clv2::ListOptionInfo<LVLineKind> SelectLinesOpt{
    "select-lines",
    "Line kind to use when printing lines.",
    clv2::ValuesRef<LVLineKind>(LineKindVals, NumLineKindVals),
    clv2::cat(SelectCategory),
    clv2::Hidden,
    clv2::CommaSeparated};
inline constexpr clv2::ListOptionInfo<LVScopeKind> SelectScopesOpt{
    "select-scopes",
    "Scope kind to use when printing scopes.",
    clv2::ValuesRef<LVScopeKind>(ScopeKindVals, NumScopeKindVals),
    clv2::cat(SelectCategory),
    clv2::Hidden,
    clv2::CommaSeparated};
inline constexpr clv2::ListOptionInfo<LVSymbolKind> SelectSymbolsOpt{
    "select-symbols",
    "Symbol kind to use when printing symbols.",
    clv2::ValuesRef<LVSymbolKind>(SymbolKindVals, NumSymbolKindVals),
    clv2::cat(SelectCategory),
    clv2::Hidden,
    clv2::CommaSeparated};
inline constexpr clv2::ListOptionInfo<LVTypeKind> SelectTypesOpt{
    "select-types",
    "Type kind to use when printing types.",
    clv2::ValuesRef<LVTypeKind>(TypeKindVals, NumTypeKindVals),
    clv2::cat(SelectCategory),
    clv2::Hidden,
    clv2::CommaSeparated};

// Warning options
inline constexpr clv2::ListOptionInfo<LVWarningKind> WarningOptionsOpt{
    "warning",
    "Warnings to generate.",
    clv2::ValuesRef<LVWarningKind>(WarningKindVals, NumWarningKindVals),
    clv2::cat(WarningCategory),
    clv2::Hidden,
    clv2::CommaSeparated};

// Internal options
inline constexpr clv2::ListOptionInfo<LVInternalKind> InternalOptionsOpt{
    "internal",
    "Traces to enable.",
    clv2::ValuesRef<LVInternalKind>(InternalKindVals, NumInternalKindVals),
    clv2::cat(InternalCategory),
    clv2::Hidden,
    clv2::CommaSeparated};

extern LVOptions ReaderOptions;

// Perform any additional post parse command line actions. Propagate the
// values captured by the command line parser, into the generic reader.
void propagateOptions();

} // namespace cmdline
} // namespace logicalview
} // namespace llvm

#endif // OPTIONS_H
