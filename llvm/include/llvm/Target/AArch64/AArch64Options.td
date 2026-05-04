//===- AArch64Options.td - CLV2 option defs for AArch64 ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def AArch64OptsReg : Registry<"llvm::a64_opts"> { let OutOfLineGetters = 1; }

// =========================================================================
// Enum types
// =========================================================================

// --- AArch64AsmPrinter.cpp ---

def A64PtrauthCheckModeDefault : EnumValue<"", ""> {
  let CppName = "Default";
  let Value = 0;
}

def A64PtrauthCheckModeUnchecked : EnumValue<"none",
    "don't test for failure"> {
  let CppName = "Unchecked";
}

def A64PtrauthCheckModePoison : EnumValue<"poison",
    "poison on failure"> {
  let CppName = "Poison";
}

def A64PtrauthCheckModeTrap : EnumValue<"trap",
    "trap on failure"> {
  let CppName = "Trap";
}

def A64PtrauthCheckModeEnum : EnumType<"A64PtrauthCheckMode",
    [A64PtrauthCheckModeDefault, A64PtrauthCheckModeUnchecked,
     A64PtrauthCheckModePoison, A64PtrauthCheckModeTrap]>;

// --- AArch64StackTagging.cpp ---

def A64StackTaggingRecordStackHistoryModeNone : EnumValue<"none",
    "Do not record stack ring history"> {
  let CppName = "None";
  let Value = 0;
}

def A64StackTaggingRecordStackHistoryModeInstr : EnumValue<"instr",
    "Insert instructions into the prologue for "
    "storing into the stack ring buffer"> {
  let CppName = "Instr";
}

def A64StackTaggingRecordStackHistoryModeEnum :
    EnumType<"A64StackTaggingRecordStackHistoryMode",
    [A64StackTaggingRecordStackHistoryModeNone,
     A64StackTaggingRecordStackHistoryModeInstr]>;

// --- AArch64StackTaggingPreRA.cpp ---

def A64UncheckedLdStModeNever : EnumValue<"never",
    "never apply unchecked-ld-st"> {
  let CppName = "Never";
  let Value = 0;
}

def A64UncheckedLdStModeSafe : EnumValue<"safe",
    "apply unchecked-ld-st when the target is definitely within range"> {
  let CppName = "Safe";
}

def A64UncheckedLdStModeAlways : EnumValue<"always",
    "always apply unchecked-ld-st"> {
  let CppName = "Always";
}

def A64UncheckedLdStModeEnum : EnumType<"A64UncheckedLdStMode",
    [A64UncheckedLdStModeNever, A64UncheckedLdStModeSafe,
     A64UncheckedLdStModeAlways]>;

// --- MCTargetDesc/AArch64MCAsmInfo.cpp ---

def A64AsmWriterVariantTyDefault : EnumValue<"", ""> {
  let CppName = "Default";
  let Value = -1;
}

def A64AsmWriterVariantTyGeneric : EnumValue<"generic",
    "Emit generic NEON assembly"> {
  let CppName = "Generic";
  let Value = 0;
}

def A64AsmWriterVariantTyApple : EnumValue<"apple",
    "Emit Apple-style NEON assembly"> {
  let CppName = "Apple";
  let Value = 1;
}

def A64AsmWriterVariantTyEnum : EnumType<"A64AsmWriterVariantTy",
    [A64AsmWriterVariantTyDefault, A64AsmWriterVariantTyGeneric,
     A64AsmWriterVariantTyApple]>;

// --- AArch64CodeLayoutOpt.cpp ---

def A64CodeLayoutOptCmpCsel : EnumValue<"cmp-csel",
    "CMP/CMN-CSEL pair alignment (32-bit)"> {
  let CppName = "CmpCsel";
  let Value = 0;
}

def A64CodeLayoutOptFcmpFcsel : EnumValue<"fcmp-fcsel",
    "FCMP-FCSEL pair alignment"> {
  let CppName = "FcmpFcsel";
}

def A64CodeLayoutOptNone : EnumValue<"none",
    "Disable the code alignment pass"> {
  let CppName = "None";
}

def A64CodeLayoutOptEnum : EnumType<"A64CodeLayoutOpt",
    [A64CodeLayoutOptCmpCsel, A64CodeLayoutOptFcmpFcsel,
     A64CodeLayoutOptNone]>;

// --- AArch64PointerAuth.cpp ---

