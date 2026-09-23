//===- ScalarOptions.td - Scalar transform options ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Options for llvm/lib/Transforms/Scalar, emitted by -gen-opt-parser-defs into
// the llvm::ScalarOptions struct (see ScalarOptions.h).
//
// Options whose consumer distinguishes "explicitly given" from "defaulted"
// (the old clv2 wasOptSpecified / getOptValIfSpecified / view.specified
// forms) are std::optional<> fields: OptionalField for values, and
// OptionalValueField<"bool", "true"> for bools, which keeps the old bare-flag
// spelling (-x meaning -x=true) working. Consumers resolve these with
// .value_or(<old default>) where they also read the value unconditionally.
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def ScalarOptions : OptionsStruct<"llvm"> {}

// =========================================================================
// Enum types (emitted as `enum class` in namespace llvm; enum defs must
// precede their first FieldEnum<> use)
// =========================================================================

def ReplaceExitValV2Enum : OptionEnum<
    "ReplaceExitValV2", [
    EnumMember<"NeverRepl", "never">,
    EnumMember<"OnlyCheapRepl", "cheap">,
    EnumMember<"UnusedIndVarInLoop", "unusedindvarinloop">,
    EnumMember<"NoHardUse", "noharduse">,
    EnumMember<"AlwaysRepl", "always">]>;

def FusionDependenceAnalysisChoiceV2Enum : OptionEnum<
    "FusionDependenceAnalysisChoiceV2", [
    EnumMember<"SCEV", "scev">,
    EnumMember<"DA", "da">,
    EnumMember<"All", "all">]>;

def CRCStrategyKindEnum : OptionEnum<
    "CRCStrategyKind", [
    EnumMember<"Disable", "disable">,
    EnumMember<"Auto", "auto">,
    EnumMember<"Table", "table">,
    EnumMember<"Clmul", "clmul">]>;

def LoopInterchangeRuleTyEnum : OptionEnum<
    "LoopInterchangeRuleTy", [
    EnumMember<"PerLoopCacheAnalysis", "cache">,
    EnumMember<"PerInstrOrderCost", "instorder">,
    EnumMember<"ForVectorization", "vectorize">,
    EnumMember<"Ignore", "ignore">]>;

def AddressingModeKindV2Enum : OptionEnum<
    "AddressingModeKindV2", [
    EnumMember<"None", "none">,
    EnumMember<"PreIndexed", "preindexed">,
    EnumMember<"PostIndexed", "postindexed">,
    EnumMember<"All", "all">]>;

def MatrixLayoutTyV2Enum : OptionEnum<
    "MatrixLayoutTyV2", [
    EnumMember<"ColumnMajor", "column-major">,
    EnumMember<"RowMajor", "row-major">]>;

// --- ADCE.cpp ---

defm SC_AdceRemoveControlFlow : BoolField<
    "adce-remove-control-flow", "true",
    "",
    /*hidden=*/1>;

defm SC_AdceRemoveLoops : BoolField<
    "adce-remove-loops", "false",
    "",
    /*hidden=*/1>;

// --- CallSiteSplitting.cpp ---

defm SC_CallsiteSplittingDuplicationThreshold : ValueField<
    "callsite-splitting-duplication-threshold", "unsigned", "5",
    "Only allow instructions before a call, if their cost is below "
    "DuplicationThreshold",
    /*hidden=*/1>;

// --- ConstantHoisting.cpp ---

defm SC_ConsthoistWithBlockFrequency : BoolField<
    "consthoist-with-block-frequency", "true",
    "Enable the use of the block frequency analysis to reduce the "
    "chance to execute const materialization more frequently than "
    "without hoisting.",
    /*hidden=*/1>;

defm SC_ConsthoistGep : BoolField<
    "consthoist-gep", "false",
    "Try hoisting constant gep expressions",
    /*hidden=*/1>;

defm SC_ConsthoistMinNumToRebase : ValueField<
    "consthoist-min-num-to-rebase", "unsigned", "0",
    "Do not rebase if number of dependent constants of a Base is less "
    "than this number.",
    /*hidden=*/1>;

// --- ConstraintElimination.cpp ---

defm SC_ConstraintEliminationMaxRows : ValueField<
    "constraint-elimination-max-rows", "unsigned", "500",
    "Maximum number of rows to keep in constraint system",
    /*hidden=*/1>;

defm SC_ConstraintEliminationDumpReproducers : BoolField<
    "constraint-elimination-dump-reproducers", "false",
    "Dump IR to reproduce successful transformations.",
    /*hidden=*/1>;

// --- DeadStoreElimination.cpp ---

defm SC_EnableDsePartialOverwriteTracking : BoolField<
    "enable-dse-partial-overwrite-tracking", "true",
    "Enable partial-overwrite tracking in DSE",
    /*hidden=*/1>;

defm SC_EnableDsePartialStoreMerging : BoolField<
    "enable-dse-partial-store-merging", "true",
    "Enable partial store merging in DSE",
    /*hidden=*/1>;

defm SC_DseMemoryssaScanlimit : ValueField<
    "dse-memoryssa-scanlimit", "unsigned", "150",
    "The number of memory instructions to scan for dead store "
    "elimination (default = 150)",
    /*hidden=*/1>;

defm SC_DseMemoryssaWalklimit : ValueField<
    "dse-memoryssa-walklimit", "unsigned", "90",
    "The maximum number of steps while walking upwards to find "
    "MemoryDefs that may be killed (default = 90)",
    /*hidden=*/1>;

defm SC_DseMemoryssaPartialStoreLimit : ValueField<
    "dse-memoryssa-partial-store-limit", "unsigned", "5",
    "The maximum number candidates that only partially overwrite the "
    "killing MemoryDef to consider (default = 5)",
    /*hidden=*/1>;

defm SC_DseMemoryssaDefsPerBlockLimit : ValueField<
    "dse-memoryssa-defs-per-block-limit", "unsigned", "5000",
    "The number of MemoryDefs we consider as candidates to eliminated "
    "other stores per basic block (default = 5000)",
    /*hidden=*/1>;

defm SC_DseMemoryssaSamebbCost : ValueField<
    "dse-memoryssa-samebb-cost", "unsigned", "1",
    "The cost of a step in the same basic block as the killing "
    "MemoryDef(default = 1)",
    /*hidden=*/1>;

defm SC_DseMemoryssaOtherbbCost : ValueField<
    "dse-memoryssa-otherbb-cost", "unsigned", "5",
    "The cost of a step in a different basic block than the killing "
    "MemoryDef(default = 5)",
    /*hidden=*/1>;

defm SC_DseMemoryssaPathCheckLimit : ValueField<
    "dse-memoryssa-path-check-limit", "unsigned", "50",
    "The maximum number of blocks to check when trying to prove that "
    "all paths to an exit go through a killing block (default = 50)",
    /*hidden=*/1>;

