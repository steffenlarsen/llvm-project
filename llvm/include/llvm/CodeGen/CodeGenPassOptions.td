//===- CodeGenPassOptions.td - CLV2 option defs for CodeGen -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

// =========================================================================
// Registries (sub-namespace suffixes to avoid GCC template depth limits)
// =========================================================================

def CGPassAsmPrintReg : Registry<"llvm::cgpass_asmprint"> { let OutOfLineGetters = 1; }
def CGPassCore1Reg : Registry<"llvm::cgpass_core1"> { let OutOfLineGetters = 1; }
def CGPassCore2Reg : Registry<"llvm::cgpass_core2"> { let OutOfLineGetters = 1; }
def CGPassGISelReg : Registry<"llvm::cgpass_gisel"> { let OutOfLineGetters = 1; }
def CGPassMachine1Reg : Registry<"llvm::cgpass_machine1"> { let OutOfLineGetters = 1; }
def CGPassMachine2Reg : Registry<"llvm::cgpass_machine2"> { let OutOfLineGetters = 1; }
def CGPassRegAllocReg : Registry<"llvm::cgpass_regalloc"> { let OutOfLineGetters = 1; }
def CGPassSched1Reg : Registry<"llvm::cgpass_sched1"> { let OutOfLineGetters = 1; }
def CGPassSched2Reg : Registry<"llvm::cgpass_sched2"> { let OutOfLineGetters = 1; }
def CGPassSelDAGReg : Registry<"llvm::cgpass_seldag"> { let OutOfLineGetters = 1; }

// =========================================================================
// Option categories
// =========================================================================

def MIR2VecCategory : OptionCategory<"MIR2Vec Options">;

// =========================================================================
// Enum types
// =========================================================================

// --- PGOMapFeaturesEnum (BitsOption enum, AsmPrinter) ---

def PGOMapFeaturesNone : EnumValue<"none"> {
  let CppName = "None";
}

def PGOMapFeaturesFuncEntryCount : EnumValue<"func-entry-count"> {
  let CppName = "FuncEntryCount";
}

def PGOMapFeaturesBBFreq : EnumValue<"bb-freq"> {
  let CppName = "BBFreq";
}

def PGOMapFeaturesBrProb : EnumValue<"br-prob"> {
  let CppName = "BrProb";
}

def PGOMapFeaturesPropellerCFG : EnumValue<"propeller-cfg"> {
  let CppName = "PropellerCFG";
}

def PGOMapFeaturesAll : EnumValue<"all"> {
  let CppName = "All";
}

def PGOMapFeaturesEnumType : EnumType<"PGOMapFeaturesEnum",
    [PGOMapFeaturesNone, PGOMapFeaturesFuncEntryCount,
     PGOMapFeaturesBBFreq, PGOMapFeaturesBrProb,
     PGOMapFeaturesPropellerCFG, PGOMapFeaturesAll]>;

// --- CGRunOutliner (TargetPassConfig) ---

def CGRunOutlinerTargetDefault : EnumValue<""> {
  let CppName = "TargetDefault";
  let Sentinel = 1;
}

def CGRunOutlinerAlwaysOutline : EnumValue<"always",
    "Run on all functions guaranteed to be beneficial"> {
  let CppName = "AlwaysOutline";
}

def CGRunOutlinerOptimisticPGO : EnumValue<"optimistic-pgo",
    "Outline cold code only. If a code block does not have profile data, "
    "optimistically assume it is cold."> {
  let CppName = "OptimisticPGO";
}

def CGRunOutlinerConservativePGO : EnumValue<"conservative-pgo",
    "Outline cold code only. If a code block does not have profile, data, "
    "conservatively assume it is hot."> {
  let CppName = "ConservativePGO";
}

def CGRunOutlinerNeverOutline : EnumValue<"never",
    "Disable all outlining"> {
  let CppName = "NeverOutline";
}


def CGRunOutlinerEmptyAlias : EnumValue<""> {
  let CppName = "AlwaysOutline";
  let Value = 1;
  let AliasOnly = 1;
}

def CGRunOutlinerEnum : EnumType<"CGRunOutliner",
    [CGRunOutlinerTargetDefault, CGRunOutlinerAlwaysOutline,
     CGRunOutlinerOptimisticPGO, CGRunOutlinerConservativePGO,
     CGRunOutlinerNeverOutline, CGRunOutlinerEmptyAlias]>;

// --- CGGlobalISelAbortMode (TargetPassConfig) ---

def CGGlobalISelAbortDisable : EnumValue<"0",
    "Disable the abort"> {
  let CppName = "Disable";
}

def CGGlobalISelAbortEnable : EnumValue<"1",
    "Enable the abort"> {
  let CppName = "Enable";
}

def CGGlobalISelAbortDisableWithDiag : EnumValue<"2",
    "Disable the abort but emit a diagnostic on failure"> {
  let CppName = "DisableWithDiag";
}

def CGGlobalISelAbortModeEnum : EnumType<"CGGlobalISelAbortMode",
    [CGGlobalISelAbortDisable, CGGlobalISelAbortEnable,
     CGGlobalISelAbortDisableWithDiag]>;

// --- CGMISchedDirection (MachineScheduler) ---

def CGMISchedDirectionUnspecified : EnumValue<"__unspecified"> {
  let CppName = "Unspecified";
  let Value = 0;
  let Sentinel = 1;
}

def CGMISchedDirectionTopDown : EnumValue<"topdown",
    "Force top-down list scheduling"> {
  let CppName = "TopDown";
}

def CGMISchedDirectionBottomUp : EnumValue<"bottomup",
    "Force bottom-up list scheduling"> {
  let CppName = "BottomUp";
}

def CGMISchedDirectionBidirectional : EnumValue<"bidirectional",
    "Force bidirectional list scheduling"> {
  let CppName = "Bidirectional";
}

def CGMISchedDirectionEnum : EnumType<"CGMISchedDirection",
    [CGMISchedDirectionUnspecified, CGMISchedDirectionTopDown,
     CGMISchedDirectionBottomUp, CGMISchedDirectionBidirectional]>;

// --- CGWindowSchedulingFlag (MachinePipeliner) ---

def CGWindowSchedulingOff : EnumValue<"off",
    "Turn off window algorithm"> {
  let CppName = "WS_Off";
}

def CGWindowSchedulingOn : EnumValue<"on",
    "Use window algorithm after SMS algorithm fails"> {
  let CppName = "WS_On";
}

def CGWindowSchedulingForce : EnumValue<"force",
    "Use window algorithm instead of SMS algorithm"> {
  let CppName = "WS_Force";
}

def CGWindowSchedulingFlagEnum : EnumType<"CGWindowSchedulingFlag",
    [CGWindowSchedulingOff, CGWindowSchedulingOn,
     CGWindowSchedulingForce]>;

// --- UseBFI (MachineLICM) ---

def UseBFINone : EnumValue<"none",
    "disable the feature"> {
  let CppName = "None";
}

def UseBFIPGO : EnumValue<"pgo",
    "enable the feature when using profile data"> {
  let CppName = "PGO";
}

def UseBFIAll : EnumValue<"all",
    "enable the feature always"> {
  let CppName = "All";
}

def UseBFIEnum : EnumType<"UseBFI",
    [UseBFINone, UseBFIPGO, UseBFIAll]>;

// --- DwarfDefaultOnOff (DwarfDebug) ---

def DwarfDefaultOnOffDefault : EnumValue<"Default"> {
  let CppName = "Default";
}

def DwarfDefaultOnOffEnable : EnumValue<"Enable"> {
  let CppName = "Enable";
}

def DwarfDefaultOnOffDisable : EnumValue<"Disable"> {
  let CppName = "Disable";
}

def DwarfDefaultOnOffEnum : EnumType<"DwarfDefaultOnOff",
    [DwarfDefaultOnOffDefault, DwarfDefaultOnOffEnable,
     DwarfDefaultOnOffDisable]>;

// --- EvictionAdvisorMode (RegAllocEvictionAdvisor) ---

def EvictionAdvisorDefault : EnumValue<"default", "Default"> {
  let CppName = "Default";
}

def EvictionAdvisorRelease : EnumValue<"release", "precompiled"> {
  let CppName = "Release";
}

def EvictionAdvisorDevelopment : EnumValue<"development", "for training"> {
  let CppName = "Development";
}

def EvictionAdvisorModeEnum : EnumType<"EvictionAdvisorMode",
    [EvictionAdvisorDefault, EvictionAdvisorRelease,
     EvictionAdvisorDevelopment]>;

// --- PriorityAdvisorMode (RegAllocPriorityAdvisor) ---

def PriorityAdvisorDefault : EnumValue<"default", "Default"> {
  let CppName = "Default";
}

def PriorityAdvisorRelease : EnumValue<"release", "precompiled"> {
  let CppName = "Release";
}

def PriorityAdvisorDevelopment : EnumValue<"development", "for training"> {
  let CppName = "Development";
}

def PriorityAdvisorDummy : EnumValue<"dummy",
    "prioritize low VReg numbers"> {
  let CppName = "Dummy";
}

def PriorityAdvisorModeEnum : EnumType<"PriorityAdvisorMode",
    [PriorityAdvisorDefault, PriorityAdvisorRelease,
     PriorityAdvisorDevelopment, PriorityAdvisorDummy]>;

// --- DebugLocVerifyLevel (GlobalISel/Legalizer) ---

def DebugLocVerifyNone : EnumValue<"none", "No verification"> {
  let CppName = "None";
}

def DebugLocVerifyLegalizations : EnumValue<"legalizations",
    "Verify legalizations"> {
  let CppName = "Legalizations";
}

def DebugLocVerifyLegalizationsAndArtifactCombiners : EnumValue<
    "legalizations+artifactcombiners",
    "Verify legalizations and artifact combines"> {
  let CppName = "LegalizationsAndArtifactCombiners";
}

def DebugLocVerifyLevelEnum : EnumType<"DebugLocVerifyLevel",
    [DebugLocVerifyNone, DebugLocVerifyLegalizations,
     DebugLocVerifyLegalizationsAndArtifactCombiners]>;

// =========================================================================
// CGPassAsmPrintReg options (18)
// =========================================================================

// --- AsmPrinter/AsmPrinter.cpp (pgo-analysis-map) ---

def CGPASS_PgoAnalysisMap : BitsOption<
    "pgo-analysis-map",
    "Enable PGO analysis map",
    "PGOMapFeaturesEnum"> {
  let Registry = CGPassAsmPrintReg;
  let Hidden = 1;
}

// --- AsmPrinter/AsmPrinter.cpp ---

