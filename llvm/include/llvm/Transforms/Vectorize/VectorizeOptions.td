//===- VectorizeOptions.td - CLV2 option defs for Vectorize -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def VectorizeOptsReg : Registry<"llvm::vec_opts"> {
  // Getter bodies are compiled once, in VectorizeOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

// =========================================================================
// Inline enum types
// =========================================================================

def TailFoldingPolicyTyNone : EnumValue<"dont-fold-tail",
    "Don't tail-fold loops."> {
  let CppName = "None";
}

def TailFoldingPolicyTyPreferFoldTail : EnumValue<"prefer-fold-tail",
    "prefer tail-folding, otherwise create an epilogue when appropriate."> {
  let CppName = "PreferFoldTail";
}

def TailFoldingPolicyTyMustFoldTail : EnumValue<"must-fold-tail",
    "always tail-fold, don't attempt vectorization if tail-folding fails."> {
  let CppName = "MustFoldTail";
}

def TailFoldingPolicyTyEnum : EnumType<"TailFoldingPolicyTy",
    [TailFoldingPolicyTyNone, TailFoldingPolicyTyPreferFoldTail,
     TailFoldingPolicyTyMustFoldTail]>;

// =========================================================================
// External enum types
// =========================================================================

// --- TargetTransformInfo.h: TailFoldingStyle ---

def TailFoldingStyleNone : EnumValue<"none",
    "Disable tail folding"> {
  let CppName = "None";
}

def TailFoldingStyleData : EnumValue<"data",
    "Create lane mask for data only, using active.lane.mask intrinsic"> {
  let CppName = "Data";
}

def TailFoldingStyleDataWithoutLaneMask : EnumValue<"data-without-lane-mask",
    "Create lane mask with compare/stepvector"> {
  let CppName = "DataWithoutLaneMask";
}

def TailFoldingStyleDataAndControlFlow : EnumValue<"data-and-control",
    "Create lane mask using active.lane.mask intrinsic, and use it for "
    "both data and control flow"> {
  let CppName = "DataAndControlFlow";
}

def TailFoldingStyleDataWithEVL : EnumValue<"data-with-evl",
    "Use predicated EVL instructions for tail folding. If EVL is "
    "unsupported, fallback to data-without-lane-mask."> {
  let CppName = "DataWithEVL";
}

def TailFoldingStyleEnum : EnumType<"TailFoldingStyle",
    [TailFoldingStyleNone, TailFoldingStyleData,
     TailFoldingStyleDataWithoutLaneMask,
     TailFoldingStyleDataAndControlFlow,
     TailFoldingStyleDataWithEVL]> {
  let External = 1;
}

// --- LoopVectorizationLegality.h: LoopVectorizeHints::ScalableForceKind ---
// Original C++ enum: { SK_Unspecified=-1, SK_FixedWidthOnly=0,
//                       SK_PreferScalable=1, SK_AlwaysScalable=2 }

def ScalableForceKindUnspecified : EnumValue<"__unspecified",
    "Not selected."> {
  let CppName = "SK_Unspecified";
  let Value = -1;
}

def ScalableForceKindFixedWidthOnly : EnumValue<"off",
    "Scalable vectorization is disabled."> {
  let CppName = "SK_FixedWidthOnly";
  let Value = 0;
}

def ScalableForceKindPreferScalable : EnumValue<"preferred",
    "Scalable vectorization is available and favored when the cost is "
    "inconclusive."> {
  let CppName = "SK_PreferScalable";
  let Value = 1;
}

def ScalableForceKindOn : EnumValue<"on",
    "Scalable vectorization is available and favored when the cost is "
    "inconclusive."> {
  let CppName = "SK_PreferScalable";
  let Value = 1;
}

def ScalableForceKindAlwaysScalable : EnumValue<"always",
    "Scalable vectorization is available and always favored when feasible"> {
  let CppName = "SK_AlwaysScalable";
  let Value = 2;
}

