//===- UtilsOptions.td - CLV2 option defs for Transforms/Utils -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def TransformUtilsOptsReg : Registry<"llvm::tu_opts"> { let OutOfLineGetters = 1; }

// --- AddDiscriminators.cpp ---

def TU_NoDiscriminators : Option<
    "no-discriminators",
    "Disable generation of discriminator information.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
}

// --- AssumeBundleBuilder.cpp ---

def TU_EnableKnowledgeRetention : Option<
    "enable-knowledge-retention",
    "enable preservation of attributes throughout code transformation",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

// --- BasicBlockUtils.cpp ---

def TU_MaxDeoptOrUnreachableSuccessorCheckDepth : Option<
    "max-deopt-or-unreachable-succ-check-depth",
    "Set the maximum path length when checking whether a basic block "
    "is followed by a block that either has a terminating "
    "deoptimizing call or is terminated with an unreachable",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "8";
  let Hidden = 1;
}

// --- CodeExtractor.cpp ---

def TU_AggregateArgsOpt : Option<
    "aggregate-extracted-args",
    "Aggregate arguments to code-extracted functions",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
  let Default = "0";
}

// --- CodeLayout.cpp ---

def TU_EnableExtTspBlockPlacement : Option<
    "enable-ext-tsp-block-placement",
    "Enable machine block placement based on the ext-tsp model, "
    "optimizing I-cache utilization.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_ApplyExtTspWithoutProfile : Option<
    "ext-tsp-apply-without-profile",
    "Whether to apply ext-tsp placement for instances w/o profile",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def TU_ForwardWeightCond : Option<
    "ext-tsp-forward-weight-cond",
    "The weight of conditional forward jumps for ExtTSP value",
    "double"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "0.1";
  let ReallyHidden = 1;
}

def TU_ForwardWeightUncond : Option<
    "ext-tsp-forward-weight-uncond",
    "The weight of unconditional forward jumps for ExtTSP value",
    "double"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "0.1";
  let ReallyHidden = 1;
}

def TU_BackwardWeightCond : Option<
    "ext-tsp-backward-weight-cond",
    "The weight of conditional backward jumps for ExtTSP value",
    "double"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "0.1";
  let ReallyHidden = 1;
}

def TU_BackwardWeightUncond : Option<
    "ext-tsp-backward-weight-uncond",
    "The weight of unconditional backward jumps for ExtTSP value",
    "double"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "0.1";
  let ReallyHidden = 1;
}

def TU_FallthroughWeightCond : Option<
    "ext-tsp-fallthrough-weight-cond",
    "The weight of conditional fallthrough jumps for ExtTSP value",
    "double"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1.0";
  let ReallyHidden = 1;
}

def TU_FallthroughWeightUncond : Option<
    "ext-tsp-fallthrough-weight-uncond",
    "The weight of unconditional fallthrough jumps for ExtTSP value",
    "double"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1.05";
  let ReallyHidden = 1;
}

def TU_ForwardDistance : Option<
    "ext-tsp-forward-distance",
    "The maximum distance (in bytes) of a forward jump for ExtTSP",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1024";
  let ReallyHidden = 1;
}

def TU_BackwardDistance : Option<
    "ext-tsp-backward-distance",
    "The maximum distance (in bytes) of a backward jump for ExtTSP",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "640";
  let ReallyHidden = 1;
}

def TU_MaxChainSize : Option<
    "ext-tsp-max-chain-size",
    "The maximum size of a chain to create",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "512";
  let ReallyHidden = 1;
}

def TU_ChainSplitThreshold : Option<
    "ext-tsp-chain-split-threshold",
    "The maximum size of a chain to apply splitting",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "128";
  let ReallyHidden = 1;
}

def TU_MaxMergeDensityRatio : Option<
    "ext-tsp-max-merge-density-ratio",
    "The maximum ratio between densities of two chains for merging",
    "double"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "100.0";
  let ReallyHidden = 1;
}

def TU_CacheEntries : Option<
    "cdsort-cache-entries",
    "The size of the cache",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let ReallyHidden = 1;
}

def TU_CacheSize : Option<
    "cdsort-cache-size",
    "The size of a line in the cache",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let ReallyHidden = 1;
}

def TU_CDMaxChainSize : Option<
    "cdsort-max-chain-size",
    "The maximum size of a chain to create",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let ReallyHidden = 1;
}

def TU_DistancePower : Option<
    "cdsort-distance-power",
    "The power exponent for the distance-based locality",
    "double"> {
  let Registry = TransformUtilsOptsReg;
  let ReallyHidden = 1;
}

