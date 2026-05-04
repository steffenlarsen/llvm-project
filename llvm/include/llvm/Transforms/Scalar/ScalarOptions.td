//===- ScalarOptions.td - CLV2 option defs for Scalar ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def ScalarOptsReg : Registry<"llvm::sc_opts"> { let OutOfLineGetters = 1; }

// --- ADCE.cpp ---

def SC_AdceRemoveControlFlow : Option<
    "adce-remove-control-flow", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_AdceRemoveLoops : Option<
    "adce-remove-loops", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

// --- CallSiteSplitting.cpp ---

def SC_CallsiteSplittingDuplicationThreshold : Option<
    "callsite-splitting-duplication-threshold",
    "Only allow instructions before a call, if "
    "their cost is below DuplicationThreshold",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "5";
  let Hidden = 1;
}

// --- ConstantHoisting.cpp ---

def SC_ConsthoistWithBlockFrequency : Option<
    "consthoist-with-block-frequency",
    "Enable the use of the block frequency analysis to reduce the "
    "chance to execute const materialization more frequently than "
    "without hoisting.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_ConsthoistGep : Option<
    "consthoist-gep",
    "Try hoisting constant gep expressions",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_ConsthoistMinNumToRebase : Option<
    "consthoist-min-num-to-rebase",
    "Do not rebase if number of dependent constants of a Base is less "
    "than this number.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

// --- ConstraintElimination.cpp ---

def SC_ConstraintEliminationMaxRows : Option<
    "constraint-elimination-max-rows",
    "Maximum number of rows to keep in constraint system",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "500";
  let Hidden = 1;
}

def SC_ConstraintEliminationDumpReproducers : Option<
    "constraint-elimination-dump-reproducers",
    "Dump IR to reproduce successful transformations.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

// --- DeadStoreElimination.cpp ---

def SC_EnableDsePartialOverwriteTracking : Option<
    "enable-dse-partial-overwrite-tracking",
    "Enable partial-overwrite tracking in DSE",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_EnableDsePartialStoreMerging : Option<
    "enable-dse-partial-store-merging",
    "Enable partial store merging in DSE",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_DseMemoryssaScanlimit : Option<
    "dse-memoryssa-scanlimit",
    "The number of memory instructions to scan for "
    "dead store elimination (default = 150)",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "150";
  let Hidden = 1;
}

def SC_DseMemoryssaWalklimit : Option<
    "dse-memoryssa-walklimit",
    "The maximum number of steps while walking upwards to find "
    "MemoryDefs that may be killed (default = 90)",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "90";
  let Hidden = 1;
}

def SC_DseMemoryssaPartialStoreLimit : Option<
    "dse-memoryssa-partial-store-limit",
    "The maximum number candidates that only partially overwrite the "
    "killing MemoryDef to consider"
    " (default = 5)",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "5";
  let Hidden = 1;
}

def SC_DseMemoryssaDefsPerBlockLimit : Option<
    "dse-memoryssa-defs-per-block-limit",
    "The number of MemoryDefs we consider as candidates to eliminated "
    "other stores per basic block (default = 5000)",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "5000";
  let Hidden = 1;
}

def SC_DseMemoryssaSamebbCost : Option<
    "dse-memoryssa-samebb-cost",
    "The cost of a step in the same basic block as the killing MemoryDef"
    "(default = 1)",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_DseMemoryssaOtherbbCost : Option<
    "dse-memoryssa-otherbb-cost",
    "The cost of a step in a different basic "
    "block than the killing MemoryDef"
    "(default = 5)",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "5";
  let Hidden = 1;
}

def SC_DseMemoryssaPathCheckLimit : Option<
    "dse-memoryssa-path-check-limit",
    "The maximum number of blocks to check when trying to prove that "
    "all paths to an exit go through a killing block (default = 50)",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "50";
  let Hidden = 1;
}

def SC_DseOptimizeMemoryssa : Option<
    "dse-optimize-memoryssa",
    "Allow DSE to optimize memory accesses.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_EnableDseInitializesAttrImprovement : Option<
    "enable-dse-initializes-attr-improvement",
    "Enable the initializes attr improvement in DSE",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_DseMaxDomCondDepth : Option<
    "dse-max-dom-cond-depth",
    "Max dominator tree recursion depth for eliminating redundant "
    "stores via dominating conditions",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "1024";
  let Hidden = 1;
}

// --- DFAJumpThreading.cpp ---

def SC_DfaJumpViewCfgBefore : Option<
    "dfa-jump-view-cfg-before",
    "View the CFG before DFA Jump Threading",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_DfaEarlyExitHeuristic : Option<
    "dfa-early-exit-heuristic",
    "Exit early if an unpredictable value come from the same loop",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_DfaMaxPathLength : Option<
    "dfa-max-path-length",
    "Max number of blocks searched to find a threading path",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "20";
  let Hidden = 1;
}

def SC_DfaMaxNumVisitedPaths : Option<
    "dfa-max-num-visited-paths",
    "Max number of blocks visited while enumerating paths around a switch",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "2500";
  let Hidden = 1;
}

def SC_DfaMaxNumPaths : Option<
    "dfa-max-num-paths",
    "Max number of paths enumerated around a switch",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "200";
  let Hidden = 1;
}

def SC_DfaCostThreshold : Option<
    "dfa-cost-threshold",
    "Maximum cost accepted for the transformation",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "50";
  let Hidden = 1;
}

def SC_DfaMaxClonedRate : Option<
    "dfa-max-cloned-rate",
    "Maximum cloned instructions rate accepted for the transformation",
    "double"> {
  let Registry = ScalarOptsReg;
  let Default = "7.5";
  let Hidden = 1;
}

def SC_DfaMaxOutUseBlocks : Option<
    "dfa-max-out-use-blocks",
    "Maximum unduplicated blocks with outer uses "
    "accepted for the transformation",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "40";
  let Hidden = 1;
}

// --- EarlyCSE.cpp ---

def SC_EarlyCseMssaOptimizationCap : Option<
    "earlycse-mssa-optimization-cap",
    "Enable imprecision in EarlyCSE in pathological cases, in exchange "
    "for faster compile. Caps the MemorySSA clobbering calls.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "500";
  let Hidden = 1;
}