def ScalableForceKindEnum : EnumType<"ScalableForceKind",
    [ScalableForceKindUnspecified, ScalableForceKindFixedWidthOnly,
     ScalableForceKindPreferScalable,
     ScalableForceKindOn, ScalableForceKindAlwaysScalable]> {
  let External = 1;
}

// --- LoopIdiomVectorize.h: LoopIdiomVectorizeStyle ---

def LoopIdiomVectorizeStyleMasked : EnumValue<"masked",
    "Use masked vector intrinsics"> {
  let CppName = "Masked";
}

def LoopIdiomVectorizeStylePredicated : EnumValue<"predicated",
    "Use VP intrinsics"> {
  let CppName = "Predicated";
}

def LoopIdiomVectorizeStyleEnum : EnumType<"LoopIdiomVectorizeStyle",
    [LoopIdiomVectorizeStyleMasked,
     LoopIdiomVectorizeStylePredicated]> {
  let External = 1;
}

// =========================================================================
// List options
// =========================================================================

def VEC_VPlanPrintAfterPasses : ListOption<
    "vplan-print-after",
    "Print VPlans after specified VPlan transformations (regexp).",
    "std::string"> {
  let Registry = VectorizeOptsReg;
  let Hidden = 1;
}

def VEC_VPlanPrintBeforePasses : ListOption<
    "vplan-print-before",
    "Print VPlans before specified VPlan transformations (regexp).",
    "std::string"> {
  let Registry = VectorizeOptsReg;
  let Hidden = 1;
}

// =========================================================================
// Enum options
// =========================================================================

def VEC_ScalableEpilogueVFCostScaleFactor : Option<
    "scalable-epilogue-vf-cost-scale-factor",
    "Scale the cost of scalable epilogue VFs by this factor.",
    "float"> {
  let Registry = VectorizeOptsReg;
  let Default = "2.0f";
  let Hidden = 1;
}

def VEC_TailFoldingPolicy : Option<
    "tail-folding-policy",
    "Tail-folding preferences over creating an epilogue loop.",
    "TailFoldingPolicyTy"> {
  let Registry = VectorizeOptsReg;
  let Default = "dont-fold-tail";
  let Hidden = 1;
}

def VEC_EpilogueTailFoldingPolicy : Option<
    "epilogue-tail-folding-policy",
    "Epilogue-tail-folding preferences over creating an epilogue loop.",
    "TailFoldingPolicyTy"> {
  let Registry = VectorizeOptsReg;
  let Default = "dont-fold-tail";
  let Hidden = 1;
}

def VEC_ForceTailFoldingStyle : Option<
    "force-tail-folding-style",
    "Force the tail folding style",
    "TailFoldingStyle"> {
  let Registry = VectorizeOptsReg;
  let Default = "none";
}

def VEC_ForceScalableVectorization : Option<
    "scalable-vectorization",
    "Control whether the compiler can use scalable vectors to "
    "vectorize a loop",
    "ScalableForceKind"> {
  let Registry = VectorizeOptsReg;
  let Hidden = 1;
}

def VEC_LoopIdiomVectorizeStyleOpt : Option<
    "loop-idiom-vectorize-style",
    "The vectorization style for loop idiom transform.",
    "LoopIdiomVectorizeStyle"> {
  let Registry = VectorizeOptsReg;
  let Default = "masked";
  let Hidden = 1;
}

// =========================================================================
// Scalar options --- LoopVectorize.cpp
// =========================================================================

def VEC_EnableEpilogueVectorization : Option<
    "enable-epilogue-vectorization",
    "Enable vectorization of epilogue loops.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def VEC_EpilogueVectorizationForceVF : Option<
    "epilogue-vectorization-force-VF",
    "When epilogue vectorization is enabled, and a value greater than "
    "1 is specified, forces the given VF for all applicable epilogue "
    "loops. Note: This allows all scalable VFs >= vscale x 1.",
    "ElementCount"> {
  let Registry = VectorizeOptsReg;
  let Hidden = 1;
}

