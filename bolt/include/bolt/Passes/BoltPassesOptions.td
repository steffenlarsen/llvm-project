//===- BoltPassesOptions.td - CLV2 option defs -----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "bolt/Utils/BoltCategories.td"


def BoltPassesOptsReg : Registry<"llvm::bolt::bolt_passes_opts"> {
  // Getter bodies are compiled once, in BoltPassesOptionsGetters.cpp.
  let OutOfLineGetters = 1;
  let ContextTypes = [CTX_BinaryContext];
}

// ============================================================================
// Enum types
// ============================================================================

// BinaryPasses.cpp: print-sorted-by-order
def PSO_Ascending  : EnumValue<"ascending", "Ascending order"> {
  let Value = 0;
  let CppName = "BOLTPASS_PSO_Ascending";
}
def PSO_Descending : EnumValue<"descending", "Descending order"> {
  let Value = 1;
  let CppName = "BOLTPASS_PSO_Descending";
}

def BoltPassPrintSortedByOrder : EnumType<
    "BoltPassPrintSortedByOrder",
    [PSO_Ascending, PSO_Descending]>;

// BinaryPasses.cpp: sctc-mode
def SCTC_Always            : EnumValue<"always", "always perform sctc"> {
  let Value = 0;
  let CppName = "BOLTPASS_SCTC_Always";
}
def SCTC_PreserveDirection : EnumValue<"preserve",
    "only perform sctc when branch direction is preserved"> {
  let Value = 1;
  let CppName = "BOLTPASS_SCTC_PreserveDirection";
}
def SCTC_Heuristic         : EnumValue<"heuristic",
    "use branch prediction data to control sctc"> {
  let Value = 2;
  let CppName = "BOLTPASS_SCTC_Heuristic";
}

def BoltPassSCTCMode : EnumType<
    "BoltPassSCTCMode",
    [SCTC_Always, SCTC_PreserveDirection, SCTC_Heuristic]>;

// BinaryPasses.cpp: reorder-blocks (external enum)
def LT_NONE             : EnumValue<"none",
    "do not reorder basic blocks"> {
  let CppName = "LT_NONE";
}
def LT_REVERSE          : EnumValue<"reverse",
    "layout blocks in reverse order"> {
  let CppName = "LT_REVERSE";
}
def LT_OPTIMIZE         : EnumValue<"normal",
    "perform optimal layout based on profile"> {
  let CppName = "LT_OPTIMIZE";
}
def LT_OPTIMIZE_BRANCH  : EnumValue<"branch-predictor",
    "perform optimal layout prioritizing branch predictions"> {
  let CppName = "LT_OPTIMIZE_BRANCH";
}
def LT_OPTIMIZE_CACHE   : EnumValue<"cache",
    "perform optimal layout prioritizing I-cache behavior"> {
  let CppName = "LT_OPTIMIZE_CACHE";
}
def LT_OPTIMIZE_CACHE_PLUS : EnumValue<"cache+",
    "perform layout optimizing I-cache behavior"> {
  let CppName = "LT_OPTIMIZE_CACHE_PLUS";
}
def LT_OPTIMIZE_EXT_TSP : EnumValue<"ext-tsp",
    "perform layout optimizing I-cache behavior"> {
  let CppName = "LT_OPTIMIZE_EXT_TSP";
}
def LT_OPTIMIZE_SHUFFLE : EnumValue<"cluster-shuffle",
    "perform random layout of clusters"> {
  let CppName = "LT_OPTIMIZE_SHUFFLE";
}

def ReorderBasicBlocksLayoutType : EnumType<
    "bolt::ReorderBasicBlocks::LayoutType",
    [LT_NONE, LT_REVERSE, LT_OPTIMIZE, LT_OPTIMIZE_BRANCH,
     LT_OPTIMIZE_CACHE, LT_OPTIMIZE_CACHE_PLUS, LT_OPTIMIZE_EXT_TSP,
     LT_OPTIMIZE_SHUFFLE]> {
  let External = 1;
}