def CGPASS_PgoAnalysisMapEmitBbSectionsCfg : Option<
    "pgo-analysis-map-emit-bb-sections-cfg",
    "Enable the post-link cfg information from the basic block sections "
    "profile in the PGO analysis map",
    "bool"> {
  let Registry = CGPassAsmPrintReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_BasicBlockAddressMapSkipBbEntries : Option<
    "basic-block-address-map-skip-bb-entries",
    "Skip emitting basic block entries in the SHT_LLVM_BB_ADDR_MAP section. "
    "It's used to save binary size when BB entries ...",
    "bool"> {
  let Registry = CGPassAsmPrintReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_EmitJumpTableSizesSection : Option<
    "emit-jump-table-sizes-section",
    "Emit a section containing jump table addresses and sizes",
    "bool"> {
  let Registry = CGPassAsmPrintReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_AsmPrintLatency : Option<
    "asm-print-latency",
    "Print instruction latencies as verbose asm comments",
    "bool"> {
  let Registry = CGPassAsmPrintReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_StackUsageFile : Option<
    "stack-usage-file",
    "Output filename for stack usage information",
    "std::string"> {
  let Registry = CGPassAsmPrintReg;
  let Hidden = 1;
}

// --- AsmPrinter/DebugHandlerBase.cpp ---

def CGPASS_TrimVarLocs : Option<
    "trim-var-locs", "",
    "bool"> {
  let Registry = CGPassAsmPrintReg;
  let Default = "true";
  let Hidden = 1;
}

// --- AsmPrinter/DwarfCompileUnit.cpp ---

def CGPASS_EmitFuncDebugLineTableOffsets : Option<
    "emit-func-debug-line-table-offsets",
    "Include line table offset in function's debug info and emit end "
    "sequence after each function's line data.",
    "bool"> {
  let Registry = CGPassAsmPrintReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_AddLinkageNamesToDeclarationCallOrigins : Option<
    "add-linkage-names-to-declaration-call-origins",
    "Add DW_AT_linkage_name to function declaration DIEs referenced by "
    "DW_AT_call_origin attributes. Enabled by default fo...",
    "cl::boolOrDefault"> {
  let Registry = CGPassAsmPrintReg;
  let Hidden = 1;
}

// --- AsmPrinter/DwarfDebug.cpp ---

def CGPASS_UseDwarfRangesBaseAddressSpecifier : Option<
    "use-dwarf-ranges-base-address-specifier",
    "Use base address specifiers in debug_ranges",
    "bool"> {
  let Registry = CGPassAsmPrintReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_GenerateArangeSection : Option<
    "generate-arange-section",
    "Generate dwarf aranges",
    "bool"> {
  let Registry = CGPassAsmPrintReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_GenerateTypeUnits : Option<
    "generate-type-units",
    "Generate DWARF4 type units.",
    "bool"> {
  let Registry = CGPassAsmPrintReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_SplitDwarfCrossCuReferences : Option<
    "split-dwarf-cross-cu-references",
    "Enable cross-cu references in DWO files",
    "bool"> {
  let Registry = CGPassAsmPrintReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_NoDwarfRangesSection : Option<
    "no-dwarf-ranges-section",
    "Disable emission .debug_ranges section.",
    "bool"> {
  let Registry = CGPassAsmPrintReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_UseGnuDebugMacro : Option<
    "use-gnu-debug-macro",
    "Emit the GNU .debug_macro format with DWARF <5",
    "bool"> {
  let Registry = CGPassAsmPrintReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DwarfUseKeyInstructions : Option<
    "dwarf-use-key-instructions",
    "Set to false to ignore Key Instructions metadata",
    "bool"> {
  let Registry = CGPassAsmPrintReg;
  let Default = "true";
  let Hidden = 1;
}

// --- AsmPrinter/PseudoProbePrinter.cpp ---

def CGPASS_PseudoProbeVerifyGuidExistenceInDesc : Option<
    "pseudo-probe-verify-guid-existence-in-desc",
    "Verify whether GUID exists in the .pseudo_probe_desc.",
    "bool"> {
  let Registry = CGPassAsmPrintReg;
  let Default = "false";
  let Hidden = 1;
}

// --- DwarfDebug.cpp (enum option) ---

def CGPASS_UseUnknownLocations : Option<
    "use-unknown-locations",
    "Make an absence of debug location information explicit.",
    "DwarfDefaultOnOff"> {
  let Registry = CGPassAsmPrintReg;
  let Default = "Default";
  let Hidden = 1;
}

// =========================================================================
// CGPassCore1Reg options (72)
// =========================================================================

// --- AssignmentTrackingAnalysis.cpp ---

def CGPASS_DebugAtaMaxBlocks : Option<
    "debug-ata-max-blocks",
    "Maximum num basic blocks before debug info dropped",
    "unsigned"> {
  let Registry = CGPassCore1Reg;
  let Default = "10000u";
  let Hidden = 1;
}

def CGPASS_MemLocFragFill : Option<
    "mem-loc-frag-fill", "",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_PrintDebugAta : Option<
    "print-debug-ata", "",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DebugAtaCoalesceFrags : Option<
    "debug-ata-coalesce-frags", "",
    "cl::boolOrDefault"> {
  let Registry = CGPassCore1Reg;
  let Hidden = 1;
}

// --- BasicBlockMatchingAndInference.cpp ---

def CGPASS_PropellerInferThreshold : Option<
    "propeller-infer-threshold",
    "Threshold for infer stale profile",
    "float"> {
  let Registry = CGPassCore1Reg;
  let Default = "0.6f";
}

// --- BasicBlockSections.cpp ---

def CGPASS_BbsectionsDetectSourceDrift : Option<
    "bbsections-detect-source-drift",
    "This checks if there is a fdo instr. profile hash mismatch for this "
    "function",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "true";
  let Hidden = 1;
}

// --- BasicTargetTransformInfo.cpp ---

def CGPASS_PartialUnrollingThreshold : Option<
    "partial-unrolling-threshold",
    "Threshold for partial unrolling",
    "unsigned"> {
  let Registry = CGPassCore1Reg;
  let Hidden = 1;
}

// --- BranchFolding.cpp ---

def CGPASS_EnableTailMerge : Option<
    "enable-tail-merge", "",
    "cl::boolOrDefault"> {
  let Default = "cl::boolOrDefault::BOU_UNSET";
  let Registry = CGPassCore1Reg;
  let Hidden = 1;
}

def CGPASS_TailMergeThreshold : Option<
    "tail-merge-threshold",
    "Max number of predecessors to consider tail merging",
    "unsigned"> {
  let Registry = CGPassCore1Reg;
  let Default = "150u";
  let Hidden = 1;
}

def CGPASS_TailMergeSize : Option<
    "tail-merge-size",
    "Min number of instructions to consider tail merging",
    "unsigned"> {
  let Registry = CGPassCore1Reg;
  let Default = "3u";
  let Hidden = 1;
}

def CGPASS_EnableHoistCommonCode : Option<
    "branch-folder-hoist-common-code",
    "Override common-code hoisting in the BranchFolding pass",
    "cl::boolOrDefault"> {
  let Registry = CGPassCore1Reg;
  let Hidden = 1;
}

def CGPASS_EnableBlockReordering : Option<
    "branch-folder-reorder-blocks",
    "Override basic-block reordering in the BranchFolding pass",
    "cl::boolOrDefault"> {
  let Registry = CGPassCore1Reg;
  let Hidden = 1;
}

// --- CFIInstrInserter.cpp ---

def CGPASS_VerifyCfiinstrs : Option<
    "verify-cfiinstrs",
    "Verify Call Frame Information instructions",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- CodeGenPrepare.cpp ---

def CGPASS_AddrSinkUsingGep : Option<
    "addr-sink-using-gep",
    "Address sinking in CGP using GEPs.",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_DisableCgpBranchOpts : Option<
    "disable-cgp-branch-opts",
    "Disable branch optimizations in CodeGenPrepare",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableCgpGcOpts : Option<
    "disable-cgp-gc-opts",
    "Disable GC optimizations in CodeGenPrepare",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableCgpSelect2branch : Option<
    "disable-cgp-select2branch",
    "Disable select to branch conversion.",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_EnableAndcmpSinking : Option<
    "enable-andcmp-sinking",
    "Enable sinking and/cmp into branches.",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_DisableCgpStoreExtract : Option<
    "disable-cgp-store-extract",
    "Disable store(extract) optimizations in CodeGenPrepare",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_StressCgpStoreExtract : Option<
    "stress-cgp-store-extract",
    "Stress test store(extract) optimizations in CodeGenPrepare",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableCgpExtLdPromotion : Option<
    "disable-cgp-ext-ld-promotion",
    "Disable ext(promotable(ld)) -> promoted(ext(ld)) optimization in "
    "CodeGenPrepare",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_StressCgpExtLdPromotion : Option<
    "stress-cgp-ext-ld-promotion",
    "Stress test ext(promotable(ld)) -> promoted(ext(ld)) optimization in "
    "CodeGenPrepare",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisablePreheaderProt : Option<
    "disable-preheader-prot",
    "Disable protection against removing loop preheaders",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_ProfileGuidedSectionPrefix : Option<
    "profile-guided-section-prefix",
    "Use profile info to add section prefix for hot/cold functions",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_ProfileUnknownInSpecialSection : Option<
    "profile-unknown-in-special-section",
    "In profiling mode like sampleFDO, put profile unknown functions into "
    "a special section",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_BbsectionsGuidedSectionPrefix : Option<
    "bbsections-guided-section-prefix",
    "Use the basic-block-sections profile to determine the text section "
    "prefix for hot functions.",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_CgpFreqRatioToSkipMerge : Option<
    "cgp-freq-ratio-to-skip-merge",
    "Skip merging empty blocks if (frequency of empty block) / (frequency "
    "of destination block) is greater than this ratio",
    "uint64_t"> {
  let Registry = CGPassCore1Reg;
  let Default = "2";
  let Hidden = 1;
}

def CGPASS_ForceSplitStore : Option<
    "force-split-store",
    "Force store splitting no matter what the target query says.",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_CgpTypePromotionMerge : Option<
    "cgp-type-promotion-merge",
    "Enable merging of redundant sexts when one is dominating the other.",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_DisableComplexAddrModes : Option<
    "disable-complex-addr-modes",
    "Disables combining addressing modes with different parts in "
    "optimizeMemoryInst.",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_AddrSinkNewPhis : Option<
    "addr-sink-new-phis",
    "Allow creation of Phis in Address sinking.",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_AddrSinkNewSelect : Option<
    "addr-sink-new-select",
    "Allow creation of selects in Address sinking.",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_AddrSinkCombineBaseReg : Option<
    "addr-sink-combine-base-reg",
    "Allow combining of BaseReg field in Address sinking.",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_AddrSinkCombineBaseGv : Option<
    "addr-sink-combine-base-gv",
    "Allow combining of BaseGV field in Address sinking.",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_AddrSinkCombineBaseOffs : Option<
    "addr-sink-combine-base-offs",
    "Allow combining of BaseOffs field in Address sinking.",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_AddrSinkCombineScaledReg : Option<
    "addr-sink-combine-scaled-reg",
    "Allow combining of ScaledReg field in Address sinking.",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_CgpSplitLargeOffsetGep : Option<
    "cgp-split-large-offset-gep",
    "Enable splitting large offset of GEP.",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_CgpIcmpEq2icmpSt : Option<
    "cgp-icmp-eq2icmp-st",
    "Enable ICMP_EQ to ICMP_S(L|G)T conversion.",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_CgpVerifyBfiUpdates : Option<
    "cgp-verify-bfi-updates",
    "Enable BFI update verification for CodeGenPrepare.",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_CgpOptimizePhiTypes : Option<
    "cgp-optimize-phi-types",
    "Enable converting phi types in CodeGenPrepare",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_CgppHugeFunc : Option<
    "cgpp-huge-func",
    "Least BB number of huge function.",
    "unsigned"> {
  let Registry = CGPassCore1Reg;
  let Default = "10000u";
  let Hidden = 1;
}

def CGPASS_CgpMaxAddressUsersToScan : Option<
    "cgp-max-address-users-to-scan",
    "Max number of address users to look at",
    "unsigned"> {
  let Registry = CGPassCore1Reg;
  let Default = "100u";
  let Hidden = 1;
}

def CGPASS_DisableCgpDeletePhis : Option<
    "disable-cgp-delete-phis",
    "Disable elimination of dead PHI nodes.",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- CodeGenTargetMachineImpl.cpp ---

def CGPASS_TrapUnreachable : Option<
    "trap-unreachable",
    "Enable generating trap for unreachable",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_NoTrapAfterNoreturn : Option<
    "no-trap-after-noreturn",
    "Do not emit a trap instruction for 'unreachable' IR instructions "
    "after noreturn calls, even if --trap-unreachable is ...",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- ComplexDeinterleavingPass.cpp ---

def CGPASS_EnableComplexDeinterleaving : Option<
    "enable-complex-deinterleaving",
    "Enable generation of complex instructions",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "true";
  let Hidden = 1;
}

// --- EarlyIfConversion.cpp ---

def CGPASS_EarlyIfcvtLimit : Option<
    "early-ifcvt-limit",
    "Maximum number of instructions per speculated block.",
    "unsigned"> {
  let Registry = CGPassCore1Reg;
  let Default = "30u";
  let Hidden = 1;
}

def CGPASS_StressEarlyIfcvt : Option<
    "stress-early-ifcvt",
    "Turn all knobs to 11",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_EnableEarlyIfcvtDataDependent : Option<
    "enable-early-ifcvt-data-dependent",
    "Enable hard-to-predict branch analysis for if-conversion",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_EarlyIfcvtMaxRegionInstrs : Option<
    "early-ifcvt-max-region-instrs",
    "Limit the number of blocks and instructions examined when searching for "
    "calls between a load and the condition it feeds",
    "unsigned"> {
  let Registry = CGPassCore1Reg;
  let Default = "64";
  let Hidden = 1;
}

def CGPASS_EarlyIfcvtMaxSteps : Option<
    "early-ifcvt-max-steps",
    "Limit the number of steps taken when searching for a recently loaded "
    "value",
    "unsigned"> {
  let Registry = CGPassCore1Reg;
  let Default = "16u";
  let Hidden = 1;
}

// --- EdgeBundles.cpp ---

def CGPASS_ViewEdgeBundles : Option<
    "view-edge-bundles",
    "Pop up a window to show edge bundle graphs",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- ExpandIRInsts.cpp ---

def CGPASS_ExpandFpConvertBits : Option<
    "expand-fp-convert-bits",
    "fp convert instructions on integers with more than <N> bits are "
    "expanded.",
    "unsigned"> {
  let Registry = CGPassCore1Reg;
  let Default = "(1u << 23)";
  let Hidden = 1;
}

def CGPASS_ExpandDivRemBits : Option<
    "expand-div-rem-bits",
    "div and rem instructions on integers with more than <N> bits are "
    "expanded.",
    "unsigned"> {
  let Registry = CGPassCore1Reg;
  let Default = "(1u << 23)";
  let Hidden = 1;
}

// --- ExpandVectorPredication.cpp ---

def CGPASS_ExpandvpOverrideEvlTransform : Option<
    "expandvp-override-evl-transform",
    "Options: <empty>|Legal|Discard|Convert. If non-empty, ignore "
    "TargetTransformInfo and always use this transformation f...",
    "std::string"> {
  let Default = "\"\"";
  let Registry = CGPassCore1Reg;
  let Hidden = 1;
}

def CGPASS_ExpandvpOverrideMaskTransform : Option<
    "expandvp-override-mask-transform",
    "Options: <empty>|Legal|Discard|Convert. If non-empty, Ignore "
    "TargetTransformInfo and always use this transformation f...",
    "std::string"> {
  let Default = "\"\"";
  let Registry = CGPassCore1Reg;
  let Hidden = 1;
}

// --- FixupStatepointCallerSaved.cpp ---

def CGPASS_FixupScsExtendSlotSize : Option<
    "fixup-scs-extend-slot-size",
    "Allow spill in spill slot of greater size than register size",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_FixupAllowGcptrInCsr : Option<
    "fixup-allow-gcptr-in-csr",
    "Allow passing GC Pointer arguments in callee saved registers",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_FixupScsEnableCopyPropagation : Option<
    "fixup-scs-enable-copy-propagation",
    "Enable simple copy propagation during register reloading",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_FixupMaxCsrStatepoints : Option<
    "fixup-max-csr-statepoints",
    "Max number of statepoints allowed to pass GC Ptrs in registers",
    "unsigned"> {
  let Registry = CGPassCore1Reg;
  let Default = "0u";
  let Hidden = 1;
}

// --- GlobalMerge.cpp ---

def CGPASS_EnableGlobalMerge : Option<
    "enable-global-merge",
    "Enable the global merge pass",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_GlobalMergeMaxOffset : Option<
    "global-merge-max-offset",
    "Set maximum offset for global merge pass",
    "unsigned"> {
  let Registry = CGPassCore1Reg;
  let Default = "0u";
  let Hidden = 1;
}

def CGPASS_GlobalMergeGroupByUse : Option<
    "global-merge-group-by-use",
    "Improve global merge pass to look at uses",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_GlobalMergeAllConst : Option<
    "global-merge-all-const",
    "Merge all const globals without looking at uses",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_GlobalMergeIgnoreSingleUse : Option<
    "global-merge-ignore-single-use",
    "Improve global merge pass to ignore globals only used alone",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_GlobalMergeOnConst : Option<
    "global-merge-on-const",
    "Enable global merge pass on constants",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_GlobalMergeMinDataSize : Option<
    "global-merge-min-data-size",
    "The minimum size in bytes of each global that should considered in "
    "merging.",
    "unsigned"> {
  let Registry = CGPassCore1Reg;
  let Default = "0u";
  let Hidden = 1;
}

def CGPASS_GlobalMergeOnExternal : Option<
    "global-merge-on-external",
    "Enable global merge pass on external linkage",
    "cl::boolOrDefault"> {
  let Registry = CGPassCore1Reg;
  let Hidden = 1;
}

// --- GlobalMergeFunctions.cpp ---

def CGPASS_DisableCgdataForMerging : Option<
    "disable-cgdata-for-merging",
    "Disable codegen data for function merging. Local merging is still "
    "enabled within a module.",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- HardwareLoops.cpp ---

def CGPASS_ForceHardwareLoops : Option<
    "force-hardware-loops",
    "Force hardware loops intrinsics to be inserted",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_ForceHardwareLoopPhi : Option<
    "force-hardware-loop-phi",
    "Force hardware loop counter to be updated through a phi",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_ForceNestedHardwareLoop : Option<
    "force-nested-hardware-loop",
    "Force allowance of nested hardware loops",
    "bool"> {
  let Registry = CGPassCore1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_HardwareLoopDecrement : Option<
    "hardware-loop-decrement",
    "Set the loop decrement value",
    "unsigned"> {
  let Registry = CGPassCore1Reg;
  let Default = "1u";
  let Hidden = 1;
}

// =========================================================================
// CGPassCore2Reg options (71)
// =========================================================================

// --- HardwareLoops.cpp (continued) ---

def CGPASS_HardwareLoopCounterBitwidth : Option<
    "hardware-loop-counter-bitwidth",
    "Set the loop counter bitwidth",
    "unsigned"> {
  let Registry = CGPassCore2Reg;
  let Default = "32u";
  let Hidden = 1;
}

def CGPASS_ForceHardwareLoopGuard : Option<
    "force-hardware-loop-guard",
    "Force generation of loop guard intrinsic",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- IfConversion.cpp ---

def CGPASS_IfcvtFnStart : Option<
    "ifcvt-fn-start", "",
    "int"> {
  let Registry = CGPassCore2Reg;
  let Default = "-1";
  let Hidden = 1;
}

def CGPASS_IfcvtFnStop : Option<
    "ifcvt-fn-stop", "",
    "int"> {
  let Registry = CGPassCore2Reg;
  let Default = "-1";
  let Hidden = 1;
}

def CGPASS_IfcvtLimit : Option<
    "ifcvt-limit", "",
    "int"> {
  let Registry = CGPassCore2Reg;
  let Default = "-1";
  let Hidden = 1;
}

def CGPASS_DisableIfcvtSimple : Option<
    "disable-ifcvt-simple", "",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableIfcvtSimpleFalse : Option<
    "disable-ifcvt-simple-false", "",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableIfcvtTriangle : Option<
    "disable-ifcvt-triangle", "",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableIfcvtTriangleRev : Option<
    "disable-ifcvt-triangle-rev", "",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableIfcvtTriangleFalse : Option<
    "disable-ifcvt-triangle-false", "",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableIfcvtDiamond : Option<
    "disable-ifcvt-diamond", "",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableIfcvtForkedDiamond : Option<
    "disable-ifcvt-forked-diamond", "",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_IfcvtBranchFold : Option<
    "ifcvt-branch-fold", "",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "true";
  let Hidden = 1;
}

// --- ImplicitNullChecks.cpp ---

def CGPASS_ImpNullCheckPageSize : Option<
    "imp-null-check-page-size",
    "The page size of the target in bytes",
    "int"> {
  let Registry = CGPassCore2Reg;
  let Default = "4096";
  let Hidden = 1;
}

def CGPASS_ImpNullMaxInstsToConsider : Option<
    "imp-null-max-insts-to-consider",
    "The max number of instructions to consider hoisting loads over (the "
    "algorithm is quadratic over this number)",
    "unsigned"> {
  let Registry = CGPassCore2Reg;
  let Default = "8u";
  let Hidden = 1;
}

// --- InterleavedAccessPass.cpp ---

def CGPASS_LowerInterleavedAccesses : Option<
    "lower-interleaved-accesses",
    "Enable lowering interleaved accesses to intrinsics",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "true";
  let Hidden = 1;
}

// --- InterleavedLoadCombinePass.cpp ---

def CGPASS_DisableInterleavedLoadCombine : Option<
    "disable-interleaved-load-combine",
    "Disable combining of interleaved loads",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- MLRegAllocEvictAdvisor.cpp ---

def CGPASS_RegallocEvictInteractiveChannelBase : Option<
    "regalloc-evict-interactive-channel-base",
    "Base file path for the interactive mode.",
    "std::string"> {
  let Default = "\"\"";
  let Registry = CGPassCore2Reg;
  let Hidden = 1;
}

def CGPASS_MlregallocMaxEvictionCount : Option<
    "mlregalloc-max-eviction-count",
    "The maximum number of times a live range can be evicted before "
    "preventing it from being evicted",
    "unsigned"> {
  let Registry = CGPassCore2Reg;
  let Default = "100u";
  let Hidden = 1;
}

def CGPASS_RegallocTrainingLog : Option<
    "regalloc-training-log",
    "Training log for the register allocator eviction model",
    "std::string"> {
  let Registry = CGPassCore2Reg;
  let Hidden = 1;
}

def CGPASS_RegallocModel : Option<
    "regalloc-model",
    "The model being trained for register allocation eviction",
    "std::string"> {
  let Default = "\"\"";
  let Registry = CGPassCore2Reg;
  let Hidden = 1;
}

// --- MLRegAllocPriorityAdvisor.cpp ---

def CGPASS_RegallocPriorityInteractiveChannelBase : Option<
    "regalloc-priority-interactive-channel-base",
    "Base file path for the interactive mode.",
    "std::string"> {
  let Registry = CGPassCore2Reg;
  let Hidden = 1;
}

def CGPASS_RegallocPriorityTrainingLog : Option<
    "regalloc-priority-training-log",
    "Training log for the register allocator priority model",
    "std::string"> {
  let Registry = CGPassCore2Reg;
  let Hidden = 1;
}

def CGPASS_RegallocPriorityModel : Option<
    "regalloc-priority-model",
    "The model being trained for register allocation priority",
    "std::string"> {
  let Registry = CGPassCore2Reg;
  let Hidden = 1;
}

// --- PHIElimination.cpp ---

def CGPASS_DisablePhiElimEdgeSplitting : Option<
    "disable-phi-elim-edge-splitting",
    "Disable critical edge splitting during PHI elimination",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_PhiElimSplitAllCriticalEdges : Option<
    "phi-elim-split-all-critical-edges",
    "Split all critical edges during PHI elimination",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_NoPhiElimLiveOutEarlyExit : Option<
    "no-phi-elim-live-out-early-exit",
    "Do not use an early exit if isLiveOutPastPHIs returns true.",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- PeepholeOptimizer.cpp ---

def CGPASS_AggressiveExtOpt : Option<
    "aggressive-ext-opt",
    "Aggressive extension optimization",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisablePeephole : Option<
    "disable-peephole",
    "Disable the peephole optimizer",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableAdvCopyOpt : Option<
    "disable-adv-copy-opt",
    "Disable advanced copy optimization",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableNonAllocatablePhysCopyOpt : Option<
    "disable-non-allocatable-phys-copy-opt",
    "Disable non-allocatable physical register copy optimization",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_RewritePhiLimit : Option<
    "rewrite-phi-limit",
    "Limit the length of PHI chains to lookup",
    "unsigned"> {
  let Registry = CGPassCore2Reg;
  let Default = "10u";
  let Hidden = 1;
}

def CGPASS_RecurrenceChainLimit : Option<
    "recurrence-chain-limit",
    "Maximum length of recurrence chain when evaluating the benefit of "
    "commuting operands",
    "unsigned"> {
  let Registry = CGPassCore2Reg;
  let Default = "3u";
  let Hidden = 1;
}

// --- PreISelIntrinsicLowering.cpp ---

def CGPASS_MemIntrinsicExpandSize : Option<
    "mem-intrinsic-expand-size",
    "Set minimum mem intrinsic size to expand in IR",
    "int64_t"> {
  let Registry = CGPassCore2Reg;
  let Default = "-1";
  let Hidden = 1;
}

// --- RDFLiveness.cpp ---

def CGPASS_RdfLivenessMaxRec : Option<
    "rdf-liveness-max-rec",
    "Maximum recursion level",
    "unsigned"> {
  let Registry = CGPassCore2Reg;
  let Default = "25u";
  let Hidden = 1;
}

// --- SafeStack.cpp ---

def CGPASS_SafestackUsePointerAddress : Option<
    "safestack-use-pointer-address", "",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_SafeStackColoring : Option<
    "safe-stack-coloring",
    "enable safe stack coloring",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "true";
  let Hidden = 1;
}

// --- SafeStackLayout.cpp ---

def CGPASS_SafeStackLayout : Option<
    "safe-stack-layout",
    "enable safe stack layout",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "true";
  let Hidden = 1;
}

// --- SelectOptimize.cpp ---

def CGPASS_ColdOperandThreshold : Option<
    "cold-operand-threshold",
    "Maximum frequency of path for an operand to be considered cold.",
    "unsigned"> {
  let Registry = CGPassCore2Reg;
  let Default = "20u";
  let Hidden = 1;
}

def CGPASS_ColdOperandMaxCostMultiplier : Option<
    "cold-operand-max-cost-multiplier",
    "Maximum cost multiplier of TCC_expensive for the dependence slice of "
    "a cold operand to be considered inexpensive.",
    "unsigned"> {
  let Registry = CGPassCore2Reg;
  let Default = "1u";
  let Hidden = 1;
}

def CGPASS_SelectOptiLoopGradientGainThreshold : Option<
    "select-opti-loop-gradient-gain-threshold",
    "Gradient gain threshold (%).",
    "unsigned"> {
  let Registry = CGPassCore2Reg;
  let Default = "25u";
  let Hidden = 1;
}

def CGPASS_SelectOptiLoopCycleGainThreshold : Option<
    "select-opti-loop-cycle-gain-threshold",
    "Minimum gain per loop (in cycles) threshold.",
    "unsigned"> {
  let Registry = CGPassCore2Reg;
  let Default = "4u";
  let Hidden = 1;
}

def CGPASS_SelectOptiLoopRelativeGainThreshold : Option<
    "select-opti-loop-relative-gain-threshold",
    "Minimum relative gain per loop threshold (1/X). Defaults to 12.5%",
    "unsigned"> {
  let Registry = CGPassCore2Reg;
  let Default = "8u";
  let Hidden = 1;
}

def CGPASS_MispredictDefaultRate : Option<
    "mispredict-default-rate",
    "Default mispredict rate (initialized to 25%).",
    "unsigned"> {
  let Registry = CGPassCore2Reg;
  let Default = "25u";
  let Hidden = 1;
}

def CGPASS_DisableLoopLevelHeuristics : Option<
    "disable-loop-level-heuristics",
    "Disable loop-level heuristics.",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- ShrinkWrap.cpp ---

def CGPASS_EnableShrinkWrap : Option<
    "enable-shrink-wrap",
    "enable the shrink-wrapping pass",
    "cl::boolOrDefault"> {
  let Default = "cl::boolOrDefault::BOU_UNSET";
  let Registry = CGPassCore2Reg;
  let Hidden = 1;
}

def CGPASS_EnableShrinkWrapRegionSplit : Option<
    "enable-shrink-wrap-region-split",
    "enable splitting of the restore block if possible",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "true";
  let Hidden = 1;
}

// --- StackColoring.cpp ---

def CGPASS_NoStackColoring : Option<
    "no-stack-coloring",
    "Disable stack coloring",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_ProtectFromEscapedAllocas : Option<
    "protect-from-escaped-allocas",
    "Do not optimize lifetime zones that are broken",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_StackcoloringLifetimeStartOnFirstUse : Option<
    "stackcoloring-lifetime-start-on-first-use",
    "Treat stack lifetimes as starting on first use, not on START marker.",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "true";
  let Hidden = 1;
}

// --- StackMapLivenessAnalysis.cpp ---

def CGPASS_EnablePatchpointLiveness : Option<
    "enable-patchpoint-liveness",
    "Enable PatchPoint Liveness Analysis Pass",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "true";
  let Hidden = 1;
}

// --- StackMaps.cpp ---

def CGPASS_StackmapVersion : Option<
    "stackmap-version",
    "Specify the stackmap encoding version (default = 3)",
    "int"> {
  let Registry = CGPassCore2Reg;
  let Default = "3";
  let Hidden = 1;
}

// --- StackProtector.cpp ---

def CGPASS_EnableSelectiondagSp : Option<
    "enable-selectiondag-sp", "",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_DisableCheckNoreturnCall : Option<
    "disable-check-noreturn-call", "",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- StackSlotColoring.cpp ---

def CGPASS_NoStackSlotSharing : Option<
    "no-stack-slot-sharing",
    "Suppress slot sharing during stack coloring",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_SscDceLimit : Option<
    "ssc-dce-limit", "",
    "int"> {
  let Registry = CGPassCore2Reg;
  let Default = "-1";
  let Hidden = 1;
}

// --- TailDuplicator.cpp ---

def CGPASS_TailDupSize : Option<
    "tail-dup-size",
    "Maximum instructions to consider tail duplicating",
    "unsigned"> {
  let Registry = CGPassCore2Reg;
  let Default = "2u";
  let Hidden = 1;
}

def CGPASS_TailDupIndirectSize : Option<
    "tail-dup-indirect-size",
    "Maximum instructions to consider tail duplicating blocks that end "
    "with indirect branches.",
    "unsigned"> {
  let Registry = CGPassCore2Reg;
  let Default = "20u";
  let Hidden = 1;
}

def CGPASS_TailDupPredSize : Option<
    "tail-dup-pred-size",
    "Maximum predecessors (maximum successors at the same time) to "
    "consider tail duplicating blocks.",
    "unsigned"> {
  let Registry = CGPassCore2Reg;
  let Default = "16u";
  let Hidden = 1;
}

def CGPASS_TailDupSuccSize : Option<
    "tail-dup-succ-size",
    "Maximum successors (maximum predecessors at the same time) to "
    "consider tail duplicating blocks.",
    "unsigned"> {
  let Registry = CGPassCore2Reg;
  let Default = "16u";
  let Hidden = 1;
}

def CGPASS_TailDupVerify : Option<
    "tail-dup-verify",
    "Verify sanity of PHI instructions during taildup",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_TailDupLimit : Option<
    "tail-dup-limit", "",
    "unsigned"> {
  let Registry = CGPassCore2Reg;
  let Default = "~0U";
  let Hidden = 1;
}

// --- TwoAddressInstructionPass.cpp ---

def CGPASS_TwoaddrReschedule : Option<
    "twoaddr-reschedule",
    "Coalesce copies by rescheduling (default=true)",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_TwoaddrAnalyzeRevcopyTied : Option<
    "twoaddr-analyze-revcopy-tied",
    "Analyze tied operands when looking for reversed copy chain",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_DataflowEdgeLimit : Option<
    "dataflow-edge-limit",
    "Maximum number of dataflow edges to traverse when evaluating the "
    "benefit of commuting operands",
    "unsigned"> {
  let Registry = CGPassCore2Reg;
  let Default = "10u";
  let Hidden = 1;
}

// --- TypePromotion.cpp ---

def CGPASS_DisableTypePromotion : Option<
    "disable-type-promotion",
    "Disable type promotion pass",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- WinEHPrepare.cpp ---

def CGPASS_DisableDemotion : Option<
    "disable-demotion",
    "Clone multicolor basic blocks but do not demote cross scopes",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableCleanups : Option<
    "disable-cleanups",
    "Do not remove implausible terminators or other similar cleanups",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DemoteCatchswitchOnly : Option<
    "demote-catchswitch-only",
    "Demote catchswitch BBs only (for wasm EH)",
    "bool"> {
  let Registry = CGPassCore2Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- WindowsSecureHotPatching.cpp ---

def CGPASS_MsSecureHotpatchFunctionsFile : Option<
    "ms-secure-hotpatch-functions-file",
    "A file containing list of mangled function names to mark for Windows "
    "Secure Hot-Patching",
    "std::string"> {
  let Default = "\"\"";
  let Registry = CGPassCore2Reg;
  let ValueDesc = "filename";
}

def CGPASS_MsSecureHotpatchFunctionsList : ListOption<
    "ms-secure-hotpatch-functions-list",
    "A list of mangled function names to mark for Windows Secure "
    "Hot-Patching",
    "std::string"> {
  let Registry = CGPassCore2Reg;
  let ValueDesc = "list";
  let CommaSeparated = 1;
}

// =========================================================================
// CGPassGISelReg options (29)
// =========================================================================

// --- LiveDebugVariables.cpp ---

def CGPASS_LiveDebugVariables : Option<
    "live-debug-variables",
    "Enable the live debug variables pass",
    "bool"> {
  let Registry = CGPassGISelReg;
  let Default = "true";
  let Hidden = 1;
}

// --- LiveIntervals.cpp ---

def CGPASS_PrecomputePhysLiveness : Option<
    "precompute-phys-liveness",
    "Eagerly compute live intervals for all physreg units.",
    "bool"> {
  let Registry = CGPassGISelReg;
  let Default = "false";
  let Hidden = 1;
}

// --- MIR2Vec.cpp ---

// --- MIRCanonicalizerPass.cpp ---

def CGPASS_CanonNthFunction : Option<
    "canon-nth-function",
    "Function number to canonicalize.",
    "unsigned"> {
  let Registry = CGPassGISelReg;
  let Default = "~0u";
  let Hidden = 1;
}

// --- MIRPrinter.cpp ---

def CGPASS_SimplifyMir : Option<
    "simplify-mir",
    "Leave out unnecessary information when printing MIR",
    "bool"> {
  let Default = "false";
  let Registry = CGPassGISelReg;
  let Hidden = 1;
}

def CGPASS_MirDebugLoc : Option<
    "mir-debug-loc",
    "Print MIR debug-locations",
    "bool"> {
  let Registry = CGPassGISelReg;
  let Default = "true";
  let Hidden = 1;
}

// --- MIRSampleProfile.cpp ---

def CGPASS_ShowFsBranchprob : Option<
    "show-fs-branchprob",
    "Print setting flow sensitive branch probabilities",
    "bool"> {
  let Registry = CGPassGISelReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_FsProfileDebugProbDiffThreshold : Option<
    "fs-profile-debug-prob-diff-threshold",
    "Only show debug message if the branch probability is greater than "
    "this value (in percentage).",
    "unsigned"> {
  let Registry = CGPassGISelReg;
  let Default = "10u";
}

def CGPASS_FsProfileDebugBwThreshold : Option<
    "fs-profile-debug-bw-threshold",
    "Only show debug message if the source branch weight is greater  "
    "than this value.",
    "unsigned"> {
  let Registry = CGPassGISelReg;
  let Default = "10000u";
}

def CGPASS_FsViewbfiBefore : Option<
    "fs-viewbfi-before",
    "View BFI before MIR loader",
    "bool"> {
  let Registry = CGPassGISelReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_FsViewbfiAfter : Option<
    "fs-viewbfi-after",
    "View BFI after MIR loader",
    "bool"> {
  let Registry = CGPassGISelReg;
  let Default = "false";
  let Hidden = 1;
}

// --- MIRVRegNamerUtils.cpp ---

def CGPASS_MirVregNamerUseStableHash : Option<
    "mir-vreg-namer-use-stable-hash",
    "Use Stable Hashing for MIR VReg Renaming",
    "bool"> {
  let Registry = CGPassGISelReg;
  let Default = "false";
  let Hidden = 1;
}

// --- GlobalISel/CombinerHelper.cpp ---

def CGPASS_ForceLegalIndexing : Option<
    "force-legal-indexing",
    "Force all indexed operations to be legal for the GlobalISel combiner",
    "bool"> {
  let Registry = CGPassGISelReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_PostIndexUseThreshold : Option<
    "post-index-use-threshold",
    "Number of uses of a base pointer to check before it is no longer "
    "considered for post-indexing.",
    "unsigned"> {
  let Registry = CGPassGISelReg;
  let Default = "32u";
  let Hidden = 1;
}

// --- GlobalISel/IRTranslator.cpp ---

def CGPASS_EnableCseInIrtranslator : Option<
    "enable-cse-in-irtranslator",
    "Should enable CSE in irtranslator",
    "bool"> {
  let Registry = CGPassGISelReg;
  let Default = "false";
}

// --- GlobalISel/Legalizer.cpp ---

def CGPASS_EnableCseInLegalizer : Option<
    "enable-cse-in-legalizer",
    "Should enable CSE in Legalizer",
    "bool"> {
  let Registry = CGPassGISelReg;
  let Default = "false";
}

def CGPASS_AllowGinsertAsArtifact : Option<
    "allow-ginsert-as-artifact",
    "Allow G_INSERT to be considered an artifact. Hack around AMDGPU "
    "test infinite loops.",
    "bool"> {
  let Registry = CGPassGISelReg;
  let Default = "true";
}

// --- GlobalISel/LegalizerInfo.cpp ---

def CGPASS_DisableGiselLegalityCheck : Option<
    "disable-gisel-legality-check",
    "Don't verify that MIR is fully legal between GlobalISel passes",
    "bool"> {
  let Registry = CGPassGISelReg;
  let Hidden = 1;
}

def CGPASS_VerboseGiselVerifyLegalizerInfo : Option<
    "verbose-gisel-verify-legalizer-info",
    "Print more information to dbgs about GlobalISel legalizer rules "
    "being verified",
    "bool"> {
  let Registry = CGPassGISelReg;
  let Default = "false";
  let Hidden = 1;
}

// --- GlobalISel/RegBankSelect.cpp ---

def CGPASS_RegbankselectFast : Option<
    "regbankselect-fast",
    "Run the Fast mode (default mapping)",
    "bool"> {
  let Registry = CGPassGISelReg;
  let Hidden = 1;
}

def CGPASS_RegbankselectGreedy : Option<
    "regbankselect-greedy",
    "Use the Greedy mode (best local mapping)",
    "bool"> {
  let Registry = CGPassGISelReg;
  let Hidden = 1;
}

// --- LiveDebugValues/InstrRefBasedImpl.cpp ---

def CGPASS_EmulateOldLivedebugvalues : Option<
    "emulate-old-livedebugvalues",
    "Act like old LiveDebugValues did",
    "bool"> {
  let Registry = CGPassGISelReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_LivedebugvaluesMaxStackSlots : Option<
    "livedebugvalues-max-stack-slots",
    "livedebugvalues-stack-ws-limit",
    "unsigned"> {
  let Registry = CGPassGISelReg;
  let Default = "250u";
  let Hidden = 1;
}

// --- LiveDebugValues/LiveDebugValues.cpp ---

def CGPASS_ForceInstrRefLivedebugvalues : Option<
    "force-instr-ref-livedebugvalues",
    "Use instruction-ref based LiveDebugValues with normal DBG_VALUE "
    "inputs",
    "bool"> {
  let Registry = CGPassGISelReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_LivedebugvaluesInputBbLimit : Option<
    "livedebugvalues-input-bb-limit",
    "Maximum input basic blocks before DBG_VALUE limit applies",
    "unsigned"> {
  let Registry = CGPassGISelReg;
  let Default = "10000u";
  let Hidden = 1;
}

def CGPASS_LivedebugvaluesInputDbgValueLimit : Option<
    "livedebugvalues-input-dbg-value-limit",
    "Maximum input DBG_VALUE insts supported by debug range extension",
    "unsigned"> {
  let Registry = CGPassGISelReg;
  let Default = "50000u";
  let Hidden = 1;
}

def CGPASS_ExperimentalDebugVariableLocations : Option<
    "experimental-debug-variable-locations",
    "Use experimental new value-tracking variable locations",
    "cl::boolOrDefault"> {
  let Registry = CGPassGISelReg;
}

// --- GlobalISel/Legalizer.cpp (enum option) ---

def CGPASS_VerifyLegalizerDebugLocs : Option<
    "verify-legalizer-debug-locs",
    "Verify that debug locations are handled",
    "DebugLocVerifyLevel"> {
  let Registry = CGPassGISelReg;
  let Default = "legalizations";
}

// =========================================================================
// CGPassMachine1Reg options (52)
// =========================================================================

// --- MachineBasicBlock.cpp ---

def CGPASS_PrintSlotindexes : Option<
    "print-slotindexes",
    "When printing machine IR, annotate instructions and blocks with "
    "SlotIndexes when available",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "true";
  let Hidden = 1;
}

// --- MachineBlockFrequencyInfo.cpp ---

def CGPASS_PrintMachineBfi : Option<
    "print-machine-bfi",
    "Print the machine block frequency info.",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- MachineBlockPlacement.cpp ---

def CGPASS_MaxBytesForAlignment : Option<
    "max-bytes-for-alignment",
    "Forces the maximum bytes allowed to be emitted when padding for "
    "alignment",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "0u";
  let Hidden = 1;
}

def CGPASS_TailDupPlacementThreshold : Option<
    "tail-dup-placement-threshold",
    "Instruction cutoff for tail duplication during layout. Tail merging "
    "during layout is forced to have a threshold that ...",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "2u";
  let Hidden = 1;
}

def CGPASS_TailDupPlacementAggressiveThreshold : Option<
    "tail-dup-placement-aggressive-threshold",
    "Instruction cutoff for aggressive tail duplication during layout. "
    "Used at -O3. Tail merging during layout is forced t...",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "4u";
  let Hidden = 1;
}

def CGPASS_AlignAllBlocks : Option<
    "align-all-blocks",
    "Force the alignment of all blocks in the function in log2 format "
    "(e.g 4 means align on 16B boundaries).",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "0u";
  let Hidden = 1;
}

def CGPASS_AlignAllNofallthruBlocks : Option<
    "align-all-nofallthru-blocks",
    "Force the alignment of all blocks that have no fall-through "
    "predecessors (i.e. don't add nops that are executed). In ...",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "0u";
  let Hidden = 1;
}

def CGPASS_BlockPlacementPredecessorLimit : Option<
    "block-placement-predecessor-limit",
    "For blocks with more predecessors, certain layout optimizations will "
    "be disabled to prevent quadratic compile time.",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "1000u";
  let Hidden = 1;
}

def CGPASS_BlockPlacementExitBlockBias : Option<
    "block-placement-exit-block-bias",
    "Block frequency percentage a loop exit block needs over the original "
    "exit to be considered the new exit.",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "0u";
  let Hidden = 1;
}

def CGPASS_LoopToColdBlockRatio : Option<
    "loop-to-cold-block-ratio",
    "Outline loop blocks from loop chain if (frequency of loop) / "
    "(frequency of block) is greater than this ratio",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "5u";
  let Hidden = 1;
}