def A64SetRAStateModeNever : EnumValue<"never",
    "Always use legacy .cfi_negate_ra_state[_with_pc]"> {
  let CppName = "Never";
  let Value = 0;
}

def A64SetRAStateModePAuthLR : EnumValue<"pauth-lr",
    "Use new CFI only for PAuth_LR (default)"> {
  let CppName = "PAuthLR";
}

def A64SetRAStateModeAlways : EnumValue<"always",
    "Use new CFI for both PAuth and PAuth_LR"> {
  let CppName = "Always";
}

def A64SetRAStateModeEnum : EnumType<"A64SetRAStateMode",
    [A64SetRAStateModeNever, A64SetRAStateModePAuthLR,
     A64SetRAStateModeAlways]>;

// =========================================================================
// Options
// =========================================================================

// --- AArch64AsmPrinter.cpp ---

def A64_PtrauthAuthChecks : Option<
    "aarch64-ptrauth-auth-checks",
    "Check pointer authentication auth/resign failures",
    "A64PtrauthCheckMode"> {
  let Registry = AArch64OptsReg;
  let Default = "A64PtrauthCheckMode::Default";
  let Hidden = 1;
}

// --- AArch64A57FPLoadBalancing.cpp ---

def A64_A57FPLoadBalancingForceAll : Option<
    "aarch64-a57-fp-load-balancing-force-all",
    "Always modify dest registers regardless of color",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_A57FPLoadBalancingOverride : Option<
    "aarch64-a57-fp-load-balancing-override",
    "Ignore balance information, always return "
    "(1: Even, 2: Odd).",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

// --- AArch64AdvSIMDScalarPass.cpp ---

def A64_SIMDScalarForceAll : Option<
    "aarch64-simd-scalar-force-all",
    "Force use of AdvSIMD scalar instructions everywhere",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

// --- AArch64Arm64ECCallLowering.cpp ---

def A64_LowerDirectToIndirect : Option<
    "arm64ec-lower-direct-to-indirect", "",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_GenerateThunks : Option<
    "arm64ec-generate-thunks", "",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- AArch64ConditionalCompares.cpp ---

def A64_CCMPLimit : Option<
    "aarch64-ccmp-limit",
    "Maximum number of instructions per speculated block.",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "30";
  let Hidden = 1;
}

def A64_StressCCMP : Option<
    "aarch64-stress-ccmp",
    "Turn all knobs to 11",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Hidden = 1;
}

// --- AArch64FrameLowering.cpp ---

def A64_EnableRedZone : Option<
    "aarch64-redzone",
    "enable use of redzone on AArch64",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_StackTaggingMergeSetTag : Option<
    "stack-tagging-merge-settag",
    "merge settag instruction in function epilog",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_OrderFrameObjects : Option<
    "aarch64-order-frame-objects",
    "sort stack allocations",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_SplitSVEObjects : Option<
    "aarch64-split-sve-objects",
    "Split allocation of ZPR & PPR objects",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_HomogeneousPrologEpilog : Option<
    "homogeneous-prolog-epilog",
    "Emit homogeneous prologue and epilogue for the size "
    "optimization (default = off)",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Hidden = 1;
}

def A64_StackHazardRemarkSize : Option<
    "aarch64-stack-hazard-remark-size", "",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_StackHazardInNonStreaming : Option<
    "aarch64-stack-hazard-in-non-streaming", "",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_DisableMultiVectorSpillFill : Option<
    "aarch64-disable-multivector-spill-fill",
    "Disable use of LD/ST pairs for SME2 or SVE2p1",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

// --- AArch64InstrInfo.cpp ---

def A64_CBDisplacementBits : Option<
    "aarch64-cb-offset-bits",
    "Restrict range of CB instructions (DEBUG)",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "9";
  let Hidden = 1;
}

def A64_TBZDisplacementBits : Option<
    "aarch64-tbz-offset-bits",
    "Restrict range of TB[N]Z instructions (DEBUG)",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "14";
  let Hidden = 1;
}

def A64_CBZDisplacementBits : Option<
    "aarch64-cbz-offset-bits",
    "Restrict range of CB[N]Z instructions (DEBUG)",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "19";
  let Hidden = 1;
}

def A64_BCCDisplacementBits : Option<
    "aarch64-bcc-offset-bits",
    "Restrict range of Bcc instructions (DEBUG)",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "19";
  let Hidden = 1;
}

