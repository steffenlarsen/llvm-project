//===- AnalysisOptions.td - Option defs for Analysis -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def AnalysisOptions : OptionsStruct<"llvm"> {}

// =========================================================================
// External enum types
// =========================================================================

// --- BlockFrequencyInfo.h: GVDAGType ---

def GVDAGTypeEnum : OptionEnum<"llvm::GVDAGType", [
    EnumMember<"GVDT_None", "none">,
    EnumMember<"GVDT_Fraction", "fraction">,
    EnumMember<"GVDT_Integer", "integer">,
    EnumMember<"GVDT_Count", "count">]> {
  let External = 1;
  let Ident = "GVDAGType";
}

// --- BlockFrequencyInfo.h: PGOViewCountsType ---

def PGOViewCountsTypeEnum : OptionEnum<"llvm::PGOViewCountsType", [
    EnumMember<"PGOVCT_None", "none">,
    EnumMember<"PGOVCT_Graph", "graph">,
    EnumMember<"PGOVCT_Text", "text">]> {
  let External = 1;
  let Ident = "PGOViewCountsType";
}

// --- CtxProfPrintMode.h: CtxProfPrintMode (aliased as
//     CtxProfAnalysisPrinterPass::PrintMode) ---

def CtxProfPrintModeEnum : OptionEnum<"llvm::CtxProfPrintMode", [
    EnumMember<"Everything", "everything">,
    EnumMember<"YAML", "yaml">]> {
  let External = 1;
  let Ident = "CtxProfPrintMode";
}

// --- ImportedFunctionsInliningStatistics.h: InlinerFunctionImportStatsOpts
//     ("No" has no CLI spelling of its own -- same asymmetry the old clv2
//     schema had: only Basic/Verbose are selectable, No is just the
//     value-initialized default when the option is never specified.) ---

def InlinerFunctionImportStatsEnum :
    OptionEnum<"llvm::InlinerFunctionImportStatsOpts", [
    EnumMember<"Basic", "basic">,
    EnumMember<"Verbose", "verbose">]> {
  let External = 1;
  let Ident = "InlinerFunctionImportStatsOpts";
}

// --- IR2Vec.h: IR2VecKind ---

def IR2VecKindEnum : OptionEnum<"llvm::IR2VecKind", [
    EnumMember<"Symbolic", "symbolic">,
    EnumMember<"FlowAware", "flow-aware">]> {
  let External = 1;
  let Ident = "IR2VecKind";
}

// --- ForceSummaryHotnessType.h: ForceSummaryHotnessType ---

def ForceSummaryHotnessEnum : OptionEnum<"llvm::ForceSummaryHotnessType", [
    EnumMember<"FSHT_None", "none">,
    EnumMember<"FSHT_AllNonCritical", "all-non-critical">,
    EnumMember<"FSHT_All", "all">]> {
  let External = 1;
  let Ident = "ForceSummaryHotnessType";
}

// =========================================================================
// Self-contained enum types (no header exposes a real C++ enum for these --
// each was previously declared file-private inside its one consuming .cpp)
// =========================================================================

// --- InlineOrder.cpp: InlinePriorityMode ---

def InlinePriorityModeEnum : OptionEnum<"InlinePriorityMode", [
    EnumMember<"Size", "size">,
    EnumMember<"Cost", "cost">,
    EnumMember<"CostBenefit", "cost-benefit">,
    EnumMember<"ML", "ml">]>;

// --- MLInlineAdvisor.cpp: SkipMLPolicyCriteria ---

def SkipMLPolicyCriteriaEnum : OptionEnum<"SkipMLPolicyCriteria", [
    EnumMember<"Never", "never">,
    EnumMember<"IfCallerIsNotCold", "if-caller-not-cold">]>;

// =========================================================================
// Options
// =========================================================================

// ===== AliasAnalysis.cpp =====

defm AN_DisableBasicAA : BoolField<
    "disable-basic-aa", "false",
    "",
    /*hidden=*/1>;

defm AN_EnableAATrace : BoolField<
    "aa-trace", "false",
    "",
    /*hidden=*/1>;

// ===== AliasSetTracker.cpp =====

defm AN_SaturationThreshold : ValueField<
    "alias-set-saturation-threshold", "unsigned", "250",
    "The maximum total number of memory locations alias "
    "sets may contain before degradation",
    /*hidden=*/1>;

// ===== AssumptionCache.cpp =====

defm AN_VerifyAssumptionCache : BoolField<
    "verify-assumption-cache", "false",
    "Enable verification of assumption cache",
    /*hidden=*/1>;

// ===== BasicAliasAnalysis.cpp =====

defm AN_EnableRecPhiAnalysis : BoolField<
    "basic-aa-recphi", "true",
    "",
    /*hidden=*/1>;

defm AN_EnableSeparateStorageAnalysis : BoolField<
    "basic-aa-separate-storage", "true",
    "",
    /*hidden=*/1>;

// ===== BlockFrequencyInfo.cpp =====

defm AN_ViewBlockFreqPropagationDAG : EnumField<
    "view-block-freq-propagation-dags", GVDAGTypeEnum, "llvm::GVDT_None",
    "Pop up a window to show a dag displaying how block "
    "frequencies propagation through the CFG.",
    /*hidden=*/1>;

defm AN_ViewBlockFreqFuncName : ValueField<
    "view-bfi-func-name", "std::string", "\"\"",
    "The option to specify "
    "the name of the function "
    "whose CFG will be displayed.",
    /*hidden=*/1>;

