//===- CodeGenPassOptionsCore1.td - Core1 option defs -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def CodeGenCore1Options : OptionsStruct<"llvm"> {}

// =========================================================================
// Options — AssignmentTrackingAnalysis.cpp
// =========================================================================

defm CGPASS_DebugAtaMaxBlocks : ValueField<"debug-ata-max-blocks", "unsigned",
    "10000u", "Maximum num basic blocks before debug info dropped",
    /*hidden=*/1>;

defm CGPASS_MemLocFragFill : BoolField<"mem-loc-frag-fill", "true", "",
    /*hidden=*/1>;

defm CGPASS_PrintDebugAta : BoolField<"print-debug-ata", "false", "",
    /*hidden=*/1>;

// std::optional<bool>: shouldCoalesceFragments needs to distinguish "left
// unset" (defer to whether instruction-referencing is enabled) from
// "explicitly specified" (including explicitly false) -- the same tri-state
// cl::boolOrDefault semantics as clv2's original CGPASS_DebugAtaCoalesceFrags.
defm CGPASS_DebugAtaCoalesceFrags : OptionalValueField<
    "debug-ata-coalesce-frags", "bool", "true", "", /*hidden=*/1>;

// =========================================================================
// Options — BasicBlockMatchingAndInference.cpp
// =========================================================================

defm CGPASS_PropellerInferThreshold : ValueField<"propeller-infer-threshold",
    "float", "0.6f", "Threshold for infer stale profile">;

// =========================================================================
// Options — BasicBlockSections.cpp
// =========================================================================

defm CGPASS_BbsectionsDetectSourceDrift : BoolField<
    "bbsections-detect-source-drift", "true",
    "This checks if there is a fdo instr. profile hash mismatch for this "
    "function", /*hidden=*/1>;

// =========================================================================
// Options — BasicTargetTransformInfo.cpp
// =========================================================================

// std::optional<unsigned>: getUnrollingPreferences needs to distinguish
// "left unset" (fall back to the subtarget's scheduling-model buffer size)
// from "explicitly specified"; clv2's original getPartialUnrollingThreshold /
// getPartialUnrollingThresholdWasSpecified pair used a fallback of 0 when
// unspecified, reused here as the bare-flag value.
defm CGPASS_PartialUnrollingThreshold : OptionalValueField<
    "partial-unrolling-threshold", "unsigned", "0u",
    "Threshold for partial unrolling", /*hidden=*/1>;

// =========================================================================
// Options — BranchFolding.cpp
// =========================================================================

// std::optional<bool>: BranchFolder needs to distinguish "left unset" (defer
// to the target's own preference) from "explicitly specified" -- the same
// tri-state cl::boolOrDefault semantics as clv2's original
// CGPASS_EnableTailMerge.
defm CGPASS_EnableTailMerge : OptionalValueField<"enable-tail-merge", "bool",
    "true", "", /*hidden=*/1>;

defm CGPASS_TailMergeThreshold : ValueField<"tail-merge-threshold",
    "unsigned", "150u",
    "Max number of predecessors to consider tail merging", /*hidden=*/1>;

// std::optional<unsigned>: BranchFolder::OptimizeFunction needs to
// distinguish "left unset" (use the pass's own computed default) from
// "explicitly specified".
defm CGPASS_TailMergeSize : OptionalValueField<"tail-merge-size", "unsigned",
    "3u", "Min number of instructions to consider tail merging",
    /*hidden=*/1>;

// std::optional<bool>: BranchFolder::OptimizeFunction needs to distinguish
// "left unset" from "explicitly specified" -- the same tri-state
// cl::boolOrDefault semantics as clv2's original CGPASS_EnableHoistCommonCode.
defm CGPASS_EnableHoistCommonCode : OptionalValueField<
    "branch-folder-hoist-common-code", "bool", "true",
    "Override common-code hoisting in the BranchFolding pass",
    /*hidden=*/1>;