def VEC_EpilogueVectorizationMinVF : Option<
    "epilogue-vectorization-minimum-VF",
    "Only loops with vectorization factor equal to or larger than "
    "the specified value are considered for epilogue vectorization.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Hidden = 1;
}

def VEC_TinyTripCountVectorThreshold : Option<
    "vectorizer-min-trip-count",
    "Loops with a constant trip count that is smaller than this "
    "value are vectorized only if no scalar iteration overheads "
    "are incurred.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "16";
  let Hidden = 1;
}

def VEC_VectorizeMemoryCheckThreshold : Option<
    "vectorize-memory-check-threshold",
    "The maximum allowed number of runtime memory checks",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "128";
  let Hidden = 1;
}

def VEC_EnableWideActiveLaneMask : Option<
    "enable-wide-lane-mask",
    "Enable use of wide lane masks when used for control flow in "
    "tail-folded loops",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_EnableInterleavedMemAccesses : Option<
    "enable-interleaved-mem-accesses",
    "Enable vectorization on interleaved memory accesses in a loop",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_EnableMaskedInterleavedMemAccesses : Option<
    "enable-masked-interleaved-mem-accesses",
    "Enable vectorization on masked interleaved memory accesses in a loop",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_ForceTargetNumScalarRegs : Option<
    "force-target-num-scalar-regs",
    "A flag that overrides the target's number of scalar registers.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_ForceTargetNumVectorRegs : Option<
    "force-target-num-vector-regs",
    "A flag that overrides the target's number of vector registers.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_ForceTargetMaxScalarInterleaveFactor : Option<
    "force-target-max-scalar-interleave",
    "A flag that overrides the target's max interleave factor for "
    "scalar loops.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_ForceTargetMaxVectorInterleaveFactor : Option<
    "force-target-max-vector-interleave",
    "A flag that overrides the target's max interleave factor for "
    "vectorized loops.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_ForceTargetInstructionCost : Option<
    "force-target-instruction-cost",
    "A flag that overrides the target's expected cost for "
    "an instruction to a single constant value. Mostly "
    "useful for getting consistent testing.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_SmallLoopCost : Option<
    "small-loop-cost",
    "The cost of a loop that is considered 'small' by the interleaver.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "20";
  let Hidden = 1;
}

def VEC_LoopVectorizeWithBlockFrequency : Option<
    "loop-vectorize-with-block-frequency",
    "Enable the use of the block frequency analysis to access PGO "
    "heuristics minimizing code growth in cold regions and being more "
    "aggressive in hot regions.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def VEC_EnableLoadStoreRuntimeInterleave : Option<
    "enable-loadstore-runtime-interleave",
    "Enable runtime interleaving until load/store ports are saturated",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def VEC_NumberOfStoresToPredicate : Option<
    "vectorize-num-stores-pred",
    "Max number of stores to be predicated behind an if.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def VEC_EnableIndVarRegisterHeur : Option<
    "enable-ind-var-reg-heur",
    "Count the induction variable only once when interleaving",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def VEC_MaxNestedScalarReductionIC : Option<
    "max-nested-scalar-reduction-interleave",
    "The maximum interleave count to use when interleaving a scalar "
    "reduction in a nested loop.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def VEC_ForceOrderedReductions : Option<
    "force-ordered-reductions",
    "Enable the vectorisation of loops with in-order (strict) "
    "FP reductions",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_PreferPredicatedReductionSelect : Option<
    "prefer-predicated-reduction-select",
    "Prefer predicating a reduction operation over an after loop select.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_EnableVPlanNativePath : Option<
    "enable-vplan-native-path",
    "Enable VPlan-native vectorization path with "
    "support for outer loop vectorization.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Hidden = 1;
}