def SC_EarlyCseDebugHash : Option<
    "earlycse-debug-hash",
    "Perform extra assertion checking to verify that SimpleValue's hash "
    "function is well-behaved w.r.t. its isEqual predicate",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

// --- ExpandMemCmp.cpp ---

def SC_MemcmpNumLoadsPerBlock : Option<
    "memcmp-num-loads-per-block",
    "The number of loads per basic block for inline expansion of "
    "memcmp that is only being compared against zero.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_MaxLoadsPerMemcmp : Option<
    "max-loads-per-memcmp",
    "Set maximum number of loads used in expanded memcmp",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_MaxLoadsPerMemcmpOptSize : Option<
    "max-loads-per-memcmp-opt-size",
    "Set maximum number of loads used in expanded memcmp for -Os/Oz",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

// --- Float2Int.cpp ---

def SC_Float2IntMaxIntegerBw : Option<
    "float2int-max-integer-bw",
    "Max integer bitwidth to consider in float2int"
    "(default=64)",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "64";
  let Hidden = 1;
}

// --- GuardWidening.cpp ---

def SC_GuardWideningWidenBranchGuards : Option<
    "guard-widening-widen-branch-guards",
    "Whether or not we should widen guards  "
    "expressed as branches by widenable conditions",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- GVN.cpp ---

def SC_EnablePre : Option<
    "enable-scalar-pre", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_EnableLoadPre : Option<
    "enable-load-pre", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
}

def SC_EnableLoadInLoopPre : Option<
    "enable-load-in-loop-pre", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
}

def SC_EnableSplitBackedgeInLoadPre : Option<
    "enable-split-backedge-in-load-pre", "",
    "bool"> {
  let Registry = ScalarOptsReg;
}

def SC_EnableGvnMemdep : Option<
    "enable-gvn-memdep", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
}

def SC_EnableGvnMemoryssa : Option<
    "enable-gvn-memoryssa", "",
    "bool"> {
  let Registry = ScalarOptsReg;
}

def SC_GvnScanUsersLimit : Option<
    "gvn-scan-users-limit",
    "The number of memory accesses to scan in a block in reaching memory "
    "values analysis (default = 100)",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "100";
  let Hidden = 1;
}

def SC_GvnMaxNumReachingBlocks : Option<
    "gvn-max-num-reaching-blocks",
    "Max number of blocks scanned per load in the MemorySSA reaching-value "
    "analysis (default = 200)",
    "uint32_t"> {
  let Registry = ScalarOptsReg;
  let Default = "200";
  let Hidden = 1;
}

def SC_GvnMaxNumDeps : Option<
    "gvn-max-num-deps",
    "Max number of dependences to attempt Load PRE (default = 100)",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "100";
  let Hidden = 1;
}

def SC_GvnMaxBlockSpeculations : Option<
    "gvn-max-block-speculations",
    "Max number of blocks we're willing to speculate on (and recurse "
    "into) when deducing if a value is fully available or not in GVN "
    "(default = 600)",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "600";
  let Hidden = 1;
}

def SC_GvnMaxNumVisitedInsts : Option<
    "gvn-max-num-visited-insts",
    "Max number of visited instructions when trying to find "
    "dominating value of select dependency (default = 100)",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "100";
  let Hidden = 1;
}

def SC_GvnMaxNumInsns : Option<
    "gvn-max-num-insns",
    "Max number of instructions to scan in each basic block in GVN "
    "(default = 100)",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "100";
  let Hidden = 1;
}

// --- GVNHoist.cpp ---

def SC_GvnMaxHoisted : Option<
    "gvn-max-hoisted",
    "Max number of instructions to hoist "
    "(default unlimited = -1)",
    "int"> {
  let Registry = ScalarOptsReg;
  let Default = "-1";
  let Hidden = 1;
}

def SC_GvnHoistMaxBbs : Option<
    "gvn-hoist-max-bbs",
    "Max number of basic blocks on the path between "
    "hoisting locations (default = 4, unlimited = -1)",
    "int"> {
  let Registry = ScalarOptsReg;
  let Default = "4";
  let Hidden = 1;
}

def SC_GvnHoistMaxDepth : Option<
    "gvn-hoist-max-depth",
    "Hoist instructions from the beginning of the BB up to the "
    "maximum specified depth (default = 100, unlimited = -1)",
    "int"> {
  let Registry = ScalarOptsReg;
  let Default = "100";
  let Hidden = 1;
}

def SC_GvnHoistMaxChainLength : Option<
    "gvn-hoist-max-chain-length",
    "Maximum length of dependent chains to hoist "
    "(default = 10, unlimited = -1)",
    "int"> {
  let Registry = ScalarOptsReg;
  let Default = "10";
  let Hidden = 1;
}

// --- InductiveRangeCheckElimination.cpp ---

def SC_IrceLoopSizeCutoff : Option<
    "irce-loop-size-cutoff", "",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "64";
  let Hidden = 1;
}

def SC_IrcePrintChangedLoops : Option<
    "irce-print-changed-loops", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_IrcePrintRangeChecks : Option<
    "irce-print-range-checks", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_IrceSkipProfitabilityChecks : Option<
    "irce-skip-profitability-checks", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_IrceMinEliminatedChecks : Option<
    "irce-min-eliminated-checks", "",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "10";
  let Hidden = 1;
}

def SC_IrceAllowUnsignedLatch : Option<
    "irce-allow-unsigned-latch", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_IrceAllowNarrowLatch : Option<
    "irce-allow-narrow-latch",
    "If set to true, IRCE may eliminate wide range checks in loops "
    "with narrow latch condition.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_IrceMaxTypeSizeForOverflowCheck : Option<
    "irce-max-type-size-for-overflow-check",
    "Maximum size of range check type for which can be produced runtime "
    "overflow check of its limit's computation",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "32";
  let Hidden = 1;
}

def SC_IrcePrintScaledBoundaryRangeChecks : Option<
    "irce-print-scaled-boundary-range-checks", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

// --- IndVarSimplify.cpp ---

