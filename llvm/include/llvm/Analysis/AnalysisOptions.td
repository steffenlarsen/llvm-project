//===- AnalysisOptions.td - CLV2 option defs for Analysis -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def AnalysisOptsReg : Registry<"llvm::an_opts"> { let OutOfLineGetters = 1; }

def IR2VecCategory : OptionCategory<"IR2Vec Options">;

// =========================================================================
// External enum types
// =========================================================================

// --- BlockFrequencyInfo.h: GVDAGType ---

def GVDAGTypeNone : EnumValue<"none",
    "do not display graphs."> {
  let CppName = "GVDT_None";
}

def GVDAGTypeFraction : EnumValue<"fraction",
    "display a graph using the "
    "fractional block frequency representation."> {
  let CppName = "GVDT_Fraction";
}

def GVDAGTypeInteger : EnumValue<"integer",
    "display a graph using the raw "
    "integer fractional block frequency representation."> {
  let CppName = "GVDT_Integer";
}

def GVDAGTypeCount : EnumValue<"count",
    "display a graph using the real "
    "profile count if available."> {
  let CppName = "GVDT_Count";
}

def GVDAGTypeEnum : EnumType<"GVDAGType",
    [GVDAGTypeNone, GVDAGTypeFraction,
     GVDAGTypeInteger, GVDAGTypeCount]> {
  let External = 1;
}

// --- BlockFrequencyInfo.h: PGOViewCountsType ---

def PGOViewCountsTypeNone : EnumValue<"none",
    "do not show."> {
  let CppName = "PGOVCT_None";
}

def PGOViewCountsTypeGraph : EnumValue<"graph",
    "show a graph."> {
  let CppName = "PGOVCT_Graph";
}

def PGOViewCountsTypeText : EnumValue<"text",
    "show in text."> {
  let CppName = "PGOVCT_Text";
}

def PGOViewCountsTypeEnum : EnumType<"PGOViewCountsType",
    [PGOViewCountsTypeNone, PGOViewCountsTypeGraph,
     PGOViewCountsTypeText]> {
  let External = 1;
}

// --- CtxProfPrintMode.h: CtxProfPrintMode (aliased as
//     CtxProfAnalysisPrinterPass::PrintMode) ---

def CtxProfPrintModeEverything : EnumValue<"everything",
    "print everything - most verbose"> {
  let CppName = "Everything";
}

def CtxProfPrintModeYAML : EnumValue<"yaml",
    "just the yaml representation of the profile"> {
  let CppName = "YAML";
}

def CtxProfPrintModeEnum : EnumType<"CtxProfPrintMode",
    [CtxProfPrintModeEverything, CtxProfPrintModeYAML]> {
  let External = 1;
}

// --- ImportedFunctionsInliningStatistics.h: InlinerFunctionImportStatsOpts ---

def InlinerFunctionImportStatsBasic : EnumValue<"basic",
    "basic statistics"> {
  let CppName = "Basic";
}

def InlinerFunctionImportStatsVerbose : EnumValue<"verbose",
    "printing of statistics for each inlined function"> {
  let CppName = "Verbose";
}

def InlinerFunctionImportStatsEnum : EnumType<"InlinerFunctionImportStatsOpts",
    [InlinerFunctionImportStatsBasic, InlinerFunctionImportStatsVerbose]> {
  let External = 1;
}

// --- IR2Vec.h: IR2VecKind ---

def IR2VecKindSymbolic : EnumValue<"symbolic",
    "Generate symbolic embeddings"> {
  let CppName = "Symbolic";
}

def IR2VecKindFlowAware : EnumValue<"flow-aware",
    "Generate flow-aware embeddings"> {
  let CppName = "FlowAware";
}

def IR2VecKindEnum : EnumType<"IR2VecKind",
    [IR2VecKindSymbolic, IR2VecKindFlowAware]> {
  let External = 1;
}

// --- ForceSummaryHotnessType.h: ForceSummaryHotnessType ---

def ForceSummaryHotnessNone : EnumValue<"none",
    "None."> {
  let CppName = "FSHT_None";
}

def ForceSummaryHotnessAllNonCritical : EnumValue<"all-non-critical",
    "All non-critical edges."> {
  let CppName = "FSHT_AllNonCritical";
}

def ForceSummaryHotnessAll : EnumValue<"all",
    "All edges."> {
  let CppName = "FSHT_All";
}

def ForceSummaryHotnessEnum : EnumType<
    "ForceSummaryHotnessType",
    [ForceSummaryHotnessNone, ForceSummaryHotnessAllNonCritical,
     ForceSummaryHotnessAll]> {
  let External = 1;
}

// =========================================================================
// Inline enum types
// =========================================================================

// --- InlineOrder.cpp: InlinePriorityMode ---

def InlinePriorityModeSize : EnumValue<"size",
    "Use callee size priority."> {
  let CppName = "Size";
}

def InlinePriorityModeCost : EnumValue<"cost",
    "Use inline cost priority."> {
  let CppName = "Cost";
}

def InlinePriorityModeCostBenefit : EnumValue<"cost-benefit",
    "Use cost-benefit ratio."> {
  let CppName = "CostBenefit";
}

def InlinePriorityModeML : EnumValue<"ml",
    "Use ML."> {
  let CppName = "ML";
}

