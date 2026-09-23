//===- VectorizeOptions.td - Options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def VectorizeOptions : OptionsStruct<"llvm"> {}

// =========================================================================
// Inline enum types
// =========================================================================

def TailFoldingPolicyTyEnum : OptionEnum<"TailFoldingPolicyTy", [
    EnumMember<"None", "dont-fold-tail">,
    EnumMember<"PreferFoldTail", "prefer-fold-tail">,
    EnumMember<"MustFoldTail", "must-fold-tail">]>;

// =========================================================================
// External enum types
// =========================================================================

// --- TargetTransformInfo.h / Analysis/TailFoldingStyle.h: TailFoldingStyle ---

def TailFoldingStyleEnum : OptionEnum<"TailFoldingStyle", [
    EnumMember<"None", "none">,
    EnumMember<"Data", "data">,
    EnumMember<"DataWithoutLaneMask", "data-without-lane-mask">,
    EnumMember<"DataAndControlFlow", "data-and-control">,
    EnumMember<"DataWithEVL", "data-with-evl">]> {
  let External = 1;
}

// --- Transforms/Vectorize/ScalableForceKind.h: ScalableForceKind ---
// Original C++ enum (unscoped): { SK_Unspecified=-1, SK_FixedWidthOnly=0,
//                                 SK_PreferScalable=1, SK_AlwaysScalable=2 }
// SK_PreferScalable has two CLI spellings ("preferred" and "on"); an
// External OptionEnum emits no `enum class` declaration of its own (only a
// spelling->value parse table using `EnumType::MemberName`, always
// qualified), so two EnumMembers sharing the same CppName is safe here.
def ScalableForceKindEnum : OptionEnum<"ScalableForceKind", [
    EnumMember<"SK_Unspecified", "__unspecified">,
    EnumMember<"SK_FixedWidthOnly", "off">,
    EnumMember<"SK_PreferScalable", "preferred">,
    EnumMember<"SK_PreferScalable", "on">,
    EnumMember<"SK_AlwaysScalable", "always">]> {
  let External = 1;
}

// --- Transforms/Vectorize/LoopIdiomVectorizeStyle.h: LoopIdiomVectorizeStyle ---

def LoopIdiomVectorizeStyleEnum : OptionEnum<"LoopIdiomVectorizeStyle", [
    EnumMember<"Masked", "masked">,
    EnumMember<"Predicated", "predicated">]> {
  let External = 1;
}

// =========================================================================
// List options
// =========================================================================

defm VEC_VPlanPrintAfterPasses : ListField<
    "vplan-print-after", "std::string",
    "Print VPlans after specified VPlan transformations (regexp).",
    /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/1>;

defm VEC_VPlanPrintBeforePasses : ListField<
    "vplan-print-before", "std::string",
    "Print VPlans before specified VPlan transformations (regexp).",
    /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/1>;

// =========================================================================
// Enum options
// =========================================================================

defm VEC_ScalableEpilogueVFCostScaleFactor : ValueField<
    "scalable-epilogue-vf-cost-scale-factor", "float", "2.0f",
    "Scale the cost of scalable epilogue VFs by this factor.",
    /*hidden=*/1>;

// std::optional<TailFoldingPolicyTy>: isTailFoldingPolicySpecified() needs a
// genuine tri-state distinct from the resolved value, so this is
// hand-authored (not EnumField<>) -- no multiclass composes std::optional<>
// field storage with FieldEnum<> tracking.
def VEC_TailFoldingPolicy : Joined<["-", "--"], "tail-folding-policy=">,
    HelpText<"Tail-folding preferences over creating an epilogue loop.">,
    Flags<[HelpHidden]>,
    Field<"std::optional<TailFoldingPolicyTy>", "VEC_TailFoldingPolicy",
          "std::nullopt">,
    FieldEnum<TailFoldingPolicyTyEnum>;
def VEC_TailFoldingPolicy_sep : Separate<["-", "--"], "tail-folding-policy">,
    Flags<[HelpHidden]>, Alias<!cast<Option>(VEC_TailFoldingPolicy)>;

// std::optional<TailFoldingPolicyTy>: isEpilogueTailFoldingPolicySpecified()
// needs the same genuine tri-state as VEC_TailFoldingPolicy above.
def VEC_EpilogueTailFoldingPolicy : Joined<["-", "--"],
    "epilogue-tail-folding-policy=">,
    HelpText<"Epilogue-tail-folding preferences over creating an epilogue "
             "loop.">,
    Flags<[HelpHidden]>,
    Field<"std::optional<TailFoldingPolicyTy>",
          "VEC_EpilogueTailFoldingPolicy", "std::nullopt">,
    FieldEnum<TailFoldingPolicyTyEnum>;