def CGPASS_ForceLoopColdBlock : Option<
    "force-loop-cold-block",
    "Force outlining cold blocks from loops.",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_PreciseRotationCost : Option<
    "precise-rotation-cost",
    "Model the cost of loop rotation more precisely by using profile data.",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_ForcePreciseRotationCost : Option<
    "force-precise-rotation-cost",
    "Force the use of precise cost loop rotation strategy.",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_MisfetchCost : Option<
    "misfetch-cost",
    "Cost that models the probabilistic risk of an instruction misfetch "
    "due to a jump comparing to falling through, whose ...",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "1u";
  let Hidden = 1;
}

def CGPASS_JumpInstCost : Option<
    "jump-inst-cost",
    "Cost of jump instructions.",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "1u";
  let Hidden = 1;
}

def CGPASS_TailDupPlacement : Option<
    "tail-dup-placement",
    "Perform tail duplication during placement. Creates more fallthrough "
    "opportunities in outline branches.",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_BranchFoldPlacement : Option<
    "branch-fold-placement",
    "Perform branch folding during placement. Reduces code size.",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_TailDupPlacementPenalty : Option<
    "tail-dup-placement-penalty",
    "Cost penalty for blocks that can avoid breaking CFG by copying. "
    "Percent as integer.",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "2u";
  let Hidden = 1;
}