def A64_BDisplacementBits : Option<
    "aarch64-b-offset-bits",
    "Restrict range of B instructions (DEBUG)",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "26";
  let Hidden = 1;
}

def A64_GatherOptSearchLimit : Option<
    "aarch64-search-limit",
    "Restrict range of instructions to search for the "
    "machine-combiner gather pattern optimization",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "2048";
  let Hidden = 1;
}

// --- AArch64ISelLowering.cpp ---

def A64_ELFLocalDynamicTLSGeneration : Option<
    "aarch64-elf-ldtls-generation",
    "Allow AArch64 Local Dynamic TLS code generation",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_EnableLogicalImm : Option<
    "aarch64-enable-logical-imm",
    "Enable AArch64 logical imm instruction "
    "optimization",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableMGatherCombine : Option<
    "aarch64-enable-mgather-combine",
    "Combine extends of AArch64 masked "
    "gather intrinsics",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableExtToTBL : Option<
    "aarch64-enable-ext-to-tbl",
    "Combine ext and trunc to TBL",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_MaxXors : Option<
    "aarch64-max-xors",
    "Maximum of xors",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "16";
  let Hidden = 1;
}

def A64_EnableSVEGISel : Option<
    "aarch64-enable-gisel-sve",
    "Enable / disable SVE scalable vectors in Global ISel",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_UseFEATCPACodegen : Option<
    "aarch64-use-featcpa-codegen",
    "Generate ISD::PTRADD nodes for pointer arithmetic in "
    "SelectionDAG for FEAT_CPA",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_BrMergingBaseCostThresh : Option<
    "aarch64-br-merging-base-cost",
    "Cost threshold for merging multiple conditionals into one branch "
    "versus splitting into multiple branches: conditionals are merged when "
    "their instruction cost is below this limit and split above it. Set to "
    "-1 to never merge branches.",
    "int"> {
  let Registry = AArch64OptsReg;
  let Default = "2";
  let Hidden = 1;
}

def A64_BrMergingCcmpBias : Option<
    "aarch64-br-merging-ccmp-bias",
    "Increases 'aarch64-br-merging-base-cost' to account for the "
    "CCMP instruction, which is always available on AArch64 and "
    "makes merging branch conditions cheaper.",
    "int"> {
  let Registry = AArch64OptsReg;
  let Default = "6";
  let Hidden = 1;
}

def A64_BrMergingCbzTbnzBias : Option<
    "aarch64-br-merging-cbz-tbnz-bias",
    "Decreases 'aarch64-br-merging-base-cost' when a condition can "
    "lower to a single CBZ/CBNZ or TBZ/TBNZ compare-and-branch, to "
    "bias toward splitting. Set to 0 to disable.",
    "int"> {
  let Registry = AArch64OptsReg;
  let Default = "6";
  let Hidden = 1;
}

def A64_BrMergingLikelyBias : Option<
    "aarch64-br-merging-likely-bias",
    "Increases 'aarch64-br-merging-base-cost' when all conditionals "
    "are likely to be executed, biasing toward merging. Set to -1 to "
    "never merge likely branches.",
    "int"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_BrMergingUnlikelyBias : Option<
    "aarch64-br-merging-unlikely-bias",
    "Decreases 'aarch64-br-merging-base-cost' when all conditionals are "
    "unlikely to be executed, biasing toward splitting. Set to -1 to never "
    "merge unlikely branches.",
    "int"> {
  let Registry = AArch64OptsReg;
  let Default = "-1";
  let Hidden = 1;
}

def A64_UseConditionalFPMRWrite : Option<
    "aarch64-use-conditional-fpmr-write",
    "Only write FPMR when the requested value differs from the "
    "current value",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

// --- AArch64LoadStoreOptimizer.cpp ---

def A64_LdStLimit : Option<
    "aarch64-load-store-scan-limit", "",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "20";
  let Hidden = 1;
}

def A64_UpdateLimit : Option<
    "aarch64-update-scan-limit", "",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "100";
  let Hidden = 1;
}

def A64_LdStConstLimit : Option<
    "aarch64-load-store-const-scan-limit", "",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "10";
  let Hidden = 1;
}

def A64_EnableRenaming : Option<
    "aarch64-load-store-renaming", "",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- AArch64LowerHomogeneousPrologEpilog.cpp ---