defm AN_ViewHotFreqPercent : ValueField<
    "view-hot-freq-percent", "unsigned", "10",
    "An integer in percent used to specify "
    "the hot blocks/edges to be displayed "
    "in red: a block or edge whose frequency "
    "is no less than the max frequency of the "
    "function multiplied by this percent.",
    /*hidden=*/1>;

defm AN_PGOViewCounts : EnumField<
    "pgo-view-counts", PGOViewCountsTypeEnum, "llvm::PGOVCT_None",
    "A boolean option to show CFG dag or text with "
    "block profile counts and branch probabilities "
    "right after PGO profile annotation step. The "
    "profile counts are computed using branch "
    "probabilities from the runtime profile data and "
    "block frequency propagation algorithm. To view "
    "the raw counts from the profile, use option "
    "-pgo-view-raw-counts instead. To limit graph "
    "display to only one function, use filtering option "
    "-view-bfi-func-name.",
    /*hidden=*/1>;

defm AN_PrintBFI : BoolField<
    "print-bfi", "false",
    "Print the block frequency info.",
    /*hidden=*/1>;

defm AN_PrintBFIFuncName : ValueField<
    "print-bfi-func-name", "std::string", "\"\"",
    "The option to specify the name of the function "
    "whose block frequency info is printed.",
    /*hidden=*/1>;

// ===== BlockFrequencyInfoImpl.cpp =====

defm AN_CheckBFIUnknownBlockQueries : BoolField<
    "check-bfi-unknown-block-queries", "false",
    "Check if block frequency is queried for an unknown block "
    "for debugging missed BFI updates",
    /*hidden=*/1>;

defm AN_UseIterativeBFIInference : BoolField<
    "use-iterative-bfi-inference", "false",
    "Apply an iterative post-processing to infer correct BFI counts",
    /*hidden=*/1>;

defm AN_IterativeBFIMaxIterationsPerBlock : ValueField<
    "iterative-bfi-max-iterations-per-block", "unsigned", "1000",
    "Iterative inference: maximum number of update iterations "
    "per block",
    /*hidden=*/1>;

defm AN_IterativeBFIPrecision : ValueField<
    "iterative-bfi-precision", "double", "1e-12",
    "Iterative inference: delta convergence precision; smaller values "
    "typically lead to better results at the cost of worsen runtime",
    /*hidden=*/1>;

// ===== BranchProbabilityInfo.cpp =====

defm AN_PrintBranchProb : BoolField<
    "print-bpi", "false",
    "Print the branch probability info.",
    /*hidden=*/1>;

defm AN_PrintBranchProbFuncName : ValueField<
    "print-bpi-func-name", "std::string", "\"\"",
    "The option to specify the name of the function "
    "whose branch probability info is printed.",
    /*hidden=*/1>;

// ===== CallGraphSCCPass.cpp =====

defm AN_MaxDevirtIterations : ValueField<
    "max-devirt-iterations", "unsigned", "4",
    "",
    /*hidden=*/1>;

// ===== CallPrinter.cpp =====

defm AN_CallGraphHeatColors : BoolField<
    "callgraph-heat-colors", "false",
    "Show heat colors in call-graph",
    /*hidden=*/1>;

defm AN_CallGraphShowWeights : BoolField<
    "callgraph-show-weights", "false",
    "Show edges labeled with weights",
    /*hidden=*/1>;

defm AN_CallMultiGraph : BoolField<
    "callgraph-multigraph", "false",
    "Show call-multigraph (do not remove parallel edges)",
    /*hidden=*/1>;

defm AN_CallGraphDotFilenamePrefix : ValueField<
    "callgraph-dot-filename-prefix", "std::string", "\"\"",
    "The prefix used for the CallGraph dot file names.",
    /*hidden=*/1>;

// ===== CaptureTracking.cpp =====

defm AN_DefaultMaxUsesToExplore : ValueField<
    "capture-tracking-max-uses-to-explore", "unsigned", "100",
    "Maximal number of uses to explore.",
    /*hidden=*/1>;

// ===== CFG.cpp =====

defm AN_DefaultMaxBBsToExplore : ValueField<
    "dom-tree-reachability-max-bbs-to-explore", "unsigned", "32",
    "Max number of BBs to explore for reachability analysis",
    /*hidden=*/1>;

// ===== CFGPrinter.cpp =====

defm AN_CFGFuncName : ValueField<
    "cfg-func-name", "std::string", "\"\"",
    "The name of a function (or its substring)"
    " whose CFG is viewed/printed.",
    /*hidden=*/1>;

defm AN_CFGDotFilenamePrefix : ValueField<
    "cfg-dot-filename-prefix", "std::string", "\"\"",
    "The prefix used for the CFG dot file names.",
    /*hidden=*/1>;

defm AN_HideUnreachablePaths : BoolField<
    "cfg-hide-unreachable-paths", "false",
    "",
    /*hidden=*/0>;

defm AN_HideDeoptimizePaths : BoolField<
    "cfg-hide-deoptimize-paths", "false",
    "",
    /*hidden=*/0>;

defm AN_HideColdPaths : ValueField<
    "cfg-hide-cold-paths", "double", "0.0",
    "Hide blocks with relative frequency below the given value",
    /*hidden=*/0>;

defm AN_CfgShowHeatColors : BoolField<
    "cfg-heat-colors", "true",
    "Show heat colors in CFG",
    /*hidden=*/1>;

defm AN_CfgUseRawEdgeWeight : BoolField<
    "cfg-raw-weights", "false",
    "Use raw weights for labels. "
    "Use percentages as default.",
    /*hidden=*/1>;

defm AN_CfgShowEdgeWeight : BoolField<
    "cfg-weights", "false",
    "Show edges labeled with weights",
    /*hidden=*/1>;

// ===== CGSCCPassManager.cpp =====