// BinaryPasses.cpp: print-sorted-by (BoltDynoStatCategory, enum list)
def BOLTDS_FORWARD_COND_BRANCHES : EnumValue<
    "executed-forward-branches", "executed forward branches"> {
  let Value = 1;
  let CppName = "BOLTDS_FORWARD_COND_BRANCHES";
}
def BOLTDS_FORWARD_COND_BRANCHES_TAKEN : EnumValue<
    "taken-forward-branches", "taken forward branches"> {
  let Value = 2;
  let CppName = "BOLTDS_FORWARD_COND_BRANCHES_TAKEN";
}
def BOLTDS_BACKWARD_COND_BRANCHES : EnumValue<
    "executed-backward-branches", "executed backward branches"> {
  let Value = 3;
  let CppName = "BOLTDS_BACKWARD_COND_BRANCHES";
}
def BOLTDS_BACKWARD_COND_BRANCHES_TAKEN : EnumValue<
    "taken-backward-branches", "taken backward branches"> {
  let Value = 4;
  let CppName = "BOLTDS_BACKWARD_COND_BRANCHES_TAKEN";
}
def BOLTDS_UNCOND_BRANCHES : EnumValue<
    "executed-unconditional-branches", "executed unconditional branches"> {
  let Value = 5;
  let CppName = "BOLTDS_UNCOND_BRANCHES";
}
def BOLTDS_FUNCTION_CALLS : EnumValue<
    "all-function-calls", "all function calls"> {
  let Value = 6;
  let CppName = "BOLTDS_FUNCTION_CALLS";
}
def BOLTDS_INDIRECT_CALLS : EnumValue<
    "indirect-calls", "indirect calls"> {
  let Value = 7;
  let CppName = "BOLTDS_INDIRECT_CALLS";
}
def BOLTDS_PLT_CALLS : EnumValue<"PLT-calls", "PLT calls"> {
  let Value = 8;
  let CppName = "BOLTDS_PLT_CALLS";
}
def BOLTDS_INSTRUCTIONS : EnumValue<
    "executed-instructions", "executed instructions"> {
  let Value = 9;
  let CppName = "BOLTDS_INSTRUCTIONS";
}
def BOLTDS_LOADS : EnumValue<
    "executed-load-instructions", "executed load instructions"> {
  let Value = 10;
  let CppName = "BOLTDS_LOADS";
}
def BOLTDS_STORES : EnumValue<
    "executed-store-instructions", "executed store instructions"> {
  let Value = 11;
  let CppName = "BOLTDS_STORES";
}
def BOLTDS_JUMP_TABLE_BRANCHES : EnumValue<
    "taken-jump-table-branches", "taken jump table branches"> {
  let Value = 12;
  let CppName = "BOLTDS_JUMP_TABLE_BRANCHES";
}
def BOLTDS_UNKNOWN_INDIRECT_BRANCHES : EnumValue<
    "taken-unknown-indirect-branches", "taken unknown indirect branches"> {
  let Value = 13;
  let CppName = "BOLTDS_UNKNOWN_INDIRECT_BRANCHES";
}
def BOLTDS_ALL_BRANCHES : EnumValue<
    "total-branches", "total branches"> {
  let Value = 14;
  let CppName = "BOLTDS_ALL_BRANCHES";
}
def BOLTDS_ALL_TAKEN : EnumValue<
    "taken-branches", "taken branches"> {
  let Value = 15;
  let CppName = "BOLTDS_ALL_TAKEN";
}
def BOLTDS_NONTAKEN_CONDITIONAL : EnumValue<
    "non-taken-conditional-branches", "non-taken conditional branches"> {
  let Value = 16;
  let CppName = "BOLTDS_NONTAKEN_CONDITIONAL";
}
def BOLTDS_TAKEN_CONDITIONAL : EnumValue<
    "taken-conditional-branches", "taken conditional branches"> {
  let Value = 17;
  let CppName = "BOLTDS_TAKEN_CONDITIONAL";
}
def BOLTDS_ALL_CONDITIONAL : EnumValue<
    "all-conditional-branches", "all conditional branches"> {
  let Value = 18;
  let CppName = "BOLTDS_ALL_CONDITIONAL";
}
def BOLTDS_VENEER_CALLS_AARCH64 : EnumValue<
    "linker-inserted-veneer-calls", "linker-inserted veneer calls"> {
  let Value = 19;
  let CppName = "BOLTDS_VENEER_CALLS_AARCH64";
}
def BOLTDS_LAST_DYNO_STAT : EnumValue<
    "all", "sorted by all names"> {
  let Value = 20;
  let CppName = "BOLTDS_LAST_DYNO_STAT";
}

def BoltDynoStatCategory : EnumType<"BoltDynoStatCategory",
    [BOLTDS_FORWARD_COND_BRANCHES, BOLTDS_FORWARD_COND_BRANCHES_TAKEN,
     BOLTDS_BACKWARD_COND_BRANCHES, BOLTDS_BACKWARD_COND_BRANCHES_TAKEN,
     BOLTDS_UNCOND_BRANCHES, BOLTDS_FUNCTION_CALLS,
     BOLTDS_INDIRECT_CALLS, BOLTDS_PLT_CALLS,
     BOLTDS_INSTRUCTIONS, BOLTDS_LOADS, BOLTDS_STORES,
     BOLTDS_JUMP_TABLE_BRANCHES, BOLTDS_UNKNOWN_INDIRECT_BRANCHES,
     BOLTDS_ALL_BRANCHES, BOLTDS_ALL_TAKEN,
     BOLTDS_NONTAKEN_CONDITIONAL, BOLTDS_TAKEN_CONDITIONAL,
     BOLTDS_ALL_CONDITIONAL, BOLTDS_VENEER_CALLS_AARCH64,
     BOLTDS_LAST_DYNO_STAT]>;

// BinaryPasses.cpp: peepholes (external enum)
def PEEP_NONE           : EnumValue<"none", "disable peepholes"> {
  let CppName = "PEEP_NONE";
}
def PEEP_DOUBLE_JUMPS   : EnumValue<"double-jumps",
    "remove double jumps when able"> {
  let CppName = "PEEP_DOUBLE_JUMPS";
}
def PEEP_TAILCALL_TRAPS : EnumValue<"tailcall-traps",
    "insert tail call traps"> {
  let CppName = "PEEP_TAILCALL_TRAPS";
}
def PEEP_USELESS_BRANCHES : EnumValue<"useless-branches",
    "remove useless conditional branches"> {
  let CppName = "PEEP_USELESS_BRANCHES";
}
def PEEP_ALL            : EnumValue<"all",
    "enable all peephole optimizations"> {
  let CppName = "PEEP_ALL";
}

def PeepholeOpts : EnumType<"bolt::Peepholes::PeepholeOpts",
    [PEEP_NONE, PEEP_DOUBLE_JUMPS, PEEP_TAILCALL_TRAPS,
     PEEP_USELESS_BRANCHES, PEEP_ALL]> {
  let External = 1;
}

// FrameOptimizer.cpp: frame-opt (external enum)
def FOP_NONE : EnumValue<"none", "do not perform frame optimization"> {
  let CppName = "FOP_NONE";
}
def FOP_HOT  : EnumValue<"hot", "perform FOP on hot functions"> {
  let CppName = "FOP_HOT";
}
def FOP_ALL  : EnumValue<"all", "perform FOP on all functions"> {
  let CppName = "FOP_ALL";
}

def FrameOptimizationType : EnumType<"bolt::FrameOptimizationType",
    [FOP_NONE, FOP_HOT, FOP_ALL]> {
  let External = 1;
}