def VEC_EpilogueTailFoldingPolicy_sep : Separate<["-", "--"],
    "epilogue-tail-folding-policy">,
    Flags<[HelpHidden]>, Alias<!cast<Option>(VEC_EpilogueTailFoldingPolicy)>;

// std::optional<TailFoldingStyle>: isForceTailFoldingStyleSpecified() needs
// the same genuine tri-state as the two options above. Not hidden, matching
// the old schema (no Hidden = 1 on this option).
def VEC_ForceTailFoldingStyle : Joined<["-", "--"],
    "force-tail-folding-style=">,
    HelpText<"Force the tail folding style">,
    Field<"std::optional<TailFoldingStyle>", "VEC_ForceTailFoldingStyle",
          "std::nullopt">,
    FieldEnum<TailFoldingStyleEnum>;
def VEC_ForceTailFoldingStyle_sep : Separate<["-", "--"],
    "force-tail-folding-style">,
    Flags<[HelpHidden]>, Alias<!cast<Option>(VEC_ForceTailFoldingStyle)>;

// Plain field with a corrected literal default: no separate "was it
// specified" helper exists for this option (LoopVectorizeHints compares the
// resolved value against SK_Unspecified directly), so the old schema's
// implicit/incorrect default is fixed here rather than introducing
// std::optional<>.
defm VEC_ForceScalableVectorization : EnumField<
    "scalable-vectorization", ScalableForceKindEnum,
    "ScalableForceKind::SK_Unspecified",
    "Control whether the compiler can use scalable vectors to "
    "vectorize a loop",
    /*hidden=*/1>;

defm VEC_LoopIdiomVectorizeStyleOpt : EnumField<
    "loop-idiom-vectorize-style", LoopIdiomVectorizeStyleEnum,
    "LoopIdiomVectorizeStyle::Masked",
    "The vectorization style for loop idiom transform.",
    /*hidden=*/1>;

// =========================================================================
// Scalar options --- LoopVectorize.cpp
// =========================================================================

defm VEC_EnableEpilogueVectorization : BoolField<
    "enable-epilogue-vectorization", "true",
    "Enable vectorization of epilogue loops.", /*hidden=*/1>;

// Plain field with a corrected literal default (ElementCount::getFixed(1)):
// no separate "was it specified" helper exists for this option, and the
// fallback used at the call site is a fixed literal, not runtime-computed.
defm VEC_EpilogueVectorizationForceVF : ValueField<
    "epilogue-vectorization-force-VF", "ElementCount",
    "ElementCount::getFixed(1)",
    "When epilogue vectorization is enabled, and a value greater than "
    "1 is specified, forces the given VF for all applicable epilogue "
    "loops. Note: This allows all scalable VFs >= vscale x 1.",
    /*hidden=*/1>;

// std::optional<unsigned>: isEpilogueVectorizationMinVFSpecified() needs a
// genuine tri-state distinct from the resolved value.
defm VEC_EpilogueVectorizationMinVF : OptionalField<
    "epilogue-vectorization-minimum-VF", "unsigned",
    "Only loops with vectorization factor equal to or larger than "
    "the specified value are considered for epilogue vectorization.",
    /*hidden=*/1>;

defm VEC_TinyTripCountVectorThreshold : ValueField<
    "vectorizer-min-trip-count", "unsigned", "16",
    "Loops with a constant trip count that is smaller than this "
    "value are vectorized only if no scalar iteration overheads "
    "are incurred.",
    /*hidden=*/1>;

defm VEC_VectorizeMemoryCheckThreshold : ValueField<
    "vectorize-memory-check-threshold", "unsigned", "128",
    "The maximum allowed number of runtime memory checks",
    /*hidden=*/1>;

defm VEC_EnableWideActiveLaneMask : BoolField<
    "enable-wide-lane-mask", "false",
    "Enable use of wide lane masks when used for control flow in "
    "tail-folded loops",
    /*hidden=*/1>;

// TODO: Remove this once the partial reduction intrinsics are no worse than
//       normal vector operations.
defm VEC_UsePartialReductionsByDefault : BoolField<
    "use-partial-reductions-by-default", "false",
    "Use partial reduction intrinsics for all supported unordered "
    "reductions.",
    /*hidden=*/1>;