def TU_FrequencyScale : Option<
    "cdsort-frequency-scale",
    "The scale factor for the frequency-based locality",
    "double"> {
  let Registry = TransformUtilsOptsReg;
  let ReallyHidden = 1;
}

// --- Debugify.cpp ---

def TU_ApplyAtomGroups : Option<
    "debugify-atoms", "",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
}

def TU_Quiet : Option<
    "debugify-quiet",
    "Suppress verbose debugify output",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
}

def TU_DebugifyFunctionsLimit : Option<
    "debugify-func-limit",
    "Set max number of processed functions per pass.",
    "uint64_t"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "4294967295";
}

def DebugifyLevelLocations : EnumValue<"locations",
    "Locations only"> {
  let CppName = "Locations";
}

def DebugifyLevelLocationsAndVariables : EnumValue<"location+variables",
    "Locations and Variables"> {
  let CppName = "LocationsAndVariables";
}

def DebugifyLevelEnum : EnumType<"DebugifyLevel",
    [DebugifyLevelLocations, DebugifyLevelLocationsAndVariables]>;

def TU_DebugifyLevel : Option<
    "debugify-level",
    "Kind of debug info to add",
    "DebugifyLevel"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "location+variables";
}

// --- FunctionImportUtils.cpp ---

def TU_UseSourceFilenameForPromotedLocals : Option<
    "use-source-filename-for-promoted-locals",
    "Uses the source file name instead of the Module hash. "
    "This requires that the source filename has a unique name / "
    "path to avoid name collisions.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_MoveSymbolGUID : ListOption<
    "thinlto-move-symbols",
    "Move the symbols with the given name. This will delete these symbols "
    "wherever they are originally defined, and make sure their "
    "linkage is External where they are imported. It is meant to be "
    "used with the name of contextual profiling roots.",
    "uint64_t"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

// --- GuardUtils.cpp ---

def TU_PredicatePassBranchWeight : Option<
    "guards-predicate-pass-branch-weight",
    "The probability of a guard failing is assumed to be the "
    "reciprocal of this value (default = 1 << 20)",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1048576";
  let Hidden = 1;
}

// --- InlineFunction.cpp ---

def TU_EnableNoAliasConversion : Option<
    "enable-noalias-to-md-conversion",
    "Convert noalias attributes to metadata during inlining.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def TU_UseNoAliasIntrinsic : Option<
    "use-noalias-intrinsic-during-inlining",
    "Use the llvm.experimental.noalias.scope.decl "
    "intrinsic during inlining.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def TU_PreserveAlignmentAssumptions : Option<
    "preserve-alignment-assumptions-during-inlining",
    "Convert align attributes to assumptions during inlining.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_InlinerAttributeWindow : Option<
    "max-inst-checked-for-throw-during-inlining",
    "the maximum number of instructions analyzed for may throw during "
    "attribute inference in inlined body",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "4";
  let Hidden = 1;
}

// --- Instrumentation.cpp ---

def TU_ClIgnoreRedundantInstrumentation : Option<
    "ignore-redundant-instrumentation",
    "Ignore redundant instrumentation",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

// --- LCSSA.cpp ---

def TU_VerifyLoopLCSSAFlag : Option<
    "verify-loop-lcssa",
    "Verify loop lcssa form (time consuming)",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

// --- Local.cpp ---

def TU_PHICSEDebugHash : Option<
    "phicse-debug-hash",
    "Perform extra assertion checking to verify that PHINodes's hash "
    "function is well-behaved w.r.t. its isEqual predicate",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_PHICSENumPHISmallSize : Option<
    "phicse-num-phi-smallsize",
    "When the basic block contains not more than this number of PHI nodes, "
    "perform a (faster!) exhaustive search instead of set-driven one.",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "32";
  let Hidden = 1;
}

def TU_MaxPhiEntriesIncreaseAfterRemovingEmptyBlock : Option<
    "max-phi-entries-increase-after-removing-empty-block",
    "Stop removing an empty block if removing it will introduce more "
    "than this number of phi entries in its successor",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1000";
  let Hidden = 1;
}

// --- LoopPeel.cpp ---