def ReplaceExitValV2NeverRepl : EnumValue<"never",
    "never replace exit value"> {
  let CppName = "NeverRepl";
}

def ReplaceExitValV2OnlyCheapRepl : EnumValue<"cheap",
    "only replace exit value when the cost is cheap"> {
  let CppName = "OnlyCheapRepl";
}

def ReplaceExitValV2UnusedIndVarInLoop : EnumValue<"unusedindvarinloop",
    "only replace exit value when it is an unused "
    "induction variable in the loop and has cheap replacement cost"> {
  let CppName = "UnusedIndVarInLoop";
}

def ReplaceExitValV2NoHardUse : EnumValue<"noharduse",
    "only replace exit values when loop def likely dead"> {
  let CppName = "NoHardUse";
}

def ReplaceExitValV2AlwaysRepl : EnumValue<"always",
    "always replace exit value whenever possible"> {
  let CppName = "AlwaysRepl";
}

def ReplaceExitValV2Enum : EnumType<"ReplaceExitValV2",
    [ReplaceExitValV2NeverRepl, ReplaceExitValV2OnlyCheapRepl,
     ReplaceExitValV2UnusedIndVarInLoop, ReplaceExitValV2NoHardUse,
     ReplaceExitValV2AlwaysRepl]>;

def SC_ReplExitVal : Option<
    "replexitval",
    "Choose the strategy to replace exit value in IndVarSimplify",
    "ReplaceExitValV2"> {
  let Registry = ScalarOptsReg;
  let Default = "cheap";
  let Hidden = 1;
}

def SC_IndvarsPostIncrementRanges : Option<
    "indvars-post-increment-ranges",
    "Use post increment control-dependent ranges in IndVarSimplify",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_DisableLftr : Option<
    "disable-lftr",
    "Disable Linear Function Test Replace optimization",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_IndvarsPredicateLoops : Option<
    "indvars-predicate-loops",
    "Predicate conditions in read only loops",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_IndvarsPredicateLoopTraps : Option<
    "indvars-predicate-loop-traps",
    "Predicate conditions that trap in loops with only local writes",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_IndvarsWidenIndvars : Option<
    "indvars-widen-indvars",
    "Allow widening of indvars to eliminate s/zext",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- InferAddressSpaces.cpp ---

def SC_AssumeDefaultIsFlatAddrspace : Option<
    "assume-default-is-flat-addrspace",
    "The default address space is assumed as the flat address space. "
    "This is mainly for test purpose.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let ReallyHidden = 1;
}

// --- JumpTableToSwitch.cpp ---

def SC_JumpTableToSwitchSizeThreshold : Option<
    "jump-table-to-switch-size-threshold",
    "Only split jump tables with size less or "
    "equal than JumpTableSizeThreshold.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "10";
  let Hidden = 1;
}

def SC_JumpTableToSwitchFunctionSizeThreshold : Option<
    "jump-table-to-switch-function-size-threshold",
    "Only split jump tables containing functions whose sizes are less "
    "or equal than this threshold.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "50";
  let Hidden = 1;
}

// --- JumpThreading.cpp ---

def SC_JumpThreadingThreshold : Option<
    "jump-threading-threshold",
    "Max block size to duplicate for jump threading",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "6";
  let Hidden = 1;
}

def SC_JumpThreadingImplicationSearchThreshold : Option<
    "jump-threading-implication-search-threshold",
    "The number of predecessors to search for a stronger "
    "condition to use to thread over a weaker condition",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "3";
  let Hidden = 1;
}

def SC_JumpThreadingPhiThreshold : Option<
    "jump-threading-phi-threshold",
    "Max PHIs in BB to duplicate for jump threading",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "76";
  let Hidden = 1;
}

def SC_JumpThreadingAcrossLoopHeaders : Option<
    "jump-threading-across-loop-headers",
    "Allow JumpThreading to thread across loop headers, for testing",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

// --- LICM.cpp ---

def SC_DisableLicmPromotion : Option<
    "disable-licm-promotion",
    "Disable memory promotion in LICM pass",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_LicmControlFlowHoisting : Option<
    "licm-control-flow-hoisting",
    "Enable control flow (and PHI) hoisting in LICM",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_LicmForceThreadModelSingle : Option<
    "licm-force-thread-model-single",
    "Force thread model single in LICM pass",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_LicmMaxNumUsesTraversed : Option<
    "licm-max-num-uses-traversed",
    "Max num uses visited for identifying load "
    "invariance in loop using invariant start (default = 8)",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def SC_LicmMaxNumFpReassociations : Option<
    "licm-max-num-fp-reassociations",
    "Set upper limit for the number of transformations performed "
    "during a single round of hoisting the reassociated expressions.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "5";
  let Hidden = 1;
}

def SC_LicmMaxNumIntReassociations : Option<
    "licm-max-num-int-reassociations",
    "Set upper limit for the number of transformations performed "
    "during a single round of hoisting the reassociated expressions.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "5";
  let Hidden = 1;
}

def SC_LicmMssaOptimizationCap : Option<
    "licm-mssa-optimization-cap",
    "Enable imprecision in LICM in pathological cases, in exchange "
    "for faster compile. Caps the MemorySSA clobbering calls.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "100";
  let Hidden = 1;
}

def SC_LicmMssaMaxAccPromotion : Option<
    "licm-mssa-max-acc-promotion",
    "[LICM & MemorySSA] When MSSA in LICM is disabled, this has no "
    "effect. When MSSA in LICM is enabled, then this is the maximum "
    "number of accesses allowed to be present in a loop in order to "
    "enable memory promotion.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "250";
  let Hidden = 1;
}

// --- LoopDataPrefetch.cpp ---

def SC_LoopPrefetchWrites : Option<
    "loop-prefetch-writes",
    "Prefetch write addresses",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_PrefetchDistance : Option<
    "prefetch-distance",
    "Number of instructions to prefetch ahead",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_MinPrefetchStride : Option<
    "min-prefetch-stride",
    "Min stride to add prefetches",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_MaxPrefetchItersAhead : Option<
    "max-prefetch-iters-ahead",
    "Max number of iterations to prefetch ahead",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

// --- LoopDeletion.cpp ---