// std::optional<bool>: isEnableInterleavedMemAccessesSpecified() needs a
// genuine tri-state distinct from the resolved value. The original
// cl::opt<bool> accepted the bare flag spelling (implying true), so this
// needs OptionalValueField, not OptionalField, to keep that working.
defm VEC_EnableInterleavedMemAccesses : OptionalValueField<
    "enable-interleaved-mem-accesses", "bool", "true",
    "Enable vectorization on interleaved memory accesses in a loop",
    /*hidden=*/1>;

// std::optional<bool>: isEnableMaskedInterleavedMemAccessesSpecified() needs
// a genuine tri-state distinct from the resolved value. The original
// cl::opt<bool> accepted the bare flag spelling (implying true), so this
// needs OptionalValueField, not OptionalField, to keep that working.
defm VEC_EnableMaskedInterleavedMemAccesses : OptionalValueField<
    "enable-masked-interleaved-mem-accesses", "bool", "true",
    "Enable vectorization on masked interleaved memory accesses in a loop",
    /*hidden=*/1>;

// std::optional<unsigned>: isForceTargetNumScalarRegsSpecified() needs a
// genuine tri-state distinct from the resolved value.
defm VEC_ForceTargetNumScalarRegs : OptionalField<
    "force-target-num-scalar-regs", "unsigned",
    "A flag that overrides the target's number of scalar registers.",
    /*hidden=*/1>;

// std::optional<unsigned>: isForceTargetNumVectorRegsSpecified() needs a
// genuine tri-state distinct from the resolved value.
defm VEC_ForceTargetNumVectorRegs : OptionalField<
    "force-target-num-vector-regs", "unsigned",
    "A flag that overrides the target's number of vector registers.",
    /*hidden=*/1>;

// std::optional<unsigned>: isForceTargetMaxScalarInterleaveFactorSpecified()
// needs a genuine tri-state distinct from the resolved value.
defm VEC_ForceTargetMaxScalarInterleaveFactor : OptionalField<
    "force-target-max-scalar-interleave", "unsigned",
    "A flag that overrides the target's max interleave factor for "
    "scalar loops.",
    /*hidden=*/1>;

// std::optional<unsigned>: isForceTargetMaxVectorInterleaveFactorSpecified()
// needs a genuine tri-state distinct from the resolved value.
defm VEC_ForceTargetMaxVectorInterleaveFactor : OptionalField<
    "force-target-max-vector-interleave", "unsigned",
    "A flag that overrides the target's max interleave factor for "
    "vectorized loops.",
    /*hidden=*/1>;

// std::optional<unsigned>: isForceTargetInstructionCostSpecified() needs a
// genuine tri-state distinct from the resolved value (used at 3 call sites
// across LoopVectorize.cpp, VPlanRecipes.cpp and VPlan.cpp).
defm VEC_ForceTargetInstructionCost : OptionalField<
    "force-target-instruction-cost", "unsigned",
    "A flag that overrides the target's expected cost for "
    "an instruction to a single constant value. Mostly "
    "useful for getting consistent testing.",
    /*hidden=*/1>;

defm VEC_SmallLoopCost : ValueField<
    "small-loop-cost", "unsigned", "20",
    "The cost of a loop that is considered 'small' by the interleaver.",
    /*hidden=*/1>;

defm VEC_LoopVectorizeWithBlockFrequency : BoolField<
    "loop-vectorize-with-block-frequency", "true",
    "Enable the use of the block frequency analysis to access PGO "
    "heuristics minimizing code growth in cold regions and being more "
    "aggressive in hot regions.",
    /*hidden=*/1>;

defm VEC_EnableLoadStoreRuntimeInterleave : BoolField<
    "enable-loadstore-runtime-interleave", "true",
    "Enable runtime interleaving until load/store ports are saturated",
    /*hidden=*/1>;

defm VEC_NumberOfStoresToPredicate : ValueField<
    "vectorize-num-stores-pred", "unsigned", "1",
    "Max number of stores to be predicated behind an if.",
    /*hidden=*/1>;

defm VEC_EnableIndVarRegisterHeur : BoolField<
    "enable-ind-var-reg-heur", "true",
    "Count the induction variable only once when interleaving",
    /*hidden=*/1>;