def VEC_VerifyEachVPlan : Option<
    "vplan-verify-each",
    "Verify VPlans after VPlan transforms.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_VPlanPrintAfterAll : Option<
    "vplan-print-after-all",
    "Print VPlans after all VPlan transformations.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_VPlanPrintBeforeAll : Option<
    "vplan-print-before-all",
    "Print VPlans before all VPlan transformations.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_VPlanPrintVectorRegionScope : Option<
    "vplan-print-vector-region-scope",
    "Limit VPlan printing to vector loop region in "
    "`-vplan-print-after*` if the plan has one.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_VPlanBuildStressTest : Option<
    "vplan-build-outerloop-stress-test",
    "Build VPlan for every supported loop nest in the function and bail "
    "out right after the build (stress test the VPlan H-CFG construction "
    "in the VPlan-native vectorization path).",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_EnableLoopInterleaving : Option<
    "interleave-loops",
    "Enable loop interleaving in Loop vectorization passes",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def VEC_EnableLoopVectorization : Option<
    "vectorize-loops",
    "Run the Loop vectorization passes",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def VEC_ForceMaskedDivRem : Option<
    "force-widen-divrem-via-masked-intrinsic",
    "Override cost based masked intrinsic widening "
    "for div/rem instructions",
    "cl::boolOrDefault"> {
  let Registry = VectorizeOptsReg;
  let Hidden = 1;
}

def VEC_EnableEarlyExitVectorization : Option<
    "enable-early-exit-vectorization",
    "Enable vectorization of early exit loops with uncountable exits.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def VEC_EnableEarlyExitVectorizationWithSideEffects : Option<
    "enable-early-exit-vectorization-with-side-effects",
    "Enable vectorization of early exit loops with side effects.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_ForcePartialAliasingVectorization : Option<
    "force-partial-aliasing-vectorization",
    "Force vectorization with partial aliasing support.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// =========================================================================
// Scalar options --- VPlanRecipes.cpp
// =========================================================================

def VEC_VPlanPrintMetadata : Option<
    "vplan-print-metadata",
    "Print metadata in VPlan output.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// =========================================================================
// Scalar options --- VPlan.cpp
// =========================================================================

def VEC_PrintVPlansInDotFormat : Option<
    "vplan-print-in-dot-format",
    "Use dot format instead of plain text when dumping VPlans",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Hidden = 1;
}

// =========================================================================
// Scalar options --- LoopVectorizationPlanner.cpp
// =========================================================================

def VEC_MaximizeBandwidth : Option<
    "vectorizer-maximize-bandwidth",
    "Maximize bandwidth when selecting vectorization factor which "
    "will be determined by the smallest type in loop.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_UseWiderVFIfCallVariantsPresent : Option<
    "vectorizer-maximize-bandwidth-for-vector-calls",
    "Try wider VFs if they enable the use of vector variants",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def VEC_ConsiderRegPressure : Option<
    "vectorizer-consider-reg-pressure",
    "Discard VFs if their register pressure is too high.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_ForceTargetSupportsScalableVectors : Option<
    "force-target-supports-scalable-vectors",
    "Pretend that scalable vectors are supported, even if the target does "
    "not support them. This flag should only be used for testing.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_PreferInLoopReductions : Option<
    "prefer-inloop-reductions",
    "Prefer in-loop vector reductions, "
    "overriding the targets preference.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_ForceTargetSupportsGatherScatterOps : Option<
    "force-target-supports-gather-scatter-ops",
    "Pretend that gather/scatter operations are supported, even if the "
    "target does not support them. This flag should only be used for testing.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_ForceTargetSupportsMaskedMemoryOps : Option<
    "force-target-supports-masked-memory-ops",
    "Assume the target supports masked memory operations (used for "
    "testing).",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// =========================================================================
// Scalar options --- LoopVectorizationLegality.cpp
// =========================================================================

def VEC_EnableIfConversion : Option<
    "enable-if-conversion",
    "Enable if-conversion during vectorization.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def VEC_AllowStridedPointerIVs : Option<
    "lv-strided-pointer-ivs",
    "Enable recognition of non-constant strided "
    "pointer induction variables.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_HintsAllowReordering : Option<
    "hints-allow-reordering",
    "Allow enabling loop hints to reorder "
    "FP operations during vectorization.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def VEC_VectorizeSCEVCheckThreshold : Option<
    "vectorize-scev-check-threshold",
    "The maximum number of SCEV checks allowed.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "16";
  let Hidden = 1;
}

