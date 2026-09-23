//===- UtilsOptions.td - Transforms/Utils options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def UtilsOptions : OptionsStruct<"llvm"> {}

// --- AddDiscriminators.cpp ---

defm TU_NoDiscriminators : BoolField<
    "no-discriminators", "false",
    "Disable generation of discriminator information.">;

// --- AssumeBundleBuilder.cpp ---

defm TU_EnableKnowledgeRetention : BoolField<
    "enable-knowledge-retention", "false",
    "enable preservation of attributes throughout code transformation",
    /*hidden=*/1>;

// --- BasicBlockUtils.cpp ---

defm TU_MaxDeoptOrUnreachableSuccessorCheckDepth : ValueField<
    "max-deopt-or-unreachable-succ-check-depth", "unsigned", "8",
    "Set the maximum path length when checking whether a basic block "
    "is followed by a block that either has a terminating "
    "deoptimizing call or is terminated with an unreachable",
    /*hidden=*/1>;

// --- CodeExtractor.cpp ---

defm TU_AggregateArgsOpt : BoolField<
    "aggregate-extracted-args", "false",
    "Aggregate arguments to code-extracted functions",
    /*hidden=*/1>;

// --- CodeLayout.cpp ---

// std::optional<bool>: SampleProfile.cpp's ext-tsp override (see
// TU_SampleProfileUseProfi below) branches on whether this was ever
// specified on the command line, not merely on its resolved value, so
// "unspecified" must survive as real tri-state.
defm TU_EnableExtTspBlockPlacement : OptionalValueField<
    "enable-ext-tsp-block-placement", "bool", "true",
    "Enable machine block placement based on the ext-tsp model, "
    "optimizing I-cache utilization.",
    /*hidden=*/1>;

defm TU_ApplyExtTspWithoutProfile : BoolField<
    "ext-tsp-apply-without-profile", "true",
    "Whether to apply ext-tsp placement for instances w/o profile",
    /*hidden=*/1>;

defm TU_ForwardWeightCond : ValueField<
    "ext-tsp-forward-weight-cond", "double", "0.1",
    "The weight of conditional forward jumps for ExtTSP value",
    /*hidden=*/1>;

defm TU_ForwardWeightUncond : ValueField<
    "ext-tsp-forward-weight-uncond", "double", "0.1",
    "The weight of unconditional forward jumps for ExtTSP value",
    /*hidden=*/1>;

defm TU_BackwardWeightCond : ValueField<
    "ext-tsp-backward-weight-cond", "double", "0.1",
    "The weight of conditional backward jumps for ExtTSP value",
    /*hidden=*/1>;

defm TU_BackwardWeightUncond : ValueField<
    "ext-tsp-backward-weight-uncond", "double", "0.1",
    "The weight of unconditional backward jumps for ExtTSP value",
    /*hidden=*/1>;

defm TU_FallthroughWeightCond : ValueField<
    "ext-tsp-fallthrough-weight-cond", "double", "1.0",
    "The weight of conditional fallthrough jumps for ExtTSP value",
    /*hidden=*/1>;

defm TU_FallthroughWeightUncond : ValueField<
    "ext-tsp-fallthrough-weight-uncond", "double", "1.05",
    "The weight of unconditional fallthrough jumps for ExtTSP value",
    /*hidden=*/1>;

defm TU_ForwardDistance : ValueField<
    "ext-tsp-forward-distance", "unsigned", "1024",
    "The maximum distance (in bytes) of a forward jump for ExtTSP",
    /*hidden=*/1>;

defm TU_BackwardDistance : ValueField<
    "ext-tsp-backward-distance", "unsigned", "640",
    "The maximum distance (in bytes) of a backward jump for ExtTSP",
    /*hidden=*/1>;

defm TU_MaxChainSize : ValueField<
    "ext-tsp-max-chain-size", "unsigned", "512",
    "The maximum size of a chain to create",
    /*hidden=*/1>;

defm TU_ChainSplitThreshold : ValueField<
    "ext-tsp-chain-split-threshold", "unsigned", "128",
    "The maximum size of a chain to apply splitting",
    /*hidden=*/1>;

defm TU_MaxMergeDensityRatio : ValueField<
    "ext-tsp-max-merge-density-ratio", "double", "100.0",
    "The maximum ratio between densities of two chains for merging",
    /*hidden=*/1>;