// IdenticalCodeFolding.cpp: icf (external enum)
def ICF_All  : EnumValue<"all", "Enable identical code folding"> {
  let CppName = "All";
}
def ICF_1    : EnumValue<"1", "Enable identical code folding"> {
  let CppName = "All";
}
def ICF_Empty : EnumValue<"", "Enable identical code folding"> {
  let CppName = "All";
}
def ICF_None : EnumValue<"none", "Disable identical code folding (default)"> {
  let CppName = "None";
}
def ICF_0    : EnumValue<"0", "Disable identical code folding (default)"> {
  let CppName = "None";
}
def ICF_Safe : EnumValue<"safe", "Enable safe identical code folding"> {
  let CppName = "Safe";
}

def ICFLevel : EnumType<"bolt::IdenticalCodeFolding::ICFLevel",
    [ICF_All, ICF_1, ICF_Empty, ICF_None, ICF_0, ICF_Safe]> {
  let External = 1;
}

// PLTCall.cpp: plt (external enum)
def OT_NONE : EnumValue<"none", "do not optimize PLT calls"> {
  let CppName = "OT_NONE";
}
def OT_HOT  : EnumValue<"hot", "optimize executed (hot) PLT calls"> {
  let CppName = "OT_HOT";
}
def OT_ALL  : EnumValue<"all", "optimize all PLT calls"> {
  let CppName = "OT_ALL";
}

def PLTCallOptType : EnumType<"bolt::PLTCall::OptType",
    [OT_NONE, OT_HOT, OT_ALL]> {
  let External = 1;
}

// ReorderData.cpp: reorder-data-algo
def RDA_COUNT : EnumValue<"count", "sort hot data by read counts"> {
  let Value = 0;
  let CppName = "BOLTPASS_RDA_COUNT";
}
def RDA_FUNCS : EnumValue<"funcs",
    "sort hot data by hot function usage and count"> {
  let Value = 1;
  let CppName = "BOLTPASS_RDA_FUNCS";
}

def BoltPassReorderDataAlgo : EnumType<
    "BoltPassReorderDataAlgo",
    [RDA_COUNT, RDA_FUNCS]>;

// ReorderFunctions.cpp: reorder-functions (external enum)
def RT_NONE         : EnumValue<"none", "do not reorder functions"> {
  let CppName = "RT_NONE";
}
def RT_EXEC_COUNT   : EnumValue<"exec-count", "order by execution count"> {
  let CppName = "RT_EXEC_COUNT";
}
def RT_HFSORT       : EnumValue<"hfsort", "use hfsort algorithm"> {
  let CppName = "RT_HFSORT";
}
def RT_HFSORT_PLUS  : EnumValue<"hfsort+", "use cache-directed sort"> {
  let CppName = "RT_HFSORT_PLUS";
}
def RT_CDSORT       : EnumValue<"cdsort", "use cache-directed sort"> {
  let CppName = "RT_CDSORT";
}
def RT_PETTIS_HANSEN : EnumValue<"pettis-hansen",
    "use Pettis-Hansen algorithm"> {
  let CppName = "RT_PETTIS_HANSEN";
}
def RT_RANDOM       : EnumValue<"random", "reorder functions randomly"> {
  let CppName = "RT_RANDOM";
}
def RT_USER         : EnumValue<"user",
    "use function order specified by -function-order"> {
  let CppName = "RT_USER";
}

def ReorderFunctionsReorderType : EnumType<
    "bolt::ReorderFunctions::ReorderType",
    [RT_NONE, RT_EXEC_COUNT, RT_HFSORT, RT_HFSORT_PLUS,
     RT_CDSORT, RT_PETTIS_HANSEN, RT_RANDOM, RT_USER]> {
  let External = 1;
}

// RetpolineInsertion.cpp: r11-availability (external enum)
def R11_NEVER  : EnumValue<"never", "r11 not available"> {
  let CppName = "NEVER";
}
def R11_ALWAYS : EnumValue<"always",
    "r11 available before calls and jumps"> {
  let CppName = "ALWAYS";
}
def R11_ABI    : EnumValue<"abi",
    "r11 available before calls but not before jumps"> {
  let CppName = "ABI";
}

def RetpolineAvailabilityOptions : EnumType<
    "bolt::RetpolineInsertion::AvailabilityOptions",
    [R11_NEVER, R11_ALWAYS, R11_ABI]> {
  let External = 1;
}

// TailDuplication.cpp: tail-duplication (external enum)
def TD_NONE       : EnumValue<"none", "do not apply"> {
  let CppName = "TD_NONE";
}
def TD_AGGRESSIVE : EnumValue<"aggressive", "aggressive strategy"> {
  let CppName = "TD_AGGRESSIVE";
}
def TD_MODERATE   : EnumValue<"moderate", "moderate strategy"> {
  let CppName = "TD_MODERATE";
}
def TD_CACHE      : EnumValue<"cache", "cache-aware duplication strategy"> {
  let CppName = "TD_CACHE";
}

def TailDuplicationMode : EnumType<
    "bolt::TailDuplication::DuplicationMode",
    [TD_NONE, TD_AGGRESSIVE, TD_MODERATE, TD_CACHE]> {
  let External = 1;
}

// ============================================================================
// AArch64RelaxationPass.cpp
// ============================================================================

def BOLTPASS_AArch64Relaxation : Option<
    "aarch64-relaxation",
    "Replace ARM non-local ADR/LDR instructions with ADRP",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "true";
  let Hidden = 1;
}

// ============================================================================
// Aligner.cpp
// ============================================================================

def BOLTPASS_AlignBlocksMinSize : Option<
    "align-blocks-min-size",
    "minimal size of the basic block that should be aligned",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "0u";
  let Hidden = 1;
}

def BOLTPASS_AlignBlocksThreshold : Option<
    "align-blocks-threshold",
    "align only blocks with frequency larger than containing function "
    "execution frequency specified in percent. E.g. 1000 means aligning "
    "blocks that are 10 times more frequently executed than the "
    "containing function.",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "800u";
  let Hidden = 1;
}

def BOLTPASS_AlignFunctionsMaxBytes : Option<
    "align-functions-max-bytes",
    "maximum number of bytes to use to align functions",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "32u";
  let Category = BoltOptCat;
}