// std::optional<bool>: BranchFolder::OptimizeFunction needs to distinguish
// "left unset" from "explicitly specified" -- the same tri-state
// cl::boolOrDefault semantics as clv2's original CGPASS_EnableBlockReordering.
defm CGPASS_EnableBlockReordering : OptionalValueField<
    "branch-folder-reorder-blocks", "bool", "true",
    "Override basic-block reordering in the BranchFolding pass",
    /*hidden=*/1>;

// =========================================================================
// Options — CFIInstrInserter.cpp
// =========================================================================

defm CGPASS_VerifyCfiinstrs : BoolField<"verify-cfiinstrs", "false",
    "Verify Call Frame Information instructions", /*hidden=*/1>;

// =========================================================================
// Options — CodeGenPrepare.cpp
// =========================================================================

// std::optional<bool>: optimizeMemoryInst needs to distinguish "left unset"
// (defer to the subtarget's own addrSinkUsingGEPs() preference) from
// "explicitly specified" (including explicitly false).
defm CGPASS_AddrSinkUsingGep : OptionalValueField<"addr-sink-using-gep",
    "bool", "true", "Address sinking in CGP using GEPs.", /*hidden=*/1>;

defm CGPASS_DisableCgpBranchOpts : BoolField<"disable-cgp-branch-opts",
    "false", "Disable branch optimizations in CodeGenPrepare",
    /*hidden=*/1>;

defm CGPASS_DisableCgpGcOpts : BoolField<"disable-cgp-gc-opts", "false",
    "Disable GC optimizations in CodeGenPrepare", /*hidden=*/1>;

defm CGPASS_DisableCgpSelect2branch : BoolField<"disable-cgp-select2branch",
    "false", "Disable select to branch conversion.", /*hidden=*/1>;

defm CGPASS_EnableAndcmpSinking : BoolField<"enable-andcmp-sinking", "true",
    "Enable sinking and/cmp into branches.", /*hidden=*/1>;

defm CGPASS_DisableCgpStoreExtract : BoolField<"disable-cgp-store-extract",
    "false", "Disable store(extract) optimizations in CodeGenPrepare",
    /*hidden=*/1>;

defm CGPASS_StressCgpStoreExtract : BoolField<"stress-cgp-store-extract",
    "false", "Stress test store(extract) optimizations in CodeGenPrepare",
    /*hidden=*/1>;

defm CGPASS_DisableCgpExtLdPromotion : BoolField<
    "disable-cgp-ext-ld-promotion", "false",
    "Disable ext(promotable(ld)) -> promoted(ext(ld)) optimization in "
    "CodeGenPrepare", /*hidden=*/1>;

defm CGPASS_StressCgpExtLdPromotion : BoolField<"stress-cgp-ext-ld-promotion",
    "false",
    "Stress test ext(promotable(ld)) -> promoted(ext(ld)) optimization in "
    "CodeGenPrepare", /*hidden=*/1>;

defm CGPASS_DisablePreheaderProt : BoolField<"disable-preheader-prot",
    "false", "Disable protection against removing loop preheaders",
    /*hidden=*/1>;

defm CGPASS_ProfileGuidedSectionPrefix : BoolField<
    "profile-guided-section-prefix", "true",
    "Use profile info to add section prefix for hot/cold functions",
    /*hidden=*/1>;

defm CGPASS_ProfileUnknownInSpecialSection : BoolField<
    "profile-unknown-in-special-section", "false",
    "In profiling mode like sampleFDO, put profile unknown functions into "
    "a special section", /*hidden=*/1>;

defm CGPASS_BbsectionsGuidedSectionPrefix : BoolField<
    "bbsections-guided-section-prefix", "true",
    "Use the basic-block-sections profile to determine the text section "
    "prefix for hot functions.", /*hidden=*/1>;

defm CGPASS_CgpFreqRatioToSkipMerge : ValueField<
    "cgp-freq-ratio-to-skip-merge", "uint64_t", "2",
    "Skip merging empty blocks if (frequency of empty block) / (frequency "
    "of destination block) is greater than this ratio", /*hidden=*/1>;