def TU_UnrollPeelCount : Option<
    "unroll-peel-count",
    "Set the unroll peeling count, for testing purposes",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_UnrollAllowPeeling : Option<
    "unroll-allow-peeling",
    "Allows loops to be peeled when the dynamic "
    "trip count is known to be low.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def TU_UnrollAllowLoopNestsPeeling : Option<
    "unroll-allow-loop-nests-peeling",
    "Allows loop nests to be peeled.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_UnrollPeelMaxCount : Option<
    "unroll-peel-max-count",
    "Max average trip count which will cause loop peeling.",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "7";
  let Hidden = 1;
}

def TU_UnrollForcePeelCount : Option<
    "unroll-force-peel-count",
    "Force a peel count regardless of profiling information.",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_DisableAdvancedPeeling : Option<
    "disable-advanced-peeling",
    "Disable advance peeling. Issues for convergent targets (D134803).",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_EnablePeelingForIV : Option<
    "enable-peeling-for-iv",
    "Enable peeling to convert Phi nodes into IVs",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

// --- LoopUnroll.cpp ---

def TU_UnrollRuntimeEpilog : Option<
    "unroll-runtime-epilog",
    "Allow runtime unrolled loops to be unrolled "
    "with epilog instead of prolog.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_UnrollVerifyDomtree : Option<
    "unroll-verify-domtree",
    "Verify domtree after unrolling",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_UnrollVerifyLoopInfo : Option<
    "unroll-verify-loopinfo",
    "Verify loopinfo after unrolling",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_UnrollAddParallelReductions : Option<
    "unroll-add-parallel-reductions",
    "Allow unrolling to add parallel reduction phis.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

// --- LoopUnrollRuntime.cpp ---

def TU_UnrollRuntimeMultiExit : Option<
    "unroll-runtime-multi-exit",
    "Allow runtime unrolling for loops with multiple exits, when "
    "epilog is generated",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_UnrollRuntimeOtherExitPredictable : Option<
    "unroll-runtime-other-exit-predictable",
    "Assume the non latch exit block to be predictable",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

// --- LoopVersioning.cpp ---

def TU_AnnotateNoAlias : Option<
    "loop-version-annotate-no-alias",
    "Add no-alias annotation for instructions that "
    "are disambiguated by memchecks",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- MetaRenamer.cpp ---

def TU_RenameExcludeFunctionPrefixes : Option<
    "rename-exclude-function-prefixes",
    "Prefixes for functions that don't need to be renamed, separated "
    "by a comma",
    "std::string"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_RenameExcludeAliasPrefixes : Option<
    "rename-exclude-alias-prefixes",
    "Prefixes for aliases that don't need to be renamed, separated "
    "by a comma",
    "std::string"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_RenameExcludeGlobalPrefixes : Option<
    "rename-exclude-global-prefixes",
    "Prefixes for global values that don't need to be renamed, separated "
    "by a comma",
    "std::string"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_RenameExcludeStructPrefixes : Option<
    "rename-exclude-struct-prefixes",
    "Prefixes for structs that don't need to be renamed, separated "
    "by a comma",
    "std::string"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_RenameOnlyInst : Option<
    "rename-only-inst",
    "only rename the instructions in the function",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

// --- MisExpect.cpp ---

def TU_PGOWarnMisExpect : Option<
    "pgo-warn-misexpect",
    "Use this option to turn on/off "
    "warnings about incorrect usage of llvm.expect intrinsics.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_MisExpectTolerance : Option<
    "misexpect-tolerance",
    "Prevents emitting diagnostics when profile counts are "
    "within N% of the threshold..",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
}

// --- MoveAutoInit.cpp ---

def TU_MoveAutoInitThreshold : Option<
    "move-auto-init-threshold",
    "Maximum instructions to analyze per moved initialization",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "128";
  let Hidden = 1;
}

// --- PredicateInfo.cpp ---

def TU_VerifyPredicateInfo : Option<
    "verify-predicateinfo",
    "Verify PredicateInfo in legacy printer pass.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

// --- ProfileVerify.cpp ---

def TU_DefaultFunctionEntryCount : Option<
    "profcheck-default-function-entry-count", "",
    "int64_t"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1000";
}

def TU_AnnotateSelect : Option<
    "profcheck-annotate-select",
    "Also inject (if missing) and verify MD_prof for "
    "`select` instructions",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1";
}

def TU_WeightsForTest : Option<
    "profcheck-weights-for-test",
    "Generate weights with small values for tests.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
}

def TU_SelectTrueWeight : Option<
    "profcheck-default-select-true-weight",
    "When annotating `select` instructions, this value will be used "
    "for the first ('true') case.",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "2";
}

def TU_SelectFalseWeight : Option<
    "profcheck-default-select-false-weight",
    "When annotating `select` instructions, this value will be used "
    "for the second ('false') case.",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "3";
}