def CGPASS_TailDupProfilePercentThreshold : Option<
    "tail-dup-profile-percent-threshold",
    "If profile count information is used in tail duplication cost model, "
    "the gained fall through number from tail duplica...",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "50u";
  let Hidden = 1;
}

def CGPASS_TriangleChainCount : Option<
    "triangle-chain-count",
    "Number of triangle-shaped-CFG's that need to be in a row for the "
    "triangle tail duplication heuristic to kick in. 0 to...",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "2u";
  let Hidden = 1;
}

def CGPASS_RenumberBlocksBeforeView : Option<
    "renumber-blocks-before-view",
    "If true, basic blocks are re-numbered before MBP layout is printed "
    "into a dot graph. Only used when a function is bei...",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_ExtTspBlockPlacementMaxBlocks : Option<
    "ext-tsp-block-placement-max-blocks",
    "Maximum number of basic blocks in a function to run ext-TSP block "
    "placement.",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "UINT_MAX";
  let Hidden = 1;
}

def CGPASS_ApplyExtTspForSize : Option<
    "apply-ext-tsp-for-size",
    "Use ext-tsp for size-aware block placement.",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- MachineBranchProbabilityInfo.cpp ---

def CGPASS_StaticLikelyProb : Option<
    "static-likely-prob",
    "branch probability threshold in percentage to be considered very "
    "likely",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "80u";
  let Hidden = 1;
}

