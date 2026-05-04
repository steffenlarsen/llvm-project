//===- BoltProfileOptions.td - CLV2 option defs ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "bolt/Utils/BoltCategories.td"


def BoltProfileOptsReg : Registry<"llvm::bolt::bolt_profile_opts"> {
  // Getter bodies are compiled once, in BoltProfileOptionsGetters.cpp.
  let OutOfLineGetters = 1;
  let ContextTypes = [CTX_BinaryContext];
}

// ---- DataAggregator.cpp options (16) ----

def BOLTPROF_BasicEvents : Option<
    "basic-events",
    "aggregate basic events (without brstack info)",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "false";
  let Category = AggregatorCat;
}

def BOLTPROF_BasicEventsAlias : Option<
    "ba",
    "Alias for --basic-events",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "false";
}

def BOLTPROF_BasicEventsAliasNl : Option<
    "nl",
    "Alias for --basic-events (deprecated. Use --ba)",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPROF_ArmSPE : Option<
    "spe",
    "Enable Arm SPE mode.",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "false";
  let Category = AggregatorCat;
}

def BOLTPROF_ITrace : Option<
    "itrace",
    "Generate brstack info with perf itrace argument",
    "std::string"> {
  let Registry = BoltProfileOptsReg;
  let Category = AggregatorCat;
}

def BOLTPROF_FilterMemProfile : Option<
    "filter-mem-profile",
    "if processing a memory profile, filter out stack or heap accesses "
    "that won't be useful for BOLT to reduce profile file size",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "true";
  let Category = AggregatorCat;
}

def BOLTPROF_ParseMemProfile : Option<
    "parse-mem-profile",
    "enable memory profile parsing if it's present in the input data, "
    "on by default unless `--itrace` is set.",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "true";
  let Category = AggregatorCat;
}

def BOLTPROF_FilterPID : ListOption<
    "pid",
    "only use samples from process with specified PID(s) (comma-separated)",
    "uint64_t"> {
  let Registry = BoltProfileOptsReg;
  let CommaSeparated = 1;
  let Category = AggregatorCat;
}

def BOLTPROF_ImputeTraceFallthrough : Option<
    "impute-trace-fall-through",
    "impute missing fall-throughs for branch-only traces",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "false";
  let Category = AggregatorCat;
}

def BOLTPROF_IgnoreBuildID : Option<
    "ignore-build-id",
    "continue even if build-ids in input binary and perf.data mismatch",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "false";
  let Category = AggregatorCat;
}

def BOLTPROF_IgnoreInterruptLBR : Option<
    "ignore-interrupt-lbr",
    "ignore kernel interrupt LBR that happens asynchronously",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "true";
  let Category = AggregatorCat;
}

def BOLTPROF_MaxSamples : Option<
    "max-samples",
    "maximum number of samples to read from LBR profile",
    "uint64_t"> {
  let Registry = BoltProfileOptsReg;
  let Default = "uint64_t(-1)";
  let Hidden = 1;
}

def BOLTPROF_ReadPreAggregated : Option<
    "pa",
    "skip perf and read data from a pre-aggregated file format",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "false";
  let Category = AggregatorCat;
}

def BOLTPROF_ReadPerfScriptAlias : Alias<"ps", "pa",
    "read pre-parsed perf script output"> {
  let Registry = BoltProfileOptsReg;
}

def BOLTPROF_PerfDataJobs : Option<
    "perfdata-jobs",
    "number of perf data files to process in parallel, "
    "0 = all HW threads (default 4)",
    "unsigned"> {
  let Registry = BoltProfileOptsReg;
  let Default = "4";
  let Category = AggregatorCat;
}

def BOLTPROF_PerfDataJobsAlias : Alias<"pj", "perfdata-jobs",
    "Alias for --perfdata-jobs"> {
  let Registry = BoltProfileOptsReg;
}

def BOLTPROF_TimeAggr : Option<
    "time-aggr",
    "time BOLT aggregator",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "false";
  let Category = AggregatorCat;
}

// ---- DataReader.cpp options (1) ----

def BOLTPROF_DumpData : Option<
    "dump-data",
    "dump parsed bolt data for debugging",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "false";
  let Hidden = 1;
}

// ---- StaleProfileMatching.cpp options (14) ----

def BOLTPROF_InferStaleProfile : Option<
    "infer-stale-profile",
    "Infer counts from stale profile data.",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPROF_StaleMatchingMinMatchedBlock : Option<
    "stale-matching-min-matched-block",
    "Percentage threshold of matched basic blocks at which stale "
    "profile inference is executed.",
    "unsigned"> {
  let Registry = BoltProfileOptsReg;
  let Default = "0u";
  let Hidden = 1;
}