def InlinePriorityModeEnum : EnumType<"InlinePriorityMode",
    [InlinePriorityModeSize, InlinePriorityModeCost,
     InlinePriorityModeCostBenefit, InlinePriorityModeML]>;

// --- MLInlineAdvisor.cpp: SkipMLPolicyCriteria ---

def SkipMLPolicyCriteriaNever : EnumValue<"never",
    "never"> {
  let CppName = "Never";
}

def SkipMLPolicyCriteriaIfCallerIsNotCold : EnumValue<"if-caller-not-cold",
    "if the caller is not cold"> {
  let CppName = "IfCallerIsNotCold";
}

def SkipMLPolicyCriteriaEnum : EnumType<"SkipMLPolicyCriteria",
    [SkipMLPolicyCriteriaNever, SkipMLPolicyCriteriaIfCallerIsNotCold]>;

// =========================================================================
// Options
// =========================================================================

// ===== AliasAnalysis.cpp =====

def AN_DisableBasicAA : Option<
    "disable-basic-aa", "",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_EnableAATrace : Option<
    "aa-trace", "",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== AliasSetTracker.cpp =====

def AN_SaturationThreshold : Option<
    "alias-set-saturation-threshold",
    "The maximum total number of memory locations alias "
    "sets may contain before degradation",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "250";
  let Hidden = 1;
}

// ===== AssumptionCache.cpp =====

def AN_VerifyAssumptionCache : Option<
    "verify-assumption-cache",
    "Enable verification of assumption cache",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== BasicAliasAnalysis.cpp =====

def AN_EnableRecPhiAnalysis : Option<
    "basic-aa-recphi", "",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AN_EnableSeparateStorageAnalysis : Option<
    "basic-aa-separate-storage", "",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// ===== BlockFrequencyInfo.cpp =====

def AN_ViewBlockFreqPropagationDAG : Option<
    "view-block-freq-propagation-dags",
    "Pop up a window to show a dag displaying how block "
    "frequencies propagation through the CFG.",
    "GVDAGType"> {
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_ViewBlockFreqFuncName : Option<
    "view-bfi-func-name",
    "The option to specify "
    "the name of the function "
    "whose CFG will be displayed.",
    "std::string"> {
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_ViewHotFreqPercent : Option<
    "view-hot-freq-percent",
    "An integer in percent used to specify "
    "the hot blocks/edges to be displayed "
    "in red: a block or edge whose frequency "
    "is no less than the max frequency of the "
    "function multiplied by this percent.",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "10";
  let Hidden = 1;
}

def AN_PGOViewCounts : Option<
    "pgo-view-counts",
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
    "PGOViewCountsType"> {
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_PrintBFI : Option<
    "print-bfi", "Print the block frequency info.",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_PrintBFIFuncName : Option<
    "print-bfi-func-name",
    "The option to specify the name of the function "
    "whose block frequency info is printed.",
    "std::string"> {
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

// ===== BlockFrequencyInfoImpl.cpp =====

def AN_CheckBFIUnknownBlockQueries : Option<
    "check-bfi-unknown-block-queries",
    "Check if block frequency is queried for an unknown block "
    "for debugging missed BFI updates",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_UseIterativeBFIInference : Option<
    "use-iterative-bfi-inference",
    "Apply an iterative post-processing to infer correct BFI counts",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_IterativeBFIMaxIterationsPerBlock : Option<
    "iterative-bfi-max-iterations-per-block",
    "Iterative inference: maximum number of update iterations "
    "per block",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "1000";
  let Hidden = 1;
}

def AN_IterativeBFIPrecision : Option<
    "iterative-bfi-precision",
    "Iterative inference: delta convergence precision; smaller values "
    "typically lead to better results at the cost of worsen runtime",
    "double"> {
  let Registry = AnalysisOptsReg;
  let Default = "1e-12";
  let Hidden = 1;
}

// ===== BranchProbabilityInfo.cpp =====

def AN_PrintBranchProb : Option<
    "print-bpi", "Print the branch probability info.",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_PrintBranchProbFuncName : Option<
    "print-bpi-func-name",
    "The option to specify the name of the function "
    "whose branch probability info is printed.",
    "std::string"> {
  let Default = "\"\"";
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

// ===== CallGraphSCCPass.cpp =====

def AN_MaxDevirtIterations : Option<
    "max-devirt-iterations", "",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "4";
  let ReallyHidden = 1;
}

// ===== CallPrinter.cpp =====

def AN_CallGraphHeatColors : Option<
    "callgraph-heat-colors", "Show heat colors in call-graph",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_CallGraphShowWeights : Option<
    "callgraph-show-weights", "Show edges labeled with weights",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_CallMultiGraph : Option<
    "callgraph-multigraph",
    "Show call-multigraph (do not remove parallel edges)",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_CallGraphDotFilenamePrefix : Option<
    "callgraph-dot-filename-prefix",
    "The prefix used for the CallGraph dot file names.",
    "std::string"> {
  let Default = "\"\"";
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

// ===== CaptureTracking.cpp =====

def AN_DefaultMaxUsesToExplore : Option<
    "capture-tracking-max-uses-to-explore",
    "Maximal number of uses to explore.",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "100";
  let Hidden = 1;
}

// ===== CFG.cpp =====

def AN_DefaultMaxBBsToExplore : Option<
    "dom-tree-reachability-max-bbs-to-explore",
    "Max number of BBs to explore for reachability analysis",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "32";
  let Hidden = 1;
}

// ===== CFGPrinter.cpp =====

def AN_CFGFuncName : Option<
    "cfg-func-name",
    "The name of a function (or its substring)"
    " whose CFG is viewed/printed.",
    "std::string"> {
  let Default = "\"\"";
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_CFGDotFilenamePrefix : Option<
    "cfg-dot-filename-prefix",
    "The prefix used for the CFG dot file names.",
    "std::string"> {
  let Default = "\"\"";
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_HideUnreachablePaths : Option<
    "cfg-hide-unreachable-paths", "",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
}

def AN_HideDeoptimizePaths : Option<
    "cfg-hide-deoptimize-paths", "",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
}

def AN_HideColdPaths : Option<
    "cfg-hide-cold-paths",
    "Hide blocks with relative frequency below the given value",
    "double"> {
  let Registry = AnalysisOptsReg;
  let Default = "0.0";
}

def AN_CfgShowHeatColors : Option<
    "cfg-heat-colors", "Show heat colors in CFG",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AN_CfgUseRawEdgeWeight : Option<
    "cfg-raw-weights",
    "Use raw weights for labels. "
    "Use percentages as default.",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_CfgShowEdgeWeight : Option<
    "cfg-weights", "Show edges labeled with weights",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== CGSCCPassManager.cpp =====

def AN_AbortOnMaxDevirtIterationsReached : Option<
    "abort-on-max-devirt-iterations-reached",
    "Abort when the max iterations for devirtualization CGSCC repeat "
    "pass is reached",
    "bool"> {
  let Registry = AnalysisOptsReg;
}

// ===== ConstantFolding.cpp =====

def AN_DisableFPCallFolding : Option<
    "disable-fp-call-folding",
    "Disable constant-folding of FP intrinsics and libcalls.",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== CtxProfAnalysis.cpp =====

def AN_UseCtxProfile : Option<
    "use-ctx-profile",
    "Use the specified contextual profile file",
    "std::string"> {
  let Registry = AnalysisOptsReg;
  let Default = "\"\"";
  let Hidden = 1;
}

def AN_CtxProfilePrinterLevel : Option<
    "ctx-profile-printer-level",
    "Verbosity level of the contextual profile printer pass.",
    "CtxProfPrintMode"> {
  let Registry = AnalysisOptsReg;
  let Default = "yaml";
  let Hidden = 1;
}

def AN_ForceIsInSpecializedModule : Option<
    "ctx-profile-force-is-specialized",
    "Treat the given module as-if it were containing the "
    "post-thinlink module containing the root",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
}