defm AN_AbortOnMaxDevirtIterationsReached : BoolField<
    "abort-on-max-devirt-iterations-reached", "false",
    "Abort when the max iterations for devirtualization CGSCC repeat "
    "pass is reached",
    /*hidden=*/0>;

// ===== ConstantFolding.cpp =====

defm AN_DisableFPCallFolding : BoolField<
    "disable-fp-call-folding", "false",
    "Disable constant-folding of FP intrinsics and libcalls.",
    /*hidden=*/1>;

// ===== CtxProfAnalysis.cpp =====

defm AN_UseCtxProfile : ValueField<
    "use-ctx-profile", "std::string", "\"\"",
    "Use the specified contextual profile file",
    /*hidden=*/1>;

defm AN_CtxProfilePrinterLevel : EnumField<
    "ctx-profile-printer-level", CtxProfPrintModeEnum,
    "llvm::CtxProfPrintMode::YAML",
    "Verbosity level of the contextual profile printer pass.",
    /*hidden=*/1>;

defm AN_ForceIsInSpecializedModule : OptionalValueField<
    "ctx-profile-force-is-specialized", "bool", "true",
    "Treat the given module as-if it were containing the "
    "post-thinlink module containing the root",
    /*hidden=*/0>;

// ===== DDG.cpp =====

defm AN_SimplifyDDG : BoolField<
    "ddg-simplify", "true",
    "Use a simplified DDG by merging nodes that are in the same strongly "
    "connected component.",
    /*hidden=*/1>;

defm AN_CreatePiBlocks : BoolField<
    "ddg-pi-blocks", "true",
    "Create pi-block nodes.",
    /*hidden=*/1>;

// ===== DDGPrinter.cpp =====

defm AN_DotOnly : BoolField<
    "dot-ddg-only", "false",
    "simple ddg dot graph",
    /*hidden=*/1>;

defm AN_DDGDotFilenamePrefix : ValueField<
    "dot-ddg-filename-prefix", "std::string", "\"ddg\"",
    "The prefix used for the DDG dot file names.",
    /*hidden=*/1>;

// ===== Delinearization.cpp =====

defm AN_UseFixedSizeArrayHeuristic : BoolField<
    "delinearize-use-fixed-size-array-heuristic", "true",
    "When printing analysis, use the heuristic for fixed-size arrays "
    "if the default delinearizetion fails.",
    /*hidden=*/1>;

// ===== DependenceAnalysis.cpp =====

defm AN_Delinearize : BoolField<
    "da-delinearize", "true",
    "Try to delinearize array references.",
    /*hidden=*/1>;

defm AN_DisableDelinearizationChecks : BoolField<
    "da-disable-delinearization-checks", "false",
    "Disable checks that try to statically verify validity of "
    "delinearized subscripts. Enabling this option may result in incorrect "
    "dependence vectors for languages that allow the subscript of one "
    "dimension to underflow or overflow into another dimension.",
    /*hidden=*/1>;

defm AN_MIVMaxLevelThreshold : ValueField<
    "da-miv-max-level-threshold", "unsigned", "7",
    "Maximum depth allowed for the recursive algorithm used to "
    "explore MIV direction vectors.",
    /*hidden=*/1>;

// ===== DevelopmentModeInlineAdvisor.cpp =====

defm AN_TrainingLog : ValueField<
    "training-log", "std::string", "\"\"",
    "Path where the development - mode inlining log is saved.",
    /*hidden=*/1>;

defm AN_TFModelUnderTrainingPath : ValueField<
    "ml-inliner-model-under-training", "std::string", "\"\"",
    "Path to SavedModel from the previous training iteration.",
    /*hidden=*/1>;

defm AN_TFOutputSpecOverride : ValueField<
    "ml-inliner-output-spec-override", "std::string", "\"\"",
    "Override the path to the output spec json file. See "
    "-ml-inliner-model-under-training documentation for the "
    "specification of that file.",
    /*hidden=*/1>;

defm AN_TFFeedPrefix : ValueField<
    "ml-inliner-trained-model-feed-prefix", "std::string", "\"action_\"",
    "Prefix for feature names.",
    /*hidden=*/1>;

// ===== FunctionPropertiesAnalysis.cpp =====

defm AN_EnableDetailedFunctionProperties : BoolField<
    "enable-detailed-function-properties", "false",
    "Whether or not to compute detailed function properties.",
    /*hidden=*/1>;

defm AN_BigBasicBlockInstructionThreshold : ValueField<
    "big-basic-block-instruction-threshold", "unsigned", "500",
    "The minimum number of instructions a basic block should contain "
    "before being considered big.",
    /*hidden=*/1>;

defm AN_MediumBasicBlockInstructionThreshold : ValueField<
    "medium-basic-block-instruction-threshold", "unsigned", "15",
    "The minimum number of instructions a basic block should contain "
    "before being considered medium-sized.",
    /*hidden=*/1>;

defm AN_CallWithManyArgumentsThreshold : ValueField<
    "call-with-many-arguments-threshold", "unsigned", "4",
    "The minimum number of arguments a function call must have before "
    "it is considered having many arguments.",
    /*hidden=*/1>;

// ===== GlobalsModRef.cpp =====

defm AN_EnableUnsafeGlobalsModRefAliasResults : BoolField<
    "enable-unsafe-globalsmodref-alias-results", "false",
    "",
    /*hidden=*/1>;

// ===== ImportedFunctionsInliningStatistics.cpp =====

defm AN_InlinerFunctionImportStats : EnumField<
    "inliner-function-import-stats", InlinerFunctionImportStatsEnum,
    "llvm::InlinerFunctionImportStatsOpts::No",
    "Enable inliner stats for imported functions",
    /*hidden=*/1>;