// --- SampleProfileInference.cpp ---

def TU_SampleProfileEvenFlowDistribution : Option<
    "sample-profile-even-flow-distribution",
    "Try to evenly distribute flow when there are multiple equally "
    "likely options.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def TU_SampleProfileRebalanceUnknown : Option<
    "sample-profile-rebalance-unknown",
    "Evenly re-distribute flow among unknown subgraphs.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def TU_SampleProfileJoinIslands : Option<
    "sample-profile-join-islands",
    "Join isolated components having positive flow.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def TU_SampleProfileProfiCostBlockInc : Option<
    "sample-profile-profi-cost-block-inc",
    "The cost of increasing a block's count by one.",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "10";
  let Hidden = 1;
}

def TU_SampleProfileProfiCostBlockDec : Option<
    "sample-profile-profi-cost-block-dec",
    "The cost of decreasing a block's count by one.",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "20";
  let Hidden = 1;
}

def TU_SampleProfileProfiCostBlockEntryInc : Option<
    "sample-profile-profi-cost-block-entry-inc",
    "The cost of increasing the entry block's count by one.",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "40";
  let Hidden = 1;
}

def TU_SampleProfileProfiCostBlockEntryDec : Option<
    "sample-profile-profi-cost-block-entry-dec",
    "The cost of decreasing the entry block's count by one.",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "10";
  let Hidden = 1;
}

def TU_SampleProfileProfiCostBlockZeroInc : Option<
    "sample-profile-profi-cost-block-zero-inc",
    "The cost of increasing a count of zero-weight block by one.",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "11";
  let Hidden = 1;
}

def TU_SampleProfileProfiCostBlockUnknownInc : Option<
    "sample-profile-profi-cost-block-unknown-inc",
    "The cost of increasing an unknown block's count by one.",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

// --- SampleProfileLoaderBaseUtil.cpp ---

def TU_SampleProfileMaxPropagateIterations : Option<
    "sample-profile-max-propagate-iterations",
    "Maximum number of iterations to go through when propagating "
    "sample block/edge weights through the CFG.",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "100";
}

def TU_SampleProfileRecordCoverage : Option<
    "sample-profile-check-record-coverage",
    "Emit a warning if less than N% of records in the input profile "
    "are matched to the IR.",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let ValueDesc = "N";
}

def TU_SampleProfileSampleCoverage : Option<
    "sample-profile-check-sample-coverage",
    "Emit a warning if less than N% of samples in the input profile "
    "are matched to the IR.",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let ValueDesc = "N";
}

def TU_NoWarnSampleUnused : Option<
    "no-warn-sample-unused",
    "Use this option to turn off/on warnings about function with "
    "samples but without debug information to use those samples. ",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_SampleProfileUseProfi : Option<
    "sample-profile-use-profi",
    "Use profi to infer block and edge counts.",
    "bool"> {
  let Default = "false";
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

// --- ScalarEvolutionExpander.cpp ---

def TU_SCEVCheapExpansionBudget : Option<
    "scev-cheap-expansion-budget",
    "When performing SCEV expansion only if it is cheap to do, this "
    "controls the budget that is considered cheap (default = 4)",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "4";
  let Hidden = 1;
}

// --- SimplifyCFG.cpp ---

def TU_RequireAndPreserveDomTree : Option<
    "simplifycfg-require-and-preserve-domtree",
    "Temporary development switch used to gradually uplift SimplifyCFG "
    "into preserving DomTree,",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_PHINodeFoldingThreshold : Option<
    "phi-node-folding-threshold",
    "Control the amount of phi node folding to perform (default = 2)",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def TU_TwoEntryPHINodeFoldingThreshold : Option<
    "two-entry-phi-node-folding-threshold",
    "Control the maximal total instruction cost that we are willing "
    "to speculatively execute to fold a 2-entry PHI node into a "
    "select (default = 4)",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "4";
  let Hidden = 1;
}

def TU_HoistCommon : Option<
    "simplifycfg-hoist-common",
    "Hoist common instructions up to the parent block",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def TU_HoistLoadsWithCondFaulting : Option<
    "simplifycfg-hoist-loads-with-cond-faulting",
    "Hoist loads if the target supports conditional faulting",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def TU_HoistStoresWithCondFaulting : Option<
    "simplifycfg-hoist-stores-with-cond-faulting",
    "Hoist stores if the target supports conditional faulting",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def TU_HoistLoadsStoresWithCondFaultingThreshold : Option<
    "hoist-loads-stores-with-cond-faulting-threshold",
    "Control the maximal conditional load/store that we are willing "
    "to speculatively execute to eliminate conditional branch "
    "(default = 6)",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "6";
  let Hidden = 1;
}