// std::optional<unsigned>: CDSortConfig's own constructor default (not a
// same-typed literal reproducible here) is only overridden when specified.
defm TU_CacheEntries : OptionalField<
    "cdsort-cache-entries", "unsigned", "The size of the cache",
    /*hidden=*/1>;

defm TU_CacheSize : OptionalField<
    "cdsort-cache-size", "unsigned", "The size of a line in the cache",
    /*hidden=*/1>;

defm TU_CDMaxChainSize : OptionalField<
    "cdsort-max-chain-size", "unsigned", "The maximum size of a chain to create",
    /*hidden=*/1>;

defm TU_DistancePower : OptionalField<
    "cdsort-distance-power", "double",
    "The power exponent for the distance-based locality",
    /*hidden=*/1>;

defm TU_FrequencyScale : OptionalField<
    "cdsort-frequency-scale", "double",
    "The scale factor for the frequency-based locality",
    /*hidden=*/1>;

// --- Debugify.cpp ---

defm TU_ApplyAtomGroups : BoolField<"debugify-atoms", "false", "">;

defm TU_Quiet : BoolField<
    "debugify-quiet", "false", "Suppress verbose debugify output">;

defm TU_DebugifyFunctionsLimit : ValueField<
    "debugify-func-limit", "uint64_t", "4294967295",
    "Set max number of processed functions per pass.">;

def DebugifyLevelEnum : OptionEnum<"DebugifyLevel", [
    EnumMember<"Locations", "locations">,
    EnumMember<"LocationsAndVariables", "location+variables">]>;

defm TU_DebugifyLevel : EnumField<
    "debugify-level", DebugifyLevelEnum,
    "DebugifyLevel::LocationsAndVariables", "Kind of debug info to add">;

// --- FunctionImportUtils.cpp ---

defm TU_UseSourceFilenameForPromotedLocals : BoolField<
    "use-source-filename-for-promoted-locals", "false",
    "Uses the source file name instead of the Module hash. "
    "This requires that the source filename has a unique name / "
    "path to avoid name collisions.",
    /*hidden=*/1>;

defm TU_MoveSymbolGUID : ListField<
    "thinlto-move-symbols", "uint64_t",
    "Move the symbols with the given name. This will delete these symbols "
    "wherever they are originally defined, and make sure their "
    "linkage is External where they are imported. It is meant to be "
    "used with the name of contextual profiling roots.",
    /*commaJoined=*/0, /*appendEquals=*/1, /*hidden=*/1>;

// --- GuardUtils.cpp ---

defm TU_PredicatePassBranchWeight : ValueField<
    "guards-predicate-pass-branch-weight", "unsigned", "1048576",
    "The probability of a guard failing is assumed to be the "
    "reciprocal of this value (default = 1 << 20)",
    /*hidden=*/1>;

// --- InlineFunction.cpp ---

defm TU_EnableNoAliasConversion : BoolField<
    "enable-noalias-to-md-conversion", "true",
    "Convert noalias attributes to metadata during inlining.",
    /*hidden=*/1>;

defm TU_UseNoAliasIntrinsic : BoolField<
    "use-noalias-intrinsic-during-inlining", "true",
    "Use the llvm.experimental.noalias.scope.decl "
    "intrinsic during inlining.",
    /*hidden=*/1>;

defm TU_PreserveAlignmentAssumptions : BoolField<
    "preserve-alignment-assumptions-during-inlining", "false",
    "Convert align attributes to assumptions during inlining.",
    /*hidden=*/1>;

defm TU_InlinerAttributeWindow : ValueField<
    "max-inst-checked-for-throw-during-inlining", "unsigned", "4",
    "the maximum number of instructions analyzed for may throw during "
    "attribute inference in inlined body",
    /*hidden=*/1>;

// --- Instrumentation.cpp ---

defm TU_ClIgnoreRedundantInstrumentation : BoolField<
    "ignore-redundant-instrumentation", "false",
    "Ignore redundant instrumentation",
    /*hidden=*/1>;

// --- LCSSA.cpp ---