// ===== DDG.cpp =====

def AN_SimplifyDDG : Option<
    "ddg-simplify",
    "Use a simplified DDG by merging nodes that are in the same strongly "
    "connected component.",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AN_CreatePiBlocks : Option<
    "ddg-pi-blocks", "Create pi-block nodes.",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// ===== DDGPrinter.cpp =====

def AN_DotOnly : Option<
    "dot-ddg-only", "simple ddg dot graph",
    "bool"> {
  let Default = "false";
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_DDGDotFilenamePrefix : Option<
    "dot-ddg-filename-prefix",
    "The prefix used for the DDG dot file names.",
    "std::string"> {
  let Registry = AnalysisOptsReg;
  let Default = "\"ddg\"";
  let Hidden = 1;
}

// ===== Delinearization.cpp =====

def AN_UseFixedSizeArrayHeuristic : Option<
    "delinearize-use-fixed-size-array-heuristic",
    "When printing analysis, use the heuristic for fixed-size arrays "
    "if the default delinearizetion fails.",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// ===== DependenceAnalysis.cpp =====

def AN_Delinearize : Option<
    "da-delinearize",
    "Try to delinearize array references.",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AN_DisableDelinearizationChecks : Option<
    "da-disable-delinearization-checks",
    "Disable checks that try to statically verify validity of "
    "delinearized subscripts. Enabling this option may result in incorrect "
    "dependence vectors for languages that allow the subscript of one "
    "dimension to underflow or overflow into another dimension.",
    "bool"> {
  let Default = "false";
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_MIVMaxLevelThreshold : Option<
    "da-miv-max-level-threshold",
    "Maximum depth allowed for the recursive algorithm used to "
    "explore MIV direction vectors.",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "7";
  let Hidden = 1;
}

// ===== DevelopmentModeInlineAdvisor.cpp =====

def AN_TrainingLog : Option<
    "training-log",
    "Path where the development - mode inlining log is saved.",
    "std::string"> {
  let Default = "\"\"";
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_TFModelUnderTrainingPath : Option<
    "ml-inliner-model-under-training",
    "Path to SavedModel from the previous training iteration.",
    "std::string"> {
  let Default = "\"\"";
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_TFOutputSpecOverride : Option<
    "ml-inliner-output-spec-override",
    "Override the path to the output spec json file. See "
    "-ml-inliner-model-under-training documentation for the "
    "specification of that file.",
    "std::string"> {
  let Default = "\"\"";
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_TFFeedPrefix : Option<
    "ml-inliner-trained-model-feed-prefix",
    "Prefix for feature names.",
    "std::string"> {
  let Registry = AnalysisOptsReg;
  let Default = "\"action_\"";
  let Hidden = 1;
}

// ===== FunctionPropertiesAnalysis.cpp =====

def AN_EnableDetailedFunctionProperties : Option<
    "enable-detailed-function-properties",
    "Whether or not to compute detailed function properties.",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_BigBasicBlockInstructionThreshold : Option<
    "big-basic-block-instruction-threshold",
    "The minimum number of instructions a basic block should contain "
    "before being considered big.",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "500";
  let Hidden = 1;
}

def AN_MediumBasicBlockInstructionThreshold : Option<
    "medium-basic-block-instruction-threshold",
    "The minimum number of instructions a basic block should contain "
    "before being considered medium-sized.",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "15";
  let Hidden = 1;
}

def AN_CallWithManyArgumentsThreshold : Option<
    "call-with-many-arguments-threshold",
    "The minimum number of arguments a function call must have before "
    "it is considered having many arguments.",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "4";
  let Hidden = 1;
}

// ===== GlobalsModRef.cpp =====

def AN_EnableUnsafeGlobalsModRefAliasResults : Option<
    "enable-unsafe-globalsmodref-alias-results", "",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== ImportedFunctionsInliningStatistics.cpp =====

def AN_InlinerFunctionImportStats : Option<
    "inliner-function-import-stats",
    "Enable inliner stats for imported functions",
    "InlinerFunctionImportStatsOpts"> {
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

// ===== IndirectCallPromotionAnalysis.cpp =====

def AN_ICPRemainingPercentThreshold : Option<
    "icp-remaining-percent-threshold",
    "The percentage threshold against remaining unpromoted indirect "
    "call count for the promotion",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "30";
  let Hidden = 1;
}

def AN_ICPTotalPercentThreshold : Option<
    "icp-total-percent-threshold",
    "The percentage threshold against total "
    "count for the promotion",
    "uint64_t"> {
  let Registry = AnalysisOptsReg;
  let Default = "uint64_t{5}";
  let Hidden = 1;
}

def AN_ICPMinimumCountThreshold : Option<
    "icp-minimum-count-threshold",
    "Minimum absolute count for promotion candidate",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_MaxNumPromotions : Option<
    "icp-max-prom",
    "Max number of promotions for a single indirect "
    "call callsite",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "3";
  let Hidden = 1;
}

