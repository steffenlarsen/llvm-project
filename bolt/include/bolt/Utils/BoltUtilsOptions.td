//===- BoltUtilsOptions.td - CLV2 option defs ------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "bolt/Utils/BoltCategories.td"

def BoltUtilsOptsReg : Registry<"llvm::bolt::bolt_utils_opts"> {
  // Getter bodies are compiled once, in BoltUtilsOptionsGetters.cpp.
  let OutOfLineGetters = 1;
  let ContextTypes = [CTX_BinaryContext];
}

def BOLT_BinaryAnalysisMode : Option<
    "binary-analysis-mode",
    "Enable binary analysis mode (set automatically by llvm-bolt-binary-analysis)",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== Enum types =====

def SFS_Profile2 : EnumValue<"profile2",
    "split each function into a hot and cold fragment using profiling "
    "information"> {
  let CppName = "Profile2";
}
def SFS_CDSplit : EnumValue<"cdsplit",
    "split each function into a hot, warm, and cold fragment using "
    "profiling information"> {
  let CppName = "CDSplit";
}
def SFS_Random2 : EnumValue<"random2",
    "split each function into a hot and cold fragment at a randomly "
    "chosen split point (ignoring any available profiling information)"> {
  let CppName = "Random2";
}
def SFS_RandomN : EnumValue<"randomN",
    "split each function into N fragments at a randomly chosen split "
    "points (ignoring any available profiling information)"> {
  let CppName = "RandomN";
}
def SFS_All : EnumValue<"all",
    "split all basic blocks of each function into fragments such that "
    "each fragment contains exactly a single basic block"> {
  let CppName = "All";
}

def SplitFunctionsStrategy : EnumType<"opts::SplitFunctionsStrategy",
    [SFS_Profile2, SFS_CDSplit, SFS_Random2, SFS_RandomN, SFS_All]> {
  let External = 1;
}

def HM_None      : EnumValue<"none", "disable heatmap"> {
  let CppName = "HM_None";
}
def HM_Optional  : EnumValue<"optional", "optional heatmap"> {
  let CppName = "HM_Optional";
}
def HM_Exclusive : EnumValue<"exclusive", "exclusive heatmap"> {
  let CppName = "HM_Exclusive";
}

def HeatmapModeKind : EnumType<"opts::HeatmapModeKind",
    [HM_None, HM_Optional, HM_Exclusive]> {
  let External = 1;
}

def PF_Fdata      : EnumValue<"fdata", "offset-based plaintext format"> {
  let CppName = "PF_Fdata";
}
def PF_YAML       : EnumValue<"yaml", "dense YAML representation"> {
  let CppName = "PF_YAML";
}
def PF_PreAgg     : EnumValue<"preagg", "pre-aggregated profile format"> {
  let CppName = "PF_PreAgg";
}
def PF_PerfScript : EnumValue<"perfscript", "perfscript profile format"> {
  let CppName = "PF_PerfScript";
}

def ProfileFormatKind : EnumType<"opts::ProfileFormatKind",
    [PF_Fdata, PF_YAML, PF_PreAgg, PF_PerfScript]> {
  let External = 1;
}

// ===== Options =====

def BOLT_AlignText : Option<
    "align-text",
    "alignment of .text section",
    "unsigned"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "0u";
  let Hidden = 1;
  let Category = BoltCat;
}

def BOLT_AlignFunctions : Option<
    "align-functions",
    "align functions at a given value (relocation mode)",
    "unsigned"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "64u";
  let Category = BoltOptCat;
}

def BOLT_AggregateOnly : Option<
    "aggregate-only",
    "exit after writing aggregated data file",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Hidden = 1;
  let Category = AggregatorCat;
}

def BOLT_BucketsPerLine : Option<
    "line-size",
    "number of entries per line (default 256)",
    "unsigned"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "256u";
  let Category = HeatmapCat;
}

def BOLT_CompactCodeModel : Option<
    "compact-code-model",
    "generate code for binaries <128MB on AArch64",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Category = BoltCat;
}

def BOLT_DiffOnly : Option<
    "diff-only",
    "stop processing once we have enough to compare two binaries",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Hidden = 1;
  let Category = BoltDiffCat;
}

def BOLT_EnableBAT : Option<
    "enable-bat",
    "write BOLT Address Translation tables",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Category = BoltCat;
}

def BOLT_EqualizeBBCounts : Option<
    "equalize-bb-counts",
    "use same count for BBs that should have equivalent count",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Hidden = 1;
  let Category = BoltOptCat;
}

def BOLT_ForcePatch : Option<
    "force-patch",
    "force patching of original entry points",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Hidden = 1;
  let Category = BoltCat;
}

def BOLT_RemoveSymtab : Option<
    "remove-symtab",
    "Remove .symtab section",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Category = BoltCat;
}

def BOLT_ExecutionCountThreshold : Option<
    "execution-count-threshold",
    "perform profiling accuracy-sensitive optimizations only if function "
    "execution count >= the threshold (default: 0)",
    "unsigned"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "0u";
  let Hidden = 1;
  let Category = BoltOptCat;
}

def BOLT_SplitStrategy : Option<
    "split-strategy",
    "strategy used to partition blocks into fragments",
    "opts::SplitFunctionsStrategy"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "profile2";
  let Category = BoltOptCat;
}

// Cache line, then the page sizes x86-64 and AArch64 actually use (4K, and
// 16K/64K on AArch64), then the PMD hugepage above a 4K base page.
def BOLT_HeatmapBlock : Option<
    "block-size",
    "heatmap bucket size, optionally followed by zoom-out sizes for "
    "coarse-grained heatmaps (default 64, 4K, 16K, 64K, 2M).",
    "std::string"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "\"64,4K,16K,64K,2M\"";
  let ValueDesc = "initial_size{,zoom-out_size,...}";
  let Category = HeatmapCat;
}