def BOLTPASS_BlockAlignment : Option<
    "block-alignment",
    "boundary to use for alignment of basic blocks",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "16u";
  let Category = BoltOptCat;
}

def BOLTPASS_UseCompactAligner : Option<
    "use-compact-aligner",
    "Use compact approach for aligning functions",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "true";
  let Category = BoltOptCat;
}

// ============================================================================
// AsmDump.cpp
// ============================================================================

def BOLTPASS_AsmDump : Option<
    "asm-dump",
    "dump function into assembly",
    "std::string"> {
  let Registry = BoltPassesOptsReg;
  let Hidden = 1;
}

// ============================================================================
// BinaryPasses.cpp
// ============================================================================

def BOLTPASS_PrintSortedByOrder : Option<
    "print-sorted-by-order",
    "use ascending or descending order when printing functions "
    "ordered by dyno stats",
    "BoltPassPrintSortedByOrder"> {
  let Registry = BoltPassesOptsReg;
  let Default = "descending";
  let Category = BoltOptCat;
}

def BOLTPASS_PrintSortedBy : ListOption<
    "print-sorted-by",
    "print functions sorted by order of dyno stats",
    "BoltDynoStatCategory"> {
  let Registry = BoltPassesOptsReg;
  let CommaSeparated = 1;
  let Category = BoltOptCat;
}

def BOLTPASS_HotTextMoveSections : ListOption<
    "hot-text-move-sections",
    "list of sections containing functions used for hugifying hot text. "
    "BOLT makes sure these functions are not placed on the same page as "
    "the hot text. (default='.stub,.mover').",
    "std::string"> {
  let Registry = BoltPassesOptsReg;
  let Category = BoltCat;
  let ValueDesc = "sec1,sec2,sec3,...";
}

def BOLTPASS_Peepholes : ListOption<
    "peepholes",
    "enable peephole optimizations",
    "bolt::Peepholes::PeepholeOpts"> {
  let Registry = BoltPassesOptsReg;
  let Category = BoltOptCat;
}

def BOLTPASS_MinBranchClusters : Option<
    "min-branch-clusters",
    "use a modified clustering algorithm geared towards minimizing branches",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPASS_PrintFuncStat : Option<
    "print-function-statistics",
    "print statistics about basic block ordering",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "0u";
  let Category = BoltOptCat;
}

def BOLTPASS_PrintLargeFunctions : Option<
    "print-large-functions",
    "print functions that could not be overwritten due to excessive size",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

def BOLTPASS_PrintUnknown : Option<
    "print-unknown",
    "print names of functions with unknown control flow",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPASS_PrintUnknownCFG : Option<
    "print-unknown-cfg",
    "dump CFG of functions with unknown control flow",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPASS_ReorderBlocks : Option<
    "reorder-blocks",
    "change layout of basic blocks in a function",
    "bolt::ReorderBasicBlocks::LayoutType"> {
  let Registry = BoltPassesOptsReg;
  let Default = "none";
  let Category = BoltOptCat;
}

def BOLTPASS_ReportBadLayout : Option<
    "report-bad-layout",
    "print top <uint> functions with suboptimal code layout on input",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "0u";
  let Hidden = 1;
}

def BOLTPASS_ReportStaleFuncs : Option<
    "report-stale",
    "print the list of functions with stale profile",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPASS_SctcMode : Option<
    "sctc-mode",
    "mode for simplify conditional tail calls",
    "BoltPassSCTCMode"> {
  let Registry = BoltPassesOptsReg;
  let Default = "always";
  let Category = BoltOptCat;
}

def BOLTPASS_StaleThreshold : Option<
    "stale-threshold",
    "maximum percentage of stale functions to tolerate (default: 100)",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "100u";
  let Hidden = 1;
}

def BOLTPASS_TSPThreshold : Option<
    "tsp-threshold",
    "maximum number of hot basic blocks in a function for which to use "
    "a precise TSP solution while re-ordering basic blocks",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "10u";
  let Hidden = 1;
}

def BOLTPASS_TopCalledLimit : Option<
    "top-called-limit",
    "maximum number of functions to print in top called functions section",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "100u";
  let Hidden = 1;
}

def BOLTPASS_ProfileDensityCutOffHot : Option<
    "profile-density-cutoff-hot",
    "Total samples cutoff for functions used to calculate profile density.",
    "int"> {
  let Registry = BoltPassesOptsReg;
  let Default = "990000";
}

def BOLTPASS_ProfileDensityThreshold : Option<
    "profile-density-threshold",
    "If the profile density is below the given threshold, it "
    "will be suggested to increase the sampling rate.",
    "double"> {
  let Registry = BoltPassesOptsReg;
  let Default = "60.0";
}

// ============================================================================
// CMOVConversion.cpp
// ============================================================================

def BOLTPASS_BiasThreshold : Option<
    "cmov-conversion-bias-threshold",
    "minimum condition bias (pct) to perform a CMOV conversion, "
    "-1 to not account bias",
    "int"> {
  let Registry = BoltPassesOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def BOLTPASS_MispredictionThreshold : Option<
    "cmov-conversion-misprediction-threshold",
    "minimum misprediction rate (pct) to perform a CMOV conversion, "
    "-1 to not account misprediction rate",
    "int"> {
  let Registry = BoltPassesOptsReg;
  let Default = "5";
  let Hidden = 1;
}

def BOLTPASS_ConvertStackMemOperand : Option<
    "cmov-conversion-convert-stack-mem-operand",
    "convert moves with stack memory operand (potentially unsafe)",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPASS_ConvertBasePtrStackMemOperand : Option<
    "cmov-conversion-convert-rbp-stack-mem-operand",
    "convert moves with rbp stack memory operand (unsafe, must be off "
    "for binaries compiled with -fomit-frame-pointer)",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Hidden = 1;
}

// ============================================================================
// FrameAnalysis.cpp
// ============================================================================

def BOLTPASS_FrameOptFunctionNames : Option<
    "funcs-fop",
    "list of functions to apply frame opts",
    "std::string"> {
  let Registry = BoltPassesOptsReg;
}