def AN_MaxNumVTableAnnotations : Option<
    "icp-max-num-vtables",
    "Max number of vtables annotated for a vtable load instruction.",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "6";
  let Hidden = 1;
}

// ===== InlineAdvisor.cpp =====

def AN_InlineRemarkAttribute : Option<
    "inline-remark-attribute",
    "Enable adding inline-remark attribute to"
    " callsites processed by inliner but not inlined",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_EnableInlineDeferral : Option<
    "inline-deferral",
    "Enable deferred inlining",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_InlineDeferralScale : Option<
    "inline-deferral-scale",
    "Scale to limit the cost of inline deferral",
    "int"> {
  let Registry = AnalysisOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def AN_AnnotateInlinePhase : Option<
    "annotate-inline-phase",
    "If true, annotate inline advisor remarks "
    "with LTO and pass information.",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_IR2VecVocabFile : Option<
    "ml-inliner-ir2vec-vocab-file",
    "Vocab file for IR2Vec; Setting this enables "
    "configuring the model to use IR2Vec embeddings.",
    "std::string"> {
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

// ===== InlineCost.cpp =====

def AN_DefaultThreshold : Option<
    "inlinedefault-threshold",
    "Default amount of inlining to perform",
    "int"> {
  let Registry = AnalysisOptsReg;
  let Default = "225";
  let Hidden = 1;
}

def AN_IgnoreTTIInlineCompatible : Option<
    "ignore-tti-inline-compatible",
    "Ignore TTI attributes compatibility check between callee/caller "
    "during inline cost calculation",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_PrintInstructionComments : Option<
    "print-instruction-comments",
    "Prints comments for instruction based on inline cost analysis",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_InlineThreshold : Option<
    "inline-threshold",
    "Control the amount of inlining to perform (default = 225)",
    "int"> {
  let Registry = AnalysisOptsReg;
  let Default = "225";
  let Hidden = 1;
}

def AN_HintThreshold : Option<
    "inlinehint-threshold",
    "Threshold for inlining functions with inline hint",
    "int"> {
  let Registry = AnalysisOptsReg;
  let Default = "325";
  let Hidden = 1;
}

def AN_ColdCallSiteThreshold : Option<
    "inline-cold-callsite-threshold",
    "Threshold for inlining cold callsites",
    "int"> {
  let Registry = AnalysisOptsReg;
  let Default = "45";
  let Hidden = 1;
}

def AN_InlineEnableCostBenefitAnalysis : Option<
    "inline-enable-cost-benefit-analysis",
    "Enable the cost-benefit analysis for the inliner",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_InlineSavingsMultiplier : Option<
    "inline-savings-multiplier",
    "Multiplier to multiply cycle savings by during inlining",
    "int"> {
  let Registry = AnalysisOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def AN_InlineSavingsProfitableMultiplier : Option<
    "inline-savings-profitable-multiplier",
    "A multiplier on top of cycle savings to decide whether the "
    "savings won't justify the cost",
    "int"> {
  let Registry = AnalysisOptsReg;
  let Default = "4";
  let Hidden = 1;
}

def AN_InlineSizeAllowance : Option<
    "inline-size-allowance",
    "The maximum size of a callee that get's "
    "inlined without cost consideration",
    "int"> {
  let Registry = AnalysisOptsReg;
  let Default = "100";
  let Hidden = 1;
}

def AN_ColdThreshold : Option<
    "inlinecold-threshold",
    "Threshold for inlining functions with cold attribute",
    "int"> {
  let Registry = AnalysisOptsReg;
  let Default = "45";
  let Hidden = 1;
}

def AN_HotCallSiteThreshold : Option<
    "hot-callsite-threshold",
    "Threshold for hot callsites ",
    "int"> {
  let Registry = AnalysisOptsReg;
  let Default = "3000";
  let Hidden = 1;
}

def AN_LocallyHotCallSiteThreshold : Option<
    "locally-hot-callsite-threshold",
    "Threshold for locally hot callsites ",
    "int"> {
  let Registry = AnalysisOptsReg;
  let Default = "525";
  let Hidden = 1;
}

def AN_ColdCallSiteRelFreq : Option<
    "cold-callsite-rel-freq",
    "Maximum block frequency, expressed as a percentage of caller's "
    "entry frequency, for a callsite to be considered cold.",
    "int"> {
  let Registry = AnalysisOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def AN_HotCallSiteRelFreq : Option<
    "hot-callsite-rel-freq",
    "Minimum block frequency, expressed as a multiple of caller's "
    "entry frequency, for a callsite to be hot in the absence of "
    "profile information.",
    "uint64_t"> {
  let Registry = AnalysisOptsReg;
  let Default = "uint64_t{60}";
  let Hidden = 1;
}

def AN_InstrCost : Option<
    "inline-instr-cost",
    "Cost of a single instruction when inlining",
    "int"> {
  let Registry = AnalysisOptsReg;
  let Default = "5";
  let Hidden = 1;
}

def AN_InlineAsmInstrCost : Option<
    "inline-asm-instr-cost",
    "Cost of a single inline asm instruction when inlining",
    "int"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_MemAccessCost : Option<
    "inline-memaccess-cost",
    "Cost of load/store instruction when inlining",
    "int"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_CallPenalty : Option<
    "inline-call-penalty",
    "Call penalty that is applied per callsite when inlining",
    "int"> {
  let Registry = AnalysisOptsReg;
  let Default = "25";
  let Hidden = 1;
}

def AN_StackSizeThreshold : Option<
    "inline-max-stacksize",
    "Do not inline functions with a stack size "
    "that exceeds the specified limit",
    "size_t"> {
  let Registry = AnalysisOptsReg;
  let Default = "std::numeric_limits<size_t>::max()";
  let Hidden = 1;
}

def AN_RecurStackSizeThreshold : Option<
    "recursive-inline-max-stacksize",
    "Do not inline recursive functions with a stack "
    "size that exceeds the specified limit",
    "size_t"> {
  let Registry = AnalysisOptsReg;
  let Default = "size_t{1024}";
  let Hidden = 1;
}

def AN_OptComputeFullInlineCost : Option<
    "inline-cost-full",
    "Compute the full inline cost of a call site even when the cost "
    "exceeds the threshold.",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_InlineCallerSupersetNoBuiltin : Option<
    "inline-caller-superset-nobuiltin",
    "Allow inlining when caller has a superset of callee's nobuiltin "
    "attributes.",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AN_DisableGEPConstOperand : Option<
    "disable-gep-const-evaluation",
    "Disables evaluation of GetElementPtr with constant operands",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_InlineAllViableCalls : Option<
    "inline-all-viable-calls",
    "Inline all viable calls, even if they exceed the inlining "
    "threshold",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== InlineOrder.cpp =====

def AN_UseInlinePriority : Option<
    "inline-priority-mode",
    "Choose the priority mode to use in module inline",
    "InlinePriorityMode"> {
  let Registry = AnalysisOptsReg;
  let Default = "size";
  let Hidden = 1;
}

def AN_ModuleInlinerTopPriorityThreshold : Option<
    "module-inliner-top-priority-threshold",
    "The cost threshold for call sites that get inlined without the "
    "cost-benefit analysis",
    "int"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== InstructionPrecedenceTracking.cpp =====

def AN_ExpensiveAsserts : Option<
    "ipt-expensive-asserts",
    "Perform expensive assert validation on every query to Instruction"
    " Precedence Tracking",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== InteractiveModelRunner.cpp =====

def AN_DebugReply : Option<
    "interactive-model-runner-echo-reply",
    "The InteractiveModelRunner will echo back to stderr "
    "the data received from the host (for debugging purposes).",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== IR2Vec.cpp =====

def AN_VocabFile : Option<
    "ir2vec-vocab-path",
    "Path to the vocabulary file for IR2Vec",
    "std::string"> {
  let Registry = AnalysisOptsReg;
  let Default = "\"\"";
  let Category = IR2VecCategory;
}

def AN_OpcWeight : Option<
    "ir2vec-opc-weight",
    "Weight for opcode embeddings",
    "float"> {
  let Registry = AnalysisOptsReg;
  let Default = "1.0f";
  let Category = IR2VecCategory;
}

def AN_TypeWeight : Option<
    "ir2vec-type-weight",
    "Weight for type embeddings",
    "float"> {
  let Registry = AnalysisOptsReg;
  let Default = "0.5f";
  let Category = IR2VecCategory;
}

def AN_ArgWeight : Option<
    "ir2vec-arg-weight",
    "Weight for argument embeddings",
    "float"> {
  let Registry = AnalysisOptsReg;
  let Default = "0.2f";
  let Category = IR2VecCategory;
}

def AN_IR2VecEmbeddingKind : Option<
    "ir2vec-kind", "IR2Vec embedding kind",
    "IR2VecKind"> {
  let Registry = AnalysisOptsReg;
  let Default = "symbolic";
  let Category = IR2VecCategory;
}

// ===== AssumptionCache.cpp =====

def AN_MaxAssumesPerValue : Option<
    "max-assumes-per-value",
    "Maximum number of assumptions to cache for a single value",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "1024";
  let Hidden = 1;
}

// ===== LastRunTrackingAnalysis.cpp =====

def AN_DisableLastRunTracking : Option<
    "disable-last-run-tracking",
    "Disable last run tracking",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== LazyValueInfo.cpp =====

def AN_PerPredRanges : Option<
    "lvi-per-pred-ranges",
    "Enable tracking of ranges for a value in a block for"
    " each predecessor",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== Loads.cpp =====

def AN_DefMaxInstsToScan : Option<
    "available-load-scan-limit",
    "Use this to specify the default maximum number of instructions "
    "to scan backward from a given instruction, when searching for "
    "available loaded value",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "6";
  let Hidden = 1;
}

// ===== LoopAccessAnalysis.cpp =====

def AN_VectorizationFactor : Option<
    "force-vector-width",
    "Sets the SIMD width. Zero is autoselect.",
    "ElementCount"> {
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_VectorizationInterleave : Option<
    "force-vector-interleave",
    "Sets the vectorization interleave count. "
    "Zero is autoselect.",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_RuntimeMemoryCheckThreshold : Option<
    "runtime-memory-check-threshold",
    "When performing memory disambiguation checks at runtime do not "
    "generate more than this number of comparisons (default = 8).",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def AN_MemoryCheckMergeThreshold : Option<
    "memory-check-merge-threshold",
    "Maximum number of comparisons done when trying to merge "
    "runtime memory checks. (default = 100)",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "100";
  let Hidden = 1;
}

def AN_MaxDependences : Option<
    "max-dependences",
    "Maximum number of dependences collected by "
    "loop-access analysis (default = 100)",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "100";
  let Hidden = 1;
}

def AN_EnableMemAccessVersioning : Option<
    "enable-mem-access-versioning",
    "Enable symbolic stride memory access versioning",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AN_EnableForwardingConflictDetection : Option<
    "store-to-load-forwarding-conflict-detection",
    "Enable conflict detection in loop-access analysis",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AN_MaxForkedSCEVDepth : Option<
    "max-forked-scev-depth",
    "Maximum recursion depth when finding forked SCEVs (default = 5)",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "5";
  let Hidden = 1;
}