def A64_FrameHelperSizeThreshold : Option<
    "frame-helper-size-threshold",
    "The minimum number of instructions that are outlined in a frame "
    "helper (default = 2)",
    "int"> {
  let Registry = AArch64OptsReg;
  let Default = "2";
  let Hidden = 1;
}

// --- AArch64PromoteConstant.cpp ---

def A64_StressPromoteConst : Option<
    "aarch64-stress-promote-const",
    "Promote all vector constants",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Hidden = 1;
}

// --- AArch64SelectionDAGInfo.cpp ---

def A64_LowerToSMERoutines : Option<
    "aarch64-lower-to-sme-routines",
    "Enable AArch64 SME memory operations "
    "to lower to librt functions",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_UseMOPS : Option<
    "aarch64-use-mops",
    "Enable AArch64 MOPS instructions "
    "for memcpy/memset/memmove",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- AArch64SpeculationHardening.cpp ---

def A64_HardenLoads : Option<
    "aarch64-slh-loads",
    "Sanitize loads from memory.",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- AArch64StackTagging.cpp ---

def A64_StackTaggingMergeInit : Option<
    "stack-tagging-merge-init",
    "merge stack variable initializers with tagging when possible",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_StackTaggingUseStackSafety : Option<
    "stack-tagging-use-stack-safety",
    "Use Stack Safety analysis results",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_StackTaggingScanLimit : Option<
    "stack-tagging-merge-init-scan-limit", "",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "40";
  let Hidden = 1;
}

def A64_StackTaggingMergeInitSizeLimit : Option<
    "stack-tagging-merge-init-size-limit", "",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "272";
  let Hidden = 1;
}

def A64_RecordStackHistory : Option<
    "stack-tagging-record-stack-history",
    "Record stack frames with tagged allocations in a thread-local "
    "ring buffer",
    "A64StackTaggingRecordStackHistoryMode"> {
  let Registry = AArch64OptsReg;
  let Default = "none";
  let Hidden = 1;
}

// --- AArch64StackTaggingPreRA.cpp ---

def A64_UncheckedLdSt : Option<
    "stack-tagging-unchecked-ld-st",
    "Unconditionally apply unchecked-ld-st optimization (even for large "
    "stack frames, or in the presence of variable sized allocas).",
    "A64UncheckedLdStMode"> {
  let Registry = AArch64OptsReg;
  let Default = "safe";
  let Hidden = 1;
}

def A64_StackTaggingFirstSlotOpt : Option<
    "stack-tagging-first-slot-opt",
    "Apply first slot optimization for stack tagging "
    "(eliminate ADDG Rt, Rn, 0, 0).",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- AArch64Subtarget.cpp ---

def A64_EnableEarlyIfConvert : Option<
    "aarch64-early-ifcvt",
    "Enable the early if converter pass",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_UseAddressTopByteIgnored : Option<
    "aarch64-use-tbi",
    "Assume that top byte of an address is ignored",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_MachOUseNonLazyBind : Option<
    "aarch64-macho-enable-nonlazybind",
    "Call nonlazybind functions via direct GOT load for Mach-O",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Hidden = 1;
}

def A64_UseAA : Option<
    "aarch64-use-aa",
    "Enable the use of AA during codegen.",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
}

def A64_OverrideVectorInsertExtractBaseCost : Option<
    "aarch64-insert-extract-base-cost",
    "Base cost of vector insert/extract element",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Hidden = 1;
}

def A64_ReservedRegsForRA : ListOption<
    "reserve-regs-for-regalloc",
    "Reserve physical "
    "registers, so they can't be used by register allocator. "
    "Should only be used for testing register allocator.",
    "std::string"> {
  let Registry = AArch64OptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

def A64_AuthenticatedLRCheckMethod : Option<
    "aarch64-authenticated-lr-check-method",
    "Override the variant of check applied "
    "to authenticated LR during tail call",
    "std::string"> {
  let Registry = AArch64OptsReg;
  let Hidden = 1;
  // Self-contained by necessity: this descriptor is a header-inline constexpr,
  // so anything the body calls must be linkable from every TU that reads the
  // option -- including tools that do not link the AArch64 backend.
  //
  // parseAuthCheckMethod() in AArch64Subtarget.cpp defaults an unrecognised
  // name to None, so without this a typo silently disables the check.
  let Validate = [{
    if (Value == "none" || Value == "load" || Value == "high-bits-notbi" ||
        Value == "xpac" || Value == "xpac-hint")
      return true;
    return reject("Cannot find option named '" + Value + "'!");
  }];
}

def A64_MinimumJumpTableEntries : Option<
    "aarch64-min-jump-table-entries",
    "Set minimum number of entries to use a jump table on AArch64",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "10";
  let Hidden = 1;
}