// std::optional<bool>: the fallback (VerifyLoopLCSSADefault, true under
// EXPENSIVE_CHECKS and false otherwise) is a build-config-dependent value,
// not the schema's own literal default, so "unspecified" must survive.
defm TU_VerifyLoopLCSSAFlag : OptionalValueField<
    "verify-loop-lcssa", "bool", "true",
    "Verify loop lcssa form (time consuming)",
    /*hidden=*/1>;

// --- Local.cpp ---

// std::optional<bool>: same EXPENSIVE_CHECKS-conditional-fallback reasoning
// as TU_VerifyLoopLCSSAFlag above.
defm TU_PHICSEDebugHash : OptionalValueField<
    "phicse-debug-hash", "bool", "true",
    "Perform extra assertion checking to verify that PHINodes's hash "
    "function is well-behaved w.r.t. its isEqual predicate",
    /*hidden=*/1>;

defm TU_PHICSENumPHISmallSize : ValueField<
    "phicse-num-phi-smallsize", "unsigned", "32",
    "When the basic block contains not more than this number of PHI nodes, "
    "perform a (faster!) exhaustive search instead of set-driven one.",
    /*hidden=*/1>;

defm TU_MaxPhiEntriesIncreaseAfterRemovingEmptyBlock : ValueField<
    "max-phi-entries-increase-after-removing-empty-block", "unsigned", "1000",
    "Stop removing an empty block if removing it will introduce more "
    "than this number of phi entries in its successor",
    /*hidden=*/1>;

// --- LoopPeel.cpp ---

// std::optional<unsigned>: "for testing purposes" -- only overrides the
// analysis-derived peel count when explicitly specified.
defm TU_UnrollPeelCount : OptionalField<
    "unroll-peel-count", "unsigned",
    "Set the unroll peeling count, for testing purposes",
    /*hidden=*/1>;

// std::optional<bool>: LoopPeel.cpp's "priority if supplied" gating over a
// separately-computed default requires "unspecified" to survive.
defm TU_UnrollAllowPeeling : OptionalValueField<
    "unroll-allow-peeling", "bool", "true",
    "Allows loops to be peeled when the dynamic "
    "trip count is known to be low.",
    /*hidden=*/1>;

defm TU_UnrollAllowLoopNestsPeeling : OptionalValueField<
    "unroll-allow-loop-nests-peeling", "bool", "true",
    "Allows loop nests to be peeled.",
    /*hidden=*/1>;

defm TU_UnrollPeelMaxCount : ValueField<
    "unroll-peel-max-count", "unsigned", "7",
    "Max average trip count which will cause loop peeling.",
    /*hidden=*/1>;

// std::optional<unsigned>: only overrides profiling information when
// explicitly specified.
defm TU_UnrollForcePeelCount : OptionalField<
    "unroll-force-peel-count", "unsigned",
    "Force a peel count regardless of profiling information.",
    /*hidden=*/1>;

defm TU_DisableAdvancedPeeling : BoolField<
    "disable-advanced-peeling", "false",
    "Disable advance peeling. Issues for convergent targets (D134803).",
    /*hidden=*/1>;

defm TU_EnablePeelingForIV : BoolField<
    "enable-peeling-for-iv", "false",
    "Enable peeling to convert Phi nodes into IVs",
    /*hidden=*/1>;

// --- LoopUnroll.cpp ---

// std::optional<bool>: MachineBlockPlacement-style "priority if supplied"
// gating, see LoopUnroll.cpp's use of the specified-bit.
defm TU_UnrollRuntimeEpilog : OptionalValueField<
    "unroll-runtime-epilog", "bool", "true",
    "Allow runtime unrolled loops to be unrolled "
    "with epilog instead of prolog.",
    /*hidden=*/1>;

// std::optional<bool>: fallback is EXPENSIVE_CHECKS-conditional, not the
// schema's own literal default.
defm TU_UnrollVerifyDomtree : OptionalValueField<
    "unroll-verify-domtree", "bool", "true",
    "Verify domtree after unrolling",
    /*hidden=*/1>;

defm TU_UnrollVerifyLoopInfo : OptionalValueField<
    "unroll-verify-loopinfo", "bool", "true",
    "Verify loopinfo after unrolling",
    /*hidden=*/1>;