defm SC_DseOptimizeMemoryssa : BoolField<
    "dse-optimize-memoryssa", "true",
    "Allow DSE to optimize memory accesses.",
    /*hidden=*/1>;

defm SC_EnableDseInitializesAttrImprovement : BoolField<
    "enable-dse-initializes-attr-improvement", "true",
    "Enable the initializes attr improvement in DSE",
    /*hidden=*/1>;

defm SC_DseMaxDomCondDepth : ValueField<
    "dse-max-dom-cond-depth", "unsigned", "1024",
    "Max dominator tree recursion depth for eliminating redundant "
    "stores via dominating conditions",
    /*hidden=*/1>;

// --- DFAJumpThreading.cpp ---

defm SC_DfaJumpViewCfgBefore : BoolField<
    "dfa-jump-view-cfg-before", "false",
    "View the CFG before DFA Jump Threading",
    /*hidden=*/1>;

defm SC_DfaEarlyExitHeuristic : BoolField<
    "dfa-early-exit-heuristic", "true",
    "Exit early if an unpredictable value come from the same loop",
    /*hidden=*/1>;

defm SC_DfaMaxPathLength : ValueField<
    "dfa-max-path-length", "unsigned", "20",
    "Max number of blocks searched to find a threading path",
    /*hidden=*/1>;

defm SC_DfaMaxNumVisitedPaths : ValueField<
    "dfa-max-num-visited-paths", "unsigned", "2500",
    "Max number of blocks visited while enumerating paths around a "
    "switch",
    /*hidden=*/1>;

defm SC_DfaMaxNumPaths : ValueField<
    "dfa-max-num-paths", "unsigned", "200",
    "Max number of paths enumerated around a switch",
    /*hidden=*/1>;

defm SC_DfaCostThreshold : ValueField<
    "dfa-cost-threshold", "unsigned", "50",
    "Maximum cost accepted for the transformation",
    /*hidden=*/1>;

defm SC_DfaMaxClonedRate : ValueField<
    "dfa-max-cloned-rate", "double", "7.5",
    "Maximum cloned instructions rate accepted for the transformation",
    /*hidden=*/1>;

defm SC_DfaMaxOutUseBlocks : ValueField<
    "dfa-max-out-use-blocks", "unsigned", "40",
    "Maximum unduplicated blocks with outer uses accepted for the "
    "transformation",
    /*hidden=*/1>;

// --- EarlyCSE.cpp ---

defm SC_EarlyCseMssaOptimizationCap : ValueField<
    "earlycse-mssa-optimization-cap", "unsigned", "500",
    "Enable imprecision in EarlyCSE in pathological cases, in exchange "
    "for faster compile. Caps the MemorySSA clobbering calls.",
    /*hidden=*/1>;

defm SC_EarlyCseDebugHash : BoolField<
    "earlycse-debug-hash", "false",
    "Perform extra assertion checking to verify that SimpleValue's hash"
    " function is well-behaved w.r.t. its isEqual predicate",
    /*hidden=*/1>;

// --- ExpandMemCmp.cpp ---

defm SC_MemcmpNumLoadsPerBlock : OptionalField<
    "memcmp-num-loads-per-block", "unsigned",
    "The number of loads per basic block for inline expansion of memcmp"
    " that is only being compared against zero.",
    /*hidden=*/1>;

defm SC_MaxLoadsPerMemcmp : OptionalField<
    "max-loads-per-memcmp", "unsigned",
    "Set maximum number of loads used in expanded memcmp",
    /*hidden=*/1>;

defm SC_MaxLoadsPerMemcmpOptSize : OptionalField<
    "max-loads-per-memcmp-opt-size", "unsigned",
    "Set maximum number of loads used in expanded memcmp for -Os/Oz",
    /*hidden=*/1>;

// --- Float2Int.cpp ---

defm SC_Float2IntMaxIntegerBw : ValueField<
    "float2int-max-integer-bw", "unsigned", "64",
    "Max integer bitwidth to consider in float2int(default=64)",
    /*hidden=*/1>;

// --- GuardWidening.cpp ---

defm SC_GuardWideningWidenBranchGuards : BoolField<
    "guard-widening-widen-branch-guards", "true",
    "Whether or not we should widen guards  expressed as branches by "
    "widenable conditions",
    /*hidden=*/1>;

// --- GVN.cpp ---

defm SC_EnablePre : BoolField<
    "enable-scalar-pre", "true",
    "",
    /*hidden=*/1>;

defm SC_EnableLoadPre : BoolField<
    "enable-load-pre", "true",
    "">;

defm SC_EnableLoadInLoopPre : BoolField<
    "enable-load-in-loop-pre", "true",
    "">;

defm SC_EnableSplitBackedgeInLoadPre : BoolField<
    "enable-split-backedge-in-load-pre", "false",
    "">;

defm SC_EnableGvnMemdep : OptionalValueField<
    "enable-gvn-memdep", "bool", "true",
    "">;

defm SC_EnableGvnMemoryssa : OptionalValueField<
    "enable-gvn-memoryssa", "bool", "true",
    "">;

defm SC_GvnScanUsersLimit : ValueField<
    "gvn-scan-users-limit", "unsigned", "100",
    "The number of memory accesses to scan in a block in reaching "
    "memory values analysis (default = 100)",
    /*hidden=*/1>;

defm SC_GvnMaxNumReachingBlocks : ValueField<
    "gvn-max-num-reaching-blocks", "uint32_t", "200",
    "Max number of blocks scanned per load in the MemorySSA "
    "reaching-value analysis (default = 200)",
    /*hidden=*/1>;

defm SC_GvnMaxNumDeps : ValueField<
    "gvn-max-num-deps", "unsigned", "100",
    "Max number of dependences to attempt Load PRE (default = 100)",
    /*hidden=*/1>;

defm SC_GvnMaxBlockSpeculations : ValueField<
    "gvn-max-block-speculations", "unsigned", "600",
    "Max number of blocks we're willing to speculate on (and recurse "
    "into) when deducing if a value is fully available or not in GVN "
    "(default = 600)",
    /*hidden=*/1>;

defm SC_GvnMaxNumVisitedInsts : ValueField<
    "gvn-max-num-visited-insts", "unsigned", "100",
    "Max number of visited instructions when trying to find dominating "
    "value of select dependency (default = 100)",
    /*hidden=*/1>;

defm SC_GvnMaxNumInsns : ValueField<
    "gvn-max-num-insns", "unsigned", "100",
    "Max number of instructions to scan in each basic block in GVN "
    "(default = 100)",
    /*hidden=*/1>;

// --- GVNHoist.cpp ---

defm SC_GvnMaxHoisted : ValueField<
    "gvn-max-hoisted", "int", "-1",
    "Max number of instructions to hoist (default unlimited = -1)",
    /*hidden=*/1>;