// ===== IndirectCallPromotionAnalysis.cpp =====

defm AN_ICPRemainingPercentThreshold : ValueField<
    "icp-remaining-percent-threshold", "unsigned", "30",
    "The percentage threshold against remaining unpromoted indirect "
    "call count for the promotion",
    /*hidden=*/1>;

defm AN_ICPTotalPercentThreshold : ValueField<
    "icp-total-percent-threshold", "uint64_t", "uint64_t{5}",
    "The percentage threshold against total "
    "count for the promotion",
    /*hidden=*/1>;

defm AN_ICPMinimumCountThreshold : ValueField<
    "icp-minimum-count-threshold", "unsigned", "0",
    "Minimum absolute count for promotion candidate",
    /*hidden=*/1>;

defm AN_MaxNumPromotions : ValueField<
    "icp-max-prom", "unsigned", "3",
    "Max number of promotions for a single indirect "
    "call callsite",
    /*hidden=*/1>;

defm AN_MaxNumVTableAnnotations : ValueField<
    "icp-max-num-vtables", "unsigned", "6",
    "Max number of vtables annotated for a vtable load instruction.",
    /*hidden=*/1>;

// ===== InlineAdvisor.cpp =====

defm AN_InlineRemarkAttribute : BoolField<
    "inline-remark-attribute", "false",
    "Enable adding inline-remark attribute to"
    " callsites processed by inliner but not inlined",
    /*hidden=*/1>;

defm AN_EnableInlineDeferral : BoolField<
    "inline-deferral", "false",
    "Enable deferred inlining",
    /*hidden=*/1>;

defm AN_InlineDeferralScale : ValueField<
    "inline-deferral-scale", "int", "2",
    "Scale to limit the cost of inline deferral",
    /*hidden=*/1>;

defm AN_AnnotateInlinePhase : BoolField<
    "annotate-inline-phase", "false",
    "If true, annotate inline advisor remarks "
    "with LTO and pass information.",
    /*hidden=*/1>;

defm AN_IR2VecVocabFile : ValueField<
    "ml-inliner-ir2vec-vocab-file", "std::string", "\"\"",
    "Vocab file for IR2Vec; Setting this enables "
    "configuring the model to use IR2Vec embeddings.",
    /*hidden=*/1>;

// ===== InlineCost.cpp =====

defm AN_DefaultThreshold : ValueField<
    "inlinedefault-threshold", "int", "225",
    "Default amount of inlining to perform",
    /*hidden=*/1>;

defm AN_IgnoreTTIInlineCompatible : BoolField<
    "ignore-tti-inline-compatible", "false",
    "Ignore TTI attributes compatibility check between callee/caller "
    "during inline cost calculation",
    /*hidden=*/1>;

defm AN_PrintInstructionComments : OptionalValueField<
    "print-instruction-comments", "bool", "true",
    "Prints comments for instruction based on inline cost analysis",
    /*hidden=*/1>;

defm AN_InlineThreshold : OptionalField<
    "inline-threshold", "int",
    "Control the amount of inlining to perform (default = 225)",
    /*hidden=*/1>;

defm AN_HintThreshold : ValueField<
    "inlinehint-threshold", "int", "325",
    "Threshold for inlining functions with inline hint",
    /*hidden=*/1>;

defm AN_ColdCallSiteThreshold : ValueField<
    "inline-cold-callsite-threshold", "int", "45",
    "Threshold for inlining cold callsites",
    /*hidden=*/1>;

defm AN_InlineEnableCostBenefitAnalysis : OptionalValueField<
    "inline-enable-cost-benefit-analysis", "bool", "true",
    "Enable the cost-benefit analysis for the inliner",
    /*hidden=*/1>;

defm AN_InlineSavingsMultiplier : OptionalField<
    "inline-savings-multiplier", "int",
    "Multiplier to multiply cycle savings by during inlining",
    /*hidden=*/1>;

defm AN_InlineSavingsProfitableMultiplier : OptionalField<
    "inline-savings-profitable-multiplier", "int",
    "A multiplier on top of cycle savings to decide whether the "
    "savings won't justify the cost",
    /*hidden=*/1>;

defm AN_InlineSizeAllowance : ValueField<
    "inline-size-allowance", "int", "100",
    "The maximum size of a callee that get's "
    "inlined without cost consideration",
    /*hidden=*/1>;

defm AN_ColdThreshold : OptionalField<
    "inlinecold-threshold", "int",
    "Threshold for inlining functions with cold attribute",
    /*hidden=*/1>;

defm AN_HotCallSiteThreshold : ValueField<
    "hot-callsite-threshold", "int", "3000",
    "Threshold for hot callsites ",
    /*hidden=*/1>;

defm AN_LocallyHotCallSiteThreshold : OptionalField<
    "locally-hot-callsite-threshold", "int",
    "Threshold for locally hot callsites ",
    /*hidden=*/1>;

defm AN_ColdCallSiteRelFreq : ValueField<
    "cold-callsite-rel-freq", "int", "2",
    "Maximum block frequency, expressed as a percentage of caller's "
    "entry frequency, for a callsite to be considered cold.",
    /*hidden=*/1>;

defm AN_HotCallSiteRelFreq : ValueField<
    "hot-callsite-rel-freq", "uint64_t", "uint64_t{60}",
    "Minimum block frequency, expressed as a multiple of caller's "
    "entry frequency, for a callsite to be hot in the absence of "
    "profile information.",
    /*hidden=*/1>;

defm AN_InstrCost : ValueField<
    "inline-instr-cost", "int", "5",
    "Cost of a single instruction when inlining",
    /*hidden=*/1>;