defm TU_UnrollAddParallelReductions : OptionalValueField<
    "unroll-add-parallel-reductions", "bool", "true",
    "Allow unrolling to add parallel reduction phis.",
    /*hidden=*/1>;

// --- LoopUnrollRuntime.cpp ---

defm TU_UnrollRuntimeMultiExit : OptionalValueField<
    "unroll-runtime-multi-exit", "bool", "true",
    "Allow runtime unrolling for loops with multiple exits, when "
    "epilog is generated",
    /*hidden=*/1>;

defm TU_UnrollRuntimeOtherExitPredictable : BoolField<
    "unroll-runtime-other-exit-predictable", "false",
    "Assume the non latch exit block to be predictable",
    /*hidden=*/1>;

// --- LoopVersioning.cpp ---

defm TU_AnnotateNoAlias : BoolField<
    "loop-version-annotate-no-alias", "true",
    "Add no-alias annotation for instructions that "
    "are disambiguated by memchecks",
    /*hidden=*/1>;

// --- MetaRenamer.cpp ---

defm TU_RenameExcludeFunctionPrefixes : ValueField<
    "rename-exclude-function-prefixes", "std::string", "\"\"",
    "Prefixes for functions that don't need to be renamed, separated "
    "by a comma",
    /*hidden=*/1>;

defm TU_RenameExcludeAliasPrefixes : ValueField<
    "rename-exclude-alias-prefixes", "std::string", "\"\"",
    "Prefixes for aliases that don't need to be renamed, separated "
    "by a comma",
    /*hidden=*/1>;

defm TU_RenameExcludeGlobalPrefixes : ValueField<
    "rename-exclude-global-prefixes", "std::string", "\"\"",
    "Prefixes for global values that don't need to be renamed, separated "
    "by a comma",
    /*hidden=*/1>;

defm TU_RenameExcludeStructPrefixes : ValueField<
    "rename-exclude-struct-prefixes", "std::string", "\"\"",
    "Prefixes for structs that don't need to be renamed, separated "
    "by a comma",
    /*hidden=*/1>;

defm TU_RenameOnlyInst : BoolField<
    "rename-only-inst", "false",
    "only rename the instructions in the function",
    /*hidden=*/1>;

// --- MisExpect.cpp ---

defm TU_PGOWarnMisExpect : BoolField<
    "pgo-warn-misexpect", "false",
    "Use this option to turn on/off "
    "warnings about incorrect usage of llvm.expect intrinsics.",
    /*hidden=*/1>;

defm TU_MisExpectTolerance : ValueField<
    "misexpect-tolerance", "unsigned", "0",
    "Prevents emitting diagnostics when profile counts are "
    "within N% of the threshold..">;

// --- MoveAutoInit.cpp ---

defm TU_MoveAutoInitThreshold : ValueField<
    "move-auto-init-threshold", "unsigned", "128",
    "Maximum instructions to analyze per moved initialization",
    /*hidden=*/1>;

// --- PredicateInfo.cpp ---

defm TU_VerifyPredicateInfo : BoolField<
    "verify-predicateinfo", "false",
    "Verify PredicateInfo in legacy printer pass.",
    /*hidden=*/1>;

// --- ProfileVerify.cpp ---

defm TU_DefaultFunctionEntryCount : ValueField<
    "profcheck-default-function-entry-count", "int64_t", "1000", "">;

defm TU_AnnotateSelect : BoolField<
    "profcheck-annotate-select", "true",
    "Also inject (if missing) and verify MD_prof for "
    "`select` instructions">;

defm TU_WeightsForTest : BoolField<
    "profcheck-weights-for-test", "false",
    "Generate weights with small values for tests.">;

defm TU_SelectTrueWeight : ValueField<
    "profcheck-default-select-true-weight", "unsigned", "2",
    "When annotating `select` instructions, this value will be used "
    "for the first ('true') case.">;

defm TU_SelectFalseWeight : ValueField<
    "profcheck-default-select-false-weight", "unsigned", "3",
    "When annotating `select` instructions, this value will be used "
    "for the second ('false') case.">;

// --- SampleProfileInference.cpp ---

defm TU_SampleProfileEvenFlowDistribution : BoolField<
    "sample-profile-even-flow-distribution", "true",
    "Try to evenly distribute flow when there are multiple equally "
    "likely options.",
    /*hidden=*/1>;