def VEC_PragmaVectorizeSCEVCheckThreshold : Option<
    "pragma-vectorize-scev-check-threshold",
    "The maximum number of SCEV checks allowed with a "
    "vectorize(enable) pragma",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "128";
  let Hidden = 1;
}

def VEC_EnableHistogramVectorization : Option<
    "enable-histogram-loop-vectorization",
    "Enables autovectorization of some loops containing histograms",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// =========================================================================
// Scalar options --- VectorCombine.cpp
// =========================================================================

def VEC_DisableVectorCombine : Option<
    "disable-vector-combine",
    "Disable all vector combine transforms",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_DisableBinopExtractShuffle : Option<
    "disable-binop-extract-shuffle",
    "Disable binop extract to shuffle transforms",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_MaxInstrsToScan : Option<
    "vector-combine-max-scan-instrs",
    "Max number of instructions to scan for vector combining.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "30";
  let Hidden = 1;
}

// =========================================================================
// Scalar options --- SLPVectorizer.cpp
// =========================================================================

def VEC_RunSLPVectorization : Option<
    "vectorize-slp",
    "Run the SLP vectorization passes",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def VEC_SLPReVec : Option<
    "slp-revec",
    "Enable vectorization for wider vector utilization",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_SLPCostThreshold : Option<
    "slp-threshold",
    "Only vectorize if you gain more than this number ",
    "int"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_ShouldVectorizeHor : Option<
    "slp-vectorize-hor",
    "Attempt to vectorize horizontal reductions",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def VEC_ShouldStartVectorizeHorAtStore : Option<
    "slp-vectorize-hor-store",
    "Attempt to vectorize horizontal reductions feeding into a store",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_SplitAlternateInstructions : Option<
    "slp-split-alternate-instructions",
    "Improve the code quality by splitting alternate instructions",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def VEC_SLPInstCountCheck : Option<
    "slp-inst-count-check",
    "Reject vectorization if vector instruction count exceeds "
    "scalar instruction count",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def VEC_MaxVectorRegSizeOption : Option<
    "slp-max-reg-size",
    "Attempt to vectorize for this register size in bits",
    "int"> {
  let Registry = VectorizeOptsReg;
  let Default = "128";
  let Hidden = 1;
}

def VEC_MaxVFOption : Option<
    "slp-max-vf",
    "Maximum SLP vectorization factor (0=unlimited)",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_ScheduleRegionSizeBudget : Option<
    "slp-schedule-budget",
    "Limit the size of the SLP scheduling region per block",
    "int"> {
  let Registry = VectorizeOptsReg;
  let Default = "100000";
  let Hidden = 1;
}

def VEC_PerLaneGatherScale : Option<
    "slp-per-lane-gather-scale",
    "Use per-lane execution scale for gather/buildvector tree entries to "
    "model LICM-hoistable buildvector sequences.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "true";
  let Hidden = 1;
}

def VEC_SLPMaxRuntimeAliasChecks : Option<
    "slp-max-runtime-alias-checks",
    "The maximum number of runtime alias checks generated to guard a single "
    "SLP-vectorized region.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def VEC_SLPRuntimeAliasChecksMaxScalarCostPercent : Option<
    "slp-runtime-alias-checks-max-scalar-cost-percent",
    "Maximum SLP runtime alias check cost, as a percentage of the guarded "
    "scalar region cost, before versioning is rejected to avoid pessimizing "
    "the scalar fallback path.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "25";
  let Hidden = 1;
}

def VEC_MinVectorRegSizeOption : Option<
    "slp-min-reg-size",
    "Attempt to vectorize for this register size in bits",
    "int"> {
  let Registry = VectorizeOptsReg;
  let Default = "128";
  let Hidden = 1;
}

def VEC_RecursionMaxDepth : Option<
    "slp-recursion-max-depth",
    "Limit the recursion depth when building a vectorizable tree",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "12";
  let Hidden = 1;
}