defm AN_InlineAsmInstrCost : ValueField<
    "inline-asm-instr-cost", "int", "0",
    "Cost of a single inline asm instruction when inlining",
    /*hidden=*/1>;

defm AN_MemAccessCost : ValueField<
    "inline-memaccess-cost", "int", "0",
    "Cost of load/store instruction when inlining",
    /*hidden=*/1>;

defm AN_CallPenalty : ValueField<
    "inline-call-penalty", "int", "25",
    "Call penalty that is applied per callsite when inlining",
    /*hidden=*/1>;

defm AN_StackSizeThreshold : OptionalField<
    "inline-max-stacksize", "size_t",
    "Do not inline functions with a stack size "
    "that exceeds the specified limit",
    /*hidden=*/1>;

defm AN_RecurStackSizeThreshold : ValueField<
    "recursive-inline-max-stacksize", "size_t", "size_t{1024}",
    "Do not inline recursive functions with a stack "
    "size that exceeds the specified limit",
    /*hidden=*/1>;

defm AN_OptComputeFullInlineCost : BoolField<
    "inline-cost-full", "false",
    "Compute the full inline cost of a call site even when the cost "
    "exceeds the threshold.",
    /*hidden=*/1>;

defm AN_InlineCallerSupersetNoBuiltin : BoolField<
    "inline-caller-superset-nobuiltin", "true",
    "Allow inlining when caller has a superset of callee's nobuiltin "
    "attributes.",
    /*hidden=*/1>;

defm AN_DisableGEPConstOperand : BoolField<
    "disable-gep-const-evaluation", "false",
    "Disables evaluation of GetElementPtr with constant operands",
    /*hidden=*/1>;

defm AN_InlineAllViableCalls : BoolField<
    "inline-all-viable-calls", "false",
    "Inline all viable calls, even if they exceed the inlining "
    "threshold",
    /*hidden=*/1>;

// ===== InlineOrder.cpp =====

defm AN_UseInlinePriority : EnumField<
    "inline-priority-mode", InlinePriorityModeEnum, "InlinePriorityMode::Size",
    "Choose the priority mode to use in module inline",
    /*hidden=*/1>;

defm AN_ModuleInlinerTopPriorityThreshold : ValueField<
    "module-inliner-top-priority-threshold", "int", "0",
    "The cost threshold for call sites that get inlined without the "
    "cost-benefit analysis",
    /*hidden=*/1>;

// ===== InstructionPrecedenceTracking.cpp =====

defm AN_ExpensiveAsserts : BoolField<
    "ipt-expensive-asserts", "false",
    "Perform expensive assert validation on every query to Instruction"
    " Precedence Tracking",
    /*hidden=*/1>;

// ===== InteractiveModelRunner.cpp =====

defm AN_DebugReply : BoolField<
    "interactive-model-runner-echo-reply", "false",
    "The InteractiveModelRunner will echo back to stderr "
    "the data received from the host (for debugging purposes).",
    /*hidden=*/1>;

// ===== IR2Vec.cpp =====

defm AN_VocabFile : ValueField<
    "ir2vec-vocab-path", "std::string", "\"\"",
    "Path to the vocabulary file for IR2Vec",
    /*hidden=*/0>;

defm AN_OpcWeight : ValueField<
    "ir2vec-opc-weight", "float", "1.0f",
    "Weight for opcode embeddings",
    /*hidden=*/0>;

defm AN_TypeWeight : ValueField<
    "ir2vec-type-weight", "float", "0.5f",
    "Weight for type embeddings",
    /*hidden=*/0>;

defm AN_ArgWeight : ValueField<
    "ir2vec-arg-weight", "float", "0.2f",
    "Weight for argument embeddings",
    /*hidden=*/0>;

defm AN_IR2VecEmbeddingKind : EnumField<
    "ir2vec-kind", IR2VecKindEnum, "llvm::IR2VecKind::Symbolic",
    "IR2Vec embedding kind",
    /*hidden=*/0>;

// ===== AssumptionCache.cpp =====

defm AN_MaxAssumesPerValue : ValueField<
    "max-assumes-per-value", "unsigned", "1024",
    "Maximum number of assumptions to cache for a single value",
    /*hidden=*/1>;

// ===== LastRunTrackingAnalysis.cpp =====

defm AN_DisableLastRunTracking : BoolField<
    "disable-last-run-tracking", "false",
    "Disable last run tracking",
    /*hidden=*/1>;

// ===== LazyValueInfo.cpp =====

defm AN_PerPredRanges : BoolField<
    "lvi-per-pred-ranges", "false",
    "Enable tracking of ranges for a value in a block for"
    " each predecessor",
    /*hidden=*/1>;

// ===== Loads.cpp =====

defm AN_DefMaxInstsToScan : ValueField<
    "available-load-scan-limit", "unsigned", "6",
    "Use this to specify the default maximum number of instructions "
    "to scan backward from a given instruction, when searching for "
    "available loaded value",
    /*hidden=*/1>;

// ===== LoopAccessAnalysis.cpp =====

// AN_VectorizationFactor's only consumer (VectorizerParams::
// getVectorizationFactor in LoopAccessAnalysis.cpp) falls back to
// ElementCount::getFixed(0) when unspecified, which is bit-for-bit the same
// as ElementCount{}'s default constructor (Quantity = 0, Scalable = false)
// -- so, unlike AN_VectorizationInterleave just below, no WasSpecified
// tracking is actually observable and a plain baked default is safe.
defm AN_VectorizationFactor : ValueField<
    "force-vector-width", "ElementCount", "ElementCount::getFixed(0)",
    "Sets the SIMD width. Zero is autoselect.",
    /*hidden=*/1>;