def BOLTPROF_StaleMatchingMaxFuncSize : Option<
    "stale-matching-max-func-size",
    "The maximum size of a function to consider for inference.",
    "unsigned"> {
  let Registry = BoltProfileOptsReg;
  let Default = "10000u";
  let Hidden = 1;
}

def BOLTPROF_StaleMatchingEvenFlowDistribution : Option<
    "stale-matching-even-flow-distribution",
    "Try to evenly distribute flow when there are multiple equally "
    "likely options.",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "true";
  let Hidden = 1;
}

def BOLTPROF_StaleMatchingRebalanceUnknown : Option<
    "stale-matching-rebalance-unknown",
    "Evenly re-distribute flow among unknown subgraphs.",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPROF_StaleMatchingJoinIslands : Option<
    "stale-matching-join-islands",
    "Join isolated components having positive flow.",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "true";
  let Hidden = 1;
}

def BOLTPROF_StaleMatchingCostBlockInc : Option<
    "stale-matching-cost-block-inc",
    "The cost of increasing a block count by one.",
    "unsigned"> {
  let Registry = BoltProfileOptsReg;
  let Default = "150u";
  let Hidden = 1;
}

def BOLTPROF_StaleMatchingCostBlockDec : Option<
    "stale-matching-cost-block-dec",
    "The cost of decreasing a block count by one.",
    "unsigned"> {
  let Registry = BoltProfileOptsReg;
  let Default = "150u";
  let Hidden = 1;
}

def BOLTPROF_StaleMatchingCostJumpInc : Option<
    "stale-matching-cost-jump-inc",
    "The cost of increasing a jump count by one.",
    "unsigned"> {
  let Registry = BoltProfileOptsReg;
  let Default = "150u";
  let Hidden = 1;
}

def BOLTPROF_StaleMatchingCostJumpDec : Option<
    "stale-matching-cost-jump-dec",
    "The cost of decreasing a jump count by one.",
    "unsigned"> {
  let Registry = BoltProfileOptsReg;
  let Default = "150u";
  let Hidden = 1;
}

def BOLTPROF_StaleMatchingCostBlockUnknownInc : Option<
    "stale-matching-cost-block-unknown-inc",
    "The cost of increasing an unknown block count by one.",
    "unsigned"> {
  let Registry = BoltProfileOptsReg;
  let Default = "1u";
  let Hidden = 1;
}

def BOLTPROF_StaleMatchingCostJumpUnknownInc : Option<
    "stale-matching-cost-jump-unknown-inc",
    "The cost of increasing an unknown jump count by one.",
    "unsigned"> {
  let Registry = BoltProfileOptsReg;
  let Default = "140u";
  let Hidden = 1;
}

def BOLTPROF_StaleMatchingCostJumpUnknownFTInc : Option<
    "stale-matching-cost-jump-unknown-ft-inc",
    "The cost of increasing an unknown fall-through jump count by one.",
    "unsigned"> {
  let Registry = BoltProfileOptsReg;
  let Default = "3u";
  let Hidden = 1;
}

def BOLTPROF_StaleMatchingWithPseudoProbes : Option<
    "stale-matching-with-pseudo-probes",
    "Turns on stale matching with block pseudo probes.",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "false";
  let Hidden = 1;
}

// ---- YAMLProfileReader.cpp options (5) ----

def BOLTPROF_NameSimilarityFunctionMatchingThreshold : Option<
    "name-similarity-function-matching-threshold",
    "Match functions using namespace and edit distance",
    "unsigned"> {
  let Registry = BoltProfileOptsReg;
  let Default = "0u";
  let Hidden = 1;
}

def BOLTPROF_IgnoreHash : Option<
    "profile-ignore-hash",
    "ignore hash while reading function profile",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPROF_MatchProfileWithFunctionHash : Option<
    "match-profile-with-function-hash",
    "Match profile with function hash",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPROF_MatchWithCallGraph : Option<
    "match-with-call-graph",
    "Match functions with call graph",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def BOLTPROF_ProfileUseDFS : Option<
    "profile-use-dfs",
    "use DFS order for YAML profile",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "false";
  let Hidden = 1;
}

// ---- YAMLProfileWriter.cpp options (1) ----

def BOLTPROF_ProfileWritePseudoProbes : Option<
    "profile-write-pseudo-probes",
    "Use pseudo probes in profile generation",
    "bool"> {
  let Registry = BoltProfileOptsReg;
  let Default = "false";
  let Hidden = 1;
}