def CGPASS_ProfileLikelyProb : Option<
    "profile-likely-prob",
    "branch probability threshold in percentage to be considered very "
    "likely when profile is available",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "51u";
  let Hidden = 1;
}

// --- MachineCFGPrinter.cpp ---

def CGPASS_McfgFuncName : Option<
    "mcfg-func-name",
    "The name of a function (or its substring) whose CFG is "
    "viewed/printed.",
    "std::string"> {
  let Registry = CGPassMachine1Reg;
  let Hidden = 1;
}

def CGPASS_McfgDotFilenamePrefix : Option<
    "mcfg-dot-filename-prefix",
    "The prefix used for the Machine CFG dot file names.",
    "std::string"> {
  let Registry = CGPassMachine1Reg;
  let Hidden = 1;
}

def CGPASS_DotMcfgOnly : Option<
    "dot-mcfg-only",
    "Print only the CFG without blocks body",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- MachineCSE.cpp ---

def CGPASS_CsusesThreshold : Option<
    "csuses-threshold",
    "Threshold for the size of CSUses",
    "int"> {
  let Registry = CGPassMachine1Reg;
  let Default = "1024";
  let Hidden = 1;
}

def CGPASS_AggressiveMachineCse : Option<
    "aggressive-machine-cse",
    "Override the profitability heuristics for Machine CSE",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- MachineCombiner.cpp ---

def CGPASS_MachineCombinerIncThreshold : Option<
    "machine-combiner-inc-threshold",
    "Incremental depth computation will be used for basic blocks with "
    "more instructions.",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "500u";
  let Hidden = 1;
}

def CGPASS_MachineCombinerDumpSubstIntrs : Option<
    "machine-combiner-dump-subst-intrs",
    "Dump all substituted intrs",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_MachineCombinerVerifyPatternOrder : Option<
    "machine-combiner-verify-pattern-order",
    "Verify that the generated patterns are ordered by increasing latency",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- MachineCopyPropagation.cpp ---

def CGPASS_McpUseIsCopyInstr : Option<
    "mcp-use-is-copy-instr", "",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_EnableSpillCopyElim : Option<
    "enable-spill-copy-elim", "",
    "cl::boolOrDefault"> {
  let Registry = CGPassMachine1Reg;
  let Hidden = 1;
}

// --- MachineDominators.cpp ---

def CGPASS_VerifyMachineDomInfo : Option<
    "verify-machine-dom-info",
    "Verify machine dominator info (time consuming)",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Hidden = 1;
}

// --- MachineFunction.cpp ---

def CGPASS_AlignAllFunctions : Option<
    "align-all-functions",
    "Force the alignment of all functions in log2 format (e.g. 4 means "
    "align on 16B boundaries).",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "0u";
  let Hidden = 1;
}

// --- MachineFunctionPass.cpp ---

def CGPASS_DroppedVariableStatsMir : Option<
    "dropped-variable-stats-mir",
    "Dump dropped debug variables stats for MIR passes",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- MachineFunctionSplitter.cpp ---

def CGPASS_MfsPsiCutoff : Option<
    "mfs-psi-cutoff",
    "Percentile profile summary cutoff used to determine cold blocks. "
    "Unused if set to zero.",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "999950u";
  let Hidden = 1;
}

def CGPASS_MfsCountThreshold : Option<
    "mfs-count-threshold",
    "Minimum number of times a block must be executed to be retained.",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "1u";
  let Hidden = 1;
}

def CGPASS_MfsSplitEhcode : Option<
    "mfs-split-ehcode",
    "Splits all EH code and it's descendants by default.",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- MachineInstr.cpp ---

def CGPASS_PrintMiAddrs : Option<
    "print-mi-addrs",
    "Print addresses of MachineInstrs when dumping",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- MachineLICM.cpp ---

def CGPASS_AvoidSpeculation : Option<
    "avoid-speculation",
    "MachineLICM should avoid speculation",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_HoistCheapInsts : Option<
    "hoist-cheap-insts",
    "MachineLICM should hoist even cheap instructions",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_HoistConstStores : Option<
    "hoist-const-stores",
    "Hoist invariant stores",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_HoistConstLoads : Option<
    "hoist-const-loads",
    "Hoist invariant loads",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_BlockFreqRatioThreshold : Option<
    "block-freq-ratio-threshold",
    "Do not hoist instructions if targetblock is N times hotter than "
    "the source.",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "100u";
  let Hidden = 1;
}

// --- MachineOperand.cpp ---

def CGPASS_PrintRegmaskNumRegs : Option<
    "print-regmask-num-regs",
    "Number of registers to limit to when printing regmask operands in "
    "IR dumps. unlimited = -1",
    "int"> {
  let Registry = CGPassMachine1Reg;
  let Default = "32";
  let Hidden = 1;
}

// --- MachineOutliner.cpp ---

def CGPASS_EnableLinkonceodrOutlining : Option<
    "enable-linkonceodr-outlining",
    "Enable the machine outliner on linkonceodr functions",
    "bool"> {
  let Registry = CGPassMachine1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_MachineOutlinerReruns : Option<
    "machine-outliner-reruns",
    "Number of times to rerun the outliner after the initial outline",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "0u";
  let Hidden = 1;
}

def CGPASS_OutlinerBenefitThreshold : Option<
    "outliner-benefit-threshold",
    "The minimum size in bytes before an outlining candidate is accepted",
    "unsigned"> {
  let Registry = CGPassMachine1Reg;
  let Default = "1u";
  let Hidden = 1;
}

// --- MachineLICM.cpp (enum option) ---

def CGPASS_DisableHoistingToHotterBlocks : Option<
    "disable-hoisting-to-hotter-blocks",
    "Disable hoisting instructions to hotter blocks",
    "UseBFI"> {
  let Registry = CGPassMachine1Reg;
  let Default = "pgo";
  let Hidden = 1;
}

// =========================================================================
// CGPassMachine2Reg options (54)
// =========================================================================

// --- MachineOutliner.cpp (continued) ---

def CGPASS_OutlinerLeafDescendants : Option<
    "outliner-leaf-descendants",
    "Consider all leaf descendants of internal nodes of the suffix tree "
    "as candidates for outlining (if false, only leaf c...",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_DisableGlobalOutlining : Option<
    "disable-global-outlining",
    "Disable global outlining only by ignoring the codegen data generation "
    "or use",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_AppendContentHashOutlinedName : Option<
    "append-content-hash-outlined-name",
    "This appends the content hash to the globally outlined function name. "
    "It's beneficial for enhancing the precision of ...",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "true";
  let Hidden = 1;
}

// --- MachinePipeliner.cpp ---

def CGPASS_EnablePipeliner : Option<
    "enable-pipeliner",
    "Enable Software Pipelining",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_EnablePipelinerOptSize : Option<
    "enable-pipeliner-opt-size",
    "Enable SWP at Os.",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_PipelinerMaxMii : Option<
    "pipeliner-max-mii",
    "Size limit for the MII.",
    "int"> {
  let Registry = CGPassMachine2Reg;
  let Default = "27";
  let Hidden = 1;
}

def CGPASS_PipelinerForceIi : Option<
    "pipeliner-force-ii",
    "Force pipeliner to use specified II.",
    "int"> {
  let Registry = CGPassMachine2Reg;
  let Default = "-1";
  let Hidden = 1;
}

def CGPASS_PipelinerMaxStages : Option<
    "pipeliner-max-stages",
    "Maximum stages allowed in the generated scheduled.",
    "int"> {
  let Registry = CGPassMachine2Reg;
  let Default = "3";
  let Hidden = 1;
}

def CGPASS_PipelinerPruneDeps : Option<
    "pipeliner-prune-deps",
    "Prune dependences between unrelated Phi nodes.",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_PipelinerPruneLoopCarried : Option<
    "pipeliner-prune-loop-carried",
    "Prune loop carried order dependences.",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_PipelinerMax : Option<
    "pipeliner-max",
    "Limit pipeliner to N loops",
    "int"> {
  let Registry = CGPassMachine2Reg;
  let Default = "-1";
}

def CGPASS_PipelinerIgnoreRecmii : Option<
    "pipeliner-ignore-recmii",
    "Ignore RecMII",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "false";
}

def CGPASS_PipelinerShowMask : Option<
    "pipeliner-show-mask",
    "Show pipeliner resource mask",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_PipelinerDbgRes : Option<
    "pipeliner-dbg-res",
    "Debug pipeliner resources",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_PipelinerAnnotateForTesting : Option<
    "pipeliner-annotate-for-testing",
    "Instead of emitting the pipelined code, annotate instructions with "
    "the generated schedule for feeding into the -modul...",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_PipelinerExperimentalCg : Option<
    "pipeliner-experimental-cg",
    "Use the experimental peeling code generator for software pipelining",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_PipelinerIiSearchRange : Option<
    "pipeliner-ii-search-range",
    "Range to search for II",
    "int"> {
  let Registry = CGPassMachine2Reg;
  let Default = "10";
  let Hidden = 1;
}

def CGPASS_PipelinerRegisterPressure : Option<
    "pipeliner-register-pressure",
    "Limit register pressure of scheduled loop",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_PipelinerRegisterPressureMargin : Option<
    "pipeliner-register-pressure-margin",
    "Margin representing the unused percentage of the register pressure "
    "limit",
    "int"> {
  let Registry = CGPassMachine2Reg;
  let Default = "5";
  let Hidden = 1;
}

def CGPASS_PipelinerMveCg : Option<
    "pipeliner-mve-cg",
    "Use the MVE code generator for software pipelining",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_PipelinerMaxNumStores : Option<
    "pipeliner-max-num-stores",
    "Maximum number of stores allowed in the target loop.",
    "unsigned"> {
  let Registry = CGPassMachine2Reg;
  let Default = "200u";
  let Hidden = 1;
}

// --- MachineRegisterInfo.cpp ---

def CGPASS_EnableSubregLiveness : Option<
    "enable-subreg-liveness",
    "Enable subregister liveness tracking.",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "true";
  let Hidden = 1;
}

// --- MachineScheduler.cpp ---

def CGPASS_EnableMisched : Option<
    "enable-misched",
    "Enable the machine instruction scheduling pass.",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_EnablePostMisched : Option<
    "enable-post-misched",
    "Enable the post-ra machine instruction scheduling pass.",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_MischedOnlyFunc : Option<
    "misched-only-func",
    "Only schedule this function",
    "std::string"> {
  let Registry = CGPassMachine2Reg;
}

def CGPASS_MischedOnlyBlock : Option<
    "misched-only-block",
    "Only schedule this MBB#",
    "unsigned"> {
  let Registry = CGPassMachine2Reg;
  let Default = "0u";
}

def CGPASS_ViewMischedCutoff : Option<
    "view-misched-cutoff",
    "Hide nodes with more predecessor/successor than cutoff",
    "unsigned"> {
  let Registry = CGPassMachine2Reg;
  let Default = "0u";
}

def CGPASS_MischedCutoff : Option<
    "misched-cutoff",
    "Stop scheduling after N instructions",
    "unsigned"> {
  let Registry = CGPassMachine2Reg;
  let Default = "~0U";
}

def CGPASS_MischedDumpReservedCycles : Option<
    "misched-dump-reserved-cycles",
    "Dump resource usage at schedule boundary.",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "false";
}

def CGPASS_MischedDetailResourceBooking : Option<
    "misched-detail-resource-booking",
    "Show details of invoking getNextResoufceCycle.",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "false";
}

def CGPASS_MischedDcpl : Option<
    "misched-dcpl",
    "Print critical path length to stdout",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_MischedLimit : Option<
    "misched-limit",
    "Limit ready list to N instructions",
    "unsigned"> {
  let Registry = CGPassMachine2Reg;
  let Default = "256u";
  let Hidden = 1;
}