defm AN_VectorizationInterleave : OptionalField<
    "force-vector-interleave", "unsigned",
    "Sets the vectorization interleave count. "
    "Zero is autoselect.",
    /*hidden=*/1>;

defm AN_RuntimeMemoryCheckThreshold : ValueField<
    "runtime-memory-check-threshold", "unsigned", "8",
    "When performing memory disambiguation checks at runtime do not "
    "generate more than this number of comparisons (default = 8).",
    /*hidden=*/1>;

defm AN_MemoryCheckMergeThreshold : ValueField<
    "memory-check-merge-threshold", "unsigned", "100",
    "Maximum number of comparisons done when trying to merge "
    "runtime memory checks. (default = 100)",
    /*hidden=*/1>;

defm AN_MaxDependences : ValueField<
    "max-dependences", "unsigned", "100",
    "Maximum number of dependences collected by "
    "loop-access analysis (default = 100)",
    /*hidden=*/1>;

defm AN_EnableMemAccessVersioning : BoolField<
    "enable-mem-access-versioning", "true",
    "Enable symbolic stride memory access versioning",
    /*hidden=*/1>;

defm AN_EnableForwardingConflictDetection : BoolField<
    "store-to-load-forwarding-conflict-detection", "true",
    "Enable conflict detection in loop-access analysis",
    /*hidden=*/1>;

defm AN_MaxForkedSCEVDepth : ValueField<
    "max-forked-scev-depth", "unsigned", "5",
    "Maximum recursion depth when finding forked SCEVs (default = 5)",
    /*hidden=*/1>;

defm AN_SpeculateUnitStride : BoolField<
    "laa-speculate-unit-stride", "true",
    "Speculate that non-constant strides are unit in LAA",
    /*hidden=*/1>;

defm AN_HoistRuntimeChecks : BoolField<
    "hoist-runtime-checks", "true",
    "Hoist inner loop runtime memory checks to outer loop if possible",
    /*hidden=*/1>;

// ===== LoopCacheAnalysis.cpp =====

defm AN_DefaultTripCount : ValueField<
    "default-trip-count", "unsigned", "100",
    "Use this to specify the default trip count of a loop",
    /*hidden=*/1>;

defm AN_TemporalReuseThreshold : ValueField<
    "temporal-reuse-threshold", "unsigned", "2",
    "Use this to specify the max. distance between array elements "
    "accessed in a loop so that the elements are classified to have "
    "temporal reuse",
    /*hidden=*/1>;

// ===== LoopInfo.cpp =====

defm AN_VerifyLoopInfo : BoolField<
    "verify-loop-info", "false",
    "Verify loop info (time consuming)",
    /*hidden=*/1>;

// ===== MemoryBuiltins.cpp =====

defm AN_ObjectSizeOffsetVisitorMaxVisitInstructions : ValueField<
    "object-size-offset-visitor-max-visit-instructions", "unsigned", "100",
    "Maximum number of instructions for ObjectSizeOffsetVisitor to "
    "look at",
    /*hidden=*/0>;

// ===== MemoryDependenceAnalysis.cpp =====

defm AN_BlockScanLimit : ValueField<
    "memdep-block-scan-limit", "unsigned", "100",
    "The number of instructions to scan in a block in memory "
    "dependency analysis",
    /*hidden=*/1>;

defm AN_BlockNumberLimit : ValueField<
    "memdep-block-number-limit", "unsigned", "200",
    "The number of blocks to scan during memory "
    "dependence analysis",
    /*hidden=*/1>;

defm AN_CacheGlobalLimit : ValueField<
    "memdep-cache-global-limit", "unsigned", "10000",
    "The max number of entries allowed in a cache (default = 10000)",
    /*hidden=*/1>;

// ===== MemoryProfileInfo.cpp =====

defm AN_MemProfReportHintedSizes : BoolField<
    "memprof-report-hinted-sizes", "false",
    "Report total allocation sizes of hinted allocations",
    /*hidden=*/1>;

defm AN_MemProfKeepAllNotColdContexts : BoolField<
    "memprof-keep-all-not-cold-contexts", "false",
    "Keep all non-cold contexts (increases cloning overheads)",
    /*hidden=*/1>;

defm AN_MinClonedColdBytePercent : ValueField<
    "memprof-cloning-cold-threshold", "unsigned", "100",
    "Min percent of cold bytes to hint alloc cold during cloning",
    /*hidden=*/1>;

defm AN_MinCallsiteColdBytePercent : ValueField<
    "memprof-callsite-cold-threshold", "unsigned", "100",
    "Min percent of cold bytes at a callsite to discard non-cold "
    "contexts",
    /*hidden=*/1>;

defm AN_MinPercentMaxColdSize : ValueField<
    "memprof-min-percent-max-cold-size", "unsigned", "100",
    "Min percent of max cold bytes for critical cold context",
    /*hidden=*/1>;

defm AN_MemProfKeepContextSizeInfo : BoolField<
    "memprof-keep-context-size-info", "false",
    "Keep context size information in memprof metadata",
    /*hidden=*/1>;

defm AN_MemProfUseAmbiguousAttributes : BoolField<
    "memprof-ambiguous-attributes", "true",
    "Apply ambiguous memprof attribute to ambiguous allocations",
    /*hidden=*/1>;

// ===== MemorySSA.cpp =====

defm AN_DotCFGMSSA : ValueField<
    "dot-cfg-mssa", "std::string", "\"\"",
    "file name for generated dot file",
    /*hidden=*/0,
    /*metaVar=*/"file name for generated dot file">;