def SC_LoopDeletionEnableSymbolicExecution : Option<
    "loop-deletion-enable-symbolic-execution",
    "Break backedge through symbolic execution of 1st iteration "
    "attempting to prove that the backedge is never taken",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- LoopDistribute.cpp ---

def SC_LoopDistributeVerify : Option<
    "loop-distribute-verify",
    "Turn on DominatorTree and LoopInfo verification "
    "after Loop Distribution",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_LoopDistributeNonIfConvertible : Option<
    "loop-distribute-non-if-convertible",
    "Whether to distribute into a loop that may not be "
    "if-convertible by the loop vectorizer",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_LoopDistributeSCEVCheckThreshold : Option<
    "loop-distribute-scev-check-threshold",
    "The maximum number of SCEV checks allowed for Loop "
    "Distribution",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def SC_LoopDistributeSCEVCheckThresholdWithPragma : Option<
    "loop-distribute-scev-check-threshold-with-pragma",
    "The maximum number of SCEV checks allowed for Loop "
    "Distribution for loop marked with #pragma clang loop "
    "distribute(enable)",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "128";
  let Hidden = 1;
}

def SC_EnableLoopDistribute : Option<
    "enable-loop-distribute",
    "Enable the new, experimental LoopDistribution Pass",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

// --- LoopFlatten.cpp ---

def SC_LoopFlattenCostThreshold : Option<
    "loop-flatten-cost-threshold",
    "Limit on the cost of instructions that can be repeated due to "
    "loop flattening",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def SC_LoopFlattenAssumeNoOverflow : Option<
    "loop-flatten-assume-no-overflow",
    "Assume that the product of the two iteration "
    "trip counts will never overflow",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_LoopFlattenWidenIv : Option<
    "loop-flatten-widen-iv",
    "Widen the loop induction variables, if possible, so "
    "overflow checks won't reject flattening",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_LoopFlattenVersionLoops : Option<
    "loop-flatten-version-loops",
    "Version loops if flattened loop could overflow",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- LoopFuse.cpp ---

def FusionDependenceAnalysisChoiceV2SCEV : EnumValue<"scev",
    "Use the scalar evolution interface"> {
  let CppName = "SCEV";
}

def FusionDependenceAnalysisChoiceV2DA : EnumValue<"da",
    "Use the dependence analysis interface"> {
  let CppName = "DA";
}

def FusionDependenceAnalysisChoiceV2All : EnumValue<"all",
    "Use all available analyses"> {
  let CppName = "All";
}

def FusionDependenceAnalysisChoiceV2Enum : EnumType<
    "FusionDependenceAnalysisChoiceV2",
    [FusionDependenceAnalysisChoiceV2SCEV,
     FusionDependenceAnalysisChoiceV2DA,
     FusionDependenceAnalysisChoiceV2All]>;

def SC_LoopFusionDependenceAnalysis : Option<
    "loop-fusion-dependence-analysis",
    "Which dependence analysis should loop fusion use?",
    "FusionDependenceAnalysisChoiceV2"> {
  let Registry = ScalarOptsReg;
  let Default = "da";
  let Hidden = 1;
}

def SC_LoopFusionPeelMaxCount : Option<
    "loop-fusion-peel-max-count",
    "Max number of iterations to be peeled from a loop, such that "
    "fusion can take place",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

// --- LoopIdiomRecognize.cpp ---

def SC_DisableLoopIdiomAll : Option<
    "disable-loop-idiom-all",
    "Options to disable Loop Idiom Recognize Pass.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let ReallyHidden = 1;
}

def SC_DisableLoopIdiomMemset : Option<
    "disable-loop-idiom-memset",
    "Proceed with loop idiom recognize pass, but do "
    "not convert loop(s) to memset.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let ReallyHidden = 1;
}

def SC_DisableLoopIdiomMemcpy : Option<
    "disable-loop-idiom-memcpy",
    "Proceed with loop idiom recognize pass, but do "
    "not convert loop(s) to memcpy.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let ReallyHidden = 1;
}

def SC_DisableLoopIdiomStrlen : Option<
    "disable-loop-idiom-strlen",
    "Proceed with loop idiom recognize pass, but do "
    "not convert loop(s) to strlen.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let ReallyHidden = 1;
}

def SC_DisableLoopIdiomWcslen : Option<
    "disable-loop-idiom-wcslen",
    "Proceed with loop idiom recognize pass, "
    "enable conversion of loop(s) to wcslen.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let ReallyHidden = 1;
}

def SC_DisableLoopIdiomHashrecognize : Option<
    "disable-loop-idiom-hashrecognize",
    "Proceed with loop idiom recognize pass, "
    "but do not optimize CRC loops.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let ReallyHidden = 1;
}

def SC_UseLirCodeSizeHeurs : Option<
    "use-lir-code-size-heurs",
    "Use loop idiom recognition code size heuristics when compiling "
    "with -Os/-Oz",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_LoopIdiomForceMemsetPatternIntrinsic : Option<
    "loop-idiom-force-memset-pattern-intrinsic",
    "Use memset.pattern intrinsic whenever possible",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_CRCStrategyDisable : EnumValue<"disable",
    "Do not optimize CRC loops"> {
  let CppName = "Disable";
}
def SC_CRCStrategyAuto : EnumValue<"auto",
    "Use costing to determine strategy"> {
  let CppName = "Auto";
}
def SC_CRCStrategyTable : EnumValue<"table",
    "Use a Sarwate table when possible"> {
  let CppName = "Table";
}
def SC_CRCStrategyClmul : EnumValue<"clmul",
    "Use carry-less multiplication when possible"> {
  let CppName = "Clmul";
}
def SC_CRCStrategyKindEnum : EnumType<"CRCStrategyKind",
    [SC_CRCStrategyDisable, SC_CRCStrategyAuto, SC_CRCStrategyTable,
     SC_CRCStrategyClmul]>;

def SC_CRCStrategy : Option<
    "loop-idiom-crc-strategy",
    "Preferred strategy for optimizing CRC loops",
    "CRCStrategyKind"> {
  let Registry = ScalarOptsReg;
  let Default = "auto";
  let Hidden = 1;
}