defm TU_SampleProfileRebalanceUnknown : BoolField<
    "sample-profile-rebalance-unknown", "true",
    "Evenly re-distribute flow among unknown subgraphs.",
    /*hidden=*/1>;

defm TU_SampleProfileJoinIslands : BoolField<
    "sample-profile-join-islands", "true",
    "Join isolated components having positive flow.",
    /*hidden=*/1>;

defm TU_SampleProfileProfiCostBlockInc : ValueField<
    "sample-profile-profi-cost-block-inc", "unsigned", "10",
    "The cost of increasing a block's count by one.",
    /*hidden=*/1>;

defm TU_SampleProfileProfiCostBlockDec : ValueField<
    "sample-profile-profi-cost-block-dec", "unsigned", "20",
    "The cost of decreasing a block's count by one.",
    /*hidden=*/1>;

defm TU_SampleProfileProfiCostBlockEntryInc : ValueField<
    "sample-profile-profi-cost-block-entry-inc", "unsigned", "40",
    "The cost of increasing the entry block's count by one.",
    /*hidden=*/1>;

defm TU_SampleProfileProfiCostBlockEntryDec : ValueField<
    "sample-profile-profi-cost-block-entry-dec", "unsigned", "10",
    "The cost of decreasing the entry block's count by one.",
    /*hidden=*/1>;

defm TU_SampleProfileProfiCostBlockZeroInc : ValueField<
    "sample-profile-profi-cost-block-zero-inc", "unsigned", "11",
    "The cost of increasing a count of zero-weight block by one.",
    /*hidden=*/1>;

defm TU_SampleProfileProfiCostBlockUnknownInc : ValueField<
    "sample-profile-profi-cost-block-unknown-inc", "unsigned", "0",
    "The cost of increasing an unknown block's count by one.",
    /*hidden=*/1>;

// --- SampleProfileLoaderBaseUtil.cpp ---

defm TU_SampleProfileMaxPropagateIterations : ValueField<
    "sample-profile-max-propagate-iterations", "unsigned", "100",
    "Maximum number of iterations to go through when propagating "
    "sample block/edge weights through the CFG.">;

defm TU_SampleProfileRecordCoverage : ValueField<
    "sample-profile-check-record-coverage", "unsigned", "0",
    "Emit a warning if less than N% of records in the input profile "
    "are matched to the IR.",
    /*hidden=*/0, /*metaVar=*/"N">;

defm TU_SampleProfileSampleCoverage : ValueField<
    "sample-profile-check-sample-coverage", "unsigned", "0",
    "Emit a warning if less than N% of samples in the input profile "
    "are matched to the IR.",
    /*hidden=*/0, /*metaVar=*/"N">;

defm TU_NoWarnSampleUnused : BoolField<
    "no-warn-sample-unused", "false",
    "Use this option to turn off/on warnings about function with "
    "samples but without debug information to use those samples. ",
    /*hidden=*/1>;

// std::optional<bool>: SampleProfile.cpp's UseProfiImplied gating branches
// on whether this was ever specified, distinct from its own resolved value.
// The bare "true" below is only the bare-flag ("-sample-profile-use-profi"
// with no "=value") value; the original cl::opt's actual default when the
// flag is absent entirely is false, so getSampleProfileUseProfi()'s
// .value_or() fallback (not this field) is what encodes that default.
defm TU_SampleProfileUseProfi : OptionalValueField<
    "sample-profile-use-profi", "bool", "true",
    "Use profi to infer block and edge counts.",
    /*hidden=*/1>;

// --- ScalarEvolutionExpander.cpp ---

defm TU_SCEVCheapExpansionBudget : ValueField<
    "scev-cheap-expansion-budget", "unsigned", "4",
    "When performing SCEV expansion only if it is cheap to do, this "
    "controls the budget that is considered cheap (default = 4)",
    /*hidden=*/1>;

// --- SimplifyCFG.cpp ---

defm TU_RequireAndPreserveDomTree : BoolField<
    "simplifycfg-require-and-preserve-domtree", "false",
    "Temporary development switch used to gradually uplift SimplifyCFG "
    "into preserving DomTree,",
    /*hidden=*/1>;