defm VEC_MaxNestedScalarReductionIC : ValueField<
    "max-nested-scalar-reduction-interleave", "unsigned", "2",
    "The maximum interleave count to use when interleaving a scalar "
    "reduction in a nested loop.",
    /*hidden=*/1>;

// std::optional<bool>: isForceOrderedReductionsSpecified() needs a genuine
// tri-state distinct from the resolved value. The original cl::opt<bool>
// accepted the bare flag spelling (implying true), so this needs
// OptionalValueField, not OptionalField, to keep that working.
defm VEC_ForceOrderedReductions : OptionalValueField<
    "force-ordered-reductions", "bool", "true",
    "Enable the vectorisation of loops with in-order (strict) "
    "FP reductions",
    /*hidden=*/1>;

defm VEC_PreferPredicatedReductionSelect : BoolField<
    "prefer-predicated-reduction-select", "false",
    "Prefer predicating a reduction operation over an after loop select.",
    /*hidden=*/1>;

defm VEC_EnableVPlanNativePath : BoolField<
    "enable-vplan-native-path", "false",
    "Enable VPlan-native vectorization path with "
    "support for outer loop vectorization.",
    /*hidden=*/1>;

defm VEC_VerifyEachVPlan : BoolField<
    "vplan-verify-each", "false",
    "Verify VPlans after VPlan transforms.", /*hidden=*/1>;

defm VEC_VPlanPrintAfterAll : BoolField<
    "vplan-print-after-all", "false",
    "Print VPlans after all VPlan transformations.", /*hidden=*/1>;

defm VEC_VPlanPrintBeforeAll : BoolField<
    "vplan-print-before-all", "false",
    "Print VPlans before all VPlan transformations.", /*hidden=*/1>;

defm VEC_VPlanPrintVectorRegionScope : BoolField<
    "vplan-print-vector-region-scope", "false",
    "Limit VPlan printing to vector loop region in "
    "`-vplan-print-after*` if the plan has one.",
    /*hidden=*/1>;

defm VEC_VPlanBuildStressTest : BoolField<
    "vplan-build-outerloop-stress-test", "false",
    "Build VPlan for every supported loop nest in the function and bail "
    "out right after the build (stress test the VPlan H-CFG construction "
    "in the VPlan-native vectorization path).",
    /*hidden=*/1>;

defm VEC_EnableLoopInterleaving : BoolField<
    "interleave-loops", "true",
    "Enable loop interleaving in Loop vectorization passes",
    /*hidden=*/1>;

defm VEC_EnableLoopVectorization : BoolField<
    "vectorize-loops", "true",
    "Run the Loop vectorization passes", /*hidden=*/1>;

// std::optional<bool>: mirrors the old cl::boolOrDefault tri-state semantics
// (unset / true / false), same rationale as CodeGenPassOptionsCore1.td's
// CGPASS_EnableTailMerge and friends.
defm VEC_ForceMaskedDivRem : OptionalValueField<
    "force-widen-divrem-via-masked-intrinsic", "bool", "true",
    "Override cost based masked intrinsic widening "
    "for div/rem instructions",
    /*hidden=*/1>;

defm VEC_EnableEarlyExitVectorization : BoolField<
    "enable-early-exit-vectorization", "true",
    "Enable vectorization of early exit loops with uncountable exits.",
    /*hidden=*/1>;

defm VEC_EnableEarlyExitVectorizationWithSideEffects : BoolField<
    "enable-early-exit-vectorization-with-side-effects", "false",
    "Enable vectorization of early exit loops with side effects.",
    /*hidden=*/1>;

defm VEC_ForcePartialAliasingVectorization : BoolField<
    "force-partial-aliasing-vectorization", "false",
    "Force vectorization with partial aliasing support.",
    /*hidden=*/1>;

// =========================================================================
// Scalar options --- VPlanRecipes.cpp
// =========================================================================

defm VEC_VPlanPrintMetadata : BoolField<
    "vplan-print-metadata", "true",
    "Print metadata in VPlan output.", /*hidden=*/1>;

// =========================================================================
// Scalar options --- VPlan.cpp
// =========================================================================

defm VEC_PrintVPlansInDotFormat : BoolField<
    "vplan-print-in-dot-format", "false",
    "Use dot format instead of plain text when dumping VPlans",
    /*hidden=*/1>;

// =========================================================================
// Scalar options --- LoopVectorizationPlanner.cpp
// =========================================================================