def BOLTPASS_FrameOptFunctionNamesFile : Option<
    "funcs-file-fop",
    "file with list of functions to frame optimize",
    "std::string"> {
  let Registry = BoltPassesOptsReg;
}

def BOLTPASS_TimeFA : Option<
    "time-fa",
    "time frame analysis steps",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPASS_ExperimentalSW : Option<
    "experimental-shrink-wrapping",
    "process functions with stack pointer arithmetic",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Hidden = 1;
}

// ============================================================================
// FrameOptimizer.cpp
// ============================================================================

def BOLTPASS_FrameOpt : Option<
    "frame-opt",
    "optimize stack frame accesses",
    "bolt::FrameOptimizationType"> {
  let Registry = BoltPassesOptsReg;
  let Default = "none";
  let Category = BoltOptCat;
}

def BOLTPASS_RemoveStores : Option<
    "frame-opt-rm-stores",
    "apply additional analysis to remove stores (experimental)",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

// ============================================================================
// IdenticalCodeFolding.cpp
// ============================================================================

def BOLTPASS_ICFUseDFS : Option<
    "icf-dfs",
    "use DFS ordering when using -icf option",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPASS_TimeICF : Option<
    "time-icf",
    "time icf steps",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPASS_ICF : Option<
    "icf",
    "fold functions with identical code",
    "bolt::IdenticalCodeFolding::ICFLevel"> {
  let Registry = BoltPassesOptsReg;
  let Default = "none";
  let ValueOptional = 1;
  let Category = BoltOptCat;
}

// ============================================================================
// IndirectCallPromotion.cpp
// ============================================================================

def BOLTPASS_ICPJTRemainingPercentThreshold : Option<
    "icp-jt-remaining-percent-threshold",
    "The percentage threshold against remaining unpromoted indirect "
    "call count for the promotion for jump tables",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "30u";
  let Hidden = 1;
}

def BOLTPASS_ICPJTTotalPercentThreshold : Option<
    "icp-jt-total-percent-threshold",
    "The percentage threshold against total count for the promotion for "
    "jump tables",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "5u";
  let Hidden = 1;
}

def BOLTPASS_ICPCallsRemainingPercentThreshold : Option<
    "icp-calls-remaining-percent-threshold",
    "The percentage threshold against remaining unpromoted indirect "
    "call count for the promotion for calls",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "50u";
  let Hidden = 1;
}

def BOLTPASS_ICPCallsTotalPercentThreshold : Option<
    "icp-calls-total-percent-threshold",
    "The percentage threshold against total count for the promotion for "
    "calls",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "30u";
  let Hidden = 1;
}

def BOLTPASS_ICPMispredictThreshold : Option<
    "indirect-call-promotion-mispredict-threshold",
    "misprediction threshold for skipping ICP on an indirect call",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "0u";
  let Category = BoltOptCat;
}

def BOLTPASS_ICPMispredictThresholdAlias : Option<
    "icp-mp-threshold",
    "alias for --indirect-call-promotion-mispredict-threshold",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "0u";
}

def BOLTPASS_ICPUseMispredicts : Option<
    "indirect-call-promotion-use-mispredicts",
    "use misprediction frequency for determining whether or not ICP "
    "should be applied at a callsite.  The "
    "-indirect-call-promotion-mispredict-threshold value will be used "
    "by this heuristic",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

def BOLTPASS_ICPUseMispredictsAlias : Option<
    "icp-use-mp",
    "alias for --indirect-call-promotion-use-mispredicts",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
}

def BOLTPASS_ICPTopN : Option<
    "indirect-call-promotion-topn",
    "limit number of targets to consider when doing indirect "
    "call promotion. 0 = no limit",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "3u";
  let Category = BoltOptCat;
}

def BOLTPASS_ICPTopNAlias : Option<
    "icp-topn",
    "alias for --indirect-call-promotion-topn",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "3u";
}

def BOLTPASS_ICPCallsTopN : Option<
    "indirect-call-promotion-calls-topn",
    "limit number of targets to consider when doing indirect "
    "call promotion on calls. 0 = no limit",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "0u";
  let Category = BoltOptCat;
}

def BOLTPASS_ICPCallsTopNAlias : Option<
    "icp-calls-topn",
    "alias for --indirect-call-promotion-calls-topn",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "0u";
}

def BOLTPASS_ICPJumpTablesTopN : Option<
    "indirect-call-promotion-jump-tables-topn",
    "limit number of targets to consider when doing indirect "
    "call promotion on jump tables. 0 = no limit",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "0u";
  let Category = BoltOptCat;
}

def BOLTPASS_ICPJumpTablesTopNAlias : Option<
    "icp-jt-topn",
    "alias for --indirect-call-promotion-jump-tables-topn",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "0u";
}

def BOLTPASS_EliminateLoads : Option<
    "icp-eliminate-loads",
    "enable load elimination using memory profiling data when "
    "performing ICP",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "true";
  let Category = BoltOptCat;
}

def BOLTPASS_ICPTopCallsites : Option<
    "icp-top-callsites",
    "optimize hottest calls until at least this percentage of all "
    "indirect calls frequency is covered. 0 = all callsites",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "99u";
  let Hidden = 1;
}

def BOLTPASS_ICPFuncsList : ListOption<
    "icp-funcs",
    "list of functions to enable ICP for",
    "std::string"> {
  let Registry = BoltPassesOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

def BOLTPASS_ICPOldCodeSequence : Option<
    "icp-old-code-sequence",
    "use old code sequence for promoted calls",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPASS_ICPJumpTablesByTarget : Option<
    "icp-jump-tables-targets",
    "for jump tables, optimize indirect jmp targets instead of indices",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPASS_ICPJumpTablesByTargetAlias : Option<
    "icp-jt-targets",
    "alias for --icp-jump-tables-targets",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPASS_ICPPeelForInline : Option<
    "icp-inline",
    "only promote call targets eligible for inlining",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Hidden = 1;
}