defm SC_GvnHoistMaxBbs : ValueField<
    "gvn-hoist-max-bbs", "int", "4",
    "Max number of basic blocks on the path between hoisting locations "
    "(default = 4, unlimited = -1)",
    /*hidden=*/1>;

defm SC_GvnHoistMaxDepth : ValueField<
    "gvn-hoist-max-depth", "int", "100",
    "Hoist instructions from the beginning of the BB up to the maximum "
    "specified depth (default = 100, unlimited = -1)",
    /*hidden=*/1>;

defm SC_GvnHoistMaxChainLength : ValueField<
    "gvn-hoist-max-chain-length", "int", "10",
    "Maximum length of dependent chains to hoist (default = 10, "
    "unlimited = -1)",
    /*hidden=*/1>;

// --- InductiveRangeCheckElimination.cpp ---

defm SC_IrceLoopSizeCutoff : ValueField<
    "irce-loop-size-cutoff", "unsigned", "64",
    "",
    /*hidden=*/1>;

defm SC_IrcePrintChangedLoops : BoolField<
    "irce-print-changed-loops", "false",
    "",
    /*hidden=*/1>;

defm SC_IrcePrintRangeChecks : BoolField<
    "irce-print-range-checks", "false",
    "",
    /*hidden=*/1>;

defm SC_IrceSkipProfitabilityChecks : BoolField<
    "irce-skip-profitability-checks", "false",
    "",
    /*hidden=*/1>;

defm SC_IrceMinEliminatedChecks : ValueField<
    "irce-min-eliminated-checks", "unsigned", "10",
    "",
    /*hidden=*/1>;

defm SC_IrceAllowUnsignedLatch : BoolField<
    "irce-allow-unsigned-latch", "true",
    "",
    /*hidden=*/1>;

defm SC_IrceAllowNarrowLatch : BoolField<
    "irce-allow-narrow-latch", "true",
    "If set to true, IRCE may eliminate wide range checks in loops with"
    " narrow latch condition.",
    /*hidden=*/1>;

defm SC_IrceMaxTypeSizeForOverflowCheck : ValueField<
    "irce-max-type-size-for-overflow-check", "unsigned", "32",
    "Maximum size of range check type for which can be produced runtime"
    " overflow check of its limit's computation",
    /*hidden=*/1>;

defm SC_IrcePrintScaledBoundaryRangeChecks : BoolField<
    "irce-print-scaled-boundary-range-checks", "false",
    "",
    /*hidden=*/1>;

// --- IndVarSimplify.cpp ---

defm SC_ReplExitVal : EnumField<
    "replexitval", ReplaceExitValV2Enum,
    "ReplaceExitValV2::OnlyCheapRepl",
    "Choose the strategy to replace exit value in IndVarSimplify",
    /*hidden=*/1>;

defm SC_IndvarsPostIncrementRanges : BoolField<
    "indvars-post-increment-ranges", "true",
    "Use post increment control-dependent ranges in IndVarSimplify",
    /*hidden=*/1>;

defm SC_DisableLftr : BoolField<
    "disable-lftr", "false",
    "Disable Linear Function Test Replace optimization",
    /*hidden=*/1>;

defm SC_IndvarsPredicateLoops : BoolField<
    "indvars-predicate-loops", "true",
    "Predicate conditions in read only loops",
    /*hidden=*/1>;

defm SC_IndvarsPredicateLoopTraps : BoolField<
    "indvars-predicate-loop-traps", "true",
    "Predicate conditions that trap in loops with only local writes",
    /*hidden=*/1>;

defm SC_IndvarsWidenIndvars : BoolField<
    "indvars-widen-indvars", "true",
    "Allow widening of indvars to eliminate s/zext",
    /*hidden=*/1>;

// --- JumpTableToSwitch.cpp ---

defm SC_JumpTableToSwitchSizeThreshold : ValueField<
    "jump-table-to-switch-size-threshold", "unsigned", "10",
    "Only split jump tables with size less or equal than "
    "JumpTableSizeThreshold.",
    /*hidden=*/1>;

defm SC_JumpTableToSwitchFunctionSizeThreshold : ValueField<
    "jump-table-to-switch-function-size-threshold", "unsigned", "50",
    "Only split jump tables containing functions whose sizes are less "
    "or equal than this threshold.",
    /*hidden=*/1>;

// --- JumpThreading.cpp ---

defm SC_JumpThreadingThreshold : OptionalField<
    "jump-threading-threshold", "unsigned",
    "Max block size to duplicate for jump threading",
    /*hidden=*/1>;

defm SC_JumpThreadingImplicationSearchThreshold : ValueField<
    "jump-threading-implication-search-threshold", "unsigned", "3",
    "The number of predecessors to search for a stronger condition to "
    "use to thread over a weaker condition",
    /*hidden=*/1>;

defm SC_JumpThreadingPhiThreshold : ValueField<
    "jump-threading-phi-threshold", "unsigned", "76",
    "Max PHIs in BB to duplicate for jump threading",
    /*hidden=*/1>;

defm SC_JumpThreadingAcrossLoopHeaders : BoolField<
    "jump-threading-across-loop-headers", "false",
    "Allow JumpThreading to thread across loop headers, for testing",
    /*hidden=*/1>;

// --- LICM.cpp ---

defm SC_DisableLicmPromotion : BoolField<
    "disable-licm-promotion", "false",
    "Disable memory promotion in LICM pass",
    /*hidden=*/1>;

defm SC_LicmControlFlowHoisting : BoolField<
    "licm-control-flow-hoisting", "false",
    "Enable control flow (and PHI) hoisting in LICM",
    /*hidden=*/1>;

defm SC_LicmMaxNumUsesTraversed : ValueField<
    "licm-max-num-uses-traversed", "unsigned", "8",
    "Max num uses visited for identifying load invariance in loop using"
    " invariant start (default = 8)",
    /*hidden=*/1>;

defm SC_LicmMaxNumFpReassociations : ValueField<
    "licm-max-num-fp-reassociations", "unsigned", "5",
    "Set upper limit for the number of transformations performed during"
    " a single round of hoisting the reassociated expressions.",
    /*hidden=*/1>;

defm SC_LicmMaxNumIntReassociations : ValueField<
    "licm-max-num-int-reassociations", "unsigned", "5",
    "Set upper limit for the number of transformations performed during"
    " a single round of hoisting the reassociated expressions.",
    /*hidden=*/1>;

defm SC_LicmMssaOptimizationCap : ValueField<
    "licm-mssa-optimization-cap", "unsigned", "100",
    "Enable imprecision in LICM in pathological cases, in exchange for "
    "faster compile. Caps the MemorySSA clobbering calls.",
    /*hidden=*/1>;