def CGPASS_MischedRegpressure : Option<
    "misched-regpressure",
    "Enable register pressure scheduling.",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_MischedCyclicpath : Option<
    "misched-cyclicpath",
    "Enable cyclic critical path analysis.",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_MischedCluster : Option<
    "misched-cluster",
    "Enable memop clustering.",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_ForceFastCluster : Option<
    "force-fast-cluster",
    "Switch to fast cluster algorithm with the lost of some fusion "
    "opportunities",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_FastClusterThreshold : Option<
    "fast-cluster-threshold",
    "The threshold for fast cluster",
    "unsigned"> {
  let Registry = CGPassMachine2Reg;
  let Default = "1000u";
  let Hidden = 1;
}

def CGPASS_MischedDumpScheduleTrace : Option<
    "misched-dump-schedule-trace",
    "Dump resource usage at schedule boundary.",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "false";
}

def CGPASS_MischedDumpScheduleTraceColHeaderWidth : Option<
    "misched-dump-schedule-trace-col-header-width",
    "Set width of the columns with the resources and schedule units",
    "unsigned"> {
  let Registry = CGPassMachine2Reg;
  let Default = "19u";
}

def CGPASS_MischedDumpScheduleTraceColWidth : Option<
    "misched-dump-schedule-trace-col-width",
    "Set width of the columns showing resource booking.",
    "unsigned"> {
  let Registry = CGPassMachine2Reg;
  let Default = "5u";
}

def CGPASS_MischedSortResourcesInTrace : Option<
    "misched-sort-resources-in-trace",
    "Sort the resources printed in the dump trace",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "true";
}

def CGPASS_MischedResourceCutoff : Option<
    "misched-resource-cutoff",
    "Number of intervals to track",
    "unsigned"> {
  let Registry = CGPassMachine2Reg;
  let Default = "10u";
  let Hidden = 1;
}

// --- MachineSink.cpp ---

def CGPASS_MachineSinkSplit : Option<
    "machine-sink-split",
    "Split critical edges during machine sinking",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_MachineSinkBfi : Option<
    "machine-sink-bfi",
    "Use block frequency info to find successors to sink",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_MachineSinkSplitProbabilityThreshold : Option<
    "machine-sink-split-probability-threshold",
    "Percentage threshold for splitting single-instruction critical edge. "
    "If the branch threshold is higher than this thre...",
    "unsigned"> {
  let Registry = CGPassMachine2Reg;
  let Default = "40u";
  let Hidden = 1;
}

def CGPASS_MachineSinkLoadInstrsThreshold : Option<
    "machine-sink-load-instrs-threshold",
    "Do not try to find alias store for a load if there is a in-path block "
    "whose instruction number is higher than this th...",
    "unsigned"> {
  let Registry = CGPassMachine2Reg;
  let Default = "2000u";
  let Hidden = 1;
}

def CGPASS_MachineSinkLoadBlocksThreshold : Option<
    "machine-sink-load-blocks-threshold",
    "Do not try to find alias store for a load if the block number in the "
    "straight line is higher than this threshold.",
    "unsigned"> {
  let Registry = CGPassMachine2Reg;
  let Default = "20u";
  let Hidden = 1;
}

def CGPASS_SinkInstsToAvoidSpills : Option<
    "sink-insts-to-avoid-spills",
    "Sink instructions into cycles to avoid register spills",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_MachineSinkCycleLimit : Option<
    "machine-sink-cycle-limit",
    "The maximum number of instructions considered for cycle sinking.",
    "unsigned"> {
  let Registry = CGPassMachine2Reg;
  let Default = "50u";
  let Hidden = 1;
}

// --- MachineStripDebug.cpp ---

def CGPASS_MirStripDebugifyOnly : Option<
    "mir-strip-debugify-only",
    "Should mir-strip-debug only strip debug info from debugified modules "
    "by default",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "true";
}

// --- MacroFusion.cpp ---

def CGPASS_MischedFusion : Option<
    "misched-fusion",
    "Enable scheduling for macro fusion.",
    "bool"> {
  let Registry = CGPassMachine2Reg;
  let Default = "true";
  let Hidden = 1;
}

// --- MachineScheduler.cpp (enum options) ---

def CGPASS_MischedPreraDirection : Option<
    "misched-prera-direction",
    "Machine instruction scheduling direction for pre-RA scheduler",
    "CGMISchedDirection"> {
  let Registry = CGPassMachine2Reg;
  let Default = "CGMISchedDirection::Unspecified";
  let Hidden = 1;
}

def CGPASS_MischedPostraDirection : Option<
    "misched-postra-direction",
    "Machine instruction scheduling direction for post-RA scheduler",
    "CGMISchedDirection"> {
  let Registry = CGPassMachine2Reg;
  let Default = "CGMISchedDirection::Unspecified";
  let Hidden = 1;
}

// --- MachinePipeliner.cpp (enum option) ---

def CGPASS_WindowSched : Option<
    "window-sched",
    "Window scheduling flag",
    "CGWindowSchedulingFlag"> {
  let Registry = CGPassMachine2Reg;
  let Default = "on";
  let Hidden = 1;
}

// =========================================================================
// CGPassRegAllocReg options (33)
// =========================================================================

// --- InlineSpiller.cpp ---

def CGPASS_RestrictStatepointRemat : Option<
    "restrict-statepoint-remat",
    "Restrict remat for statepoint operands",
    "bool"> {
  let Registry = CGPassRegAllocReg;
  let Hidden = 1;
}

// --- RegAllocBase.cpp ---

def CGPASS_VerifyRegalloc : Option<
    "verify-regalloc",
    "Verify during register allocation",
    "bool"> {
  let Registry = CGPassRegAllocReg;
  let Hidden = 1;
}

// --- RegAllocEvictionAdvisor.cpp ---