// std::optional<bool>: isMaximizeBandwidthSpecified() needs a genuine
// tri-state distinct from the resolved value. The original cl::opt<bool>
// accepted the bare "-vectorizer-maximize-bandwidth" spelling (implying
// true), so this needs OptionalValueField, not OptionalField, to keep
// that bare-flag spelling working.
defm VEC_MaximizeBandwidth : OptionalValueField<
    "vectorizer-maximize-bandwidth", "bool", "true",
    "Maximize bandwidth when selecting vectorization factor which "
    "will be determined by the smallest type in loop.",
    /*hidden=*/1>;

defm VEC_UseWiderVFIfCallVariantsPresent : BoolField<
    "vectorizer-maximize-bandwidth-for-vector-calls", "true",
    "Try wider VFs if they enable the use of vector variants",
    /*hidden=*/1>;

// std::optional<bool>: isConsiderRegPressureSpecified() needs a genuine
// tri-state distinct from the resolved value. The original cl::opt<bool>
// accepted the bare "-vectorizer-consider-reg-pressure" spelling (implying
// true), so this needs OptionalValueField, not OptionalField, to keep
// that bare-flag spelling working.
defm VEC_ConsiderRegPressure : OptionalValueField<
    "vectorizer-consider-reg-pressure", "bool", "true",
    "Discard VFs if their register pressure is too high.",
    /*hidden=*/1>;

defm VEC_ForceTargetSupportsScalableVectors : BoolField<
    "force-target-supports-scalable-vectors", "false",
    "Pretend that scalable vectors are supported, even if the target does "
    "not support them. This flag should only be used for testing.",
    /*hidden=*/1>;

defm VEC_PreferInLoopReductions : BoolField<
    "prefer-inloop-reductions", "false",
    "Prefer in-loop vector reductions, "
    "overriding the targets preference.",
    /*hidden=*/1>;

defm VEC_ForceTargetSupportsGatherScatterOps : BoolField<
    "force-target-supports-gather-scatter-ops", "false",
    "Pretend that gather/scatter operations are supported, even if the "
    "target does not support them. This flag should only be used for "
    "testing.",
    /*hidden=*/1>;

defm VEC_ForceTargetSupportsMaskedMemoryOps : BoolField<
    "force-target-supports-masked-memory-ops", "false",
    "Assume the target supports masked memory operations (used for "
    "testing).",
    /*hidden=*/1>;

// =========================================================================
// Scalar options --- LoopVectorizationLegality.cpp
// =========================================================================

defm VEC_EnableIfConversion : BoolField<
    "enable-if-conversion", "true",
    "Enable if-conversion during vectorization.", /*hidden=*/1>;

defm VEC_AllowStridedPointerIVs : BoolField<
    "lv-strided-pointer-ivs", "false",
    "Enable recognition of non-constant strided "
    "pointer induction variables.",
    /*hidden=*/1>;

defm VEC_HintsAllowReordering : BoolField<
    "hints-allow-reordering", "true",
    "Allow enabling loop hints to reorder "
    "FP operations during vectorization.",
    /*hidden=*/1>;

defm VEC_VectorizeSCEVCheckThreshold : ValueField<
    "vectorize-scev-check-threshold", "unsigned", "16",
    "The maximum number of SCEV checks allowed.",
    /*hidden=*/1>;

defm VEC_PragmaVectorizeSCEVCheckThreshold : ValueField<
    "pragma-vectorize-scev-check-threshold", "unsigned", "128",
    "The maximum number of SCEV checks allowed with a "
    "vectorize(enable) pragma",
    /*hidden=*/1>;

defm VEC_EnableHistogramVectorization : BoolField<
    "enable-histogram-loop-vectorization", "false",
    "Enables autovectorization of some loops containing histograms",
    /*hidden=*/1>;

// =========================================================================
// Scalar options --- VectorCombine.cpp
// =========================================================================

defm VEC_DisableVectorCombine : BoolField<
    "disable-vector-combine", "false",
    "Disable all vector combine transforms", /*hidden=*/1>;

defm VEC_DisableBinopExtractShuffle : BoolField<
    "disable-binop-extract-shuffle", "false",
    "Disable binop extract to shuffle transforms", /*hidden=*/1>;

defm VEC_MaxInstrsToScan : ValueField<
    "vector-combine-max-scan-instrs", "unsigned", "30",
    "Max number of instructions to scan for vector combining.",
    /*hidden=*/1>;