defm CGPASS_ForceSplitStore : BoolField<"force-split-store", "false",
    "Force store splitting no matter what the target query says.",
    /*hidden=*/1>;

defm CGPASS_CgpTypePromotionMerge : BoolField<"cgp-type-promotion-merge",
    "true",
    "Enable merging of redundant sexts when one is dominating the other.",
    /*hidden=*/1>;

defm CGPASS_DisableComplexAddrModes : BoolField<"disable-complex-addr-modes",
    "false",
    "Disables combining addressing modes with different parts in "
    "optimizeMemoryInst.", /*hidden=*/1>;

defm CGPASS_AddrSinkNewPhis : BoolField<"addr-sink-new-phis", "false",
    "Allow creation of Phis in Address sinking.", /*hidden=*/1>;

defm CGPASS_AddrSinkNewSelect : BoolField<"addr-sink-new-select", "true",
    "Allow creation of selects in Address sinking.", /*hidden=*/1>;

defm CGPASS_AddrSinkCombineBaseReg : BoolField<"addr-sink-combine-base-reg",
    "true", "Allow combining of BaseReg field in Address sinking.",
    /*hidden=*/1>;

defm CGPASS_AddrSinkCombineBaseGv : BoolField<"addr-sink-combine-base-gv",
    "true", "Allow combining of BaseGV field in Address sinking.",
    /*hidden=*/1>;

defm CGPASS_AddrSinkCombineBaseOffs : BoolField<
    "addr-sink-combine-base-offs", "true",
    "Allow combining of BaseOffs field in Address sinking.", /*hidden=*/1>;

defm CGPASS_AddrSinkCombineScaledReg : BoolField<
    "addr-sink-combine-scaled-reg", "true",
    "Allow combining of ScaledReg field in Address sinking.", /*hidden=*/1>;

defm CGPASS_CgpSplitLargeOffsetGep : BoolField<"cgp-split-large-offset-gep",
    "true", "Enable splitting large offset of GEP.", /*hidden=*/1>;

defm CGPASS_CgpIcmpEq2icmpSt : BoolField<"cgp-icmp-eq2icmp-st", "false",
    "Enable ICMP_EQ to ICMP_S(L|G)T conversion.", /*hidden=*/1>;

defm CGPASS_CgpVerifyBfiUpdates : BoolField<"cgp-verify-bfi-updates",
    "false", "Enable BFI update verification for CodeGenPrepare.",
    /*hidden=*/1>;

defm CGPASS_CgpOptimizePhiTypes : BoolField<"cgp-optimize-phi-types", "true",
    "Enable converting phi types in CodeGenPrepare", /*hidden=*/1>;

defm CGPASS_CgppHugeFunc : ValueField<"cgpp-huge-func", "unsigned", "10000u",
    "Least BB number of huge function.", /*hidden=*/1>;

defm CGPASS_CgpMaxAddressUsersToScan : ValueField<
    "cgp-max-address-users-to-scan", "unsigned", "100u",
    "Max number of address users to look at", /*hidden=*/1>;

defm CGPASS_DisableCgpDeletePhis : BoolField<"disable-cgp-delete-phis",
    "false", "Disable elimination of dead PHI nodes.", /*hidden=*/1>;

// =========================================================================
// Options — CodeGenTargetMachineImpl.cpp
// =========================================================================

defm CGPASS_TrapUnreachable : BoolField<"trap-unreachable", "false",
    "Enable generating trap for unreachable", /*hidden=*/1>;

defm CGPASS_NoTrapAfterNoreturn : BoolField<"no-trap-after-noreturn",
    "false",
    "Do not emit a trap instruction for 'unreachable' IR instructions "
    "after noreturn calls, even if --trap-unreachable is ...",
    /*hidden=*/1>;

// =========================================================================
// Options — ComplexDeinterleavingPass.cpp
// =========================================================================

defm CGPASS_EnableComplexDeinterleaving : BoolField<
    "enable-complex-deinterleaving", "true",
    "Enable generation of complex instructions", /*hidden=*/1>;

// =========================================================================
// Options — EarlyIfConversion.cpp
// =========================================================================