defm TU_PHINodeFoldingThreshold : ValueField<
    "phi-node-folding-threshold", "unsigned", "2",
    "Control the amount of phi node folding to perform (default = 2)",
    /*hidden=*/1>;

defm TU_TwoEntryPHINodeFoldingThreshold : ValueField<
    "two-entry-phi-node-folding-threshold", "unsigned", "4",
    "Control the maximal total instruction cost that we are willing "
    "to speculatively execute to fold a 2-entry PHI node into a "
    "select (default = 4)",
    /*hidden=*/1>;

defm TU_HoistCommon : BoolField<
    "simplifycfg-hoist-common", "true",
    "Hoist common instructions up to the parent block",
    /*hidden=*/1>;

defm TU_HoistLoadsWithCondFaulting : BoolField<
    "simplifycfg-hoist-loads-with-cond-faulting", "true",
    "Hoist loads if the target supports conditional faulting",
    /*hidden=*/1>;

defm TU_HoistStoresWithCondFaulting : BoolField<
    "simplifycfg-hoist-stores-with-cond-faulting", "true",
    "Hoist stores if the target supports conditional faulting",
    /*hidden=*/1>;

defm TU_HoistLoadsStoresWithCondFaultingThreshold : ValueField<
    "hoist-loads-stores-with-cond-faulting-threshold", "unsigned", "6",
    "Control the maximal conditional load/store that we are willing "
    "to speculatively execute to eliminate conditional branch "
    "(default = 6)",
    /*hidden=*/1>;

defm TU_HoistCommonSkipLimit : ValueField<
    "simplifycfg-hoist-common-skip-limit", "unsigned", "20",
    "Allow reordering across at most this many "
    "instructions when hoisting",
    /*hidden=*/1>;

defm TU_SinkCommon : BoolField<
    "simplifycfg-sink-common", "true",
    "Sink common instructions down to the end block",
    /*hidden=*/1>;

defm TU_HoistCondStores : BoolField<
    "simplifycfg-hoist-cond-stores", "true",
    "Hoist conditional stores if an unconditional store precedes",
    /*hidden=*/1>;

defm TU_MergeCondStores : BoolField<
    "simplifycfg-merge-cond-stores", "true",
    "Hoist conditional stores even if an unconditional store does not "
    "precede - hoist multiple conditional stores into a single "
    "predicated store",
    /*hidden=*/1>;

defm TU_MergeCondStoresAggressively : BoolField<
    "simplifycfg-merge-cond-stores-aggressively", "false",
    "When merging conditional stores, do so even if the resultant "
    "basic blocks are unlikely to be if-converted as a result",
    /*hidden=*/1>;

defm TU_SpeculateOneExpensiveInst : BoolField<
    "speculate-one-expensive-inst", "true",
    "Allow exactly one expensive instruction to be speculatively "
    "executed",
    /*hidden=*/1>;

defm TU_MaxSpeculationDepth : ValueField<
    "max-speculation-depth", "unsigned", "10",
    "Limit maximum recursion depth when calculating costs of "
    "speculatively executed instructions",
    /*hidden=*/1>;

defm TU_MaxSmallBlockSize : ValueField<
    "simplifycfg-max-small-block-size", "int", "10",
    "Max size of a block which is still considered "
    "small enough to thread through",
    /*hidden=*/1>;

defm TU_BranchFoldThreshold : ValueField<
    "simplifycfg-branch-fold-threshold", "unsigned", "2",
    "Maximum cost of combining conditions when "
    "folding branches",
    /*hidden=*/1>;

defm TU_BranchFoldToCommonDestVectorMultiplier : ValueField<
    "simplifycfg-branch-fold-common-dest-vector-multiplier", "unsigned", "2",
    "Multiplier to apply to threshold when determining whether or not "
    "to fold branch to common destination when vector operations are "
    "present",
    /*hidden=*/1>;

defm TU_EnableMergeCompatibleInvokes : BoolField<
    "simplifycfg-merge-compatible-invokes", "true",
    "Allow SimplifyCFG to merge invokes together when appropriate",
    /*hidden=*/1>;

defm TU_MaxSwitchCasesPerResult : ValueField<
    "max-switch-cases-per-result", "unsigned", "16",
    "Limit cases to analyze when converting a switch to select",
    /*hidden=*/1>;