def CGPASS_EnableLocalReassign : Option<
    "enable-local-reassign",
    "Local reassignment can yield better allocation decisions, but may be "
    "compile time intensive",
    "bool"> {
  let Registry = CGPassRegAllocReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_RegallocEvictionMaxInterferenceCutoff : Option<
    "regalloc-eviction-max-interference-cutoff",
    "Number of interferences after which we declare an interference "
    "unevictable and bail out.",
    "unsigned"> {
  let Registry = CGPassRegAllocReg;
  let Default = "10u";
  let Hidden = 1;
}

// --- RegAllocFast.cpp ---

def CGPASS_RafastIgnoreMissingDefs : Option<
    "rafast-ignore-missing-defs", "",
    "bool"> {
  let Registry = CGPassRegAllocReg;
  let Default = "false";
  let Hidden = 1;
}

// --- RegAllocGreedy.cpp ---

def CGPASS_RegallocCsrFirstTimeCost : Option<
    "regalloc-csr-first-time-cost",
    "Cost for first time use of callee-saved register.",
    "unsigned"> {
  let Registry = CGPassRegAllocReg;
  let Default = "0u";
  let Hidden = 1;
}

def CGPASS_RegallocCsrCostScale : Option<
    "regalloc-csr-cost-scale",
    "Scale for the callee-saved register cost, in percentage.",
    "unsigned"> {
  let Registry = CGPassRegAllocReg;
  let Default = "80u";
  let Hidden = 1;
}

def CGPASS_GreedyRegclassPriorityTrumpsGlobalness : Option<
    "greedy-regclass-priority-trumps-globalness",
    "Change the greedy register allocator's live range priority calculation "
    "to make the AllocationPriority of the register...",
    "bool"> {
  let Registry = CGPassRegAllocReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_GreedyReverseLocalAssignment : Option<
    "greedy-reverse-local-assignment",
    "Reverse allocation order of local live ranges, such that shorter local "
    "live ranges will tend to be allocated first",
    "bool"> {
  let Registry = CGPassRegAllocReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_LcrMaxDepth : Option<
    "lcr-max-depth",
    "Last chance recoloring max depth",
    "unsigned"> {
  let Registry = CGPassRegAllocReg;
  let Default = "5u";
  let Hidden = 1;
}

def CGPASS_LcrMaxInterf : Option<
    "lcr-max-interf",
    "Last chance recoloring maximum number of considered interference "
    "at a time",
    "unsigned"> {
  let Registry = CGPassRegAllocReg;
  let Default = "8u";
  let Hidden = 1;
}

def CGPASS_ExhaustiveRegisterSearch : Option<
    "exhaustive-register-search",
    "Exhaustive Search for registers bypassing the depth and interference "
    "cutoffs of last chance recoloring",
    "bool"> {
  let Registry = CGPassRegAllocReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_GrowRegionComplexityBudget : Option<
    "grow-region-complexity-budget",
    "growRegion() does not scale with the number of BB edges, so limit its "
    "budget and bail out once we reach the limit.",
    "uint64_t"> {
  let Registry = CGPassRegAllocReg;
  let Default = "10000";
  let Hidden = 1;
}

def CGPASS_SplitThresholdForRegWithHint : Option<
    "split-threshold-for-reg-with-hint",
    "The threshold for splitting a virtual register with a hint, in "
    "percentage",
    "unsigned"> {
  let Registry = CGPassRegAllocReg;
  let Default = "75u";
  let Hidden = 1;
}

// --- RegAllocPBQP.cpp ---

def CGPASS_PbqpCoalescing : Option<
    "pbqp-coalescing",
    "Attempt coalescing during PBQP register allocation.",
    "bool"> {
  let Registry = CGPassRegAllocReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_PbqpDumpGraphs : Option<
    "pbqp-dump-graphs",
    "Dump graphs for each function/round in the compilation unit.",
    "bool"> {
  let Registry = CGPassRegAllocReg;
  let Default = "false";
  let Hidden = 1;
}

// --- RegAllocScore.cpp ---

def CGPASS_RegallocCopyWeight : Option<
    "regalloc-copy-weight", "",
    "double"> {
  let Registry = CGPassRegAllocReg;
  let Default = "0.2";
  let Hidden = 1;
}

def CGPASS_RegallocLoadWeight : Option<
    "regalloc-load-weight", "",
    "double"> {
  let Registry = CGPassRegAllocReg;
  let Default = "4.0";
  let Hidden = 1;
}

def CGPASS_RegallocStoreWeight : Option<
    "regalloc-store-weight", "",
    "double"> {
  let Registry = CGPassRegAllocReg;
  let Default = "1.0";
  let Hidden = 1;
}

def CGPASS_RegallocCheapRematWeight : Option<
    "regalloc-cheap-remat-weight", "",
    "double"> {
  let Registry = CGPassRegAllocReg;
  let Default = "0.2";
  let Hidden = 1;
}

def CGPASS_RegallocExpensiveRematWeight : Option<
    "regalloc-expensive-remat-weight", "",
    "double"> {
  let Registry = CGPassRegAllocReg;
  let Default = "1.0";
  let Hidden = 1;
}

// --- RegisterClassInfo.cpp ---

def CGPASS_StressRegalloc : Option<
    "stress-regalloc",
    "Limit all regclasses to N registers",
    "unsigned"> {
  let Registry = CGPassRegAllocReg;
  let Default = "0u";
  let Hidden = 1;
}

// --- RegisterCoalescer.cpp ---

def CGPASS_JoinLiveintervals : Option<
    "join-liveintervals",
    "Coalesce copies (default=true)",
    "bool"> {
  let Registry = CGPassRegAllocReg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_TerminalRule : Option<
    "terminal-rule",
    "Apply the terminal rule",
    "bool"> {
  let Registry = CGPassRegAllocReg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_JoinSplitedges : Option<
    "join-splitedges",
    "Coalesce copies on split edges (default=subtarget)",
    "bool"> {
  let Registry = CGPassRegAllocReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_VerifyCoalescing : Option<
    "verify-coalescing",
    "Verify machine instrs before and after register coalescing",
    "bool"> {
  let Registry = CGPassRegAllocReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_LateRematUpdateThreshold : Option<
    "late-remat-update-threshold",
    "During rematerialization for a copy, if the def instruction has many "
    "other copy uses to be rematerialized, delay the ...",
    "unsigned"> {
  let Registry = CGPassRegAllocReg;
  let Default = "100u";
  let Hidden = 1;
}

def CGPASS_LargeIntervalSizeThreshold : Option<
    "large-interval-size-threshold",
    "If the valnos size of an interval is larger than the threshold, it is "
    "regarded as a large interval.",
    "unsigned"> {
  let Registry = CGPassRegAllocReg;
  let Default = "100u";
  let Hidden = 1;
}

def CGPASS_LargeIntervalFreqThreshold : Option<
    "large-interval-freq-threshold",
    "For a large interval, if it is coalesced with other live intervals "
    "many times more than the threshold, stop its coale...",
    "unsigned"> {
  let Registry = CGPassRegAllocReg;
  let Default = "256u";
  let Hidden = 1;
}

def CGPASS_JoinGlobalcopies : Option<
    "join-globalcopies",
    "Coalesce copies that span blocks (default=subtarget)",
    "cl::boolOrDefault"> {
  let Registry = CGPassRegAllocReg;
  let Hidden = 1;
}

// --- SplitKit.cpp ---

def CGPASS_EnableSplitLoopivHeuristic : Option<
    "enable-split-loopiv-heuristic",
    "Enable loop iv regalloc heuristic",
    "bool"> {
  let Registry = CGPassRegAllocReg;
  let Default = "true";
}

// --- RegAllocEvictionAdvisor.cpp (enum option) ---

def CGPASS_RegallocEnableAdvisor : Option<
    "regalloc-enable-advisor",
    "Enable regalloc advisor mode",
    "EvictionAdvisorMode"> {
  let Registry = CGPassRegAllocReg;
  let Default = "default";
  let Hidden = 1;
}

// --- RegAllocPriorityAdvisor.cpp (enum option) ---

def CGPASS_RegallocEnablePriorityAdvisor : Option<
    "regalloc-enable-priority-advisor",
    "Enable regalloc priority advisor mode",
    "PriorityAdvisorMode"> {
  let Registry = CGPassRegAllocReg;
  let Default = "default";
  let Hidden = 1;
}

// =========================================================================
// CGPassSched1Reg options (50)
// =========================================================================

// --- AntiDepBreaker.cpp ---

def CGPASS_AggAntidepDebugdiv : Option<
    "agg-antidep-debugdiv", "",
    "int"> {
  let Registry = CGPassSched1Reg;
  let Default = "0";
  let Hidden = 1;
}

def CGPASS_AggAntidepDebugmod : Option<
    "agg-antidep-debugmod", "",
    "int"> {
  let Registry = CGPassSched1Reg;
  let Default = "0";
  let Hidden = 1;
}

// --- DFAPacketizer.cpp ---

def CGPASS_DfaInstrLimit : Option<
    "dfa-instr-limit",
    "If present, stops packetizing after N instructions",
    "unsigned"> {
  let Registry = CGPassSched1Reg;
  let Default = "0u";
  let Hidden = 1;
}

// --- ModuloSchedule.cpp ---

def CGPASS_PipelinerSwapBranchTargetsMve : Option<
    "pipeliner-swap-branch-targets-mve",
    "Swap target blocks of a conditional branch for MVE expander",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- PostRASchedulerList.cpp ---

def CGPASS_PostRaScheduler : Option<
    "post-RA-scheduler",
    "Enable scheduling after register allocation",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_BreakAntiDependencies : Option<
    "break-anti-dependencies",
    "Break post-RA scheduling anti-dependencies: 'critical', 'all', or "
    "'none'",
    "std::string"> {
  let Registry = CGPassSched1Reg;
  let Default = "\"none\"";
  let Hidden = 1;
}

def CGPASS_PostraSchedDebugdiv : Option<
    "postra-sched-debugdiv",
    "Debug control MBBs that are scheduled",
    "int"> {
  let Registry = CGPassSched1Reg;
  let Default = "0";
  let Hidden = 1;
}

def CGPASS_PostraSchedDebugmod : Option<
    "postra-sched-debugmod",
    "Debug control MBBs that are scheduled",
    "int"> {
  let Registry = CGPassSched1Reg;
  let Default = "0";
  let Hidden = 1;
}

// --- ScheduleDAG.cpp ---

def CGPASS_StressSched : Option<
    "stress-sched",
    "Stress test instruction scheduling",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- ScheduleDAGInstrs.cpp ---

def CGPASS_EnableAaSchedMi : Option<
    "enable-aa-sched-mi",
    "Enable use of AA during MI DAG construction",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_UseTbaaInSchedMi : Option<
    "use-tbaa-in-sched-mi",
    "Enable use of TBAA during MI DAG construction",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_Schedmodel : Option<
    "schedmodel",
    "Use TargetSchedModel for latency lookup",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_Scheditins : Option<
    "scheditins",
    "Use InstrItineraryData for latency lookup",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_DagMapsHugeRegion : Option<
    "dag-maps-huge-region",
    "The limit to use while constructing the DAG prior to scheduling, at "
    "which point a trade-off is made to avoid excessiv...",
    "unsigned"> {
  let Registry = CGPassSched1Reg;
  let Default = "500u";
  let Hidden = 1;
}

def CGPASS_SchedPrintCycles : Option<
    "sched-print-cycles",
    "Report top/bottom cycles when dumping SUnit instances",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- TargetInstrInfo.cpp ---

def CGPASS_DisableSchedHazard : Option<
    "disable-sched-hazard",
    "Disable hazard detection during preRA scheduling",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_AccReassoc : Option<
    "acc-reassoc",
    "Enable reassociation of accumulation chains",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_AccMinDepth : Option<
    "acc-min-depth",
    "Minimum length of accumulator chains required for the optimization "
    "to kick in",
    "unsigned"> {
  let Registry = CGPassSched1Reg;
  let Default = "8u";
  let Hidden = 1;
}

def CGPASS_AccMaxWidth : Option<
    "acc-max-width",
    "Maximum number of branches in the accumulator tree",
    "unsigned"> {
  let Registry = CGPassSched1Reg;
  let Default = "3u";
  let Hidden = 1;
}

// --- TargetLoweringBase.cpp ---

def CGPASS_JumpIsExpensive : Option<
    "jump-is-expensive",
    "Do not create extra branches to split comparison logic.",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_MinJumpTableEntries : Option<
    "min-jump-table-entries",
    "Set minimum number of entries to use a jump table.",
    "unsigned"> {
  let Registry = CGPassSched1Reg;
  let Default = "4u";
  let Hidden = 1;
}

def CGPASS_MaxJumpTableSize : Option<
    "max-jump-table-size",
    "Set maximum size of jump tables.",
    "unsigned"> {
  let Registry = CGPassSched1Reg;
  let Default = "UINT_MAX";
  let Hidden = 1;
}

def CGPASS_JumpTableDensity : Option<
    "jump-table-density",
    "Minimum density for building a jump table in a normal function",
    "unsigned"> {
  let Registry = CGPassSched1Reg;
  let Default = "10u";
  let Hidden = 1;
}

def CGPASS_OptsizeJumpTableDensity : Option<
    "optsize-jump-table-density",
    "Minimum density for building a jump table in an optsize function",
    "unsigned"> {
  let Registry = CGPassSched1Reg;
  let Default = "40u";
  let Hidden = 1;
}

def CGPASS_MinBitTestCmps : Option<
    "min-bit-test-cmps",
    "Set minimum of largest number of comparisons to use bit test for "
    "switch.",
    "unsigned"> {
  let Registry = CGPassSched1Reg;
  let Default = "2u";
  let Hidden = 1;
}

def CGPASS_MaxStoreMemset : Option<
    "max-store-memset",
    "Override target's MaxStoresPerMemset and MaxStoresPerMemsetOptSize. "
    "Set to 0 to use the target default.",
    "unsigned"> {
  let Registry = CGPassSched1Reg;
  let Default = "0u";
  let Hidden = 1;
}

def CGPASS_MaxStoreMemcpy : Option<
    "max-store-memcpy",
    "Override target's MaxStoresPerMemcpy and MaxStoresPerMemcpyOptSize. "
    "Set to 0 to use the target default.",
    "unsigned"> {
  let Registry = CGPassSched1Reg;
  let Default = "0u";
  let Hidden = 1;
}

def CGPASS_MaxStoreMemmove : Option<
    "max-store-memmove",
    "Override target's MaxStoresPerMemmove and MaxStoresPerMemmoveOptSize. "
    "Set to 0 to use the target default.",
    "unsigned"> {
  let Registry = CGPassSched1Reg;
  let Default = "0u";
  let Hidden = 1;
}

def CGPASS_DisableStrictnodeMutation : Option<
    "disable-strictnode-mutation",
    "Don't mutate strict-float node to a legalize node",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- TargetLoweringObjectFileImpl.cpp ---

def CGPASS_JumptableInFunctionSection : Option<
    "jumptable-in-function-section",
    "Putting Jump Table in function section",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- TargetPassConfig.cpp ---

def CGPASS_EnableIpra : Option<
    "enable-ipra",
    "Enable interprocedural register allocation to reduce load/store at "
    "procedure calls.",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_VerifyMachineinstrs : Option<
    "verify-machineinstrs",
    "Verify generated machine code",
    "cl::boolOrDefault"> {
  let Registry = CGPassSched1Reg;
  let Hidden = 1;
}

def CGPASS_DisableAtexitBasedGlobalDtorLowering : Option<
    "disable-atexit-based-global-dtor-lowering",
    "For MachO, disable atexit()-based global destructor lowering",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DebugifyAndStripAllSafe : Option<
    "debugify-and-strip-all-safe",
    "Debugify MIR before and Strip debug after each pass except those "
    "known to be unsafe when debug info is present",
    "cl::boolOrDefault"> {
  let Registry = CGPassSched1Reg;
  let Hidden = 1;
}

def CGPASS_DebugifyCheckAndStripAllSafe : Option<
    "debugify-check-and-strip-all-safe",
    "Debugify MIR before, by checking and stripping the debug info after, "
    "each pass except those known to be unsafe when d...",
    "cl::boolOrDefault"> {
  let Registry = CGPassSched1Reg;
  let Hidden = 1;
}

def CGPASS_FastIsel : Option<
    "fast-isel",
    "Enable the fast instruction selector",
    "cl::boolOrDefault"> {
  let Registry = CGPassSched1Reg;
  let Hidden = 1;
}

def CGPASS_GlobalIsel : Option<
    "global-isel",
    "Enable the global instruction selector",
    "cl::boolOrDefault"> {
  let Registry = CGPassSched1Reg;
  let Hidden = 1;
}

def CGPASS_PrintAfterIsel : Option<
    "print-after-isel",
    "Print machine instrs after ISel",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableExpandReductions : Option<
    "disable-expand-reductions",
    "Disable the expand reduction intrinsics pass from running",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_EnableGcEmptyBasicBlocks : Option<
    "enable-gc-empty-basic-blocks",
    "Enable garbage-collecting empty basic blocks",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_FsProfileFile : Option<
    "fs-profile-file",
    "Flow Sensitive profile file name.",
    "std::string"> {
  let Registry = CGPassSched1Reg;
  let Hidden = 1;
}

def CGPASS_DisablePostRa : Option<
    "disable-post-ra",
    "Disable Post Regalloc Scheduler",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableBranchFold : Option<
    "disable-branch-fold",
    "Disable branch folding",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableTailDuplicate : Option<
    "disable-tail-duplicate",
    "Disable tail duplication",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableEarlyTaildup : Option<
    "disable-early-taildup",
    "Disable pre-register allocation tail duplication",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- TargetPassConfig.cpp (enum options) ---

def CGPASS_EnableMachineOutliner : Option<
    "enable-machine-outliner",
    "Enable the machine outliner",
    "CGRunOutliner"> {
  let Registry = CGPassSched1Reg;
  let Default = "CGRunOutliner::TargetDefault";
  let Hidden = 1;
  let ValueOptional = 1;
}

def CGPASS_GlobalISelAbort : Option<
    "global-isel-abort",
    "Enable abort calls when global instruction selection fails to "
    "lower/select an instruction",
    "CGGlobalISelAbortMode"> {
  let Registry = CGPassSched1Reg;
  let Default = "1";
  let Hidden = 1;
}

// --- TargetMachine.cpp ---

def CGPASS_NoKernelInfoEndLto : Option<
    "no-kernel-info-end-lto",
    "remove the kernel-info pass at the end of the full LTO pipeline",
    "bool"> {
  let Registry = CGPassSched1Reg;
  let Default = "false";
  let Hidden = 1;
}

// =========================================================================
// CGPassSched2Reg options (48)
// =========================================================================

// --- TargetPassConfig.cpp (continued) ---

def CGPASS_DisableBlockPlacement : Option<
    "disable-block-placement",
    "Disable probability-driven block placement",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_EnableBlockPlacementStats : Option<
    "enable-block-placement-stats",
    "Collect probability-driven block placement stats",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableSsc : Option<
    "disable-ssc",
    "Disable Stack Slot Coloring",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableMachineDce : Option<
    "disable-machine-dce",
    "Disable Machine Dead Code Elimination",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableEarlyIfcvt : Option<
    "disable-early-ifcvt",
    "Disable Early If-conversion",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableMachineLicm : Option<
    "disable-machine-licm",
    "Disable Machine LICM",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableMachineCse : Option<
    "disable-machine-cse",
    "Disable Machine Common Subexpression Elimination",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisablePostraMachineLicm : Option<
    "disable-postra-machine-licm",
    "Disable Machine LICM",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableMachineSink : Option<
    "disable-machine-sink",
    "Disable Machine Sinking",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisablePostraMachineSink : Option<
    "disable-postra-machine-sink",
    "Disable PostRA Machine Sinking",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableLsr : Option<
    "disable-lsr",
    "Disable Loop Strength Reduction Pass",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableConstantHoisting : Option<
    "disable-constant-hoisting",
    "Disable ConstantHoisting",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableCgp : Option<
    "disable-cgp",
    "Disable Codegen Prepare",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableCopyprop : Option<
    "disable-copyprop",
    "Disable Copy Propagation pass",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisablePartialLibcallInlining : Option<
    "disable-partial-libcall-inlining",
    "Disable Partial Libcall Inlining",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_EnableImplicitNullChecks : Option<
    "enable-implicit-null-checks",
    "Fold null checks into faulting memory operations",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_PrintIselInput : Option<
    "print-isel-input",
    "Print LLVM IR input to isel pass",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_PrintRegusage : Option<
    "print-regusage",
    "Print register usage details collected for IPRA",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Hidden = 1;
}

def CGPASS_EnableGlobalMergeFunc : Option<
    "enable-global-merge-func",
    "Enable global merge functions that are based on hash function",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableCfiFixup : Option<
    "disable-cfi-fixup",
    "Disable the CFI fixup pass",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableRaFsprofileLoader : Option<
    "disable-ra-fsprofile-loader",
    "Disable MIRProfileLoader before RegAlloc",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableLayoutFsprofileLoader : Option<
    "disable-layout-fsprofile-loader",
    "Disable MIRProfileLoader before BlockPlacement",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_FsRemappingFile : Option<
    "fs-remapping-file",
    "Flow Sensitive profile remapping file name.",
    "std::string"> {
  let Registry = CGPassSched2Reg;
  let Hidden = 1;
}

def CGPASS_MischedPostra : Option<
    "misched-postra",
    "Run MachineScheduler post regalloc (independent of preRA sched)",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_EarlyLiveIntervals : Option<
    "early-live-intervals",
    "Run live interval analysis earlier in the pipeline",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableReplaceWithVecLib : Option<
    "disable-replace-with-vec-lib",
    "Disable replace with vector math call pass",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_StartAfter : Option<
    "start-after",
    "Resume compilation after a specific pass",
    "std::string"> {
  let Registry = CGPassSched2Reg;
  let Hidden = 1;
}

def CGPASS_StartBefore : Option<
    "start-before",
    "Resume compilation before a specific pass",
    "std::string"> {
  let Registry = CGPassSched2Reg;
  let Hidden = 1;
}

def CGPASS_StopAfter : Option<
    "stop-after",
    "Stop compilation after a specific pass",
    "std::string"> {
  let Registry = CGPassSched2Reg;
  let Hidden = 1;
}

def CGPASS_StopBefore : Option<
    "stop-before",
    "Stop compilation before a specific pass",
    "std::string"> {
  let Registry = CGPassSched2Reg;
  let Hidden = 1;
}

def CGPASS_EnableSplitMachineFunctions : Option<
    "enable-split-machine-functions",
    "Split out cold blocks from machine functions based on profile "
    "information.",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableSelectOptimize : Option<
    "disable-select-optimize",
    "Disable the select-optimization pass from running",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_SplitStaticData : Option<
    "split-static-data",
    "Split static data sections into hot and cold sections using profile "
    "information",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_BasicBlockSectionMatchInfer : Option<
    "basic-block-section-match-infer",
    "Enable matching and inference when generating basic block sections",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
}

def CGPASS_EmitBbHash : Option<
    "emit-bb-hash",
    "Emit the hash of basic block in the SHT_LLVM_BB_ADDR_MAP section.",
    "bool"> {
  let Registry = CGPassSched2Reg;
}

// --- TargetRegisterInfo.cpp ---

def CGPASS_HugeSizeForSplit : Option<
    "huge-size-for-split",
    "A threshold of live range size which may cause high compile time "
    "cost in global splitting.",
    "unsigned"> {
  let Registry = CGPassSched2Reg;
  let Default = "5000u";
  let Hidden = 1;
}

// --- TargetSchedule.cpp ---

def CGPASS_SchedModelForceEnableIntervals : Option<
    "sched-model-force-enable-intervals",
    "Force the use of resource intervals in the schedule model",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

// --- VLIWMachineScheduler.cpp ---

def CGPASS_IgnoreBbRegPressure : Option<
    "ignore-bb-reg-pressure", "",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_UseNewerCandidate : Option<
    "use-newer-candidate", "",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_MischedVerboseLevel : Option<
    "misched-verbose-level", "",
    "unsigned"> {
  let Registry = CGPassSched2Reg;
  let Default = "1u";
  let Hidden = 1;
}

def CGPASS_CheckEarlyAvail : Option<
    "check-early-avail", "",
    "bool"> {
  let Registry = CGPassSched2Reg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_VliwMischedRegPressure : Option<
    "vliw-misched-reg-pressure",
    "High register pressure threhold.",
    "float"> {
  let Registry = CGPassSched2Reg;
  let Default = "0.75f";
  let Hidden = 1;
}

// --- WindowScheduler.cpp ---

def CGPASS_WindowSearchNum : Option<
    "window-search-num",
    "The number of searches per loop in the window algorithm. 0 means no "
    "search number limit.",
    "unsigned"> {
  let Registry = CGPassSched2Reg;
  let Default = "6u";
  let Hidden = 1;
}

def CGPASS_WindowSearchRatio : Option<
    "window-search-ratio",
    "The ratio of searches per loop in the window algorithm. 100 means "
    "search all positions in the loop, while 0 means not...",
    "unsigned"> {
  let Registry = CGPassSched2Reg;
  let Default = "40u";
  let Hidden = 1;
}

def CGPASS_WindowIiCoeff : Option<
    "window-ii-coeff",
    "The coefficient used when initializing II in the window algorithm.",
    "unsigned"> {
  let Registry = CGPassSched2Reg;
  let Default = "5u";
  let Hidden = 1;
}

def CGPASS_WindowRegionLimit : Option<
    "window-region-limit",
    "The lower limit of the scheduling region in the window algorithm.",
    "unsigned"> {
  let Registry = CGPassSched2Reg;
  let Default = "3u";
  let Hidden = 1;
}

def CGPASS_WindowDiffLimit : Option<
    "window-diff-limit",
    "The lower limit of the difference between best II and base II in "
    "the window algorithm. If the difference is smaller t...",
    "unsigned"> {
  let Registry = CGPassSched2Reg;
  let Default = "2u";
  let Hidden = 1;
}

def CGPASS_WindowIiLimit : Option<
    "window-ii-limit",
    "The upper limit of II in the window algorithm.",
    "unsigned"> {
  let Registry = CGPassSched2Reg;
  let Default = "1000u";
  let Hidden = 1;
}

// =========================================================================
// CGPassSelDAGReg options (52)
// =========================================================================

// --- SelectionDAG/DAGCombiner.cpp ---

def CGPASS_CombinerGlobalAliasAnalysis : Option<
    "combiner-global-alias-analysis",
    "Enable DAG combiner's use of IR alias analysis",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_CombinerTopologicalSorting : Option<
    "combiner-topological-sorting",
    "DAG combiner nodes consistently processed in topological order",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_CombinerAaOnlyFunc : Option<
    "combiner-aa-only-func",
    "Only use DAG-combiner alias analysis in this function",
    "std::string"> {
  let Registry = CGPassSelDAGReg;
}

def CGPASS_CombinerUseTbaa : Option<
    "combiner-use-tbaa",
    "Enable DAG combiner's use of TBAA",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_CombinerStressLoadSlicing : Option<
    "combiner-stress-load-slicing",
    "Bypass the profitability model of load slicing",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_CombinerSplitLoadIndex : Option<
    "combiner-split-load-index",
    "DAG combiner may split indexing from loads",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_CombinerStoreMerging : Option<
    "combiner-store-merging",
    "DAG combiner enable merging multiple stores into a wider store",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_CombinerTokenfactorInlineLimit : Option<
    "combiner-tokenfactor-inline-limit",
    "Limit the number of operands to inline for Token Factors",
    "unsigned"> {
  let Registry = CGPassSelDAGReg;
  let Default = "2048u";
  let Hidden = 1;
}

def CGPASS_CombinerStoreMergeDependenceLimit : Option<
    "combiner-store-merge-dependence-limit",
    "Limit the number of times for the same StoreNode and RootNode to "
    "bail out in store merging dependence check",
    "unsigned"> {
  let Registry = CGPassSelDAGReg;
  let Default = "10u";
  let Hidden = 1;
}

def CGPASS_CombinerReduceLoadOpStoreWidth : Option<
    "combiner-reduce-load-op-store-width",
    "DAG combiner enable reducing the width of load/op/store sequence",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_CombinerReduceLoadOpStoreWidthForceNarrowingProfitable : Option<
    "combiner-reduce-load-op-store-width-force-narrowing-profitable",
    "DAG combiner force override the narrowing profitable check when "
    "reducing the width of load/op/store sequences",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_CombinerShrinkLoadReplaceStoreWithStore : Option<
    "combiner-shrink-load-replace-store-with-store",
    "DAG combiner enable load/<replace bytes>/store with a narrower store",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_CombinerDisabled : Option<
    "combiner-disabled",
    "Disable the DAG combiner",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

// --- SelectionDAG/LegalizeTypes.cpp ---

def CGPASS_EnableLegalizeTypesChecking : Option<
    "enable-legalize-types-checking", "",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

// --- SelectionDAG/ResourcePriorityQueue.cpp ---

def CGPASS_DisableDfaSched : Option<
    "disable-dfa-sched",
    "Disable use of DFA during scheduling",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DfaSchedRegPressureThreshold : Option<
    "dfa-sched-reg-pressure-threshold",
    "Track reg pressure and switch priority to in-depth",
    "int"> {
  let Registry = CGPassSelDAGReg;
  let Default = "5";
  let Hidden = 1;
}

// --- SelectionDAG/ScheduleDAGRRList.cpp ---

def CGPASS_DisableSchedCycles : Option<
    "disable-sched-cycles",
    "Disable cycle-level precision during preRA scheduling",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableSchedRegPressure : Option<
    "disable-sched-reg-pressure",
    "Disable regpressure priority in sched=list-ilp",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableSchedLiveUses : Option<
    "disable-sched-live-uses",
    "Disable live use priority in sched=list-ilp",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_DisableSchedVrcycle : Option<
    "disable-sched-vrcycle",
    "Disable virtual register cycle interference checks",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableSchedPhysregJoin : Option<
    "disable-sched-physreg-join",
    "Disable physreg def-use affinity",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableSchedStalls : Option<
    "disable-sched-stalls",
    "Disable no-stall priority in sched=list-ilp",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_DisableSchedCriticalPath : Option<
    "disable-sched-critical-path",
    "Disable critical path priority in sched=list-ilp",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_DisableSchedHeight : Option<
    "disable-sched-height",
    "Disable scheduled-height priority in sched=list-ilp",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_Disable2addrHack : Option<
    "disable-2addr-hack",
    "Disable scheduler's two-address hack",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_MaxSchedReorder : Option<
    "max-sched-reorder",
    "Number of instructions to allow ahead of the critical path in "
    "sched=list-ilp",
    "int"> {
  let Registry = CGPassSelDAGReg;
  let Default = "6";
  let Hidden = 1;
}

def CGPASS_SchedAvgIpc : Option<
    "sched-avg-ipc",
    "Average inst/cycle when no target itinerary exists.",
    "unsigned"> {
  let Registry = CGPassSelDAGReg;
  let Default = "1u";
  let Hidden = 1;
}

// --- SelectionDAG/ScheduleDAGSDNodes.cpp ---

def CGPASS_SchedHighLatencyCycles : Option<
    "sched-high-latency-cycles",
    "Roughly estimate the number of cycles that 'long latency' "
    "instructions take for targets with no itinerary",
    "int"> {
  let Registry = CGPassSelDAGReg;
  let Default = "10";
  let Hidden = 1;
}

// --- SelectionDAG/SelectionDAG.cpp ---

def CGPASS_EnableMemcpyDagOpt : Option<
    "enable-memcpy-dag-opt",
    "Gang up loads and stores generated by inlining of memcpy",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_LdstmemcpyGlueMax : Option<
    "ldstmemcpy-glue-max",
    "Number limit for gluing ld/st of memcpy.",
    "int"> {
  let Registry = CGPassSelDAGReg;
  let Default = "0";
  let Hidden = 1;
}

def CGPASS_HasPredecessorMaxSteps : Option<
    "has-predecessor-max-steps",
    "DAG combiner limit number of steps when searching DAG for predecessor "
    "nodes",
    "unsigned"> {
  let Registry = CGPassSelDAGReg;
  let Default = "8192u";
  let Hidden = 1;
}

// --- SelectionDAG/SelectionDAGBuilder.cpp ---

def CGPASS_InsertAssertAlign : Option<
    "insert-assert-align",
    "Insert the experimental `assertalign` node.",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_LimitFloatPrecision : Option<
    "limit-float-precision",
    "Generate low-precision inline sequences for some float libcalls",
    "unsigned"> {
  let Default = "0";
  let Registry = CGPassSelDAGReg;
  let Hidden = 1;
}

def CGPASS_SwitchPeelThreshold : Option<
    "switch-peel-threshold",
    "Set the case probability threshold for peeling the case from a "
    "switch statement. A value greater than 100 will void t...",
    "unsigned"> {
  let Registry = CGPassSelDAGReg;
  let Default = "66u";
  let Hidden = 1;
}

// --- SelectionDAG/SelectionDAGDumper.cpp ---

def CGPASS_DagDumpVerbose : Option<
    "dag-dump-verbose",
    "Display more information when dumping selection DAG nodes.",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_PrintSdnodeAddrs : Option<
    "print-sdnode-addrs",
    "Print addresses of SDNodes when dumping",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

// --- SelectionDAG/SelectionDAGISel.cpp ---

def CGPASS_FastIselAbort : Option<
    "fast-isel-abort",
    "Enable abort calls when 'fast' instruction selection fails to lower "
    "an instruction: 0 disable the abort, 1 will abort...",
    "int"> {
  let Registry = CGPassSelDAGReg;
  let Default = "0";
  let Hidden = 1;
}

def CGPASS_FastIselReportOnFallback : Option<
    "fast-isel-report-on-fallback",
    "Emit a diagnostic when 'fast' instruction selection falls back to "
    "SelectionDAG.",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_UseMbpi : Option<
    "use-mbpi",
    "use Machine Branch Probability Info",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "true";
  let Hidden = 1;
}

def CGPASS_DumpSortedDags : Option<
    "dump-sorted-dags",
    "Print DAGs with sorted nodes in debug dump",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_FilterViewDags : Option<
    "filter-view-dags",
    "Only display the basic block whose name matches this for all "
    "view-*-dags options",
    "std::string"> {
  let Registry = CGPassSelDAGReg;
  let Hidden = 1;
}

def CGPASS_ViewDagCombine1Dags : Option<
    "view-dag-combine1-dags",
    "Pop up a window to show dags before the first dag combine pass",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_ViewLegalizeTypesDags : Option<
    "view-legalize-types-dags",
    "Pop up a window to show dags before legalize types",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_ViewDagCombineLtDags : Option<
    "view-dag-combine-lt-dags",
    "Pop up a window to show dags before the post legalize types dag "
    "combine pass",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_ViewLegalizeDags : Option<
    "view-legalize-dags",
    "Pop up a window to show dags before legalize",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_ViewDagCombine2Dags : Option<
    "view-dag-combine2-dags",
    "Pop up a window to show dags before the second dag combine pass",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_ViewIselDags : Option<
    "view-isel-dags",
    "Pop up a window to show isel dags as they are selected",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_ViewSchedDags : Option<
    "view-sched-dags",
    "Pop up a window to show sched dags as they are processed",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_ViewSunitDags : Option<
    "view-sunit-dags",
    "Pop up a window to show SUnit dags after they are processed",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

// --- SelectionDAG/StatepointLowering.cpp ---

def CGPASS_UseRegistersForDeoptValues : Option<
    "use-registers-for-deopt-values",
    "Allow using registers for non pointer deopt args",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_UseRegistersForGcValuesInLandingPad : Option<
    "use-registers-for-gc-values-in-landing-pad",
    "Allow using registers for gc pointer in landing pad",
    "bool"> {
  let Registry = CGPassSelDAGReg;
  let Default = "false";
  let Hidden = 1;
}

def CGPASS_MaxRegistersForGcValues : Option<
    "max-registers-for-gc-values",
    "Max number of VRegs allowed to pass GC pointer meta args in",
    "unsigned"> {
  let Registry = CGPassSelDAGReg;
  let Default = "0u";
  let Hidden = 1;
}