def AN_SpeculateUnitStride : Option<
    "laa-speculate-unit-stride",
    "Speculate that non-constant strides are unit in LAA",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AN_HoistRuntimeChecks : Option<
    "hoist-runtime-checks",
    "Hoist inner loop runtime memory checks to outer loop if possible",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// ===== LoopCacheAnalysis.cpp =====

def AN_DefaultTripCount : Option<
    "default-trip-count",
    "Use this to specify the default trip count of a loop",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "100";
  let Hidden = 1;
}

def AN_TemporalReuseThreshold : Option<
    "temporal-reuse-threshold",
    "Use this to specify the max. distance between array elements "
    "accessed in a loop so that the elements are classified to have "
    "temporal reuse",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "2";
  let Hidden = 1;
}

// ===== LoopInfo.cpp =====

def AN_VerifyLoopInfo : Option<
    "verify-loop-info",
    "Verify loop info (time consuming)",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

// ===== MemoryBuiltins.cpp =====

def AN_ObjectSizeOffsetVisitorMaxVisitInstructions : Option<
    "object-size-offset-visitor-max-visit-instructions",
    "Maximum number of instructions for ObjectSizeOffsetVisitor to "
    "look at",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "100";
}

// ===== MemoryDependenceAnalysis.cpp =====

def AN_BlockScanLimit : Option<
    "memdep-block-scan-limit",
    "The number of instructions to scan in a block in memory "
    "dependency analysis",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "100";
  let Hidden = 1;
}