// =========================================================================
// Scalar options --- SLPVectorizer.cpp
// =========================================================================

defm VEC_RunSLPVectorization : BoolField<
    "vectorize-slp", "true",
    "Run the SLP vectorization passes", /*hidden=*/1>;

defm VEC_SLPReVec : BoolField<
    "slp-revec", "false",
    "Enable vectorization for wider vector utilization", /*hidden=*/1>;

// std::optional<int>: isSLPCostThresholdSpecified() needs a genuine
// tri-state distinct from the resolved value.
defm VEC_SLPCostThreshold : OptionalField<
    "slp-threshold", "int",
    "Only vectorize if you gain more than this number ",
    /*hidden=*/1>;

defm VEC_ShouldVectorizeHor : BoolField<
    "slp-vectorize-hor", "true",
    "Attempt to vectorize horizontal reductions", /*hidden=*/1>;

defm VEC_ShouldStartVectorizeHorAtStore : BoolField<
    "slp-vectorize-hor-store", "false",
    "Attempt to vectorize horizontal reductions feeding into a store",
    /*hidden=*/1>;

defm VEC_SplitAlternateInstructions : BoolField<
    "slp-split-alternate-instructions", "true",
    "Improve the code quality by splitting alternate instructions",
    /*hidden=*/1>;

defm VEC_SLPInstCountCheck : BoolField<
    "slp-inst-count-check", "true",
    "Reject vectorization if vector instruction count exceeds "
    "scalar instruction count",
    /*hidden=*/1>;

// std::optional<int>: isMaxVectorRegSizeOptionSpecified() needs a genuine
// tri-state distinct from the resolved value.
defm VEC_MaxVectorRegSizeOption : OptionalField<
    "slp-max-reg-size", "int",
    "Attempt to vectorize for this register size in bits",
    /*hidden=*/1>;

// std::optional<unsigned>: isMaxVFOptionSpecified() needs a genuine
// tri-state distinct from the resolved value.
defm VEC_MaxVFOption : OptionalField<
    "slp-max-vf", "unsigned",
    "Maximum SLP vectorization factor (0=unlimited)",
    /*hidden=*/1>;

defm VEC_ScheduleRegionSizeBudget : ValueField<
    "slp-schedule-budget", "int", "100000",
    "Limit the size of the SLP scheduling region per block",
    /*hidden=*/1>;

defm VEC_PerLaneGatherScale : BoolField<
    "slp-per-lane-gather-scale", "true",
    "Use per-lane execution scale for gather/buildvector tree entries to "
    "model LICM-hoistable buildvector sequences.",
    /*hidden=*/1>;

defm VEC_SLPMaxRuntimeAliasChecks : ValueField<
    "slp-max-runtime-alias-checks", "unsigned", "8",
    "The maximum number of runtime alias checks generated to guard a single "
    "SLP-vectorized region.",
    /*hidden=*/1>;

defm VEC_SLPRuntimeAliasChecksMaxScalarCostPercent : ValueField<
    "slp-runtime-alias-checks-max-scalar-cost-percent", "unsigned", "25",
    "Maximum SLP runtime alias check cost, as a percentage of the guarded "
    "scalar region cost, before versioning is rejected to avoid pessimizing "
    "the scalar fallback path.",
    /*hidden=*/1>;

// std::optional<int>: isMinVectorRegSizeOptionSpecified() needs a genuine
// tri-state distinct from the resolved value.
defm VEC_MinVectorRegSizeOption : OptionalField<
    "slp-min-reg-size", "int",
    "Attempt to vectorize for this register size in bits",
    /*hidden=*/1>;

defm VEC_RecursionMaxDepth : ValueField<
    "slp-recursion-max-depth", "unsigned", "12",
    "Limit the recursion depth when building a vectorizable tree",
    /*hidden=*/1>;

defm VEC_MinTreeSize : ValueField<
    "slp-min-tree-size", "unsigned", "3",
    "Only vectorize small trees if they are fully vectorizable",
    /*hidden=*/1>;

defm VEC_LookAheadMaxDepth : ValueField<
    "slp-max-look-ahead-depth", "int", "2",
    "The maximum look-ahead depth for operand reordering scores",
    /*hidden=*/1>;

defm VEC_RootLookAheadMaxDepth : ValueField<
    "slp-max-root-look-ahead-depth", "int", "2",
    "The maximum look-ahead depth for searching best rooting option",
    /*hidden=*/1>;