def A64_StreamingHazardSize : Option<
    "aarch64-stack-hazard-size",
    "Hazard size for streaming mode memory accesses. 0 = disabled.",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_StreamingHazardSizeAlias : Alias<
    "aarch64-streaming-hazard-size", "aarch64-stack-hazard-size"> {
  let Registry = AArch64OptsReg;
}

def A64_VScaleForTuning : Option<
    "sve-vscale-for-tuning",
    "Force a vscale for tuning factor for SVE",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Hidden = 1;
}

def A64_EnableSubregLivenessTracking : Option<
    "aarch64-enable-subreg-liveness-tracking",
    "Enable subreg liveness tracking",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_UseScalarIncVL : Option<
    "sve-use-scalar-inc-vl",
    "Prefer add+cnt over addvl/inc/dec",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

// --- AArch64TargetMachine.cpp ---

def A64_EnableCCMP : Option<
    "aarch64-enable-ccmp",
    "Enable the CCMP formation pass",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableCondBrTuning : Option<
    "aarch64-enable-cond-br-tune",
    "Enable the conditional branch tuning pass",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableCopyPropagation : Option<
    "aarch64-enable-copy-propagation",
    "Enable the copy propagation with AArch64 copy instr",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableMCR : Option<
    "aarch64-enable-mcr",
    "Enable the machine combiner pass",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableStPairSuppress : Option<
    "aarch64-enable-stp-suppress",
    "Suppress STP for AArch64",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableAdvSIMDScalar : Option<
    "aarch64-enable-simd-scalar",
    "Enable use of AdvSIMD scalar integer instructions",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_EnablePromoteConstant : Option<
    "aarch64-enable-promote-const",
    "Enable the promote constant pass",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableCollectLOH : Option<
    "aarch64-enable-collect-loh",
    "Enable the pass that emits the linker optimization hints (LOH)",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableDeadRegisterElimination : Option<
    "aarch64-enable-dead-defs",
    "Enable the pass that removes dead"
    " definitions and replaces stores to"
    " them with stores to the zero"
    " register",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableRedundantCopyElimination : Option<
    "aarch64-enable-copyelim",
    "Enable the redundant copy elimination pass",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableLoadStoreOpt : Option<
    "aarch64-enable-ldst-opt",
    "Enable the load/store pair"
    " optimization pass",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableAtomicTidy : Option<
    "aarch64-enable-atomic-cfg-tidy",
    "Run SimplifyCFG after expanding atomic operations"
    " to make use of cmpxchg flow-based information",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableEarlyIfConversion : Option<
    "aarch64-enable-early-ifcvt",
    "Run early if-conversion",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableCondOpt : Option<
    "aarch64-enable-condopt",
    "Enable the condition optimizer pass",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableGEPOpt : Option<
    "aarch64-enable-gep-opt",
    "Enable optimizations on complex GEPs",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_EnableSelectOpt : Option<
    "aarch64-select-opt",
    "Enable select to branch optimizations",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_BranchRelaxation : Option<
    "aarch64-enable-branch-relax",
    "Relax out of range conditional branches",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableCompressJumpTables : Option<
    "aarch64-enable-compress-jump-tables",
    "Use smallest entry possible for jump tables",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableGlobalMerge : Option<
    "aarch64-enable-global-merge",
    "Enable the global merge pass",
    "std::optional<bool>"> {
  let Registry = AArch64OptsReg;
  let Hidden = 1;
}