defm CGPASS_EarlyIfcvtLimit : ValueField<"early-ifcvt-limit", "unsigned",
    "30u", "Maximum number of instructions per speculated block.",
    /*hidden=*/1>;

defm CGPASS_StressEarlyIfcvt : BoolField<"stress-early-ifcvt", "false",
    "Turn all knobs to 11", /*hidden=*/1>;

defm CGPASS_EnableEarlyIfcvtDataDependent : BoolField<
    "enable-early-ifcvt-data-dependent", "false",
    "Enable hard-to-predict branch analysis for if-conversion",
    /*hidden=*/1>;

defm CGPASS_EarlyIfcvtMaxRegionInstrs : ValueField<
    "early-ifcvt-max-region-instrs", "unsigned", "64",
    "Limit the number of blocks and instructions examined when searching "
    "for calls between a load and the condition it feeds", /*hidden=*/1>;

defm CGPASS_EarlyIfcvtMaxSteps : ValueField<"early-ifcvt-max-steps",
    "unsigned", "16u",
    "Limit the number of steps taken when searching for a recently loaded "
    "value", /*hidden=*/1>;

// =========================================================================
// Options — EdgeBundles.cpp
// =========================================================================

defm CGPASS_ViewEdgeBundles : BoolField<"view-edge-bundles", "false",
    "Pop up a window to show edge bundle graphs", /*hidden=*/1>;

// =========================================================================
// Options — ExpandIRInsts.cpp
// =========================================================================

defm CGPASS_ExpandFpConvertBits : ValueField<"expand-fp-convert-bits",
    "unsigned", "(1u << 23)",
    "fp convert instructions on integers with more than <N> bits are "
    "expanded.", /*hidden=*/1>;

defm CGPASS_ExpandDivRemBits : ValueField<"expand-div-rem-bits", "unsigned",
    "(1u << 23)",
    "div and rem instructions on integers with more than <N> bits are "
    "expanded.", /*hidden=*/1>;

// =========================================================================
// Options — ExpandVectorPredication.cpp
// =========================================================================

defm CGPASS_ExpandvpOverrideEvlTransform : ValueField<
    "expandvp-override-evl-transform", "std::string", "\"\"",
    "Options: <empty>|Legal|Discard|Convert. If non-empty, ignore "
    "TargetTransformInfo and always use this transformation f...",
    /*hidden=*/1>;

defm CGPASS_ExpandvpOverrideMaskTransform : ValueField<
    "expandvp-override-mask-transform", "std::string", "\"\"",
    "Options: <empty>|Legal|Discard|Convert. If non-empty, Ignore "
    "TargetTransformInfo and always use this transformation f...",
    /*hidden=*/1>;

// =========================================================================
// Options — FixupStatepointCallerSaved.cpp
// =========================================================================

defm CGPASS_FixupScsExtendSlotSize : BoolField<
    "fixup-scs-extend-slot-size", "false",
    "Allow spill in spill slot of greater size than register size",
    /*hidden=*/1>;

defm CGPASS_FixupAllowGcptrInCsr : BoolField<"fixup-allow-gcptr-in-csr",
    "false", "Allow passing GC Pointer arguments in callee saved registers",
    /*hidden=*/1>;

defm CGPASS_FixupScsEnableCopyPropagation : BoolField<
    "fixup-scs-enable-copy-propagation", "true",
    "Enable simple copy propagation during register reloading",
    /*hidden=*/1>;

// std::optional<unsigned>: StatepointProcessor needs to distinguish "left
// unset" from "explicitly specified" -- the same tri-state semantics as
// clv2's original CGPASS_FixupMaxCsrStatepoints (queried via
// wasOptSpecified).
defm CGPASS_FixupMaxCsrStatepoints : OptionalValueField<
    "fixup-max-csr-statepoints", "unsigned", "0u",
    "Max number of statepoints allowed to pass GC Ptrs in registers",
    /*hidden=*/1>;

// =========================================================================
// Options — GlobalMerge.cpp
// =========================================================================

