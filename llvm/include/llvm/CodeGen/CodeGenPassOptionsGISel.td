//===- CodeGenPassOptionsGISel.td - GlobalISel option defs -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def CodeGenGISelOptions : OptionsStruct<"llvm"> {}

// =========================================================================
// Enum: DebugLocVerifyLevel (GlobalISel/Legalizer.cpp)
// =========================================================================

def DebugLocVerifyLevelEnum : OptionEnum<"DebugLocVerifyLevel", [
    EnumMember<"None", "none">,
    EnumMember<"Legalizations", "legalizations">,
    EnumMember<"LegalizationsAndArtifactCombiners",
               "legalizations+artifactcombiners">]>;

// =========================================================================
// Options — LiveDebugVariables.cpp
// =========================================================================

defm CGPASS_LiveDebugVariables : BoolField<"live-debug-variables", "true",
    "Enable the live debug variables pass", /*hidden=*/1>;

// =========================================================================
// Options — LiveIntervals.cpp
// =========================================================================

defm CGPASS_PrecomputePhysLiveness : BoolField<"precompute-phys-liveness",
    "false", "Eagerly compute live intervals for all physreg units.",
    /*hidden=*/1>;

// =========================================================================
// Options — MIRCanonicalizerPass.cpp
// =========================================================================

defm CGPASS_CanonNthFunction : ValueField<"canon-nth-function", "unsigned",
    "~0u", "Function number to canonicalize.", /*hidden=*/1>;

// =========================================================================
// Options — MIRPrinter.cpp
// =========================================================================

defm CGPASS_SimplifyMir : BoolField<"simplify-mir", "false",
    "Leave out unnecessary information when printing MIR", /*hidden=*/1>;

defm CGPASS_MirDebugLoc : BoolField<"mir-debug-loc", "true",
    "Print MIR debug-locations", /*hidden=*/1>;

// =========================================================================
// Options — MIRSampleProfile.cpp
// =========================================================================

defm CGPASS_ShowFsBranchprob : BoolField<"show-fs-branchprob", "false",
    "Print setting flow sensitive branch probabilities", /*hidden=*/1>;

defm CGPASS_FsProfileDebugProbDiffThreshold : ValueField<
    "fs-profile-debug-prob-diff-threshold", "unsigned", "10u",
    "Only show debug message if the branch probability is greater than "
    "this value (in percentage).">;

defm CGPASS_FsProfileDebugBwThreshold : ValueField<
    "fs-profile-debug-bw-threshold", "unsigned", "10000u",
    "Only show debug message if the source branch weight is greater  "
    "than this value.">;

defm CGPASS_FsViewbfiBefore : BoolField<"fs-viewbfi-before", "false",
    "View BFI before MIR loader", /*hidden=*/1>;

defm CGPASS_FsViewbfiAfter : BoolField<"fs-viewbfi-after", "false",
    "View BFI after MIR loader", /*hidden=*/1>;

// =========================================================================
// Options — MIRVRegNamerUtils.cpp
// =========================================================================

defm CGPASS_MirVregNamerUseStableHash : BoolField<
    "mir-vreg-namer-use-stable-hash", "false",
    "Use Stable Hashing for MIR VReg Renaming", /*hidden=*/1>;

// =========================================================================
// Options — GlobalISel/CombinerHelper.cpp
// =========================================================================

defm CGPASS_ForceLegalIndexing : BoolField<"force-legal-indexing", "false",
    "Force all indexed operations to be legal for the GlobalISel combiner",
    /*hidden=*/1>;

defm CGPASS_PostIndexUseThreshold : ValueField<"post-index-use-threshold",
    "unsigned", "32u",
    "Number of uses of a base pointer to check before it is no longer "
    "considered for post-indexing.", /*hidden=*/1>;

// =========================================================================
// Options — GlobalISel/IRTranslator.cpp
// =========================================================================

defm CGPASS_EnableCseInIrtranslator : OptionalValueField<
    "enable-cse-in-irtranslator", "bool", "true",
    "Should enable CSE in irtranslator">;

// =========================================================================
// Options — GlobalISel/Legalizer.cpp
// =========================================================================

defm CGPASS_EnableCseInLegalizer : OptionalValueField<
    "enable-cse-in-legalizer", "bool", "true", "Should enable CSE in Legalizer">;

defm CGPASS_AllowGinsertAsArtifact : BoolField<"allow-ginsert-as-artifact",
    "true", "Allow G_INSERT to be considered an artifact. Hack around "
    "AMDGPU test infinite loops.">;

// =========================================================================
// Options — GlobalISel/LegalizerInfo.cpp
// =========================================================================

defm CGPASS_DisableGiselLegalityCheck : BoolField<
    "disable-gisel-legality-check", "false",
    "Don't verify that MIR is fully legal between GlobalISel passes",
    /*hidden=*/1>;

defm CGPASS_VerboseGiselVerifyLegalizerInfo : BoolField<
    "verbose-gisel-verify-legalizer-info", "false",
    "Print more information to dbgs about GlobalISel legalizer rules "
    "being verified", /*hidden=*/1>;

// =========================================================================
// Options — GlobalISel/RegBankSelect.cpp
// =========================================================================

defm CGPASS_RegbankselectFast : OptionalValueField<"regbankselect-fast",
    "bool", "true", "Run the Fast mode (default mapping)", /*hidden=*/1>;

defm CGPASS_RegbankselectGreedy : OptionalValueField<"regbankselect-greedy",
    "bool", "true", "Use the Greedy mode (best local mapping)",
    /*hidden=*/1>;

// =========================================================================
// Options — LiveDebugValues/InstrRefBasedImpl.cpp
// =========================================================================

defm CGPASS_EmulateOldLivedebugvalues : BoolField<
    "emulate-old-livedebugvalues", "false", "Act like old LiveDebugValues "
    "did", /*hidden=*/1>;

defm CGPASS_LivedebugvaluesMaxStackSlots : ValueField<
    "livedebugvalues-max-stack-slots", "unsigned", "250u",
    "livedebugvalues-stack-ws-limit", /*hidden=*/1>;

// =========================================================================
// Options — LiveDebugValues/LiveDebugValues.cpp
// =========================================================================

defm CGPASS_ForceInstrRefLivedebugvalues : BoolField<
    "force-instr-ref-livedebugvalues", "false",
    "Use instruction-ref based LiveDebugValues with normal DBG_VALUE "
    "inputs", /*hidden=*/1>;

defm CGPASS_LivedebugvaluesInputBbLimit : ValueField<
    "livedebugvalues-input-bb-limit", "unsigned", "10000u",
    "Maximum input basic blocks before DBG_VALUE limit applies",
    /*hidden=*/1>;

defm CGPASS_LivedebugvaluesInputDbgValueLimit : ValueField<
    "livedebugvalues-input-dbg-value-limit", "unsigned", "50000u",
    "Maximum input DBG_VALUE insts supported by debug range extension",
    /*hidden=*/1>;

defm CGPASS_ExperimentalDebugVariableLocations : OptionalValueField<
    "experimental-debug-variable-locations", "bool", "true",
    "Use experimental new value-tracking variable locations">;

// =========================================================================
// Options — GlobalISel/Legalizer.cpp (enum option)
// =========================================================================

defm CGPASS_VerifyLegalizerDebugLocs : EnumField<
    "verify-legalizer-debug-locs", DebugLocVerifyLevelEnum,
    "DebugLocVerifyLevel::Legalizations",
    "Verify that debug locations are handled">;