defm AN_MaxCheckLimit : ValueField<
    "memssa-check-limit", "unsigned", "100",
    "The maximum number of stores/phis MemorySSA"
    "will consider trying to walk past (default = 100)",
    /*hidden=*/1>;

defm AN_VerifyMemorySSA : BoolField<
    "verify-memoryssa", "false",
    "Enable verification of MemorySSA.",
    /*hidden=*/1>;

// ===== MLInlineAdvisor.cpp =====

defm AN_InteractiveChannelBaseName : ValueField<
    "inliner-interactive-channel-base", "std::string", "\"\"",
    "Base file path for the interactive mode. The incoming filename should "
    "have the name <inliner-interactive-channel-base>.in, while the "
    "outgoing name should be <inliner-interactive-channel-base>.out",
    /*hidden=*/1>;

defm AN_InteractiveIncludeDefault : BoolField<
    "inliner-interactive-include-default", "false",
    "",
    /*hidden=*/1>;

defm AN_SkipPolicy : EnumField<
    "ml-inliner-skip-policy", SkipMLPolicyCriteriaEnum,
    "SkipMLPolicyCriteria::Never", "",
    /*hidden=*/1>;

defm AN_ModelSelector : ValueField<
    "ml-inliner-model-selector", "std::string", "\"\"",
    "",
    /*hidden=*/1>;

defm AN_StopImmediatelyForTest : BoolField<
    "ml-inliner-stop-immediately", "false",
    "",
    /*hidden=*/1>;

defm AN_SizeIncreaseThreshold : ValueField<
    "ml-advisor-size-increase-threshold", "float", "2.0f",
    "Maximum factor by which expected native size may increase before "
    "blocking any further inlining.",
    /*hidden=*/1>;

defm AN_KeepFPICache : BoolField<
    "ml-advisor-keep-fpi-cache", "false",
    "For test - keep the ML Inline advisor's FunctionPropertiesInfo cache",
    /*hidden=*/1>;

// ===== ModuleSummaryAnalysis.cpp =====

defm AN_ForceSummaryEdgesCold : EnumField<
    "force-summary-edges-cold", ForceSummaryHotnessEnum, "llvm::FSHT_None",
    "Force all edges in the function summary to cold",
    /*hidden=*/1>;

defm AN_ModuleSummaryDotFile : ValueField<
    "module-summary-dot-file", "std::string", "\"\"",
    "File to emit dot graph of new summary into",
    /*hidden=*/1>;

defm AN_EnableMemProfIndirectCallSupport : BoolField<
    "enable-memprof-indirect-call-support", "true",
    "Enable MemProf support for summarizing and cloning indirect calls",
    /*hidden=*/1>;

defm AN_MaxSummaryIndirectEdges : ValueField<
    "module-summary-max-indirect-edges", "unsigned", "0",
    "Max number of summary edges added from "
    "indirect call profile metadata",
    /*hidden=*/1>;

// ===== ObjCARCAnalysisUtils.cpp =====

defm AN_EnableARCOptimizations : BoolField<
    "enable-objc-arc-opts", "true",
    "enable/disable all ARC Optimizations",
    /*hidden=*/1>;

// ===== PHITransAddr.cpp =====

defm AN_EnableAddPhiTranslation : BoolField<
    "gvn-add-phi-translation", "false",
    "Enable phi-translation of add instructions",
    /*hidden=*/1>;

// ===== ProfileSummaryInfo.cpp =====

defm AN_PartialProfile : BoolField<
    "partial-profile", "false",
    "Specify the current profile is used as a partial profile.",
    /*hidden=*/1>;

defm AN_ScalePartialSampleProfileWorkingSetSize : BoolField<
    "scale-partial-sample-profile-working-set-size", "true",
    "If true, scale the working set size of the partial sample profile "
    "by the partial profile ratio to reflect the size of the program "
    "being compiled.",
    /*hidden=*/1>;

defm AN_PartialSampleProfileWorkingSetSizeScaleFactor : ValueField<
    "partial-sample-profile-working-set-size-scale-factor", "double", "0.008",
    "The scale factor used to scale the working set size of the "
    "partial sample profile along with the partial profile ratio. "
    "This includes the factor of the profile counter per block "
    "and the factor to scale the working set size to use the same "
    "shared thresholds as PGO.",
    /*hidden=*/1>;

// ===== RegionInfo.cpp =====

defm AN_VerifyRegionInfo : BoolField<
    "verify-region-info", "false",
    "Verify region info (time consuming)",
    /*hidden=*/0>;

// ===== RegionPrinter.cpp =====

defm AN_OnlySimpleRegions : BoolField<
    "only-simple-regions", "false",
    "Show only simple regions in the graphviz viewer",
    /*hidden=*/1>;

// ===== ScalarEvolution.cpp =====

defm AN_MaxBruteForceIterations : ValueField<
    "scalar-evolution-max-iterations", "unsigned", "100",
    "Maximum number of iterations SCEV will "
    "symbolically execute a constant "
    "derived loop",
    /*hidden=*/1>;

defm AN_VerifySCEV : BoolField<
    "verify-scev", "false",
    "Verify ScalarEvolution's backedge taken counts (slow)",
    /*hidden=*/1>;

defm AN_VerifySCEVStrict : BoolField<
    "verify-scev-strict", "false",
    "Enable stricter verification with -verify-scev is passed",
    /*hidden=*/1>;

defm AN_VerifyIR : BoolField<
    "scev-verify-ir", "false",
    "Verify IR correctness when making sensitive SCEV queries (slow)",
    /*hidden=*/1>;

defm AN_MulOpsInlineThreshold : ValueField<
    "scev-mulops-inline-threshold", "unsigned", "32",
    "Threshold for inlining multiplication operands into a SCEV",
    /*hidden=*/1>;