defm VEC_MinProfitableStridedLoads : ValueField<
    "slp-min-strided-loads", "unsigned", "2",
    "The minimum number of loads, which should be considered strided, "
    "if the stride is > 1 or is runtime value",
    /*hidden=*/1>;

defm VEC_MinProfitableStridedStores : ValueField<
    "slp-min-strided-stores", "unsigned", "2",
    "The minimum number of stores, which should be considered strided, "
    "if the stride is > 1 or is runtime value",
    /*hidden=*/1>;

defm VEC_MaxProfitableStride : ValueField<
    "slp-max-stride", "unsigned", "8",
    "The maximum stride, considered to be profitable.",
    /*hidden=*/1>;

defm VEC_EnableStridedStores : BoolField<
    "slp-enable-strided-stores", "false",
    "Enable SLP trees to be built from strided store chains.",
    /*hidden=*/1>;

defm VEC_DisableTreeReorder : BoolField<
    "slp-disable-tree-reorder", "false",
    "Disable tree reordering even if it is "
    "profitable. Used for testing only.",
    /*hidden=*/1>;

defm VEC_ForceStridedLoads : BoolField<
    "slp-force-strided-loads", "false",
    "Generate strided loads even if they are not "
    "profitable. Used for testing only.",
    /*hidden=*/1>;

defm VEC_ViewSLPTree : BoolField<
    "view-slp-tree", "false",
    "Display the SLP trees with Graphviz", /*hidden=*/1>;

defm VEC_PHINodeVectorizationBudget : ValueField<
    "slp-phi-vectorization-budget", "unsigned", "1024",
    "Maximum product of vector lanes times PHI incoming values before "
    "gathering instead of vectorizing.",
    /*hidden=*/1>;

defm VEC_EnableMaskedStores : BoolField<
    "slp-enable-masked-stores", "true",
    "Enable vectorization of non-consecutive stores as masked stores.",
    /*hidden=*/1>;

defm VEC_VectorizeNonPowerOf2 : BoolField<
    "slp-vectorize-non-power-of-2", "false",
    "Try to vectorize with non-power-of-2 number of elements.",
    /*hidden=*/1>;

// Was silently dead pre-migration: SLPVectorizer.cpp read a bare, never
// assigned local static instead of any option getter (no consumer function
// existed at all). Fixed as part of this migration: the flag now genuinely
// controls the behavior its help text describes.
defm VEC_ForcePostProcessStoresOperands : BoolField<
    "slp-postprocess-stores-operands", "false",
    "Force operand-chain vectorization of post-process stores.",
    /*hidden=*/1>;

defm VEC_NonVectReductions : BoolField<
    "slp-non-vectorizables-as-reductions", "false",
    "Allow operand-chain vectorization through non-vectorizable reductions.",
    /*hidden=*/1>;

defm VEC_VectorizePoorThroughput : BoolField<
    "slp-vectorize-poor-throughput", "true",
    "Use poor-throughput instructions (e.g. fdiv, frem, fsqrt) as "
    "standalone vectorization seeds.",
    /*hidden=*/1>;

defm VEC_VectorizeCopyableElements : BoolField<
    "slp-copyable-elements", "true",
    "Try to replace values with the idempotent instructions for "
    "better vectorization.",
    /*hidden=*/1>;

defm VEC_VectorizeOnceUsed : BoolField<
    "slp-vectorize-once-used", "true",
    "Use instructions with the single user as standalone vectorization "
    "seeds.",
    /*hidden=*/1>;

defm VEC_SBVecMaxUsersToConsider : ValueField<
    "sbvec-max-users-to-consider", "unsigned", "16",
    "Limit the number of a seed's users that getNextUserBundles() will "
    "examine as candidates for a matching bundle, to cap compilation time.",
    /*hidden=*/1>;

defm VEC_VectorizeReassociatedOps : BoolField<
    "slp-reassociate-ops", "true",
    "Gather operands of associative binary chains into one node.",
    /*hidden=*/1>;

defm VEC_ReassocReorderColumnLimit : ValueField<
    "slp-reassociate-reorder-limit", "unsigned", "32",
    "Max flattened operand columns for which associative-chain reordering "
    "runs the full operand reorder.",
    /*hidden=*/1>;

defm VEC_LoopAwareTripCount : ValueField<
    "slp-cost-loop-trip-count", "unsigned", "2",
    "Loop trip count, considered by the cost model during "
    "modeling (0=loops are ignored and considered flat code)",
    /*hidden=*/1>;