def TU_HoistCommonSkipLimit : Option<
    "simplifycfg-hoist-common-skip-limit",
    "Allow reordering across at most this many "
    "instructions when hoisting",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "20";
  let Hidden = 1;
}

def TU_SinkCommon : Option<
    "simplifycfg-sink-common",
    "Sink common instructions down to the end block",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def TU_HoistCondStores : Option<
    "simplifycfg-hoist-cond-stores",
    "Hoist conditional stores if an unconditional store precedes",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def TU_MergeCondStores : Option<
    "simplifycfg-merge-cond-stores",
    "Hoist conditional stores even if an unconditional store does not "
    "precede - hoist multiple conditional stores into a single "
    "predicated store",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def TU_MergeCondStoresAggressively : Option<
    "simplifycfg-merge-cond-stores-aggressively",
    "When merging conditional stores, do so even if the resultant "
    "basic blocks are unlikely to be if-converted as a result",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_SpeculateOneExpensiveInst : Option<
    "speculate-one-expensive-inst",
    "Allow exactly one expensive instruction to be speculatively "
    "executed",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def TU_MaxSpeculationDepth : Option<
    "max-speculation-depth",
    "Limit maximum recursion depth when calculating costs of "
    "speculatively executed instructions",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "10";
  let Hidden = 1;
}

def TU_MaxSmallBlockSize : Option<
    "simplifycfg-max-small-block-size",
    "Max size of a block which is still considered "
    "small enough to thread through",
    "int"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "10";
  let Hidden = 1;
}

def TU_BranchFoldThreshold : Option<
    "simplifycfg-branch-fold-threshold",
    "Maximum cost of combining conditions when "
    "folding branches",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def TU_BranchFoldToCommonDestVectorMultiplier : Option<
    "simplifycfg-branch-fold-common-dest-vector-multiplier",
    "Multiplier to apply to threshold when determining whether or not "
    "to fold branch to common destination when vector operations are "
    "present",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def TU_EnableMergeCompatibleInvokes : Option<
    "simplifycfg-merge-compatible-invokes",
    "Allow SimplifyCFG to merge invokes together when appropriate",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def TU_MaxSwitchCasesPerResult : Option<
    "max-switch-cases-per-result",
    "Limit cases to analyze when converting a switch to select",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "16";
  let Hidden = 1;
}

def TU_MaxJumpThreadingLiveBlocks : Option<
    "max-jump-threading-live-blocks",
    "Limit number of blocks a define in a threaded block is allowed "
    "to be live in",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "24";
  let Hidden = 1;
}

// --- SimplifyLibCalls.cpp ---

def TU_EnableUnsafeFPShrink : Option<
    "enable-double-float-shrink",
    "Enable unsafe double to float "
    "shrinking for math lib calls",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_OptimizeHotColdNew : Option<
    "optimize-hot-cold-new",
    "Enable hot/cold operator new library calls",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def OptExistingHCNNone : EnumValue<"none",
    "Do not optimize existing hot/cold operator new library calls"> {
  let CppName = "None";
}

def OptExistingHCNCold : EnumValue<"cold",
    "Only optimize existing hot/cold operator new library calls if determined "
    "to be cold"> {
  let CppName = "Cold";
}

def OptExistingHCNAlways : EnumValue<"always",
    "Always optimize existing hot/cold operator new library calls"> {
  let CppName = "Always";
}

// ValueOptional: a bare --optimize-existing-hot-cold-new means "always".
def OptExistingHCNEmpty : EnumValue<"",
    "Always optimize existing hot/cold operator new library calls"> {
  let CppName = "Always";
  let AliasOnly = 1;
}

def OptimizeExistingHotColdNewKindEnum :
    EnumType<"OptimizeExistingHotColdNewKind",
             [OptExistingHCNNone, OptExistingHCNCold, OptExistingHCNAlways,
              OptExistingHCNEmpty]>;

def TU_OptimizeExistingHotColdNew : Option<
    "optimize-existing-hot-cold-new",
    "Enable optimization of existing hot/cold operator new library calls",
    "OptimizeExistingHotColdNewKind"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "none";
  let ValueOptional = 1;
  let Hidden = 1;
}