defm AN_AddOpsInlineThreshold : ValueField<
    "scev-addops-inline-threshold", "unsigned", "500",
    "Threshold for inlining addition operands into a SCEV",
    /*hidden=*/1>;

defm AN_MaxSCEVCompareDepth : ValueField<
    "scalar-evolution-max-scev-compare-depth", "unsigned", "32",
    "Maximum depth of recursive SCEV complexity comparisons",
    /*hidden=*/1>;

defm AN_MaxSCEVOperationsImplicationDepth : ValueField<
    "scalar-evolution-max-scev-operations-implication-depth", "unsigned", "2",
    "Maximum depth of recursive SCEV operations implication analysis",
    /*hidden=*/1>;

defm AN_MaxValueCompareDepth : ValueField<
    "scalar-evolution-max-value-compare-depth", "unsigned", "2",
    "Maximum depth of recursive value complexity comparisons",
    /*hidden=*/1>;

defm AN_MaxArithDepth : ValueField<
    "scalar-evolution-max-arith-depth", "unsigned", "32",
    "Maximum depth of recursive arithmetics",
    /*hidden=*/1>;

defm AN_MaxConstantEvolvingDepth : ValueField<
    "scalar-evolution-max-constant-evolving-depth", "unsigned", "32",
    "Maximum depth of recursive constant evolving",
    /*hidden=*/1>;

defm AN_MaxCastDepth : ValueField<
    "scalar-evolution-max-cast-depth", "unsigned", "8",
    "Maximum depth of recursive SExt/ZExt/Trunc",
    /*hidden=*/1>;

defm AN_MaxAddRecSize : ValueField<
    "scalar-evolution-max-add-rec-size", "unsigned", "8",
    "Max coefficients in AddRec during evolving",
    /*hidden=*/1>;

defm AN_HugeExprThreshold : ValueField<
    "scalar-evolution-huge-expr-threshold", "unsigned", "4096",
    "Size of the expression which is considered huge",
    /*hidden=*/1>;

defm AN_RangeIterThreshold : ValueField<
    "scev-range-iter-threshold", "unsigned", "32",
    "Threshold for switching to iteratively computing SCEV ranges",
    /*hidden=*/1>;

defm AN_MaxLoopGuardCollectionDepth : ValueField<
    "scalar-evolution-max-loop-guard-collection-depth", "unsigned", "1",
    "Maximum depth for recursive loop guard collection",
    /*hidden=*/1>;

defm AN_UseExpensiveRangeSharpening : BoolField<
    "scalar-evolution-use-expensive-range-sharpening", "false",
    "Use more powerful methods of sharpening expression ranges. May "
    "be costly in terms of compile time",
    /*hidden=*/1>;

defm AN_MaxPhiSCCAnalysisSize : ValueField<
    "scalar-evolution-max-scc-analysis-depth", "unsigned", "8",
    "Maximum amount of nodes to process while searching SCEVUnknown "
    "Phi strongly connected components",
    /*hidden=*/1>;

defm AN_EnableFiniteLoopControl : BoolField<
    "scalar-evolution-finite-loop", "true",
    "Handle <= and >= in finite loops",
    /*hidden=*/1>;

defm AN_UseContextForNoWrapFlagInference : BoolField<
    "scalar-evolution-use-context-for-no-wrap-flag-strenghening", "true",
    "Infer nuw/nsw flags using context where suitable",
    /*hidden=*/1>;

// ===== ScopedNoAliasAA.cpp =====

defm AN_EnableScopedNoAlias : BoolField<
    "enable-scoped-noalias", "true",
    "",
    /*hidden=*/1>;

// ===== StackSafetyAnalysis.cpp =====

defm AN_StackSafetyMaxIterations : ValueField<
    "stack-safety-max-iterations", "int", "20",
    "",
    /*hidden=*/1>;

defm AN_StackSafetyPrint : BoolField<
    "stack-safety-print", "false",
    "",
    /*hidden=*/1>;

defm AN_StackSafetyRun : BoolField<
    "stack-safety-run", "false",
    "",
    /*hidden=*/1>;

// ===== StaticDataProfileInfo.cpp =====

defm AN_AnnotateStringLiteralSectionPrefix : BoolField<
    "memprof-annotate-string-literal-section-prefix", "false",
    "If true, annotate the string literal data section prefix",
    /*hidden=*/1>;

// ===== TargetTransformInfo.cpp =====

defm AN_CacheLineSize : OptionalField<
    "cache-line-size", "unsigned",
    "Use this to override the target cache line size when "
    "specified by the user.",
    /*hidden=*/1>;

defm AN_MinPageSize : OptionalField<
    "min-page-size", "unsigned",
    "Use this to override the target's minimum page size.",
    /*hidden=*/1>;

defm AN_PredictableBranchThreshold : OptionalField<
    "predictable-branch-threshold", "unsigned",
    "Use this to override the target's predictable branch threshold (%).",
    /*hidden=*/1>;

// ===== TypeBasedAliasAnalysis.cpp =====

defm AN_EnableTBAA : BoolField<
    "enable-tbaa", "true",
    "",
    /*hidden=*/1>;

// ===== ValueTracking.cpp =====

defm AN_DomConditionsMaxUses : ValueField<
    "dom-conditions-max-uses", "unsigned", "20",
    "",
    /*hidden=*/1>;

// ===== VectorUtils.cpp =====

defm AN_MaxInterleaveGroupFactor : ValueField<
    "max-interleave-group-factor", "unsigned", "8",
    "Maximum factor for an interleaved access group (default = 8)",
    /*hidden=*/1>;