defm SC_LicmMssaMaxAccPromotion : ValueField<
    "licm-mssa-max-acc-promotion", "unsigned", "250",
    "[LICM & MemorySSA] When MSSA in LICM is disabled, this has no "
    "effect. When MSSA in LICM is enabled, then this is the maximum "
    "number of accesses allowed to be present in a loop in order to "
    "enable memory promotion.",
    /*hidden=*/1>;

// --- LoopDataPrefetch.cpp ---

defm SC_LoopPrefetchWrites : OptionalValueField<
    "loop-prefetch-writes", "bool", "true",
    "Prefetch write addresses",
    /*hidden=*/1>;

defm SC_PrefetchDistance : OptionalField<
    "prefetch-distance", "unsigned",
    "Number of instructions to prefetch ahead",
    /*hidden=*/1>;

defm SC_MinPrefetchStride : OptionalField<
    "min-prefetch-stride", "unsigned",
    "Min stride to add prefetches",
    /*hidden=*/1>;

defm SC_MaxPrefetchItersAhead : OptionalField<
    "max-prefetch-iters-ahead", "unsigned",
    "Max number of iterations to prefetch ahead",
    /*hidden=*/1>;

// --- LoopDeletion.cpp ---

defm SC_LoopDeletionEnableSymbolicExecution : BoolField<
    "loop-deletion-enable-symbolic-execution", "true",
    "Break backedge through symbolic execution of 1st iteration "
    "attempting to prove that the backedge is never taken",
    /*hidden=*/1>;

// --- LoopDistribute.cpp ---

defm SC_LoopDistributeVerify : BoolField<
    "loop-distribute-verify", "false",
    "Turn on DominatorTree and LoopInfo verification after Loop "
    "Distribution",
    /*hidden=*/1>;

defm SC_LoopDistributeNonIfConvertible : BoolField<
    "loop-distribute-non-if-convertible", "false",
    "Whether to distribute into a loop that may not be if-convertible "
    "by the loop vectorizer",
    /*hidden=*/1>;

defm SC_LoopDistributeSCEVCheckThreshold : ValueField<
    "loop-distribute-scev-check-threshold", "unsigned", "8",
    "The maximum number of SCEV checks allowed for Loop Distribution",
    /*hidden=*/1>;

defm SC_LoopDistributeSCEVCheckThresholdWithPragma : ValueField<
    "loop-distribute-scev-check-threshold-with-pragma", "unsigned", "128",
    "The maximum number of SCEV checks allowed for Loop Distribution "
    "for loop marked with #pragma clang loop distribute(enable)",
    /*hidden=*/1>;

defm SC_EnableLoopDistribute : BoolField<
    "enable-loop-distribute", "false",
    "Enable the new, experimental LoopDistribution Pass",
    /*hidden=*/1>;

// --- LoopFlatten.cpp ---

defm SC_LoopFlattenCostThreshold : ValueField<
    "loop-flatten-cost-threshold", "unsigned", "2",
    "Limit on the cost of instructions that can be repeated due to loop"
    " flattening",
    /*hidden=*/1>;

defm SC_LoopFlattenAssumeNoOverflow : BoolField<
    "loop-flatten-assume-no-overflow", "false",
    "Assume that the product of the two iteration trip counts will "
    "never overflow",
    /*hidden=*/1>;

defm SC_LoopFlattenWidenIv : BoolField<
    "loop-flatten-widen-iv", "true",
    "Widen the loop induction variables, if possible, so overflow "
    "checks won't reject flattening",
    /*hidden=*/1>;

defm SC_LoopFlattenVersionLoops : BoolField<
    "loop-flatten-version-loops", "true",
    "Version loops if flattened loop could overflow",
    /*hidden=*/1>;

// --- LoopFuse.cpp ---

// Not present upstream at the rebase base (stale-base drift); its only
// getter in LoopFuse.cpp has no caller. Preserved as-is, not fixed.
defm SC_LoopFusionDependenceAnalysis : EnumField<
    "loop-fusion-dependence-analysis", FusionDependenceAnalysisChoiceV2Enum,
    "FusionDependenceAnalysisChoiceV2::DA",
    "Which dependence analysis should loop fusion use?",
    /*hidden=*/1>;

defm SC_LoopFusionPeelMaxCount : ValueField<
    "loop-fusion-peel-max-count", "unsigned", "0",
    "Max number of iterations to be peeled from a loop, such that "
    "fusion can take place",
    /*hidden=*/1>;

// --- LoopIdiomRecognize.cpp ---

defm SC_DisableLoopIdiomAll : BoolField<
    "disable-loop-idiom-all", "false",
    "Options to disable Loop Idiom Recognize Pass.",
    /*hidden=*/1>;

defm SC_DisableLoopIdiomMemset : BoolField<
    "disable-loop-idiom-memset", "false",
    "Proceed with loop idiom recognize pass, but do not convert loop(s)"
    " to memset.",
    /*hidden=*/1>;

defm SC_DisableLoopIdiomMemcpy : BoolField<
    "disable-loop-idiom-memcpy", "false",
    "Proceed with loop idiom recognize pass, but do not convert loop(s)"
    " to memcpy.",
    /*hidden=*/1>;

defm SC_DisableLoopIdiomStrlen : BoolField<
    "disable-loop-idiom-strlen", "false",
    "Proceed with loop idiom recognize pass, but do not convert loop(s)"
    " to strlen.",
    /*hidden=*/1>;

defm SC_DisableLoopIdiomWcslen : BoolField<
    "disable-loop-idiom-wcslen", "false",
    "Proceed with loop idiom recognize pass, enable conversion of "
    "loop(s) to wcslen.",
    /*hidden=*/1>;

defm SC_DisableLoopIdiomHashrecognize : BoolField<
    "disable-loop-idiom-hashrecognize", "false",
    "Proceed with loop idiom recognize pass, but do not optimize CRC "
    "loops.",
    /*hidden=*/1>;

defm SC_UseLirCodeSizeHeurs : BoolField<
    "use-lir-code-size-heurs", "true",
    "Use loop idiom recognition code size heuristics when compiling "
    "with -Os/-Oz",
    /*hidden=*/1>;

defm SC_LoopIdiomForceMemsetPatternIntrinsic : BoolField<
    "loop-idiom-force-memset-pattern-intrinsic", "false",
    "Use memset.pattern intrinsic whenever possible",
    /*hidden=*/1>;

defm SC_CRCStrategy : EnumField<
    "loop-idiom-crc-strategy", CRCStrategyKindEnum,
    "CRCStrategyKind::Auto",
    "Preferred strategy for optimizing CRC loops",
    /*hidden=*/1>;

// --- LoopInterchange.cpp ---

defm SC_LoopInterchangeThreshold : ValueField<
    "loop-interchange-threshold", "int", "0",
    "Interchange if you gain more than this number",
    /*hidden=*/1>;