def TU_UnrollUniformWeights : Option<
    "unroll-uniform-weights",
    "If new branch weights must be found, work harder to keep them uniform.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def TU_MinExistingHotColdNewHint : Option<
    "min-existing-hot-cold-new-hint",
    "Take the minimum of compiler hint and existing hint when optimizing "
    "existing hot/cold operator new library calls",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def TU_OptimizeNoBuiltinHotColdNew : Option<
    "optimize-nobuiltin-hot-cold-new-new",
    "Enable transformation of nobuiltin operator new library calls",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_ColdNewHintValue : Option<
    "cold-new-hint-value",
    "Value to pass to hot/cold operator new for cold allocation",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1";
  let Hidden = 1;
  let Validate = [{
    if (Value > 255)
      return reject("'" + llvm::Twine(Value) +
                    "' value must be in the range [0, 255]!");
    return true;
  }];
}

def TU_NotColdNewHintValue : Option<
    "notcold-new-hint-value",
    "Value to pass to hot/cold operator new for "
    "notcold (warm) allocation",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "128";
  let Hidden = 1;
  let Validate = [{
    if (Value > 255)
      return reject("'" + llvm::Twine(Value) +
                    "' value must be in the range [0, 255]!");
    return true;
  }];
}

def TU_HotNewHintValue : Option<
    "hot-new-hint-value",
    "Value to pass to hot/cold operator new for hot allocation",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "254";
  let Hidden = 1;
  let Validate = [{
    if (Value > 255)
      return reject("'" + llvm::Twine(Value) +
                    "' value must be in the range [0, 255]!");
    return true;
  }];
}

def TU_AmbiguousNewHintValue : Option<
    "ambiguous-new-hint-value",
    "Value to pass to hot/cold operator new for ambiguous allocation",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "222";
  let Hidden = 1;
  let Validate = [{
    if (Value > 255)
      return reject("'" + llvm::Twine(Value) +
                    "' value must be in the range [0, 255]!");
    return true;
  }];
}

// --- SizeOpts.cpp ---

def TU_EnablePGSO : Option<
    "pgso",
    "Enable the profile guided size optimizations. ",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def TU_PGSOLargeWorkingSetSizeOnly : Option<
    "pgso-lwss-only",
    "Apply the profile guided size optimizations only "
    "if the working set size is large (except for cold code.)",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def TU_PGSOColdCodeOnly : Option<
    "pgso-cold-code-only",
    "Apply the profile guided size optimizations only "
    "to cold code.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_PGSOColdCodeOnlyForInstrPGO : Option<
    "pgso-cold-code-only-for-instr-pgo",
    "Apply the profile guided size optimizations only "
    "to cold code under instrumentation PGO.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_PGSOColdCodeOnlyForSamplePGO : Option<
    "pgso-cold-code-only-for-sample-pgo",
    "Apply the profile guided size optimizations only "
    "to cold code under sample PGO.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_PGSOColdCodeOnlyForPartialSamplePGO : Option<
    "pgso-cold-code-only-for-partial-sample-pgo",
    "Apply the profile guided size optimizations only "
    "to cold code under partial-profile sample PGO.",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_ForcePGSO : Option<
    "force-pgso",
    "Force the (profiled-guided) size optimizations. ",
    "bool"> {
  let Registry = TransformUtilsOptsReg;
  let Hidden = 1;
}

def TU_PgsoCutoffInstrProf : Option<
    "pgso-cutoff-instr-prof",
    "The profile guided size optimization profile summary cutoff "
    "for instrumentation profile.",
    "int"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "950000";
  let Hidden = 1;
}

def TU_PgsoCutoffSampleProf : Option<
    "pgso-cutoff-sample-prof",
    "The profile guided size optimization profile summary cutoff "
    "for sample profile.",
    "int"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "990000";
  let Hidden = 1;
}

// --- SymbolRewriter.cpp ---

def TU_RewriteMapFiles : ListOption<
    "rewrite-map-file",
    "Symbol Rewrite Map",
    "std::string"> {
  let Registry = TransformUtilsOptsReg;
  let ValueDesc = "filename";
  let Hidden = 1;
}

// --- UnifyLoopExits.cpp ---

def TU_MaxBooleansInControlFlowHub : Option<
    "max-booleans-in-control-flow-hub",
    "Set the maximum number of outgoing blocks for using a boolean "
    "value to record the exiting block in the ControlFlowHub.",
    "unsigned"> {
  let Registry = TransformUtilsOptsReg;
  let Default = "32";
  let Hidden = 1;
}
