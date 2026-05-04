//===- SupportOptionsOptInfos.h - clv2 OptionInfo decls for Support ------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// clv2 OptionInfo declarations for LLVMSupport library command-line flags.
//
// Declared here so tools can name them when building their option
// registries.
//
// Usage:
//   #include "llvm/Support/SupportOptionsOptInfos.h"
//   // Include SUP_* names in your OptionsRegistry<...> and populate a
//   // support::ParsedValues struct from Opts.get<&SUP_...>().
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SUPPORTOPTIONSOPTINFOS_H
#define LLVM_SUPPORT_SUPPORTOPTIONSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include <optional>

namespace llvm::clv2 {

// --- Statistic.cpp ---

inline constexpr OptionInfo<bool> SUP_Stats{
    "stats", "Enable statistics output from program (available with Asserts)",
    Init{false}, Hidden};

inline constexpr OptionInfo<bool> SUP_StatsJson{
    "stats-json", "Display statistics as json data", Init{false}, Hidden};

// --- Timer.cpp ---

inline constexpr OptionInfo<std::string> SUP_InfoOutputFile{
    "info-output-file", "File to append -stats and -timer output to",
    value_desc("filename"), Hidden};

inline constexpr OptionInfo<bool> SUP_TrackMemory{
    "track-memory", "Enable -time-passes memory tracking (this may be slow)",
    Init{false}, Hidden};

inline constexpr OptionInfo<bool> SUP_SortTimers{
    "sort-timers",
    "In the report, sort the timers in each group in wall clock time order",
    Init{true}, Hidden};

// --- Signals.cpp ---

inline constexpr OptionInfo<bool> SUP_DisableSymbolication{
    "disable-symbolication", "Disable symbolizing crash backtraces.",
    Init{false}, Hidden};

inline constexpr OptionInfo<std::string> SUP_CrashDiagnosticsDir{
    "crash-diagnostics-dir", "Directory for crash diagnostic files.",
    value_desc("directory"), Hidden};

// --- RandomNumberGenerator.cpp ---

inline constexpr OptionInfo<uint64_t> SUP_RngSeed{
    "rng-seed", "Seed for the random number generator", value_desc("seed"),
    Init{uint64_t{0}}, Hidden};

// --- GraphWriter.cpp ---

inline constexpr OptionInfo<bool> SUP_ViewBackground{
    "view-background",
    "Execute graph viewer in the background. Creates tmp file litter.",
    Init{false}, Hidden};

inline constexpr OptionInfo<std::string> SUP_DagFileLocation{
    "dag-file-location",
    "Location to place the DAG graphs selected to be viewed", Hidden};

inline constexpr OptionInfo<bool> SUP_NoOpenDagViewer{
    "no-open-dag-viewer",
    "Don't open the DAG viewer program, just write the file", Init{false},
    Hidden};

// --- Debug.cpp (only meaningful in non-NDEBUG builds) ---

inline constexpr OptionInfo<bool> SUP_Debug{"debug", "Enable debug output",
                                            Init{false}, Hidden};

inline constexpr OptionInfo<unsigned> SUP_DebugBufferSize{
    "debug-buffer-size",
    "Buffer the last N characters of debug output until program termination. "
    "[default 0 -- immediate print-out]",
    Init{0u}, Hidden};

inline constexpr ListOptionInfo<std::string> SUP_DebugOnly{
    "debug-only",
    "Enable a specific type of debug output (comma separated list of types "
    "using the format \"type[:level]\", where the level is an optional "
    "integer. The level can be set to 1, 2, 3, etc. to control the verbosity "
    "of the output. Setting a debug-type level to zero acts as an opt-out for "
    "this specific debug-type without affecting the others.",
    value_desc("debug string"), Hidden, CommaSeparated};

// --- DebugCounter.cpp ---

inline constexpr ListOptionInfo<std::string> SUP_DebugCounter{
    "debug-counter", "Comma separated list of debug counter skip and count",
    value_desc("value"), Hidden, CommaSeparated};

inline constexpr OptionInfo<bool> SUP_PrintDebugCounter{
    "print-debug-counter",
    "Print out debug counter info after all counters accumulated", Init{false},
    Hidden};

inline constexpr OptionInfo<bool> SUP_PrintDebugCounterQueries{
    "print-debug-counter-queries",
    "Print out information about counter queries", Init{false}, Hidden};

inline constexpr OptionInfo<bool> SUP_BreakOnLastCount{
    "debug-counter-break-on-last",
    "Insert a break point on the last enabled count of a chunks list",
    Init{false}, Hidden};

// --- WithColor.cpp ---

inline constexpr OptionCategory ColorOptionsCategory{"Color Options"};
inline constexpr OptionInfo<cl::boolOrDefault> SUP_Color{
    "color", "Use colors in output (default=autodetect)",
    cat(ColorOptionsCategory)};

// --- Registry grouping all Support options ---

inline constexpr OptionsRegistry<
    &SUP_Stats, &SUP_StatsJson, &SUP_InfoOutputFile, &SUP_TrackMemory,
    &SUP_SortTimers, &SUP_DisableSymbolication, &SUP_CrashDiagnosticsDir,
    &SUP_RngSeed, &SUP_ViewBackground, &SUP_DagFileLocation,
    &SUP_NoOpenDagViewer, &SUP_Debug, &SUP_DebugBufferSize, &SUP_DebugOnly,
    &SUP_DebugCounter, &SUP_PrintDebugCounter, &SUP_PrintDebugCounterQueries,
    &SUP_BreakOnLastCount>
    SupportOptsReg;

} // namespace llvm::clv2

#endif // LLVM_SUPPORT_SUPPORTOPTIONSOPTINFOS_H