// ============================================================================
// Inliner.cpp
// ============================================================================

def BOLTPASS_AdjustProfile : Option<
    "inline-ap",
    "adjust function profile after inlining",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

def BOLTPASS_ForceInlineFunctions : ListOption<
    "force-inline",
    "list of functions to always consider for inlining",
    "std::string"> {
  let Registry = BoltPassesOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

def BOLTPASS_SkipInlineFunctions : ListOption<
    "skip-inline",
    "list of functions to never consider for inlining",
    "std::string"> {
  let Registry = BoltPassesOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

def BOLTPASS_InlineAll : Option<
    "inline-all",
    "inline all functions",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

def BOLTPASS_InlineIgnoreLeafCFI : Option<
    "inline-ignore-leaf-cfi",
    "inline leaf functions with CFI programs (can break unwinding)",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "true";
  let Hidden = 1;
}

def BOLTPASS_InlineIgnoreCFI : Option<
    "inline-ignore-cfi",
    "inline functions with CFI programs (can break exception handling)",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPASS_InlineLimit : Option<
    "inline-limit",
    "maximum number of call sites to inline",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "0u";
  let Hidden = 1;
}

def BOLTPASS_InlineMaxIters : Option<
    "inline-max-iters",
    "maximum number of inline iterations",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "3u";
  let Hidden = 1;
}

def BOLTPASS_InlineSmallFunctions : Option<
    "inline-small-functions",
    "inline functions if increase in size is less than defined by "
    "-inline-small-functions-bytes",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

def BOLTPASS_InlineSmallFunctionsBytes : Option<
    "inline-small-functions-bytes",
    "max number of bytes for the function to be considered small for "
    "inlining purposes",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "4u";
  let Hidden = 1;
}

def BOLTPASS_NoInline : Option<
    "no-inline",
    "disable all inlining (overrides other inlining options)",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

// ============================================================================
// Instrumentation.cpp
// ============================================================================

def BOLTPASS_InstrumentationFilename : Option<
    "instrumentation-file",
    "file name where instrumented profile will be saved (default: "
    "/tmp/prof.fdata)",
    "std::string"> {
  let Registry = BoltPassesOptsReg;
  let Default = "\"/tmp/prof.fdata\"";
  let Category = BoltInstrCat;
}

def BOLTPASS_InstrumentationBinpath : Option<
    "instrumentation-binpath",
    "path to instrumented binary in case if /proc/self/map_files "
    "is not accessible due to access restriction issues",
    "std::string"> {
  let Registry = BoltPassesOptsReg;
  let Category = BoltInstrCat;
}

def BOLTPASS_InstrumentationFileAppendPID : Option<
    "instrumentation-file-append-pid",
    "append PID to saved profile file name (default: false)",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltInstrCat;
}

def BOLTPASS_ConservativeInstrumentation : Option<
    "conservative-instrumentation",
    "disable instrumentation optimizations that sacrifice profile "
    "accuracy (for debugging, default: false)",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltInstrCat;
}

def BOLTPASS_InstrumentationMaxSize : Option<
    "instrumentation-max-size",
    "Set max memory size of the instrumentation bump allocator "
    "default: 0x6400000)",
    "uint32_t"> {
  let Registry = BoltPassesOptsReg;
  let Default = "uint32_t{0x6400000}";
  let Category = BoltInstrCat;
}

def BOLTPASS_InstrumentationSleepTime : Option<
    "instrumentation-sleep-time",
    "interval between profile writes (default: 0 = write only at "
    "program end).  This is useful for service workloads when you "
    "want to dump profile every X minutes or if you are killing the "
    "program and the profile is not being dumped at the end.",
    "uint32_t"> {
  let Registry = BoltPassesOptsReg;
  let Default = "uint32_t{0}";
  let Category = BoltInstrCat;
}

def BOLTPASS_InstrumentationNoCountersClear : Option<
    "instrumentation-no-counters-clear",
    "Don't clear counters across dumps "
    "(use with instrumentation-sleep-time option)",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltInstrCat;
}

def BOLTPASS_InstrumentationWaitForks : Option<
    "instrumentation-wait-forks",
    "Wait until all forks of instrumented process will finish "
    "(use with instrumentation-sleep-time option)",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltInstrCat;
}

def BOLTPASS_InstrumentHotOnly : Option<
    "instrument-hot-only",
    "only insert instrumentation on hot functions "
    "(needs profile, default: false)",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltInstrCat;
}

def BOLTPASS_InstrumentCalls : Option<
    "instrument-calls",
    "record profile for inter-function control flow activity (default: true)",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "true";
  let Category = BoltInstrCat;
}

// ============================================================================
// JTFootprintReduction.cpp
// ============================================================================

def BOLTPASS_JTFootprintOnlyPIC : Option<
    "jt-footprint-optimize-for-icache",
    "with jt-footprint-reduction, only process PIC jumptables and turn"
    " off other transformations that increase code size",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

// ============================================================================
// LongJmp.cpp
// ============================================================================

def BOLTPASS_GroupStubs : Option<
    "group-stubs",
    "share stubs across functions",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "true";
  let Category = BoltOptCat;
}

def BOLTPASS_ExperimentalRelaxation : Option<
    "relax-exp",
    "run experimental relaxation pass",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

def BOLTPASS_RelaxPLT : Option<
    "relax-plt",
    "indicate PLT proximity to hot text",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "true";
  let Category = BoltOptCat;
}

// ============================================================================
// LoopInversionPass.cpp
// ============================================================================

def BOLTPASS_LoopReorder : Option<
    "loop-inversion-opt",
    "reorder unconditional jump instructions in loops optimization",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "true";
  let Hidden = 1;
}

// ============================================================================
// MCF.cpp
// ============================================================================

def BOLTPASS_IterativeGuess : Option<
    "iterative-guess",
    "in non-LBR mode, guess edge counts using iterative technique",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Hidden = 1;
}

// ============================================================================
// PAuthGadgetScanner.cpp
// ============================================================================