defm CGPASS_EnableGlobalMerge : BoolField<"enable-global-merge", "true",
    "Enable the global merge pass", /*hidden=*/1>;

// std::optional<unsigned>: GlobalMerge needs to distinguish "left unset"
// from "explicitly specified" -- the same tri-state semantics as clv2's
// original CGPASS_GlobalMergeMaxOffset (queried via wasOptSpecified).
defm CGPASS_GlobalMergeMaxOffset : OptionalValueField<
    "global-merge-max-offset", "unsigned", "0u",
    "Set maximum offset for global merge pass", /*hidden=*/1>;

defm CGPASS_GlobalMergeGroupByUse : BoolField<"global-merge-group-by-use",
    "true", "Improve global merge pass to look at uses", /*hidden=*/1>;

// std::optional<bool>: GlobalMerge needs to distinguish "left unset" from
// "explicitly specified" -- the same tri-state semantics as clv2's original
// CGPASS_GlobalMergeAllConst (queried via wasOptSpecified).
defm CGPASS_GlobalMergeAllConst : OptionalValueField<
    "global-merge-all-const", "bool", "true",
    "Merge all const globals without looking at uses", /*hidden=*/1>;

defm CGPASS_GlobalMergeIgnoreSingleUse : BoolField<
    "global-merge-ignore-single-use", "true",
    "Improve global merge pass to ignore globals only used alone",
    /*hidden=*/1>;

defm CGPASS_GlobalMergeOnConst : BoolField<"global-merge-on-const", "false",
    "Enable global merge pass on constants", /*hidden=*/1>;

// std::optional<unsigned>: GlobalMerge needs to distinguish "left unset"
// from "explicitly specified" -- the same tri-state semantics as clv2's
// original CGPASS_GlobalMergeMinDataSize (queried via wasOptSpecified).
defm CGPASS_GlobalMergeMinDataSize : OptionalValueField<
    "global-merge-min-data-size", "unsigned", "0u",
    "The minimum size in bytes of each global that should considered in "
    "merging.", /*hidden=*/1>;

// std::optional<bool>: GlobalMerge needs to distinguish "left unset" (defer
// to the caller-provided default) from "explicitly specified" -- the same
// tri-state cl::boolOrDefault semantics as clv2's original
// CGPASS_GlobalMergeOnExternal.
defm CGPASS_GlobalMergeOnExternal : OptionalValueField<
    "global-merge-on-external", "bool", "true",
    "Enable global merge pass on external linkage", /*hidden=*/1>;

// =========================================================================
// Options — GlobalMergeFunctions.cpp
// =========================================================================

defm CGPASS_DisableCgdataForMerging : BoolField<
    "disable-cgdata-for-merging", "false",
    "Disable codegen data for function merging. Local merging is still "
    "enabled within a module.", /*hidden=*/1>;

// =========================================================================
// Options — HardwareLoops.cpp (continued; the other 2 options in this same
// source file belong to CGPassCore2Reg, already migrated)
// =========================================================================

// std::optional<bool>: HardwareLoopsLegacy needs to distinguish "left unset"
// (keep the pass's own default) from "explicitly specified" -- the same
// tri-state semantics as clv2's original CGPASS_ForceHardwareLoops (queried
// via wasOptSpecified).
defm CGPASS_ForceHardwareLoops : OptionalValueField<"force-hardware-loops",
    "bool", "true", "Force hardware loops intrinsics to be inserted",
    /*hidden=*/1>;

defm CGPASS_ForceHardwareLoopPhi : OptionalValueField<
    "force-hardware-loop-phi", "bool", "true",
    "Force hardware loop counter to be updated through a phi",
    /*hidden=*/1>;

defm CGPASS_ForceNestedHardwareLoop : OptionalValueField<
    "force-nested-hardware-loop", "bool", "true",
    "Force allowance of nested hardware loops", /*hidden=*/1>;

defm CGPASS_HardwareLoopDecrement : OptionalValueField<
    "hardware-loop-decrement", "unsigned", "1u",
    "Set the loop decrement value", /*hidden=*/1>;