def AN_BlockNumberLimit : Option<
    "memdep-block-number-limit",
    "The number of blocks to scan during memory "
    "dependence analysis",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "200";
  let Hidden = 1;
}

def AN_CacheGlobalLimit : Option<
    "memdep-cache-global-limit",
    "The max number of entries allowed in a cache (default = 10000)",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "10000";
  let Hidden = 1;
}

// ===== MemoryProfileInfo.cpp =====

def AN_MemProfReportHintedSizes : Option<
    "memprof-report-hinted-sizes",
    "Report total allocation sizes of hinted allocations",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_MemProfKeepAllNotColdContexts : Option<
    "memprof-keep-all-not-cold-contexts",
    "Keep all non-cold contexts (increases cloning overheads)",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_MinClonedColdBytePercent : Option<
    "memprof-cloning-cold-threshold",
    "Min percent of cold bytes to hint alloc cold during cloning",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "100";
  let Hidden = 1;
}

def AN_MinCallsiteColdBytePercent : Option<
    "memprof-callsite-cold-threshold",
    "Min percent of cold bytes at a callsite to discard non-cold "
    "contexts",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "100";
  let Hidden = 1;
}

def AN_MinPercentMaxColdSize : Option<
    "memprof-min-percent-max-cold-size",
    "Min percent of max cold bytes for critical cold context",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "100";
  let Hidden = 1;
}