// --- LoopInterchange.cpp ---

def SC_LoopInterchangeThreshold : Option<
    "loop-interchange-threshold",
    "Interchange if you gain more than this number",
    "int"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_LoopInterchangeMaxMemInstrCount : Option<
    "loop-interchange-max-mem-instr-ratio",
    "Maximum number of load/store instructions squared in relation to "
    "the total number of instructions. Higher value may lead to more "
    "interchanges at the cost of compile-time",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "4";
  let Hidden = 1;
}

def SC_LoopInterchangeMinLoopNestDepth : Option<
    "loop-interchange-min-loop-nest-depth",
    "Minimum depth of loop nest considered for the transform",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def SC_LoopInterchangeMaxLoopNestDepth : Option<
    "loop-interchange-max-loop-nest-depth",
    "Maximum depth of loop nest considered for the transform",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "10";
  let Hidden = 1;
}

def LoopInterchangeRuleTyPerLoopCacheAnalysis : EnumValue<"cache",
    "Prioritize loop cache cost"> {
  let CppName = "PerLoopCacheAnalysis";
}

def LoopInterchangeRuleTyPerInstrOrderCost : EnumValue<"instorder",
    "Prioritize the IVs order of each instruction"> {
  let CppName = "PerInstrOrderCost";
}

def LoopInterchangeRuleTyForVectorization : EnumValue<"vectorize",
    "Prioritize vectorization"> {
  let CppName = "ForVectorization";
}

def LoopInterchangeRuleTyIgnore : EnumValue<"ignore",
    "Ignore profitability, force interchange (does not "
    "work with other options)"> {
  let CppName = "Ignore";
}

def LoopInterchangeRuleTyEnum : EnumType<"LoopInterchangeRuleTy",
    [LoopInterchangeRuleTyPerLoopCacheAnalysis,
     LoopInterchangeRuleTyPerInstrOrderCost,
     LoopInterchangeRuleTyForVectorization,
     LoopInterchangeRuleTyIgnore]>;

def SC_LoopInterchangeProfitabilities : ListOption<
    "loop-interchange-profitabilities",
    "List of profitability heuristics to be used. They are applied in "
    "the given order",
    "LoopInterchangeRuleTy"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

def SC_LoopInterchangeReductionToMem : Option<
    "loop-interchange-reduction-to-mem",
    "Support for the inner-loop reduction pattern.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

// --- LoopLoadElimination.cpp ---

def SC_RuntimeCheckPerLoopLoadElim : Option<
    "runtime-check-per-loop-load-elim",
    "Max number of memchecks allowed per eliminated load on average",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_LoopLoadEliminationSCEVCheckThreshold : Option<
    "loop-load-elimination-scev-check-threshold",
    "The maximum number of SCEV checks allowed for Loop "
    "Load Elimination",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "8";
  let Hidden = 1;
}

// --- LoopPredication.cpp ---

def SC_LoopPredicationEnableIvTruncation : Option<
    "loop-predication-enable-iv-truncation", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_LoopPredicationEnableCountDownLoop : Option<
    "loop-predication-enable-count-down-loop", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_LoopPredicationSkipProfitabilityChecks : Option<
    "loop-predication-skip-profitability-checks", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_LoopPredicationLatchProbabilityScale : Option<
    "loop-predication-latch-probability-scale",
    "scale factor for the latch probability. Value should be greater "
    "than 1. Lower values are ignored",
    "float"> {
  let Registry = ScalarOptsReg;
  let Default = "2.0";
  let Hidden = 1;
}

def SC_LoopPredicationPredicateWidenableBranchesToDeopt : Option<
    "loop-predication-predicate-widenable-branches-to-deopt",
    "Whether or not we should predicate guards "
    "expressed as widenable branches to deoptimize blocks",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_LoopPredicationInsertAssumesOfPredicatedGuardsConditions : Option<
    "loop-predication-insert-assumes-of-predicated-guards-conditions",
    "Whether or not we should insert assumes of conditions of "
    "predicated guards",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- LoopRotation.cpp ---

def SC_RotationMaxHeaderSize : Option<
    "rotation-max-header-size",
    "The default maximum header size for automatic loop rotation",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "16";
  let Hidden = 1;
}

def SC_RotationPrepareForLto : Option<
    "rotation-prepare-for-lto",
    "Run loop-rotation in the prepare-for-lto stage. This option "
    "should be used for testing only.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_EnableLoopHeaderDuplicationAtMinSize : Option<
    "enable-loop-header-duplication-at-minsize",
    "Enable loop header duplication even for minsize",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

// --- LoopSimplifyCFG.cpp ---

def SC_EnableLoopSimplifyCfgTermFolding : Option<
    "enable-loop-simplifycfg-term-folding", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
}

// --- LoopSink.cpp ---

def SC_SinkFreqPercentThreshold : Option<
    "sink-freq-percent-threshold",
    "Do not sink instructions that require cloning unless they "
    "execute less than this percent of the time.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "90";
  let Hidden = 1;
}

def SC_MaxUsesForSinking : Option<
    "max-uses-for-sinking",
    "Do not sink instructions that have too many uses.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "30";
  let Hidden = 1;
}

// --- LoopStrengthReduce.cpp ---

def SC_EnableLsrPhielim : Option<
    "enable-lsr-phielim",
    "Enable LSR phi elimination",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_LsrInsnsCost : Option<
    "lsr-insns-cost",
    "Add instruction count to a LSR cost model",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_LsrExpNarrow : Option<
    "lsr-exp-narrow",
    "Narrow LSR complex solution using"
    " expectation of registers number",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_LsrFilterSameScaledReg : Option<
    "lsr-filter-same-scaled-reg",
    "Narrow LSR search space by filtering non-optimal formulae"
    " with the same ScaledReg and Scale",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AddressingModeKindV2None : EnumValue<"none",
    "Don't prefer any addressing mode"> {
  let CppName = "None";
}

def AddressingModeKindV2PreIndexed : EnumValue<"preindexed",
    "Prefer pre-indexed addressing mode"> {
  let CppName = "PreIndexed";
}