defm SC_LoopInterchangeMaxMemInstrCount : ValueField<
    "loop-interchange-max-mem-instr-ratio", "unsigned", "4",
    "Maximum number of load/store instructions squared in relation to "
    "the total number of instructions. Higher value may lead to more "
    "interchanges at the cost of compile-time",
    /*hidden=*/1>;

defm SC_LoopInterchangeMinLoopNestDepth : ValueField<
    "loop-interchange-min-loop-nest-depth", "unsigned", "2",
    "Minimum depth of loop nest considered for the transform",
    /*hidden=*/1>;

defm SC_LoopInterchangeMaxLoopNestDepth : ValueField<
    "loop-interchange-max-loop-nest-depth", "unsigned", "10",
    "Maximum depth of loop nest considered for the transform",
    /*hidden=*/1>;

defm SC_LoopInterchangeProfitabilities : EnumListField<
    "loop-interchange-profitabilities", LoopInterchangeRuleTyEnum,
    "List of profitability heuristics to be used. They are applied in "
    "the given order",
    /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/1>;

defm SC_LoopInterchangeReductionToMem : BoolField<
    "loop-interchange-reduction-to-mem", "false",
    "Support for the inner-loop reduction pattern.",
    /*hidden=*/1>;

// --- LoopLoadElimination.cpp ---

defm SC_RuntimeCheckPerLoopLoadElim : ValueField<
    "runtime-check-per-loop-load-elim", "unsigned", "1",
    "Max number of memchecks allowed per eliminated load on average",
    /*hidden=*/1>;

defm SC_LoopLoadEliminationSCEVCheckThreshold : ValueField<
    "loop-load-elimination-scev-check-threshold", "unsigned", "8",
    "The maximum number of SCEV checks allowed for Loop Load "
    "Elimination",
    /*hidden=*/1>;

// --- LoopPredication.cpp ---

defm SC_LoopPredicationEnableIvTruncation : BoolField<
    "loop-predication-enable-iv-truncation", "true",
    "",
    /*hidden=*/1>;

defm SC_LoopPredicationEnableCountDownLoop : BoolField<
    "loop-predication-enable-count-down-loop", "true",
    "",
    /*hidden=*/1>;

defm SC_LoopPredicationSkipProfitabilityChecks : BoolField<
    "loop-predication-skip-profitability-checks", "false",
    "",
    /*hidden=*/1>;

defm SC_LoopPredicationLatchProbabilityScale : ValueField<
    "loop-predication-latch-probability-scale", "float", "2.0f",
    "scale factor for the latch probability. Value should be greater "
    "than 1. Lower values are ignored",
    /*hidden=*/1>;

defm SC_LoopPredicationPredicateWidenableBranchesToDeopt : BoolField<
    "loop-predication-predicate-widenable-branches-to-deopt", "true",
    "Whether or not we should predicate guards expressed as widenable "
    "branches to deoptimize blocks",
    /*hidden=*/1>;

defm SC_LoopPredicationInsertAssumesOfPredicatedGuardsConditions : BoolField<
    "loop-predication-insert-assumes-of-predicated-guards-conditions", "true",
    "Whether or not we should insert assumes of conditions of "
    "predicated guards",
    /*hidden=*/1>;

// --- LoopRotation.cpp ---

defm SC_RotationMaxHeaderSize : ValueField<
    "rotation-max-header-size", "unsigned", "16",
    "The default maximum header size for automatic loop rotation",
    /*hidden=*/1>;

defm SC_RotationPrepareForLto : BoolField<
    "rotation-prepare-for-lto", "false",
    "Run loop-rotation in the prepare-for-lto stage. This option should"
    " be used for testing only.",
    /*hidden=*/1>;

defm SC_EnableLoopHeaderDuplicationAtMinSize : BoolField<
    "enable-loop-header-duplication-at-minsize", "false",
    "Enable loop header duplication even for minsize",
    /*hidden=*/1>;

// --- LoopSimplifyCFG.cpp ---

defm SC_EnableLoopSimplifyCfgTermFolding : BoolField<
    "enable-loop-simplifycfg-term-folding", "true",
    "">;

// --- LoopSink.cpp ---

defm SC_SinkFreqPercentThreshold : ValueField<
    "sink-freq-percent-threshold", "unsigned", "90",
    "Do not sink instructions that require cloning unless they execute "
    "less than this percent of the time.",
    /*hidden=*/1>;

defm SC_MaxUsesForSinking : ValueField<
    "max-uses-for-sinking", "unsigned", "30",
    "Do not sink instructions that have too many uses.",
    /*hidden=*/1>;

// --- LoopStrengthReduce.cpp ---

defm SC_EnableLsrPhielim : BoolField<
    "enable-lsr-phielim", "true",
    "Enable LSR phi elimination",
    /*hidden=*/1>;

defm SC_LsrInsnsCost : OptionalValueField<
    "lsr-insns-cost", "bool", "true",
    "Add instruction count to a LSR cost model",
    /*hidden=*/1>;

defm SC_LsrExpNarrow : BoolField<
    "lsr-exp-narrow", "false",
    "Narrow LSR complex solution using expectation of registers number",
    /*hidden=*/1>;

defm SC_LsrFilterSameScaledReg : BoolField<
    "lsr-filter-same-scaled-reg", "true",
    "Narrow LSR search space by filtering non-optimal formulae with the"
    " same ScaledReg and Scale",
    /*hidden=*/1>;

// std::optional<AddressingModeKindV2>: an unset value defers to the target's
// preferred addressing mode. Hand-authored because no multiclass composes
// std::optional<> storage with FieldEnum<>.
def SC_LsrPreferredAddressingMode : Joined<["-", "--"],
    "lsr-preferred-addressing-mode=">,
    HelpText<"A flag that overrides the target's preferred addressing mode.">,
    Flags<[HelpHidden]>,
    Field<"std::optional<AddressingModeKindV2>", "SC_LsrPreferredAddressingMode",
          "std::nullopt">,
    FieldEnum<AddressingModeKindV2Enum>;
def SC_LsrPreferredAddressingMode_sep : Separate<["-", "--"],
    "lsr-preferred-addressing-mode">,
    Flags<[HelpHidden]>, Alias<!cast<Option>(SC_LsrPreferredAddressingMode)>;

defm SC_LsrComplexityLimit : ValueField<
    "lsr-complexity-limit", "unsigned", "65535",
    "LSR search space complexity limit",
    /*hidden=*/1>;

defm SC_LsrSetupcostDepthLimit : ValueField<
    "lsr-setupcost-depth-limit", "unsigned", "7",
    "The limit on recursion depth for LSRs setup cost",
    /*hidden=*/1>;

// std::optional<bool>: mirrors the upstream cl::boolOrDefault tri-state.
defm SC_LsrDropSolution : OptionalValueField<
    "lsr-drop-solution", "bool", "true",
    "Attempt to drop solution if it is less profitable",
    /*hidden=*/1>;