def AN_MemProfKeepContextSizeInfo : Option<
    "memprof-keep-context-size-info",
    "Keep context size information in memprof metadata",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_MemProfUseAmbiguousAttributes : Option<
    "memprof-ambiguous-attributes",
    "Apply ambiguous memprof attribute to ambiguous allocations",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// ===== MemorySSA.cpp =====

def AN_DotCFGMSSA : Option<
    "dot-cfg-mssa",
    "file name for generated dot file",
    "std::string"> {
  let Registry = AnalysisOptsReg;
  let Default = "\"\"";
  let ValueDesc = "file name for generated dot file";
}

def AN_MaxCheckLimit : Option<
    "memssa-check-limit",
    "The maximum number of stores/phis MemorySSA"
    "will consider trying to walk past (default = 100)",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "100";
  let Hidden = 1;
}

def AN_VerifyMemorySSA : Option<
    "verify-memoryssa",
    "Enable verification of MemorySSA.",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

// ===== MLInlineAdvisor.cpp =====

def AN_InteractiveChannelBaseName : Option<
    "inliner-interactive-channel-base",
    "Base file path for the interactive mode. The incoming filename should "
    "have the name <inliner-interactive-channel-base>.in, while the "
    "outgoing name should be <inliner-interactive-channel-base>.out",
    "std::string"> {
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_InteractiveIncludeDefault : Option<
    "inliner-interactive-include-default", "",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_SkipPolicy : Option<
    "ml-inliner-skip-policy", "",
    "SkipMLPolicyCriteria"> {
  let Registry = AnalysisOptsReg;
  let Default = "never";
  let Hidden = 1;
}

def AN_ModelSelector : Option<
    "ml-inliner-model-selector", "",
    "std::string"> {
  let Registry = AnalysisOptsReg;
  let Default = "\"\"";
  let Hidden = 1;
}

def AN_StopImmediatelyForTest : Option<
    "ml-inliner-stop-immediately", "",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_SizeIncreaseThreshold : Option<
    "ml-advisor-size-increase-threshold",
    "Maximum factor by which expected native size may increase before "
    "blocking any further inlining.",
    "float"> {
  let Registry = AnalysisOptsReg;
  let Default = "2.0f";
  let Hidden = 1;
}

def AN_KeepFPICache : Option<
    "ml-advisor-keep-fpi-cache",
    "For test - keep the ML Inline advisor's FunctionPropertiesInfo cache",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== ModuleSummaryAnalysis.cpp =====

def AN_ForceSummaryEdgesCold : Option<
    "force-summary-edges-cold",
    "Force all edges in the function summary to cold",
    "ForceSummaryHotnessType"> {
  let Default = "FSHT_None";
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_ModuleSummaryDotFile : Option<
    "module-summary-dot-file",
    "File to emit dot graph of new summary into",
    "std::string"> {
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_EnableMemProfIndirectCallSupport : Option<
    "enable-memprof-indirect-call-support",
    "Enable MemProf support for summarizing and cloning indirect calls",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AN_MaxSummaryIndirectEdges : Option<
    "module-summary-max-indirect-edges",
    "Max number of summary edges added from "
    "indirect call profile metadata",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== ObjCARCAnalysisUtils.cpp =====

def AN_EnableARCOptimizations : Option<
    "enable-objc-arc-opts",
    "enable/disable all ARC Optimizations",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// ===== PHITransAddr.cpp =====

def AN_EnableAddPhiTranslation : Option<
    "gvn-add-phi-translation",
    "Enable phi-translation of add instructions",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== ProfileSummaryInfo.cpp =====

def AN_PartialProfile : Option<
    "partial-profile",
    "Specify the current profile is used as a partial profile.",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_ScalePartialSampleProfileWorkingSetSize : Option<
    "scale-partial-sample-profile-working-set-size",
    "If true, scale the working set size of the partial sample profile "
    "by the partial profile ratio to reflect the size of the program "
    "being compiled.",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AN_PartialSampleProfileWorkingSetSizeScaleFactor : Option<
    "partial-sample-profile-working-set-size-scale-factor",
    "The scale factor used to scale the working set size of the "
    "partial sample profile along with the partial profile ratio. "
    "This includes the factor of the profile counter per block "
    "and the factor to scale the working set size to use the same "
    "shared thresholds as PGO.",
    "double"> {
  let Registry = AnalysisOptsReg;
  let Default = "0.008";
  let Hidden = 1;
}

// ===== RegionInfo.cpp =====

def AN_VerifyRegionInfo : Option<
    "verify-region-info",
    "Verify region info (time consuming)",
    "bool"> {
  let Registry = AnalysisOptsReg;
}

// ===== RegionPrinter.cpp =====

def AN_OnlySimpleRegions : Option<
    "only-simple-regions",
    "Show only simple regions in the graphviz viewer",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== ScalarEvolution.cpp =====

def AN_MaxBruteForceIterations : Option<
    "scalar-evolution-max-iterations",
    "Maximum number of iterations SCEV will "
    "symbolically execute a constant "
    "derived loop",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "100";
  let ReallyHidden = 1;
}

def AN_VerifySCEV : Option<
    "verify-scev",
    "Verify ScalarEvolution's backedge taken counts (slow)",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_VerifySCEVStrict : Option<
    "verify-scev-strict",
    "Enable stricter verification with -verify-scev is passed",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Hidden = 1;
}

def AN_VerifyIR : Option<
    "scev-verify-ir",
    "Verify IR correctness when making sensitive SCEV queries (slow)",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_MulOpsInlineThreshold : Option<
    "scev-mulops-inline-threshold",
    "Threshold for inlining multiplication operands into a SCEV",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "32";
  let Hidden = 1;
}