def AddressingModeKindV2PostIndexed : EnumValue<"postindexed",
    "Prefer post-indexed addressing mode"> {
  let CppName = "PostIndexed";
}

def AddressingModeKindV2All : EnumValue<"all",
    "Consider all addressing modes"> {
  let CppName = "All";
}

def AddressingModeKindV2Enum : EnumType<"AddressingModeKindV2",
    [AddressingModeKindV2None, AddressingModeKindV2PreIndexed,
     AddressingModeKindV2PostIndexed, AddressingModeKindV2All]>;

def SC_LsrPreferredAddressingMode : Option<
    "lsr-preferred-addressing-mode",
    "A flag that overrides the target's preferred addressing mode.",
    "AddressingModeKindV2"> {
  let Registry = ScalarOptsReg;
  let Default = "none";
  let Hidden = 1;
}

def SC_LsrComplexityLimit : Option<
    "lsr-complexity-limit",
    "LSR search space complexity limit",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "65535";
  let Hidden = 1;
}

def SC_LsrSetupcostDepthLimit : Option<
    "lsr-setupcost-depth-limit",
    "The limit on recursion depth for LSRs setup cost",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "7";
  let Hidden = 1;
}

def SC_LsrDropSolution : Option<
    "lsr-drop-solution",
    "Attempt to drop solution if it is less profitable",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_LsrEnableVscaleImmediates : Option<
    "lsr-enable-vscale-immediates",
    "Enable analysis of vscale-relative immediates in LSR",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_LsrDropScaledRegForVscale : Option<
    "lsr-drop-scaled-reg-for-vscale",
    "Avoid using scaled registers with vscale-relative addressing",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_StressIvChain : Option<
    "stress-ivchain", "Stress test IV chain generation",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

// --- LoopUnrollAndJamPass.cpp ---

def SC_AllowUnrollAndJam : Option<
    "allow-unroll-and-jam",
    "Allows loops to be unroll-and-jammed.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_UnrollAndJamCount : Option<
    "unroll-and-jam-count",
    "Use this unroll count for all loops including those with "
    "unroll_and_jam_count pragma values, for testing purposes",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_UnrollAndJamThreshold : Option<
    "unroll-and-jam-threshold",
    "Threshold to use for inner loop when doing unroll and jam.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "60";
  let Hidden = 1;
}

def SC_PragmaUnrollAndJamThreshold : Option<
    "pragma-unroll-and-jam-threshold",
    "Unrolled size limit for loops with an unroll_and_jam(full) or "
    "unroll_count pragma.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "1024";
  let Hidden = 1;
}

// --- LoopUnrollPass.cpp ---

def SC_ForgetScevLoopUnroll : Option<
    "forget-scev-loop-unroll",
    "Forget everything in SCEV when doing LoopUnroll, instead of just"
    " the current top-most loop. This is sometimes preferred to reduce"
    " compile time.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_UnrollThreshold : Option<
    "unroll-threshold",
    "The cost threshold for loop unrolling",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_UnrollOptSizeThreshold : Option<
    "unroll-optsize-threshold",
    "The cost threshold for loop unrolling when optimizing for "
    "size",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_UnrollPartialThreshold : Option<
    "unroll-partial-threshold",
    "The cost threshold for partial loop unrolling",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_UnrollMaxPercentThresholdBoost : Option<
    "unroll-max-percent-threshold-boost",
    "The maximum 'boost' (represented as a percentage >= 100) applied "
    "to the threshold when aggressively unrolling a loop due to the "
    "dynamic cost savings. If completely unrolling a loop will reduce "
    "the total runtime from X to Y, we boost the loop unroll "
    "threshold to DefaultThreshold*std::min(MaxPercentThresholdBoost, "
    "X/Y). This limit avoids excessive code bloat.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "400";
  let Hidden = 1;
}

def SC_UnrollMaxIterationCountToAnalyze : Option<
    "unroll-max-iteration-count-to-analyze",
    "Don't allow loop unrolling to simulate more than this number of "
    "iterations when checking full unroll profitability",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "10";
  let Hidden = 1;
}

def SC_UnrollCount : Option<
    "unroll-count",
    "Use this unroll count for all loops including those with "
    "unroll_count pragma values, for testing purposes",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_UnrollMaxCount : Option<
    "unroll-max-count",
    "Set the max unroll count for partial and runtime unrolling, for"
    "testing purposes",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_UnrollFullMaxCount : Option<
    "unroll-full-max-count",
    "Set the max unroll count for full unrolling, for testing purposes",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_UnrollAllowPartial : Option<
    "unroll-allow-partial",
    "Allows loops to be partially unrolled until "
    "-unroll-threshold loop size is reached.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_UnrollAllowRemainder : Option<
    "unroll-allow-remainder",
    "Allow generation of a loop remainder (extra iterations) "
    "when unrolling a loop.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_UnrollRuntime : Option<
    "unroll-runtime",
    "Unroll loops with run-time trip counts",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_UnrollMaxUpperbound : Option<
    "unroll-max-upperbound",
    "The max of trip count upper bound that is considered in unrolling",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def SC_PragmaUnrollThreshold : Option<
    "pragma-unroll-threshold",
    "Unrolled size limit for loops with unroll metadata "
    "(full, enable, or count).",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "16384";
  let Hidden = 1;
}

def SC_FlatLoopTripcountThreshold : Option<
    "flat-loop-tripcount-threshold",
    "If the runtime tripcount for the loop is lower than the "
    "threshold, the loop is considered as flat and will be less "
    "aggressively unrolled.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "5";
  let Hidden = 1;
}

def SC_UnrollRemainder : Option<
    "unroll-remainder",
    "Allow the loop remainder to be unrolled.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_UnrollRevisitChildLoops : Option<
    "unroll-revisit-child-loops",
    "Enqueue and re-visit child loops in the loop PM after unrolling. "
    "This shouldn't typically be needed as child loops (or their "
    "clones) were already visited.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_UnrollThresholdAggressive : Option<
    "unroll-threshold-aggressive",
    "Threshold (max size of unrolled loop) to use in aggressive (O3) "
    "optimizations",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "300";
  let Hidden = 1;
}