def VEC_MinTreeSize : Option<
    "slp-min-tree-size",
    "Only vectorize small trees if they are fully vectorizable",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "3";
  let Hidden = 1;
}

def VEC_LookAheadMaxDepth : Option<
    "slp-max-look-ahead-depth",
    "The maximum look-ahead depth for operand reordering scores",
    "int"> {
  let Registry = VectorizeOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def VEC_RootLookAheadMaxDepth : Option<
    "slp-max-root-look-ahead-depth",
    "The maximum look-ahead depth for searching best rooting option",
    "int"> {
  let Registry = VectorizeOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def VEC_MinProfitableStridedLoads : Option<
    "slp-min-strided-loads",
    "The minimum number of loads, which should be considered strided, "
    "if the stride is > 1 or is runtime value",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def VEC_MinProfitableStridedStores : Option<
    "slp-min-strided-stores",
    "The minimum number of stores, which should be considered strided, "
    "if the stride is > 1 or is runtime value",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def VEC_MaxProfitableStride : Option<
    "slp-max-stride",
    "The maximum stride, considered to be profitable.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def VEC_EnableStridedStores : Option<
    "slp-enable-strided-stores",
    "Enable SLP trees to be built from strided store chains.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_DisableTreeReorder : Option<
    "slp-disable-tree-reorder",
    "Disable tree reordering even if it is "
    "profitable. Used for testing only.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_ForceStridedLoads : Option<
    "slp-force-strided-loads",
    "Generate strided loads even if they are not "
    "profitable. Used for testing only.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_ViewSLPTree : Option<
    "view-slp-tree",
    "Display the SLP trees with Graphviz",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Hidden = 1;
}

def VEC_PHINodeVectorizationBudget : Option<
    "slp-phi-vectorization-budget",
    "Maximum product of vector lanes times PHI incoming values before "
    "gathering instead of vectorizing.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "1024";
  let Hidden = 1;
}

def VEC_EnableMaskedStores : Option<
    "slp-enable-masked-stores",
    "Enable vectorization of non-consecutive stores as masked stores.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def VEC_VectorizeNonPowerOf2 : Option<
    "slp-vectorize-non-power-of-2",
    "Try to vectorize with non-power-of-2 number of elements.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_ForcePostProcessStoresOperands : Option<
    "slp-postprocess-stores-operands",
    "Force operand-chain vectorization of post-process stores.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_NonVectReductions : Option<
    "slp-non-vectorizables-as-reductions",
    "Allow operand-chain vectorization through non-vectorizable reductions.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_VectorizePoorThroughput : Option<
    "slp-vectorize-poor-throughput",
    "Use poor-throughput instructions (e.g. fdiv, frem, fsqrt) as "
    "standalone vectorization seeds.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def VEC_VectorizeCopyableElements : Option<
    "slp-copyable-elements",
    "Try to replace values with the idempotent instructions for "
    "better vectorization.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def VEC_VectorizeOnceUsed : Option<
    "slp-vectorize-once-used",
    "Use instructions with the single user as standalone vectorization seeds.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "true";
  let Hidden = 1;
}

def VEC_SBVecMaxUsersToConsider : Option<
    "sbvec-max-users-to-consider",
    "Limit the number of a seed's users that getNextUserBundles() will "
    "examine as candidates for a matching bundle, to cap compilation time.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "16";
  let Hidden = 1;
}

def VEC_VectorizeReassociatedOps : Option<
    "slp-reassociate-ops",
    "Gather operands of associative binary chains into one node.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "true";
  let Hidden = 1;
}

def VEC_ReassocReorderColumnLimit : Option<
    "slp-reassociate-reorder-limit",
    "Max flattened operand columns for which associative-chain reordering "
    "runs the full operand reorder.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "32";
  let Hidden = 1;
}

def VEC_LoopAwareTripCount : Option<
    "slp-cost-loop-trip-count",
    "Loop trip count, considered by the cost model during "
    "modeling (0=loops are ignored and considered flat code)",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def VEC_SLPEnableRuntimeAliasChecks : Option<
    "slp-vectorize-with-runtime-alias-checks",
    "Allow SLP to version a block with runtime alias checks to "
    "vectorize trees blocked by may-alias memory dependencies.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// =========================================================================