def AN_AddOpsInlineThreshold : Option<
    "scev-addops-inline-threshold",
    "Threshold for inlining addition operands into a SCEV",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "500";
  let Hidden = 1;
}

def AN_MaxSCEVCompareDepth : Option<
    "scalar-evolution-max-scev-compare-depth",
    "Maximum depth of recursive SCEV complexity comparisons",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "32";
  let Hidden = 1;
}

def AN_MaxSCEVOperationsImplicationDepth : Option<
    "scalar-evolution-max-scev-operations-implication-depth",
    "Maximum depth of recursive SCEV operations implication analysis",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def AN_MaxValueCompareDepth : Option<
    "scalar-evolution-max-value-compare-depth",
    "Maximum depth of recursive value complexity comparisons",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def AN_MaxArithDepth : Option<
    "scalar-evolution-max-arith-depth",
    "Maximum depth of recursive arithmetics",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "32";
  let Hidden = 1;
}

def AN_MaxConstantEvolvingDepth : Option<
    "scalar-evolution-max-constant-evolving-depth",
    "Maximum depth of recursive constant evolving",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "32";
  let Hidden = 1;
}

def AN_MaxCastDepth : Option<
    "scalar-evolution-max-cast-depth",
    "Maximum depth of recursive SExt/ZExt/Trunc",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def AN_MaxAddRecSize : Option<
    "scalar-evolution-max-add-rec-size",
    "Max coefficients in AddRec during evolving",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def AN_HugeExprThreshold : Option<
    "scalar-evolution-huge-expr-threshold",
    "Size of the expression which is considered huge",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "4096";
  let Hidden = 1;
}

def AN_RangeIterThreshold : Option<
    "scev-range-iter-threshold",
    "Threshold for switching to iteratively computing SCEV ranges",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "32";
  let Hidden = 1;
}

def AN_MaxLoopGuardCollectionDepth : Option<
    "scalar-evolution-max-loop-guard-collection-depth",
    "Maximum depth for recursive loop guard collection",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AN_UseExpensiveRangeSharpening : Option<
    "scalar-evolution-use-expensive-range-sharpening",
    "Use more powerful methods of sharpening expression ranges. May "
    "be costly in terms of compile time",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_MaxPhiSCCAnalysisSize : Option<
    "scalar-evolution-max-scc-analysis-depth",
    "Maximum amount of nodes to process while searching SCEVUnknown "
    "Phi strongly connected components",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def AN_EnableFiniteLoopControl : Option<
    "scalar-evolution-finite-loop",
    "Handle <= and >= in finite loops",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def AN_UseContextForNoWrapFlagInference : Option<
    "scalar-evolution-use-context-for-no-wrap-flag-strenghening",
    "Infer nuw/nsw flags using context where suitable",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// ===== ScopedNoAliasAA.cpp =====

def AN_EnableScopedNoAlias : Option<
    "enable-scoped-noalias", "",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// ===== StackSafetyAnalysis.cpp =====

def AN_StackSafetyMaxIterations : Option<
    "stack-safety-max-iterations", "",
    "int"> {
  let Registry = AnalysisOptsReg;
  let Default = "20";
  let Hidden = 1;
}

def AN_StackSafetyPrint : Option<
    "stack-safety-print", "",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_StackSafetyRun : Option<
    "stack-safety-run", "",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== StaticDataProfileInfo.cpp =====

def AN_AnnotateStringLiteralSectionPrefix : Option<
    "memprof-annotate-string-literal-section-prefix",
    "If true, annotate the string literal data section prefix",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// ===== TargetTransformInfo.cpp =====

def AN_CacheLineSize : Option<
    "cache-line-size",
    "Use this to override the target cache line size when "
    "specified by the user.",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_MinPageSize : Option<
    "min-page-size",
    "Use this to override the target's minimum page size.",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def AN_PredictableBranchThreshold : Option<
    "predictable-branch-threshold",
    "Use this to override the target's predictable branch threshold (%).",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "99";
  let Hidden = 1;
}

// ===== TypeBasedAliasAnalysis.cpp =====

def AN_EnableTBAA : Option<
    "enable-tbaa", "",
    "bool"> {
  let Registry = AnalysisOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// ===== ValueTracking.cpp =====

def AN_DomConditionsMaxUses : Option<
    "dom-conditions-max-uses", "",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "20";
  let Hidden = 1;
}

// ===== VectorUtils.cpp =====

def AN_MaxInterleaveGroupFactor : Option<
    "max-interleave-group-factor",
    "Maximum factor for an interleaved access group (default = 8)",
    "unsigned"> {
  let Registry = AnalysisOptsReg;
  let Default = "8";
  let Hidden = 1;
}