def SC_UnrollThresholdDefault : Option<
    "unroll-threshold-default",
    "Default threshold (max size of unrolled "
    "loop), used in all but O3 optimizations",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "150";
  let Hidden = 1;
}

def SC_PragmaUnrollFullMaxIterations : Option<
    "pragma-unroll-full-max-iterations",
    "Maximum allowed iterations to unroll under pragma unroll full.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "1000000";
  let Hidden = 1;
}

// --- LoopVersioningLICM.cpp ---

def SC_LicmVersioningInvariantThreshold : Option<
    "licm-versioning-invariant-threshold",
    "LoopVersioningLICM's minimum allowed percentage "
    "of possible invariant instructions per loop",
    "float"> {
  let Registry = ScalarOptsReg;
  let Default = "25.0";
  let Hidden = 1;
}

def SC_LicmVersioningMaxDepthThreshold : Option<
    "licm-versioning-max-depth-threshold",
    "LoopVersioningLICM's threshold for maximum allowed loop nest/depth",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "2";
  let Hidden = 1;
}

// --- LowerExpectIntrinsic.cpp ---

def SC_LikelyBranchWeight : Option<
    "likely-branch-weight",
    "Weight of the branch likely to be taken (default = 2000)",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "2000";
  let Hidden = 1;
}

def SC_UnlikelyBranchWeight : Option<
    "unlikely-branch-weight",
    "Weight of the branch unlikely to be taken (default = 1)",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- LowerMatrixIntrinsics.cpp ---

def SC_FuseMatrix : Option<
    "fuse-matrix",
    "Enable/disable fusing matrix instructions.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_FuseMatrixTileSize : Option<
    "fuse-matrix-tile-size",
    "Tile size for matrix instruction fusion using square-shaped tiles.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "4";
  let Hidden = 1;
}

def SC_FuseMatrixLoopsThreshold : Option<
    "fuse-matrix-loops-threshold",
    "Generate loop nests for tiling when expected "
    "number of operations exceeds threshold.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "200";
  let Hidden = 1;
}

def SC_ForceFuseMatrix : Option<
    "force-fuse-matrix",
    "Force matrix instruction fusion even if not profitable.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_MatrixAllowContract : Option<
    "matrix-allow-contract",
    "Allow the use of FMAs if available and profitable. This may "
    "result in different results, due to less rounding error.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_VerifyMatrixShapes : Option<
    "verify-matrix-shapes",
    "Enable/disable matrix shape verification.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def MatrixLayoutTyV2ColumnMajor : EnumValue<"column-major",
    "Use column-major layout"> {
  let CppName = "ColumnMajor";
}

def MatrixLayoutTyV2RowMajor : EnumValue<"row-major",
    "Use row-major layout"> {
  let CppName = "RowMajor";
}

def MatrixLayoutTyV2Enum : EnumType<"MatrixLayoutTyV2",
    [MatrixLayoutTyV2ColumnMajor, MatrixLayoutTyV2RowMajor]>;

def SC_MatrixDefaultLayout : Option<
    "matrix-default-layout",
    "Sets the default matrix layout",
    "MatrixLayoutTyV2"> {
  let Registry = ScalarOptsReg;
  let Default = "column-major";
}

def SC_MatrixPrintAfterTransposeOpt : Option<
    "matrix-print-after-transpose-opt", "",
    "bool"> {
  let Registry = ScalarOptsReg;
}

def SC_MatrixSplitMatmulRemainderOverThreshold : Option<
    "matrix-split-matmul-remainder-over-threshold",
    "Illegal remainder vectors over this size in bits should be split "
    "in the inner loop of matmul",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

// --- MemCpyOptimizer.cpp ---

def SC_EnableMemcpyoptWithoutLibcalls : Option<
    "enable-memcpyopt-without-libcalls",
    "Enable memcpyopt even when libcalls are disabled",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

// --- NewGVN.cpp ---

def SC_EnableStoreRefinement : Option<
    "enable-store-refinement", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_EnablePhiOfOps : Option<
    "enable-phi-of-ops", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- PlaceSafepoints.cpp ---

def SC_SppAllBackedges : Option<
    "spp-all-backedges", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_SppCountedLoopTripWidth : Option<
    "spp-counted-loop-trip-width", "",
    "int"> {
  let Registry = ScalarOptsReg;
  let Default = "32";
  let Hidden = 1;
}

def SC_SppSplitBackedge : Option<
    "spp-split-backedge", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_SppNoEntry : Option<
    "spp-no-entry", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_SppNoCall : Option<
    "spp-no-call", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_SppNoBackedge : Option<
    "spp-no-backedge", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

// --- Reassociate.cpp ---

def SC_ReassociateUseCseLocal : Option<
    "reassociate-use-cse-local",
    "Only reorder expressions within a basic block "
    "when exposing CSE opportunities",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- RewriteStatepointsForGC.cpp ---

def SC_SppPrintLiveset : Option<
    "spp-print-liveset", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_SppPrintLivesetSize : Option<
    "spp-print-liveset-size", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_SppPrintBasePointers : Option<
    "spp-print-base-pointers", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_SppRematerializationThreshold : Option<
    "spp-rematerialization-threshold", "",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "6";
  let Hidden = 1;
}

def SC_Rs4gcClobberNonLive : Option<
    "rs4gc-clobber-non-live", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_Rs4gcAllowStatepointWithNoDeoptInfo : Option<
    "rs4gc-allow-statepoint-with-no-deopt-info", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_Rs4gcRematDerivedAtUses : Option<
    "rs4gc-remat-derived-at-uses", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- SeparateConstOffsetFromGEP.cpp ---

def SC_DisableSeparateConstOffsetFromGep : Option<
    "disable-separate-const-offset-from-gep",
    "Do not separate the constant offset from a GEP instruction",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_ReassociateGepsVerifyNoDeadCode : Option<
    "reassociate-geps-verify-no-dead-code",
    "Verify this pass produces no dead code",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

// --- SimpleLoopUnswitch.cpp ---