defm SC_LsrEnableVscaleImmediates : BoolField<
    "lsr-enable-vscale-immediates", "true",
    "Enable analysis of vscale-relative immediates in LSR",
    /*hidden=*/1>;

defm SC_LsrDropScaledRegForVscale : BoolField<
    "lsr-drop-scaled-reg-for-vscale", "true",
    "Avoid using scaled registers with vscale-relative addressing",
    /*hidden=*/1>;

defm SC_StressIvChain : BoolField<
    "stress-ivchain", "false",
    "Stress test IV chain generation",
    /*hidden=*/1>;

// --- LoopUnrollAndJamPass.cpp ---

defm SC_AllowUnrollAndJam : OptionalValueField<
    "allow-unroll-and-jam", "bool", "true",
    "Allows loops to be unroll-and-jammed.",
    /*hidden=*/1>;

defm SC_UnrollAndJamCount : OptionalField<
    "unroll-and-jam-count", "unsigned",
    "Use this unroll count for all loops including those with "
    "unroll_and_jam_count pragma values, for testing purposes",
    /*hidden=*/1>;

defm SC_UnrollAndJamThreshold : OptionalField<
    "unroll-and-jam-threshold", "unsigned",
    "Threshold to use for inner loop when doing unroll and jam.",
    /*hidden=*/1>;

defm SC_PragmaUnrollAndJamThreshold : ValueField<
    "pragma-unroll-and-jam-threshold", "unsigned", "1024",
    "Unrolled size limit for loops with an unroll_and_jam(full) or "
    "unroll_count pragma.",
    /*hidden=*/1>;

// --- LoopUnrollPass.cpp ---

defm SC_ForgetScevLoopUnroll : BoolField<
    "forget-scev-loop-unroll", "false",
    "Forget everything in SCEV when doing LoopUnroll, instead of just "
    "the current top-most loop. This is sometimes preferred to reduce "
    "compile time.",
    /*hidden=*/1>;

defm SC_UnrollThreshold : OptionalField<
    "unroll-threshold", "unsigned",
    "The cost threshold for loop unrolling",
    /*hidden=*/1>;

defm SC_UnrollOptSizeThreshold : ValueField<
    "unroll-optsize-threshold", "unsigned", "0",
    "The cost threshold for loop unrolling when optimizing for size",
    /*hidden=*/1>;

defm SC_UnrollPartialThreshold : OptionalField<
    "unroll-partial-threshold", "unsigned",
    "The cost threshold for partial loop unrolling",
    /*hidden=*/1>;

defm SC_UnrollMaxPercentThresholdBoost : OptionalField<
    "unroll-max-percent-threshold-boost", "unsigned",
    "The maximum 'boost' (represented as a percentage >= 100) applied "
    "to the threshold when aggressively unrolling a loop due to the "
    "dynamic cost savings. If completely unrolling a loop will reduce "
    "the total runtime from X to Y, we boost the loop unroll threshold "
    "to DefaultThreshold*std::min(MaxPercentThresholdBoost, X/Y). This "
    "limit avoids excessive code bloat.",
    /*hidden=*/1>;

defm SC_UnrollMaxIterationCountToAnalyze : OptionalField<
    "unroll-max-iteration-count-to-analyze", "unsigned",
    "Don't allow loop unrolling to simulate more than this number of "
    "iterations when checking full unroll profitability",
    /*hidden=*/1>;

defm SC_UnrollCount : OptionalField<
    "unroll-count", "unsigned",
    "Use this unroll count for all loops including those with "
    "unroll_count pragma values, for testing purposes",
    /*hidden=*/1>;

defm SC_UnrollMaxCount : OptionalField<
    "unroll-max-count", "unsigned",
    "Set the max unroll count for partial and runtime unrolling, "
    "fortesting purposes",
    /*hidden=*/1>;

defm SC_UnrollFullMaxCount : OptionalField<
    "unroll-full-max-count", "unsigned",
    "Set the max unroll count for full unrolling, for testing purposes",
    /*hidden=*/1>;

defm SC_UnrollAllowPartial : OptionalValueField<
    "unroll-allow-partial", "bool", "true",
    "Allows loops to be partially unrolled until -unroll-threshold loop"
    " size is reached.",
    /*hidden=*/1>;

defm SC_UnrollAllowRemainder : OptionalValueField<
    "unroll-allow-remainder", "bool", "true",
    "Allow generation of a loop remainder (extra iterations) when "
    "unrolling a loop.",
    /*hidden=*/1>;

defm SC_UnrollRuntime : OptionalValueField<
    "unroll-runtime", "bool", "true",
    "Unroll loops with run-time trip counts",
    /*hidden=*/1>;

defm SC_UnrollMaxUpperbound : OptionalField<
    "unroll-max-upperbound", "unsigned",
    "The max of trip count upper bound that is considered in unrolling",
    /*hidden=*/1>;

defm SC_PragmaUnrollThreshold : ValueField<
    "pragma-unroll-threshold", "unsigned", "16384",
    "Unrolled size limit for loops with unroll metadata (full, enable, "
    "or count).",
    /*hidden=*/1>;

defm SC_FlatLoopTripcountThreshold : ValueField<
    "flat-loop-tripcount-threshold", "unsigned", "5",
    "If the runtime tripcount for the loop is lower than the threshold,"
    " the loop is considered as flat and will be less aggressively "
    "unrolled.",
    /*hidden=*/1>;

defm SC_UnrollRemainder : OptionalValueField<
    "unroll-remainder", "bool", "true",
    "Allow the loop remainder to be unrolled.",
    /*hidden=*/1>;

defm SC_UnrollRevisitChildLoops : BoolField<
    "unroll-revisit-child-loops", "false",
    "Enqueue and re-visit child loops in the loop PM after unrolling. "
    "This shouldn't typically be needed as child loops (or their "
    "clones) were already visited.",
    /*hidden=*/1>;

defm SC_UnrollThresholdAggressive : ValueField<
    "unroll-threshold-aggressive", "unsigned", "300",
    "Threshold (max size of unrolled loop) to use in aggressive (O3) "
    "optimizations",
    /*hidden=*/1>;

defm SC_UnrollThresholdDefault : ValueField<
    "unroll-threshold-default", "unsigned", "150",
    "Default threshold (max size of unrolled loop), used in all but O3 "
    "optimizations",
    /*hidden=*/1>;

defm SC_PragmaUnrollFullMaxIterations : ValueField<
    "pragma-unroll-full-max-iterations", "unsigned", "1000000",
    "Maximum allowed iterations to unroll under pragma unroll full.",
    /*hidden=*/1>;

// --- LoopVersioningLICM.cpp ---

defm SC_LicmVersioningInvariantThreshold : ValueField<
    "licm-versioning-invariant-threshold", "float", "25.0f",
    "LoopVersioningLICM's minimum allowed percentage of possible "
    "invariant instructions per loop",
    /*hidden=*/1>;