def BOLTPASS_AuthTrapsOnFailure : Option<
    "auth-traps-on-failure",
    "Assume authentication instructions always trap on failure",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BinaryAnalysisCat;
}

// ============================================================================
// PLTCall.cpp
// ============================================================================

def BOLTPASS_PLT : Option<
    "plt",
    "optimize PLT calls (requires linking with -znow on "
    "non-x86 architectures)",
    "bolt::PLTCall::OptType"> {
  let Registry = BoltPassesOptsReg;
  let Default = "none";
  let Category = BoltOptCat;
}

// ============================================================================
// ProfileQualityStats.cpp
// ============================================================================

def BOLTPASS_TopFunctionsForProfileQualityCheck : Option<
    "top-functions-for-profile-quality-check",
    "number of hottest functions to print aggregated "
    "profile quality stats of.",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "1000u";
  let Hidden = 1;
}

def BOLTPASS_PercentileForProfileQualityCheck : Option<
    "percentile-for-profile-quality-check",
    "Percentile of profile quality distributions over hottest "
    "functions to report.",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "95u";
  let Hidden = 1;
}

// ============================================================================
// RegAnalysis.cpp
// ============================================================================

def BOLTPASS_AssumeABI : Option<
    "assume-abi",
    "assume the ABI is never violated",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

// ============================================================================
// RegReAssign.cpp
// ============================================================================

def BOLTPASS_AggressiveReAssign : Option<
    "use-aggr-reg-reassign",
    "use register liveness analysis to try to find more opportunities "
    "for -reg-reassign optimization",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

// ============================================================================
// ReorderAlgorithm.cpp
// ============================================================================

def BOLTPASS_ColdThreshold : Option<
    "cold-threshold",
    "tenths of percents of main entry frequency to use as a "
    "threshold when evaluating whether a basic block is cold "
    "(0 means it is only considered cold if the block has zero "
    "samples). Default: 0 ",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "0u";
  let Hidden = 1;
}

def BOLTPASS_PrintClusters : Option<
    "print-clusters",
    "print clusters",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPASS_RandomSeed : Option<
    "bolt-seed",
    "seed for randomization",
    "uint32_t"> {
  let Registry = BoltPassesOptsReg;
  let Default = "uint32_t{42}";
  let Hidden = 1;
}

// ============================================================================
// ReorderData.cpp
// ============================================================================

def BOLTPASS_PrintReorderedData : Option<
    "print-reordered-data",
    "print section contents after reordering",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPASS_ReorderData : ListOption<
    "reorder-data",
    "list of sections to reorder",
    "std::string"> {
  let Registry = BoltPassesOptsReg;
  let CommaSeparated = 1;
  let Category = BoltOptCat;
  let ValueDesc = "section1,section2,section3,...";
}

def BOLTPASS_ReorderDataAlgo : Option<
    "reorder-data-algo",
    "algorithm used to reorder data sections",
    "BoltPassReorderDataAlgo"> {
  let Registry = BoltPassesOptsReg;
  let Default = "count";
  let Category = BoltOptCat;
}

def BOLTPASS_ReorderDataMaxSymbols : Option<
    "reorder-data-max-symbols",
    "maximum number of symbols to reorder",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "std::numeric_limits<unsigned>::max()";
  let Category = BoltOptCat;
}

def BOLTPASS_ReorderDataMaxBytes : Option<
    "reorder-data-max-bytes",
    "maximum number of bytes to reorder",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "std::numeric_limits<unsigned>::max()";
  let Category = BoltOptCat;
}

def BOLTPASS_ReorderSymbols : ListOption<
    "reorder-symbols",
    "list of symbol names that can be reordered",
    "std::string"> {
  let Registry = BoltPassesOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

def BOLTPASS_SkipSymbols : ListOption<
    "reorder-skip-symbols",
    "list of symbol names that cannot be reordered",
    "std::string"> {
  let Registry = BoltPassesOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

def BOLTPASS_ReorderInplace : Option<
    "reorder-data-inplace",
    "reorder data sections in place",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

// ============================================================================
// ReorderFunctions.cpp
// ============================================================================

def BOLTPASS_ReorderFunctions : Option<
    "reorder-functions",
    "reorder and cluster functions (works only with relocations)",
    "bolt::ReorderFunctions::ReorderType"> {
  let Registry = BoltPassesOptsReg;
  let Default = "none";
  let Category = BoltOptCat;
}

def BOLTPASS_ReorderFunctionsUseHotSize : Option<
    "reorder-functions-use-hot-size",
    "use a function's hot size when doing clustering",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "true";
  let Category = BoltOptCat;
}

def BOLTPASS_FunctionOrderFile : Option<
    "function-order",
    "file containing an ordered list of functions to use for function "
    "reordering; can be combined with --reorder-functions algorithms",
    "std::string"> {
  let Registry = BoltPassesOptsReg;
  let Category = BoltOptCat;
}

def BOLTPASS_GenerateFunctionOrderFile : Option<
    "generate-function-order",
    "file to dump the ordered list of functions to use for function "
    "reordering",
    "std::string"> {
  let Registry = BoltPassesOptsReg;
  let Category = BoltOptCat;
}

def BOLTPASS_LinkSectionsFile : Option<
    "generate-link-sections",
    "generate a list of function sections in a format suitable for "
    "inclusion in a linker script",
    "std::string"> {
  let Registry = BoltPassesOptsReg;
  let Category = BoltOptCat;
}

def BOLTPASS_UseEdgeCounts : Option<
    "use-edge-counts",
    "use edge count data when doing clustering",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "true";
  let Category = BoltOptCat;
}

def BOLTPASS_CgFromPerfData : Option<
    "cg-from-perf-data",
    "use perf data directly when constructing the call graph"
    " for stale functions",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "true";
  let Category = BoltOptCat;
}

def BOLTPASS_CgIgnoreRecursiveCalls : Option<
    "cg-ignore-recursive-calls",
    "ignore recursive calls when constructing the call graph",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "true";
  let Category = BoltOptCat;
}