defm VEC_SLPEnableRuntimeAliasChecks : BoolField<
    "slp-vectorize-with-runtime-alias-checks", "true",
    "Allow SLP to version a block with runtime alias checks to "
    "vectorize trees blocked by may-alias memory dependencies.",
    /*hidden=*/1>;

// =========================================================================
// Scalar options --- LoopIdiomVectorize.cpp
// =========================================================================

defm VEC_DisableLoopIdiomVectorizeAll : BoolField<
    "disable-loop-idiom-vectorize-all", "false",
    "Disable Loop Idiom Vectorize Pass.", /*hidden=*/1>;

defm VEC_DisableByteCmp : BoolField<
    "disable-loop-idiom-vectorize-bytecmp", "false",
    "Proceed with Loop Idiom Vectorize Pass, but do "
    "not convert byte-compare loop(s).",
    /*hidden=*/1>;

defm VEC_ByteCmpVF : ValueField<
    "loop-idiom-vectorize-bytecmp-vf", "unsigned", "16",
    "The vectorization factor for byte-compare patterns.",
    /*hidden=*/1>;

defm VEC_DisableFindFirstByte : BoolField<
    "disable-loop-idiom-vectorize-find-first-byte", "false",
    "Do not convert find-first-byte loop(s).", /*hidden=*/1>;

defm VEC_VerifyLoops : BoolField<
    "loop-idiom-vectorize-verify", "false",
    "Verify loops generated Loop Idiom Vectorize Pass.", /*hidden=*/1>;

// =========================================================================
// Scalar options --- SandboxVectorizer
// =========================================================================

defm VEC_PrintPassPipeline : BoolField<
    "sbvec-print-pass-pipeline", "false",
    "Prints the pass pipeline and returns.", /*hidden=*/1>;

defm VEC_UserDefinedPassPipeline : ValueField<
    "sbvec-passes", "std::string", "\"*\"",
    "Comma-separated list of vectorizer passes. If not set "
    "we run the predefined pipeline.",
    /*hidden=*/1>;

defm VEC_AllowFiles : ValueField<
    "sbvec-allow-files", "std::string", "\".*\"",
    "Run the vectorizer only on file paths that match any in the "
    "list of comma-separated regex's.",
    /*hidden=*/1>;

defm VEC_OverrideVecRegBits : ValueField<
    "sbvec-vec-reg-bits", "unsigned", "0",
    "Override the vector register size in bits, "
    "which is otherwise found by querying TTI.",
    /*hidden=*/1>;

defm VEC_AllowNonPow2 : BoolField<
    "sbvec-allow-non-pow2", "false",
    "Allow non-power-of-2 vectorization.", /*hidden=*/1>;

defm VEC_CollectSeeds : ValueField<
    "sbvec-collect-seeds", "std::string", "\"stores\"",
    "Collect these seeds. Use empty for none or a comma-separated "
    "list of 'stores' and 'loads'.",
    /*hidden=*/1>;

defm VEC_CostThreshold : ValueField<
    "sbvec-cost-threshold", "int", "0",
    "Vectorization cost threshold.", /*hidden=*/1>;

defm VEC_AlwaysVerify : BoolField<
    "sbvec-always-verify", "false",
    "Helps find bugs by verifying the IR whenever we "
    "emit new instructions (*very* expensive).",
    /*hidden=*/1>;

defm VEC_StopAt : ValueField<
    "sbvec-stop-at", "uint64_t", "std::numeric_limits<uint64_t>::max()",
    "Vectorize if the invocation count is < than this. 0 "
    "disables vectorization.",
    /*hidden=*/1>;

defm VEC_StopBundle : ValueField<
    "sbvec-stop-bndl", "uint64_t", "std::numeric_limits<uint64_t>::max()",
    "Vectorize up to this many bundles.", /*hidden=*/1>;

defm VEC_SeedBundleSizeLimit : ValueField<
    "sbvec-seed-bundle-size-limit", "unsigned", "32",
    "Limit the size of the seed bundle to cap compilation time.",
    /*hidden=*/1>;

defm VEC_SeedGroupsLimit : ValueField<
    "sbvec-seed-groups-limit", "unsigned", "256",
    "Limit the number of collected seeds groups in a BB to "
    "cap compilation time.",
    /*hidden=*/1>;