defm SC_LicmVersioningMaxDepthThreshold : ValueField<
    "licm-versioning-max-depth-threshold", "unsigned", "2",
    "LoopVersioningLICM's threshold for maximum allowed loop nest/depth",
    /*hidden=*/1>;

// --- LowerExpectIntrinsic.cpp ---

defm SC_LikelyBranchWeight : ValueField<
    "likely-branch-weight", "unsigned", "2000",
    "Weight of the branch likely to be taken (default = 2000)",
    /*hidden=*/1>;

defm SC_UnlikelyBranchWeight : ValueField<
    "unlikely-branch-weight", "unsigned", "1",
    "Weight of the branch unlikely to be taken (default = 1)",
    /*hidden=*/1>;

// --- LowerMatrixIntrinsics.cpp ---

defm SC_FuseMatrix : BoolField<
    "fuse-matrix", "true",
    "Enable/disable fusing matrix instructions.",
    /*hidden=*/1>;

defm SC_FuseMatrixTileSize : ValueField<
    "fuse-matrix-tile-size", "unsigned", "4",
    "Tile size for matrix instruction fusion using square-shaped tiles.",
    /*hidden=*/1>;

defm SC_FuseMatrixLoopsThreshold : ValueField<
    "fuse-matrix-loops-threshold", "unsigned", "200",
    "Generate loop nests for tiling when expected number of operations "
    "exceeds threshold.",
    /*hidden=*/1>;

defm SC_ForceFuseMatrix : BoolField<
    "force-fuse-matrix", "false",
    "Force matrix instruction fusion even if not profitable.",
    /*hidden=*/1>;

defm SC_MatrixAllowContract : BoolField<
    "matrix-allow-contract", "false",
    "Allow the use of FMAs if available and profitable. This may result"
    " in different results, due to less rounding error.",
    /*hidden=*/1>;

defm SC_VerifyMatrixShapes : BoolField<
    "verify-matrix-shapes", "false",
    "Enable/disable matrix shape verification.",
    /*hidden=*/1>;

defm SC_MatrixDefaultLayout : EnumField<
    "matrix-default-layout", MatrixLayoutTyV2Enum,
    "MatrixLayoutTyV2::ColumnMajor",
    "Sets the default matrix layout">;

defm SC_MatrixPrintAfterTransposeOpt : BoolField<
    "matrix-print-after-transpose-opt", "false",
    "">;

defm SC_MatrixSplitMatmulRemainderOverThreshold : ValueField<
    "matrix-split-matmul-remainder-over-threshold", "unsigned", "0",
    "Illegal remainder vectors over this size in bits should be split "
    "in the inner loop of matmul",
    /*hidden=*/1>;

// --- NewGVN.cpp ---

defm SC_EnableStoreRefinement : BoolField<
    "enable-store-refinement", "false",
    "",
    /*hidden=*/1>;

defm SC_EnablePhiOfOps : BoolField<
    "enable-phi-of-ops", "true",
    "",
    /*hidden=*/1>;

// --- PlaceSafepoints.cpp ---

defm SC_SppAllBackedges : BoolField<
    "spp-all-backedges", "false",
    "",
    /*hidden=*/1>;

defm SC_SppCountedLoopTripWidth : ValueField<
    "spp-counted-loop-trip-width", "int", "32",
    "",
    /*hidden=*/1>;

defm SC_SppSplitBackedge : BoolField<
    "spp-split-backedge", "false",
    "",
    /*hidden=*/1>;

defm SC_SppNoEntry : BoolField<
    "spp-no-entry", "false",
    "",
    /*hidden=*/1>;

defm SC_SppNoCall : BoolField<
    "spp-no-call", "false",
    "",
    /*hidden=*/1>;

defm SC_SppNoBackedge : BoolField<
    "spp-no-backedge", "false",
    "",
    /*hidden=*/1>;

// --- Reassociate.cpp ---

defm SC_ReassociateUseCseLocal : BoolField<
    "reassociate-use-cse-local", "true",
    "Only reorder expressions within a basic block when exposing CSE "
    "opportunities",
    /*hidden=*/1>;

// --- RewriteStatepointsForGC.cpp ---

defm SC_SppPrintLiveset : BoolField<
    "spp-print-liveset", "false",
    "",
    /*hidden=*/1>;

defm SC_SppPrintLivesetSize : BoolField<
    "spp-print-liveset-size", "false",
    "",
    /*hidden=*/1>;

defm SC_SppPrintBasePointers : BoolField<
    "spp-print-base-pointers", "false",
    "",
    /*hidden=*/1>;

defm SC_SppRematerializationThreshold : ValueField<
    "spp-rematerialization-threshold", "unsigned", "6",
    "",
    /*hidden=*/1>;

// std::optional<bool>: the upstream cl::opt defaulted to true under
// EXPENSIVE_CHECKS and false otherwise, which a literal default cannot
// express; RewriteStatepointsForGC.cpp resolves the unset case.
defm SC_Rs4gcClobberNonLive : OptionalValueField<
    "rs4gc-clobber-non-live", "bool", "true",
    "",
    /*hidden=*/1>;

defm SC_Rs4gcAllowStatepointWithNoDeoptInfo : BoolField<
    "rs4gc-allow-statepoint-with-no-deopt-info", "true",
    "",
    /*hidden=*/1>;

defm SC_Rs4gcRematDerivedAtUses : BoolField<
    "rs4gc-remat-derived-at-uses", "true",
    "",
    /*hidden=*/1>;

// --- SeparateConstOffsetFromGEP.cpp ---

defm SC_DisableSeparateConstOffsetFromGep : BoolField<
    "disable-separate-const-offset-from-gep", "false",
    "Do not separate the constant offset from a GEP instruction",
    /*hidden=*/1>;

defm SC_ReassociateGepsVerifyNoDeadCode : BoolField<
    "reassociate-geps-verify-no-dead-code", "false",
    "Verify this pass produces no dead code",
    /*hidden=*/1>;

// --- SimpleLoopUnswitch.cpp ---

defm SC_EnableNontrivialUnswitch : BoolField<
    "enable-nontrivial-unswitch", "false",
    "Forcibly enables non-trivial loop unswitching rather than "
    "following the configuration passed into the pass.",
    /*hidden=*/1>;

defm SC_UnswitchThreshold : ValueField<
    "unswitch-threshold", "int", "50",
    "The cost threshold for unswitching a loop.",
    /*hidden=*/1>;

defm SC_EnableUnswitchCostMultiplier : BoolField<
    "enable-unswitch-cost-multiplier", "true",
    "Enable unswitch cost multiplier that prohibits exponential "
    "explosion in nontrivial unswitch.",
    /*hidden=*/1>;