def BOLTPASS_CgUseSplitHotSize : Option<
    "cg-use-split-hot-size",
    "use hot/cold data on basic blocks to determine hot sizes for "
    "call graph functions",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

// ============================================================================
// RetpolineInsertion.cpp
// ============================================================================

def BOLTPASS_InsertRetpolines : Option<
    "insert-retpolines",
    "run retpoline insertion pass",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltCat;
}

def BOLTPASS_RetpolineLfence : Option<
    "retpoline-lfence",
    "determine if lfence instruction should exist in the retpoline",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "true";
  let Hidden = 1;
}

def BOLTPASS_R11Availability : Option<
    "r11-availability",
    "determine the availability of r11 before indirect branches",
    "bolt::RetpolineInsertion::AvailabilityOptions"> {
  let Registry = BoltPassesOptsReg;
  let Default = "never";
  let Category = BoltCat;
}

// ============================================================================
// ShrinkWrapping.cpp
// ============================================================================

def BOLTPASS_ShrinkWrappingThreshold : Option<
    "shrink-wrapping-threshold",
    "Percentage of prologue execution count to use as threshold when"
    " evaluating whether a block is cold enough to be profitable to"
    " move eligible spills there",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "30u";
  let Category = BoltOptCat;
}

// ============================================================================
// SplitFunctions.cpp
// ============================================================================

def BOLTPASS_AggressiveSplitting : Option<
    "split-all-cold",
    "outline as many cold basic blocks as possible",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

def BOLTPASS_SplitAlignThreshold : Option<
    "split-align-threshold",
    "when deciding to split a function, apply this alignment "
    "while doing the size comparison (see -split-threshold). "
    "Default value: 2.",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "2u";
  let Hidden = 1;
}

def BOLTPASS_SplitFunctions : Option<
    "split-functions",
    "split functions into fragments",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

def BOLTPASS_SplitThreshold : Option<
    "split-threshold",
    "split function only if its main size is reduced by more than "
    "given amount of bytes. Default value: 0, i.e. split iff the "
    "size is reduced. Note that on some architectures the size can "
    "increase after splitting.",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "0u";
  let Hidden = 1;
}

def BOLTPASS_CallScale : Option<
    "call-scale",
    "Call score scale coefficient (when --split-strategy=cdsplit)",
    "double"> {
  let Registry = BoltPassesOptsReg;
  let Default = "0.95";
  let Hidden = 1;
}

def BOLTPASS_CallPower : Option<
    "call-power",
    "Call score power (when --split-strategy=cdsplit)",
    "double"> {
  let Registry = BoltPassesOptsReg;
  let Default = "0.05";
  let Hidden = 1;
}

def BOLTPASS_JumpPower : Option<
    "jump-power",
    "Jump score power (when --split-strategy=cdsplit)",
    "double"> {
  let Registry = BoltPassesOptsReg;
  let Default = "0.15";
  let Hidden = 1;
}

// ============================================================================
// StokeInfo.cpp
// ============================================================================

def BOLTPASS_StokeOutputDataFilename : Option<
    "stoke-out",
    "output data (.csv) for Stoke's use",
    "std::string"> {
  let Registry = BoltPassesOptsReg;
}

// ============================================================================
// TailDuplication.cpp
// ============================================================================

def BOLTPASS_TailDuplication : Option<
    "tail-duplication",
    "duplicate unconditional branches that cross a cache line",
    "bolt::TailDuplication::DuplicationMode"> {
  let Registry = BoltPassesOptsReg;
  let Default = "none";
  let Hidden = 1;
}

def BOLTPASS_TailDuplicationMinimumOffset : Option<
    "tail-duplication-minimum-offset",
    "minimum offset needed between block and successor to allow duplication",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "64u";
  let Hidden = 1;
}

def BOLTPASS_TailDuplicationMaximumDuplication : Option<
    "tail-duplication-maximum-duplication",
    "tail blocks whose size (in bytes) exceeds the value are never "
    "duplicated",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "24u";
  let Hidden = 1;
}

def BOLTPASS_TailDuplicationMinimumDuplication : Option<
    "tail-duplication-minimum-duplication",
    "tail blocks with size (in bytes) not exceeding the value are "
    "always duplicated",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "2u";
  let Hidden = 1;
}

def BOLTPASS_TailDuplicationConstCopyPropagation : Option<
    "tail-duplication-const-copy-propagation",
    "enable const and copy propagation after tail duplication",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPASS_TailDuplicationMaxCacheDistance : Option<
    "tail-duplication-max-cache-distance",
    "The weight of backward jumps for ExtTSP value",
    "unsigned"> {
  let Registry = BoltPassesOptsReg;
  let Default = "256u";
  let Hidden = 1;
}

def BOLTPASS_TailDuplicationCacheBackwardWeight : Option<
    "tail-duplication-cache-backward-weight",
    "The maximum distance (in bytes) of backward jumps for ExtTSP value",
    "double"> {
  let Registry = BoltPassesOptsReg;
  let Default = "0.5";
  let Hidden = 1;
}

// ============================================================================
// VeneerElimination.cpp
// ============================================================================

def BOLTPASS_EliminateVeneers : Option<
    "elim-link-veneers",
    "run veneer elimination pass",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "true";
  let Hidden = 1;
}

def BOLTPASS_DropCortexA53843419Veneers : Option<
    "drop-cortex-a53-843419-veneers",
    "inline and drop Cortex-A53 erratum 843419 linker veneers; only "
    "use if the BOLTed binary will not run on Cortex-A53",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "false";
  let Category = BoltOptCat;
}

// ============================================================================
// X86MCPlusBuilder.cpp
// ============================================================================

def BOLTPASS_X86StripRedundantAddressSize : Option<
    "x86-strip-redundant-address-size",
    "Remove redundant Address-Size override prefix",
    "bool"> {
  let Registry = BoltPassesOptsReg;
  let Default = "true";
  let Category = BoltOptCat;
}