def SC_EnableNontrivialUnswitch : Option<
    "enable-nontrivial-unswitch",
    "Forcibly enables non-trivial loop unswitching rather than "
    "following the configuration passed into the pass.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_UnswitchThreshold : Option<
    "unswitch-threshold",
    "The cost threshold for unswitching a loop.",
    "int"> {
  let Registry = ScalarOptsReg;
  let Default = "50";
  let Hidden = 1;
}

def SC_EnableUnswitchCostMultiplier : Option<
    "enable-unswitch-cost-multiplier",
    "Enable unswitch cost multiplier that prohibits exponential "
    "explosion in nontrivial unswitch.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_UnswitchSiblingsToplevelDiv : Option<
    "unswitch-siblings-toplevel-div",
    "Toplevel siblings divisor for cost multiplier.",
    "int"> {
  let Registry = ScalarOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def SC_UnswitchParentBlocksDiv : Option<
    "unswitch-parent-blocks-div",
    "Outer loop size divisor for cost multiplier.",
    "int"> {
  let Registry = ScalarOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def SC_UnswitchNumInitialUnscaledCandidates : Option<
    "unswitch-num-initial-unscaled-candidates",
    "Number of unswitch candidates that are ignored when calculating "
    "cost multiplier.",
    "int"> {
  let Registry = ScalarOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def SC_SimpleLoopUnswitchGuards : Option<
    "simple-loop-unswitch-guards",
    "If enabled, simple loop unswitching will also consider "
    "llvm.experimental.guard intrinsics as unswitch candidates.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_SimpleLoopUnswitchDropNonTrivialImplicitNullChecks : Option<
    "simple-loop-unswitch-drop-non-trivial-implicit-null-checks",
    "If enabled, drop make.implicit metadata in unswitched implicit "
    "null checks to save time analyzing if we can keep it.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_SimpleLoopUnswitchMemoryssaThreshold : Option<
    "simple-loop-unswitch-memoryssa-threshold",
    "Max number of memory uses to explore during "
    "partial unswitching analysis",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "100";
  let Hidden = 1;
}

def SC_FreezeLoopUnswitchCond : Option<
    "freeze-loop-unswitch-cond",
    "If enabled, the freeze instruction will be added to condition "
    "of loop unswitch to prevent miscompilation.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_SimpleLoopUnswitchInjectInvariantConditions : Option<
    "simple-loop-unswitch-inject-invariant-conditions",
    "Whether we should inject new invariants and unswitch them to "
    "eliminate some existing (non-invariant) conditions.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_SimpleLoopUnswitchInjectInvariantConditionHotnessThreshold : Option<
    "simple-loop-unswitch-inject-invariant-condition-hotness-threshold",
    "Only try to inject loop invariant conditions and "
    "unswitch on them to eliminate branches that are "
    "not-taken 1/<this option> times or less.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "16";
  let Hidden = 1;
}

def SC_SimpleLoopUnswitchEstimateProfile : Option<
    "simple-loop-unswitch-estimate-profile", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- SimplifyCFGPass.cpp ---

def SC_BonusInstThreshold : Option<
    "bonus-inst-threshold",
    "Control the number of bonus instructions (default = 1)",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_KeepLoops : Option<
    "keep-loops",
    "Preserve canonical loop structure (default = true)",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SC_SwitchRangeToIcmp : Option<
    "switch-range-to-icmp",
    "Convert switches into an integer range comparison (default = false)",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_SwitchToLookup : Option<
    "switch-to-lookup",
    "Convert switches to lookup tables (default = false)",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_ForwardSwitchCond : Option<
    "forward-switch-cond",
    "Forward switch condition to phi ops (default = false)",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_HoistCommonInsts : Option<
    "hoist-common-insts",
    "hoist common instructions (default = false)",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_HoistLoadsStoresWithCondFaulting : Option<
    "hoist-loads-stores-with-cond-faulting",
    "Hoist loads/stores if the target supports conditional faulting "
    "(default = false)",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_SinkCommonInsts : Option<
    "sink-common-insts",
    "Sink common instructions (default = false)",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_SpeculateUnpredictables : Option<
    "speculate-unpredictables",
    "Speculate unpredictable branches (default = false)",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

// --- SpeculativeExecution.cpp ---

def SC_SpecExecMaxSpeculationCost : Option<
    "spec-exec-max-speculation-cost",
    "Speculative execution is not applied to basic blocks where "
    "the cost of the instructions to speculatively execute "
    "exceeds this limit.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "7";
  let Hidden = 1;
}

def SC_SpecExecMaxNotHoisted : Option<
    "spec-exec-max-not-hoisted",
    "Speculative execution is not applied to basic blocks where the "
    "number of instructions that would not be speculatively executed "
    "exceeds this limit.",
    "unsigned"> {
  let Registry = ScalarOptsReg;
  let Default = "5";
  let Hidden = 1;
}

def SC_SpecExecOnlyIfDivergentTarget : Option<
    "spec-exec-only-if-divergent-target",
    "Speculative execution is applied only to targets with divergent "
    "branches, even if the pass was configured to apply only to all "
    "targets.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

// --- SROA.cpp ---

def SC_SroaSkipMem2Reg : Option<
    "sroa-skip-mem2reg", "",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

// --- StraightLineStrengthReduce.cpp ---

def SC_EnablePoisonReuseGuard : Option<
    "enable-poison-reuse-guard",
    "Enable poison-reuse guard",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
}

// --- StructurizeCFG.cpp ---

def SC_StructurizecfgSkipUniformRegions : Option<
    "structurizecfg-skip-uniform-regions",
    "Force whether the StructurizeCFG pass skips uniform regions",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_StructurizecfgRelaxedUniformRegions : Option<
    "structurizecfg-relaxed-uniform-regions",
    "Allow relaxed uniform region checks",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- TailRecursionElimination.cpp ---

def SC_TreDisableEntrycountRecompute : Option<
    "tre-disable-entrycount-recompute",
    "Force disabling recomputing of function entry count, on "
    "successful tail recursion elimination.",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}

def SC_DisableTailCallElimForColdCalls : Option<
    "disable-tail-call-elim-for-cold-calls",
    "Disable tail call elimination and optimization for cold calls or "
    "in cold functions",
    "bool"> {
  let Registry = ScalarOptsReg;
  let Hidden = 1;
}