defm SC_UnswitchSiblingsToplevelDiv : ValueField<
    "unswitch-siblings-toplevel-div", "int", "2",
    "Toplevel siblings divisor for cost multiplier.",
    /*hidden=*/1>;

defm SC_UnswitchParentBlocksDiv : ValueField<
    "unswitch-parent-blocks-div", "int", "8",
    "Outer loop size divisor for cost multiplier.",
    /*hidden=*/1>;

defm SC_UnswitchNumInitialUnscaledCandidates : ValueField<
    "unswitch-num-initial-unscaled-candidates", "int", "8",
    "Number of unswitch candidates that are ignored when calculating "
    "cost multiplier.",
    /*hidden=*/1>;

defm SC_SimpleLoopUnswitchGuards : BoolField<
    "simple-loop-unswitch-guards", "true",
    "If enabled, simple loop unswitching will also consider "
    "llvm.experimental.guard intrinsics as unswitch candidates.",
    /*hidden=*/1>;

defm SC_SimpleLoopUnswitchDropNonTrivialImplicitNullChecks : BoolField<
    "simple-loop-unswitch-drop-non-trivial-implicit-null-checks", "false",
    "If enabled, drop make.implicit metadata in unswitched implicit "
    "null checks to save time analyzing if we can keep it.",
    /*hidden=*/1>;

defm SC_SimpleLoopUnswitchMemoryssaThreshold : ValueField<
    "simple-loop-unswitch-memoryssa-threshold", "unsigned", "100",
    "Max number of memory uses to explore during partial unswitching "
    "analysis",
    /*hidden=*/1>;

defm SC_FreezeLoopUnswitchCond : BoolField<
    "freeze-loop-unswitch-cond", "true",
    "If enabled, the freeze instruction will be added to condition of "
    "loop unswitch to prevent miscompilation.",
    /*hidden=*/1>;

defm SC_SimpleLoopUnswitchInjectInvariantConditions : BoolField<
    "simple-loop-unswitch-inject-invariant-conditions", "true",
    "Whether we should inject new invariants and unswitch them to "
    "eliminate some existing (non-invariant) conditions.",
    /*hidden=*/1>;

defm SC_SimpleLoopUnswitchInjectInvariantConditionHotnessThreshold : ValueField<
    "simple-loop-unswitch-inject-invariant-condition-hotness-threshold", "unsigned", "16",
    "Only try to inject loop invariant conditions and unswitch on them "
    "to eliminate branches that are not-taken 1/<this option> times or "
    "less.",
    /*hidden=*/1>;

defm SC_SimpleLoopUnswitchEstimateProfile : BoolField<
    "simple-loop-unswitch-estimate-profile", "true",
    "",
    /*hidden=*/1>;

// --- SimplifyCFGPass.cpp ---

defm SC_BonusInstThreshold : OptionalField<
    "bonus-inst-threshold", "unsigned",
    "Control the number of bonus instructions (default = 1)",
    /*hidden=*/1>;

defm SC_KeepLoops : OptionalValueField<
    "keep-loops", "bool", "true",
    "Preserve canonical loop structure (default = true)",
    /*hidden=*/1>;

defm SC_SwitchRangeToIcmp : OptionalValueField<
    "switch-range-to-icmp", "bool", "true",
    "Convert switches into an integer range comparison (default = "
    "false)",
    /*hidden=*/1>;

defm SC_SwitchToLookup : OptionalValueField<
    "switch-to-lookup", "bool", "true",
    "Convert switches to lookup tables (default = false)",
    /*hidden=*/1>;

defm SC_ForwardSwitchCond : OptionalValueField<
    "forward-switch-cond", "bool", "true",
    "Forward switch condition to phi ops (default = false)",
    /*hidden=*/1>;

defm SC_HoistCommonInsts : OptionalValueField<
    "hoist-common-insts", "bool", "true",
    "hoist common instructions (default = false)",
    /*hidden=*/1>;

defm SC_HoistLoadsStoresWithCondFaulting : OptionalValueField<
    "hoist-loads-stores-with-cond-faulting", "bool", "true",
    "Hoist loads/stores if the target supports conditional faulting "
    "(default = false)",
    /*hidden=*/1>;

defm SC_SinkCommonInsts : OptionalValueField<
    "sink-common-insts", "bool", "true",
    "Sink common instructions (default = false)",
    /*hidden=*/1>;

defm SC_SpeculateUnpredictables : OptionalValueField<
    "speculate-unpredictables", "bool", "true",
    "Speculate unpredictable branches (default = false)",
    /*hidden=*/1>;

// --- SpeculativeExecution.cpp ---

defm SC_SpecExecMaxSpeculationCost : ValueField<
    "spec-exec-max-speculation-cost", "unsigned", "7",
    "Speculative execution is not applied to basic blocks where the "
    "cost of the instructions to speculatively execute exceeds this "
    "limit.",
    /*hidden=*/1>;

defm SC_SpecExecMaxNotHoisted : ValueField<
    "spec-exec-max-not-hoisted", "unsigned", "5",
    "Speculative execution is not applied to basic blocks where the "
    "number of instructions that would not be speculatively executed "
    "exceeds this limit.",
    /*hidden=*/1>;

defm SC_SpecExecOnlyIfDivergentTarget : BoolField<
    "spec-exec-only-if-divergent-target", "false",
    "Speculative execution is applied only to targets with divergent "
    "branches, even if the pass was configured to apply only to all "
    "targets.",
    /*hidden=*/1>;

// --- SROA.cpp ---

defm SC_SroaSkipMem2Reg : BoolField<
    "sroa-skip-mem2reg", "false",
    "",
    /*hidden=*/1>;

// --- StraightLineStrengthReduce.cpp ---

defm SC_EnablePoisonReuseGuard : BoolField<
    "enable-poison-reuse-guard", "true",
    "Enable poison-reuse guard">;

// --- StructurizeCFG.cpp ---

// std::optional<bool>: only overrides the pass's constructor argument when
// given explicitly.
defm SC_StructurizecfgSkipUniformRegions : OptionalValueField<
    "structurizecfg-skip-uniform-regions", "bool", "true",
    "Force whether the StructurizeCFG pass skips uniform regions",
    /*hidden=*/1>;

defm SC_StructurizecfgRelaxedUniformRegions : BoolField<
    "structurizecfg-relaxed-uniform-regions", "true",
    "Allow relaxed uniform region checks",
    /*hidden=*/1>;

// --- TailRecursionElimination.cpp ---

defm SC_TreDisableEntrycountRecompute : BoolField<
    "tre-disable-entrycount-recompute", "false",
    "Force disabling recomputing of function entry count, on successful"
    " tail recursion elimination.",
    /*hidden=*/1>;

defm SC_DisableTailCallElimForColdCalls : BoolField<
    "disable-tail-call-elim-for-cold-calls", "false",
    "Disable tail call elimination and optimization for cold calls or "
    "in cold functions",
    /*hidden=*/1>;