def A64_EnableLoopDataPrefetch : Option<
    "aarch64-enable-loop-data-prefetch",
    "Enable the loop data prefetch pass",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableGlobalISelAtO : Option<
    "aarch64-enable-global-isel-at-O",
    "Enable GlobalISel at or below an opt level (-1 to disable)",
    "int"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_EnableSMEPeepholeOpt : Option<
    "enable-aarch64-sme-peephole-opt",
    "Perform SME peephole optimization",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableFalkorHWPFFix : Option<
    "aarch64-enable-falkor-hwpf-fix", "",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableBranchTargets : Option<
    "aarch64-enable-branch-targets",
    "Enable the AArch64 branch target pass",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_SVEVectorBitsMax : Option<
    "aarch64-sve-vector-bits-max",
    "Assume SVE vector registers are at most this big, "
    "with zero meaning no maximum size is assumed.",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_SVEVectorBitsMin : Option<
    "aarch64-sve-vector-bits-min",
    "Assume SVE vector registers are at least this big, "
    "with zero meaning no minimum size is assumed.",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_ForceStreaming : Option<
    "force-streaming",
    "Force the use of streaming code for all functions",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_ForceStreamingCompatible : Option<
    "force-streaming-compatible",
    "Force the use of streaming-compatible code for all functions",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_EnableGISelLoadStoreOptPreLegal : Option<
    "aarch64-enable-gisel-ldst-prelegal",
    "Enable GlobalISel's pre-legalizer load/store optimization pass",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableGISelLoadStoreOptPostLegal : Option<
    "aarch64-enable-gisel-ldst-postlegal",
    "Enable GlobalISel's post-legalizer load/store optimization pass",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_EnableSinkFold : Option<
    "aarch64-enable-sink-fold",
    "Enable sinking and folding of instruction copies",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableMachinePipeliner : Option<
    "aarch64-enable-pipeliner",
    "Enable Machine Pipeliner for AArch64",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_EnableSRLTSubregToRegMitigation : Option<
    "aarch64-srlt-mitigate-sr2r",
    "Enable SUBREG_TO_REG mitigation by adding 'implicit-def' for "
    "super-regs when using Subreg Liveness Tracking",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableSVEShuffleOpt : Option<
    "aarch64-enable-sve-shuffle-opts",
    "Enable pattern matching of shuffles that could make use of SVE "
    "instructions like tbl or the bottom/top variants",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- AArch64TargetObjectFile.cpp ---

def A64_EmitDebugTLSLocation : Option<
    "aarch64-emit-debug-tls-location",
    "Emit the TLS DWARF location with DTPREL relocation for AArch64",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Hidden = 1;
}

// --- AArch64TargetTransformInfo.cpp ---

def A64_EnableFalkorHWPFUnrollFix : Option<
    "enable-falkor-hwpf-unroll-fix", "",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_SVEPreferFixedOverScalableIfEqualCost : Option<
    "sve-prefer-fixed-over-scalable-if-equal", "",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Hidden = 1;
}

def A64_SVEGatherOverhead : Option<
    "sve-gather-overhead", "",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "10";
  let Hidden = 1;
}

def A64_SVEScatterOverhead : Option<
    "sve-scatter-overhead", "",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "10";
  let Hidden = 1;
}

def A64_SVETailFoldInsnThreshold : Option<
    "sve-tail-folding-insn-threshold", "",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "15";
  let Hidden = 1;
}

def A64_NeonNonConstStrideOverhead : Option<
    "neon-nonconst-stride-overhead", "",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "10";
  let Hidden = 1;
}

def A64_CallPenaltyChangeSM : Option<
    "call-penalty-sm-change",
    "Penalty of calling a function that requires a change to PSTATE.SM",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "5";
  let Hidden = 1;
}

def A64_InlineCallPenaltyChangeSM : Option<
    "inline-call-penalty-sm-change",
    "Penalty of inlining a call that requires a change to PSTATE.SM",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "10";
  let Hidden = 1;
}

def A64_EnableOrLikeSelectOpt : Option<
    "enable-aarch64-or-like-select", "",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_EnableLSRCostOpt : Option<
    "enable-aarch64-lsr-cost-opt", "",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def A64_BaseHistCntCost : Option<
    "aarch64-base-histcnt-cost",
    "The cost of a histcnt instruction",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "8";
  let Hidden = 1;
}

def A64_DMBLookaheadThreshold : Option<
    "dmb-lookahead-threshold",
    "The number of instructions to search for a redundant dmb",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "10";
  let Hidden = 1;
}