def BOLT_HeatmapCdfPct : Option<
    "heatmap-cdf-pct",
    "Sample CDF cutoff, in millionths, at which to report the working set.",
    "int"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "990000";
  let ValueDesc = "n";
  let Category = HeatmapCat;
}

def BOLT_HeatmapMaxAddress : Option<
    "max-address",
    "maximum address considered valid for heatmap (default 4GB)",
    "uint64_t"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "uint64_t{0xffffffff}";
  let Category = HeatmapCat;
}

def BOLT_HeatmapMinAddress : Option<
    "min-address",
    "minimum address considered valid for heatmap (default 0)",
    "uint64_t"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "uint64_t{0}";
  let Category = HeatmapCat;
}

def BOLT_HeatmapPrintMappings : Option<
    "print-mappings",
    "print mappings in the legend, between characters/blocks and text "
    "sections",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Category = HeatmapCat;
}

def BOLT_HeatmapOutput : Option<
    "heatmap",
    "print heatmap to a given file",
    "std::string"> {
  let Registry = BoltUtilsOptsReg;
  let Category = HeatmapCat;
}

def BOLT_HeatmapMode : Option<
    "heatmap-mode",
    "heatmap mode",
    "opts::HeatmapModeKind"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "none";
  let Hidden = 1;
}

def BOLT_HotData : Option<
    "hot-data",
    "hot data symbols support (relocation mode)",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Category = BoltCat;
}

def BOLT_HotFunctionsAtEnd : Option<
    "hot-functions-at-end",
    "if reorder-functions is used, order functions putting hottest last",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Category = BoltCat;
}

def BOLT_HotText : Option<
    "hot-text",
    "Generate hot text symbols",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Category = BoltCat;
}

def BOLT_Instrument : Option<
    "instrument",
    "instrument code to generate accurate profile data",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

def BOLT_LargeCodeModel : Option<
    "large-code-model",
    "use large code model for exception handling encodings",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Category = BoltCat;
}

def BOLT_Lite : Option<
    "lite",
    "skip processing of cold functions",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Category = BoltCat;
}

def BOLT_OutputFilename : Option<
    "o",
    "<output file>",
    "std::string"> {
  let Registry = BoltUtilsOptsReg;
  let Category = BoltOutputCat;
  let Prefix = 1;  // -ofile as well as -o file
}

def BOLT_PerfData : ListOption<
    "perfdata",
    "<data file>",
    "std::string"> {
  let Registry = BoltUtilsOptsReg;
  let CommaSeparated = 1;
  let Category = AggregatorCat;
}

def BOLT_PerfDataAlias : Alias<"p", "perfdata", "alias for -perfdata"> {
  let Registry = BoltUtilsOptsReg;
}

def BOLT_PrintCacheMetrics : Option<
    "print-cache-metrics",
    "calculate and print various metrics for instruction cache",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

def BOLT_PrintOnlyFile : Option<
    "print-only-file",
    "file with list of functions to print",
    "std::string"> {
  let Registry = BoltUtilsOptsReg;
  let Hidden = 1;
  let Category = BoltCat;
}

def BOLT_PrintSections : Option<
    "print-sections",
    "print all registered sections",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Hidden = 1;
  let Category = BoltCat;
}

def BOLT_ProfileFormat : Option<
    "profile-format",
    "format to dump profile output in aggregation mode",
    "opts::ProfileFormatKind"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "fdata";
  let Hidden = 1;
}

def BOLT_SaveProfile : Option<
    "w",
    "save recorded profile to a file",
    "std::string"> {
  let Registry = BoltUtilsOptsReg;
  let Category = BoltOutputCat;
}

def BOLT_ShowDensity : Option<
    "show-density",
    "show profile density details",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Category = AggregatorCat;
}

def BOLT_SplitEH : Option<
    "split-eh",
    "split C++ exception handling code",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Hidden = 1;
  let Category = BoltOptCat;
}

def BOLT_StrictMode : Option<
    "strict",
    "trust the input to be from a well-formed source",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Category = BoltCat;
}

def BOLT_TimeOpts : Option<
    "time-opts",
    "print time spent in each optimization",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

def BOLT_TimeRewrite : Option<
    "time-rewrite",
    "print time spent in rewriting passes",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Hidden = 1;
  let Category = BoltCat;
}

def BOLT_UseOldText : Option<
    "use-old-text",
    "reuse space in old .text if possible (relocation mode)",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Category = BoltCat;
}

def BOLT_UpdateDebugSections : Option<
    "update-debug-sections",
    "update DWARF debug sections of the executable",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Category = BoltCat;
}

def BOLT_Verbosity : Option<
    "v",
    "set verbosity level for diagnostic output",
    "unsigned"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "0u";
  let Category = BoltCat;
}

// Controls whether liveness analysis is used by FixupBranches and LongJmpPass.
// Needed for branch inversion on AArch64.
def BOLT_FixBranchesWithLiveness : Option<
    "fix-branches-with-liveness",
    "use liveness analysis during branch fixup "
    "(needed for branch inversion on AArch64)",
    "bool"> {
  let Registry = BoltUtilsOptsReg;
  let Default = "false";
  let Category = BoltCat;
}

def BOLT_PrintOnly : ListOption<
    "print-only",
    "list of functions to print",
    "std::string"> {
  let Registry = BoltUtilsOptsReg;
  let ValueDesc = "func1,func2,func3,...";
  let Hidden = 1;
  let CommaSeparated = 1;
  let Category = BoltCat;
}