defm TU_MaxJumpThreadingLiveBlocks : ValueField<
    "max-jump-threading-live-blocks", "unsigned", "24",
    "Limit number of blocks a define in a threaded block is allowed "
    "to be live in",
    /*hidden=*/1>;

// --- SimplifyLibCalls.cpp ---

// std::optional<bool>: "priority if supplied" over a separately-computed
// UnsafeFPShrink local, mirroring TU_UnrollRuntimeEpilog's reasoning.
defm TU_EnableUnsafeFPShrink : OptionalValueField<
    "enable-double-float-shrink", "bool", "true",
    "Enable unsafe double to float "
    "shrinking for math lib calls",
    /*hidden=*/1>;

defm TU_OptimizeHotColdNew : BoolField<
    "optimize-hot-cold-new", "false",
    "Enable hot/cold operator new library calls",
    /*hidden=*/1>;

// Self-generated enum (no pre-existing llvm::OptimizeExistingHotColdNewKind
// before this migration): hand-declared in UtilsOptions.h and marked
// External here purely to route around the OptionEnum emitter's 1:1
// EnumMember-to-enumerator mapping, which would otherwise emit "Always"
// twice (once for the "always" spelling, once for the bare/"-eq-empty"
// spelling below) and fail to compile as a duplicate enumerator. This
// mirrors ChangePrinterEnum's External=1 shape in IROptions.td, except the
// enum there already existed elsewhere for unrelated reasons -- here the
// hand-declaration exists solely to satisfy this constraint.
def OptimizeExistingHotColdNewKindEnum :
    OptionEnum<"llvm::OptimizeExistingHotColdNewKind", [
        EnumMember<"None", "none">,
        EnumMember<"Cold", "cold">,
        EnumMember<"Always", "always">,
        // Empty spelling: covers "-optimize-existing-hot-cold-new=" (equals
        // with nothing after), which clv2's ValueOptional treated
        // identically to the bare flag below (both produce an empty Val).
        EnumMember<"Always", "">]> {
  let External = 1;
  let Ident = "OptimizeExistingHotColdNewKind";
}

def TU_OptimizeExistingHotColdNew : Joined<
    ["-", "--"], "optimize-existing-hot-cold-new=">,
    HelpText<"Enable optimization of existing hot/cold operator new library "
             "calls">,
    Flags<[HelpHidden]>,
    Field<"llvm::OptimizeExistingHotColdNewKind",
          "TU_OptimizeExistingHotColdNew",
          "llvm::OptimizeExistingHotColdNewKind::None">,
    FieldEnum<OptimizeExistingHotColdNewKindEnum>;

// Bare "-optimize-existing-hot-cold-new" (no "=value") means "always",
// matching the old clv2 ValueOptional spelling (mirrors IR_PrintChanged/
// IR_PrintChangedBare in IROptions.td).
def TU_OptimizeExistingHotColdNewBare : Flag<
    ["-", "--"], "optimize-existing-hot-cold-new">,
    Flags<[HelpHidden]>,
    Assign<"TU_OptimizeExistingHotColdNew",
           "llvm::OptimizeExistingHotColdNewKind::Always">;

defm TU_UnrollUniformWeights : BoolField<
    "unroll-uniform-weights", "false",
    "If new branch weights must be found, work harder to keep them uniform.",
    /*hidden=*/1>;

defm TU_MinExistingHotColdNewHint : BoolField<
    "min-existing-hot-cold-new-hint", "false",
    "Take the minimum of compiler hint and existing hint when optimizing "
    "existing hot/cold operator new library calls",
    /*hidden=*/1>;

defm TU_OptimizeNoBuiltinHotColdNew : BoolField<
    "optimize-nobuiltin-hot-cold-new-new", "false",
    "Enable transformation of nobuiltin operator new library calls",
    /*hidden=*/1>;

// The old clv2 Validate callbacks for the 4 hint-value options below
// (rejecting values > 255) have no OptTable schema equivalent and are
// dropped here, matching every prior clv2 -> OptTable migration that had
// per-option Validate callbacks (see e.g. IROptions.td): an invalid value
// is still caught downstream, just later -- SimplifyLibCalls.cpp truncates
// each of these into a uint8_t hint byte, so an out-of-range value simply
// wraps rather than being rejected at parse time.