// Scalar options --- LoopIdiomVectorize.cpp
// =========================================================================

def VEC_DisableLoopIdiomVectorizeAll : Option<
    "disable-loop-idiom-vectorize-all",
    "Disable Loop Idiom Vectorize Pass.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_DisableByteCmp : Option<
    "disable-loop-idiom-vectorize-bytecmp",
    "Proceed with Loop Idiom Vectorize Pass, but do "
    "not convert byte-compare loop(s).",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_ByteCmpVF : Option<
    "loop-idiom-vectorize-bytecmp-vf",
    "The vectorization factor for byte-compare patterns.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "16";
  let Hidden = 1;
}

def VEC_DisableFindFirstByte : Option<
    "disable-loop-idiom-vectorize-find-first-byte",
    "Do not convert find-first-byte loop(s).",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_VerifyLoops : Option<
    "loop-idiom-vectorize-verify",
    "Verify loops generated Loop Idiom Vectorize Pass.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

// =========================================================================
// Scalar options --- SandboxVectorizer
// =========================================================================

def VEC_PrintPassPipeline : Option<
    "sbvec-print-pass-pipeline",
    "Prints the pass pipeline and returns.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_UserDefinedPassPipeline : Option<
    "sbvec-passes",
    "Comma-separated list of vectorizer passes. If not set "
    "we run the predefined pipeline.",
    "std::string"> {
  let Registry = VectorizeOptsReg;
  let Default = "\"*\"";
  let Hidden = 1;
}

def VEC_AllowFiles : Option<
    "sbvec-allow-files",
    "Run the vectorizer only on file paths that match any in the "
    "list of comma-separated regex's.",
    "std::string"> {
  let Registry = VectorizeOptsReg;
  let Default = "\".*\"";
  let Hidden = 1;
}

def VEC_OverrideVecRegBits : Option<
    "sbvec-vec-reg-bits",
    "Override the vector register size in bits, "
    "which is otherwise found by querying TTI.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_AllowNonPow2 : Option<
    "sbvec-allow-non-pow2",
    "Allow non-power-of-2 vectorization.",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_CollectSeeds : Option<
    "sbvec-collect-seeds",
    "Collect these seeds. Use empty for none or a comma-separated "
    "list of 'stores' and 'loads'.",
    "std::string"> {
  let Registry = VectorizeOptsReg;
  let Default = "\"stores\"";
  let Hidden = 1;
}

def VEC_CostThreshold : Option<
    "sbvec-cost-threshold",
    "Vectorization cost threshold.",
    "int"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_AlwaysVerify : Option<
    "sbvec-always-verify",
    "Helps find bugs by verifying the IR whenever we "
    "emit new instructions (*very* expensive).",
    "bool"> {
  let Registry = VectorizeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def VEC_StopAt : Option<
    "sbvec-stop-at",
    "Vectorize if the invocation count is < than this. 0 "
    "disables vectorization.",
    "unsigned long"> {
  let Registry = VectorizeOptsReg;
  let Default = "std::numeric_limits<unsigned long>::max()";
  let Hidden = 1;
}

def VEC_StopBundle : Option<
    "sbvec-stop-bndl",
    "Vectorize up to this many bundles.",
    "unsigned long"> {
  let Registry = VectorizeOptsReg;
  let Default = "std::numeric_limits<unsigned long>::max()";
  let Hidden = 1;
}

def VEC_SeedBundleSizeLimit : Option<
    "sbvec-seed-bundle-size-limit",
    "Limit the size of the seed bundle to cap compilation time.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "32";
  let Hidden = 1;
}

def VEC_SeedGroupsLimit : Option<
    "sbvec-seed-groups-limit",
    "Limit the number of collected seeds groups in a BB to "
    "cap compilation time.",
    "unsigned"> {
  let Registry = VectorizeOptsReg;
  let Default = "256";
  let Hidden = 1;
}