def A64_ForceUnrollThreshold : Option<
    "aarch64-force-unroll-threshold",
    "Threshold for forced unrolling of small loops in AArch64",
    "int"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_SVETailFolding : Option<
    "sve-tail-folding",
    "Control the use of vectorisation using tail-folding for SVE where the"
    " option is specified in the form (Initial)[+(Flag1|Flag2|...)]:"
    "\ndisabled      (Initial) No loop types will vectorize using "
    "tail-folding"
    "\ndefault       (Initial) Uses the default tail-folding settings for "
    "the target CPU"
    "\nall           (Initial) All legal loop types will vectorize using "
    "tail-folding"
    "\nsimple        (Initial) Use tail-folding for simple loops (not "
    "reductions or recurrences)"
    "\nreductions    Use tail-folding for loops containing reductions"
    "\nnoreductions  Inverse of above"
    "\nrecurrences   Use tail-folding for loops containing fixed order "
    "recurrences"
    "\nnorecurrences Inverse of above"
    "\nreverse       Use tail-folding for loops requiring reversed "
    "predicates"
    "\nnoreverse     Inverse of above",
    "std::string"> {
  let Registry = AArch64OptsReg;
}

def A64_EnableFixedwidthAutovecInStreamingMode : Option<
    "enable-fixedwidth-autovec-in-streaming-mode", "",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_EnableScalableAutovecInStreamingMode : Option<
    "enable-scalable-autovec-in-streaming-mode", "",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

// --- GISel/AArch64PostLegalizerCombiner.cpp ---

def A64_EnableConsecutiveMemOpOpt : Option<
    "aarch64-postlegalizer-consecutive-memops",
    "Enable consecutive memop optimization "
    "in AArch64PostLegalizerCombiner",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- MCTargetDesc/AArch64MCAsmInfo.cpp ---

def A64_AsmWriterVariant : Option<
    "aarch64-neon-syntax",
    "Choose style of NEON code to emit from AArch64 backend:",
    "A64AsmWriterVariantTy"> {
  let Registry = AArch64OptsReg;
  let Default = "A64AsmWriterVariantTy::Default";
}

// --- MCTargetDesc/AArch64TargetStreamer.cpp ---

def A64_MarkBTIProperty : Option<
    "aarch64-mark-bti-property",
    "Add .note.gnu.property with BTI to assembly files",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

// --- MCTargetDesc/AArch64MCLFIRewriter.cpp ---

def A64_LFIGuardElim : Option<
    "aarch64-lfi-guard-elim",
    "Enable the LFI guard elimination optimization",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- AArch64CodeLayoutOpt.cpp ---

def A64_CodeLayoutOptEnable : BitsOption<
    "aarch64-code-layout-opt-enable",
    "Enable code alignment optimization for instruction pairs",
    "A64CodeLayoutOpt"> {
  let Registry = AArch64OptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

def A64_CodeLayoutOptAlignFunctions : Option<
    "aarch64-code-layout-opt-align-functions",
    "Function alignment in bytes for code layout optimization "
    "(must be a power of 2)",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "64";
  let Hidden = 1;
}

// --- AArch64PointerAuth.cpp ---

def A64_CfiLlvmSetRaSignState : Option<
    "aarch64-cfi-llvm-set-ra-sign-state",
    "Control emission of .cfi_set_ra_state for PAC return address "
    "signing CFI",
    "A64SetRAStateMode"> {
  let Registry = AArch64OptsReg;
  let Default = "pauth-lr";
  let Hidden = 1;
}

// --- AArch64PTrueCoalescing.cpp ---

def A64_EnablePTrueCoalescing : Option<
    "aarch64-enable-ptrue-coalescing",
    "Enable coalescing of compatible AArch64 SVE PTRUE instructions",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def A64_UMOVFoldLimit : Option<
    "aarch64-umov-fold-scan-limit",
    "How far back to scan from a GPR store to find a UMOV that can be folded "
    "into a direct FPR store.",
    "unsigned"> {
  let Registry = AArch64OptsReg;
  let Default = "16";
  let Hidden = 1;
}

// --- AArch64ISelLowering.cpp ---

// Development flag to allow incremental bring up. Will be removed once the
// implementation is complete.
def A64_EnableSVEFixedLengthBfloatSupport : Option<
    "aarch64-sve-vls-bfloat-support",
    "Use SVE for fixed-length vector bfloat operations",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "false";
  let Hidden = 1;
}

// --- AArch64InstrInfo.cpp ---

def A64_UseCompactUnwindFrameRecordForOutlinedFunctions : Option<
    "aarch64-outliner-compact-unwind-frame",
    "Use a frame record for Mach-O non-leaf outlined functions",
    "bool"> {
  let Registry = AArch64OptsReg;
  let Default = "true";
  let Hidden = 1;
}