// The 4 hint values below must fit in the __hot_cold_t byte parameter
// they feed (see SimplifyLibCalls.cpp); clv2's per-option `Validate`
// callback used to enforce this at parse time, so maxValue=255 restores
// that same "value must be in the range [0, 255]!" parse-time rejection
// (see MaxValue<> in OptParser.td).
defm TU_ColdNewHintValue : ValueField<
    "cold-new-hint-value", "unsigned", "1",
    "Value to pass to hot/cold operator new for cold allocation",
    /*hidden=*/1, /*metaVar=*/"", /*maxValue=*/255>;

defm TU_NotColdNewHintValue : ValueField<
    "notcold-new-hint-value", "unsigned", "128",
    "Value to pass to hot/cold operator new for "
    "notcold (warm) allocation",
    /*hidden=*/1, /*metaVar=*/"", /*maxValue=*/255>;

defm TU_HotNewHintValue : ValueField<
    "hot-new-hint-value", "unsigned", "254",
    "Value to pass to hot/cold operator new for hot allocation",
    /*hidden=*/1, /*metaVar=*/"", /*maxValue=*/255>;

defm TU_AmbiguousNewHintValue : ValueField<
    "ambiguous-new-hint-value", "unsigned", "222",
    "Value to pass to hot/cold operator new for ambiguous allocation",
    /*hidden=*/1, /*metaVar=*/"", /*maxValue=*/255>;

// --- SizeOpts.cpp ---

defm TU_EnablePGSO : BoolField<
    "pgso", "true",
    "Enable the profile guided size optimizations. ",
    /*hidden=*/1>;

defm TU_PGSOLargeWorkingSetSizeOnly : BoolField<
    "pgso-lwss-only", "true",
    "Apply the profile guided size optimizations only "
    "if the working set size is large (except for cold code.)",
    /*hidden=*/1>;

defm TU_PGSOColdCodeOnly : BoolField<
    "pgso-cold-code-only", "false",
    "Apply the profile guided size optimizations only "
    "to cold code.",
    /*hidden=*/1>;

defm TU_PGSOColdCodeOnlyForInstrPGO : BoolField<
    "pgso-cold-code-only-for-instr-pgo", "false",
    "Apply the profile guided size optimizations only "
    "to cold code under instrumentation PGO.",
    /*hidden=*/1>;

defm TU_PGSOColdCodeOnlyForSamplePGO : BoolField<
    "pgso-cold-code-only-for-sample-pgo", "false",
    "Apply the profile guided size optimizations only "
    "to cold code under sample PGO.",
    /*hidden=*/1>;

defm TU_PGSOColdCodeOnlyForPartialSamplePGO : BoolField<
    "pgso-cold-code-only-for-partial-sample-pgo", "false",
    "Apply the profile guided size optimizations only "
    "to cold code under partial-profile sample PGO.",
    /*hidden=*/1>;

defm TU_ForcePGSO : BoolField<
    "force-pgso", "false",
    "Force the (profiled-guided) size optimizations. ",
    /*hidden=*/1>;

defm TU_PgsoCutoffInstrProf : ValueField<
    "pgso-cutoff-instr-prof", "int", "950000",
    "The profile guided size optimization profile summary cutoff "
    "for instrumentation profile.",
    /*hidden=*/1>;

defm TU_PgsoCutoffSampleProf : ValueField<
    "pgso-cutoff-sample-prof", "int", "990000",
    "The profile guided size optimization profile summary cutoff "
    "for sample profile.",
    /*hidden=*/1>;

// --- SymbolRewriter.cpp ---

defm TU_RewriteMapFiles : ListField<
    "rewrite-map-file", "std::string", "Symbol Rewrite Map",
    /*commaJoined=*/0, /*appendEquals=*/1, /*hidden=*/1, /*metaVar=*/"filename">;

// --- UnifyLoopExits.cpp ---

defm TU_MaxBooleansInControlFlowHub : ValueField<
    "max-booleans-in-control-flow-hub", "unsigned", "32",
    "Set the maximum number of outgoing blocks for using a boolean "
    "value to record the exiting block in the ControlFlowHub.",
    /*hidden=*/1>;
