//===- AArch64Options.td - Option defs for AArch64 -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def AArch64Options : OptionsStruct<"llvm"> {}

// =========================================================================
// Enum types
// =========================================================================

// --- AArch64AsmPrinter.cpp ---

def A64PtrauthCheckModeDefault : EnumMember<"Default", "">;
def A64PtrauthCheckModeUnchecked : EnumMember<"Unchecked", "none">;
def A64PtrauthCheckModePoison : EnumMember<"Poison", "poison">;
def A64PtrauthCheckModeTrap : EnumMember<"Trap", "trap">;

def A64PtrauthCheckModeEnum : OptionEnum<"A64PtrauthCheckMode", [
    A64PtrauthCheckModeDefault, A64PtrauthCheckModeUnchecked,
    A64PtrauthCheckModePoison, A64PtrauthCheckModeTrap]>;

// --- AArch64StackTagging.cpp ---

def A64StackTaggingRecordStackHistoryModeNone : EnumMember<"None", "none">;
def A64StackTaggingRecordStackHistoryModeInstr : EnumMember<"Instr", "instr">;

def A64StackTaggingRecordStackHistoryModeEnum :
    OptionEnum<"A64StackTaggingRecordStackHistoryMode",
    [A64StackTaggingRecordStackHistoryModeNone,
     A64StackTaggingRecordStackHistoryModeInstr]>;

// --- AArch64StackTaggingPreRA.cpp ---

def A64UncheckedLdStModeNever : EnumMember<"Never", "never">;
def A64UncheckedLdStModeSafe : EnumMember<"Safe", "safe">;
def A64UncheckedLdStModeAlways : EnumMember<"Always", "always">;

def A64UncheckedLdStModeEnum : OptionEnum<"A64UncheckedLdStMode",
    [A64UncheckedLdStModeNever, A64UncheckedLdStModeSafe,
     A64UncheckedLdStModeAlways]>;

// --- MCTargetDesc/AArch64MCAsmInfo.cpp ---

// Generic and Apple are listed before Default so they keep ordinals 0/1,
// matching the assembler-dialect indices AssemblerDialect is set to
// (AArch64MCAsmInfo.cpp casts them directly to unsigned); Default is a
// sentinel only ever compared by equality, never cast to an int, so its own
// ordinal doesn't matter. Same "sentinel goes last" pattern as
// NVPTXOptions.td's NVPTXDivPrecisionTargetDefault.
def A64AsmWriterVariantTyGeneric : EnumMember<"Generic", "generic">;
def A64AsmWriterVariantTyApple : EnumMember<"Apple", "apple">;
def A64AsmWriterVariantTyDefault : EnumMember<"Default", "">;

def A64AsmWriterVariantTyEnum : OptionEnum<"A64AsmWriterVariantTy", [
    A64AsmWriterVariantTyGeneric, A64AsmWriterVariantTyApple,
    A64AsmWriterVariantTyDefault]>;

// --- AArch64CodeLayoutOpt.cpp ---

def A64CodeLayoutOptCmpCsel : EnumMember<"CmpCsel", "cmp-csel">;
def A64CodeLayoutOptFcmpFcsel : EnumMember<"FcmpFcsel", "fcmp-fcsel">;
def A64CodeLayoutOptNone : EnumMember<"None", "none">;

def A64CodeLayoutOptEnum : OptionEnum<"A64CodeLayoutOpt",
    [A64CodeLayoutOptCmpCsel, A64CodeLayoutOptFcmpFcsel,
     A64CodeLayoutOptNone]>;

// --- AArch64PointerAuth.cpp ---

def A64SetRAStateModeNever : EnumMember<"Never", "never">;
def A64SetRAStateModePAuthLR : EnumMember<"PAuthLR", "pauth-lr">;
def A64SetRAStateModeAlways : EnumMember<"Always", "always">;

def A64SetRAStateModeEnum : OptionEnum<"A64SetRAStateMode",
    [A64SetRAStateModeNever, A64SetRAStateModePAuthLR,
     A64SetRAStateModeAlways]>;

// --- AArch64Subtarget.cpp ---

// Self-contained (not External): AArch64PAuth::AuthCheckMethod lives in the
// private lib/Target/AArch64/AArch64PointerAuth.h header, unreachable from
// the 8 external driver tools that must be able to include AArch64Options.h
// (see the comment on A64_AuthenticatedLRCheckMethod below for the full
// rationale). Member order here is arbitrary -- AArch64Subtarget.cpp's
// toAuthCheckMethod() maps each enumerator by name, never by ordinal, to
// the real AArch64PAuth::AuthCheckMethod (whose own declaration order,
// None/DummyLoad/HighBitsNoTBI/XPACHint/XPAC, does NOT match this list).
def A64AuthCheckMethodNone : EnumMember<"None", "none">;
def A64AuthCheckMethodDummyLoad : EnumMember<"DummyLoad", "load">;
def A64AuthCheckMethodHighBitsNoTBI : EnumMember<"HighBitsNoTBI", "high-bits-notbi">;
def A64AuthCheckMethodXPAC : EnumMember<"XPAC", "xpac">;
def A64AuthCheckMethodXPACHint : EnumMember<"XPACHint", "xpac-hint">;

def A64AuthCheckMethodEnum : OptionEnum<"A64AuthCheckMethod", [
    A64AuthCheckMethodNone, A64AuthCheckMethodDummyLoad,
    A64AuthCheckMethodHighBitsNoTBI, A64AuthCheckMethodXPAC,
    A64AuthCheckMethodXPACHint]>;

// =========================================================================
// Options
// =========================================================================

// --- AArch64AsmPrinter.cpp ---

defm A64_PtrauthAuthChecks : EnumField<
    "aarch64-ptrauth-auth-checks", A64PtrauthCheckModeEnum,
    "A64PtrauthCheckMode::Default",
    "Check pointer authentication auth/resign failures",
    /*hidden=*/1>;

// --- AArch64A57FPLoadBalancing.cpp ---

defm A64_A57FPLoadBalancingForceAll : BoolField<
    "aarch64-a57-fp-load-balancing-force-all", "false",
    "Always modify dest registers regardless of color",
    /*hidden=*/1>;

defm A64_A57FPLoadBalancingOverride : ValueField<
    "aarch64-a57-fp-load-balancing-override", "unsigned", "0",
    "Ignore balance information, always return " #
    "(1: Even, 2: Odd).",
    /*hidden=*/1>;

// --- AArch64AdvSIMDScalarPass.cpp ---

defm A64_SIMDScalarForceAll : BoolField<
    "aarch64-simd-scalar-force-all", "false",
    "Force use of AdvSIMD scalar instructions everywhere",
    /*hidden=*/1>;

// --- AArch64Arm64ECCallLowering.cpp ---

defm A64_LowerDirectToIndirect : BoolField<
    "arm64ec-lower-direct-to-indirect", "true",
    "",
    /*hidden=*/1>;

defm A64_GenerateThunks : BoolField<
    "arm64ec-generate-thunks", "true",
    "",
    /*hidden=*/1>;

// --- AArch64ConditionalCompares.cpp ---

defm A64_CCMPLimit : ValueField<
    "aarch64-ccmp-limit", "unsigned", "30",
    "Maximum number of instructions per speculated block.",
    /*hidden=*/1>;

defm A64_StressCCMP : BoolField<
    "aarch64-stress-ccmp", "false",
    "Turn all knobs to 11",
    /*hidden=*/1>;

// --- AArch64FrameLowering.cpp ---

defm A64_EnableRedZone : BoolField<
    "aarch64-redzone", "false",
    "enable use of redzone on AArch64",
    /*hidden=*/1>;

defm A64_StackTaggingMergeSetTag : BoolField<
    "stack-tagging-merge-settag", "true",
    "merge settag instruction in function epilog",
    /*hidden=*/1>;

defm A64_OrderFrameObjects : BoolField<
    "aarch64-order-frame-objects", "true",
    "sort stack allocations",
    /*hidden=*/1>;

defm A64_SplitSVEObjects : BoolField<
    "aarch64-split-sve-objects", "true",
    "Split allocation of ZPR & PPR objects",
    /*hidden=*/1>;

defm A64_HomogeneousPrologEpilog : BoolField<
    "homogeneous-prolog-epilog", "false",
    "Emit homogeneous prologue and epilogue for the size " #
    "optimization (default = off)",
    /*hidden=*/1>;

defm A64_StackHazardRemarkSize : ValueField<
    "aarch64-stack-hazard-remark-size", "unsigned", "0",
    "",
    /*hidden=*/1>;

defm A64_StackHazardInNonStreaming : BoolField<
    "aarch64-stack-hazard-in-non-streaming", "false",
    "",
    /*hidden=*/1>;

defm A64_DisableMultiVectorSpillFill : BoolField<
    "aarch64-disable-multivector-spill-fill", "false",
    "Disable use of LD/ST pairs for SME2 or SVE2p1",
    /*hidden=*/1>;

// --- AArch64InstrInfo.cpp ---

defm A64_CBDisplacementBits : ValueField<
    "aarch64-cb-offset-bits", "unsigned", "9",
    "Restrict range of CB instructions (DEBUG)",
    /*hidden=*/1>;

defm A64_TBZDisplacementBits : ValueField<
    "aarch64-tbz-offset-bits", "unsigned", "14",
    "Restrict range of TB[N]Z instructions (DEBUG)",
    /*hidden=*/1>;

defm A64_CBZDisplacementBits : ValueField<
    "aarch64-cbz-offset-bits", "unsigned", "19",
    "Restrict range of CB[N]Z instructions (DEBUG)",
    /*hidden=*/1>;

defm A64_BCCDisplacementBits : ValueField<
    "aarch64-bcc-offset-bits", "unsigned", "19",
    "Restrict range of Bcc instructions (DEBUG)",
    /*hidden=*/1>;

defm A64_BDisplacementBits : ValueField<
    "aarch64-b-offset-bits", "unsigned", "26",
    "Restrict range of B instructions (DEBUG)",
    /*hidden=*/1>;

defm A64_GatherOptSearchLimit : ValueField<
    "aarch64-search-limit", "unsigned", "2048",
    "Restrict range of instructions to search for the " #
    "machine-combiner gather pattern optimization",
    /*hidden=*/1>;

// --- AArch64ISelLowering.cpp ---

defm A64_ELFLocalDynamicTLSGeneration : BoolField<
    "aarch64-elf-ldtls-generation", "false",
    "Allow AArch64 Local Dynamic TLS code generation",
    /*hidden=*/1>;

defm A64_EnableLogicalImm : BoolField<
    "aarch64-enable-logical-imm", "true",
    "Enable AArch64 logical imm instruction " #
    "optimization",
    /*hidden=*/1>;

defm A64_EnableMGatherCombine : BoolField<
    "aarch64-enable-mgather-combine", "true",
    "Combine extends of AArch64 masked " #
    "gather intrinsics",
    /*hidden=*/1>;

defm A64_EnableExtToTBL : BoolField<
    "aarch64-enable-ext-to-tbl", "true",
    "Combine ext and trunc to TBL",
    /*hidden=*/1>;

defm A64_MaxXors : ValueField<
    "aarch64-max-xors", "unsigned", "16",
    "Maximum of xors",
    /*hidden=*/1>;

defm A64_EnableSVEGISel : BoolField<
    "aarch64-enable-gisel-sve", "false",
    "Enable / disable SVE scalable vectors in Global ISel",
    /*hidden=*/1>;

defm A64_UseFEATCPACodegen : BoolField<
    "aarch64-use-featcpa-codegen", "false",
    "Generate ISD::PTRADD nodes for pointer arithmetic in " #
    "SelectionDAG for FEAT_CPA",
    /*hidden=*/1>;

defm A64_BrMergingBaseCostThresh : ValueField<
    "aarch64-br-merging-base-cost", "int", "2",
    "Cost threshold for merging multiple conditionals into one branch " #
    "versus splitting into multiple branches: conditionals are merged when " #
    "their instruction cost is below this limit and split above it. Set to " #
    "-1 to never merge branches.",
    /*hidden=*/1>;

defm A64_BrMergingCcmpBias : ValueField<
    "aarch64-br-merging-ccmp-bias", "int", "6",
    "Increases 'aarch64-br-merging-base-cost' to account for the " #
    "CCMP instruction, which is always available on AArch64 and " #
    "makes merging branch conditions cheaper.",
    /*hidden=*/1>;

defm A64_BrMergingCbzTbnzBias : ValueField<
    "aarch64-br-merging-cbz-tbnz-bias", "int", "6",
    "Decreases 'aarch64-br-merging-base-cost' when a condition can " #
    "lower to a single CBZ/CBNZ or TBZ/TBNZ compare-and-branch, to " #
    "bias toward splitting. Set to 0 to disable.",
    /*hidden=*/1>;

defm A64_BrMergingLikelyBias : ValueField<
    "aarch64-br-merging-likely-bias", "int", "0",
    "Increases 'aarch64-br-merging-base-cost' when all conditionals " #
    "are likely to be executed, biasing toward merging. Set to -1 to " #
    "never merge likely branches.",
    /*hidden=*/1>;

defm A64_BrMergingUnlikelyBias : ValueField<
    "aarch64-br-merging-unlikely-bias", "int", "-1",
    "Decreases 'aarch64-br-merging-base-cost' when all conditionals are " #
    "unlikely to be executed, biasing toward splitting. Set to -1 to never " #
    "merge unlikely branches.",
    /*hidden=*/1>;

defm A64_UseConditionalFPMRWrite : BoolField<
    "aarch64-use-conditional-fpmr-write", "false",
    "Only write FPMR when the requested value differs from the " #
    "current value",
    /*hidden=*/1>;

// --- AArch64LoadStoreOptimizer.cpp ---

defm A64_LdStLimit : ValueField<
    "aarch64-load-store-scan-limit", "unsigned", "20",
    "",
    /*hidden=*/1>;

defm A64_UpdateLimit : ValueField<
    "aarch64-update-scan-limit", "unsigned", "100",
    "",
    /*hidden=*/1>;

defm A64_LdStConstLimit : ValueField<
    "aarch64-load-store-const-scan-limit", "unsigned", "10",
    "",
    /*hidden=*/1>;

defm A64_EnableRenaming : BoolField<
    "aarch64-load-store-renaming", "true",
    "",
    /*hidden=*/1>;

// --- AArch64LowerHomogeneousPrologEpilog.cpp ---

defm A64_FrameHelperSizeThreshold : ValueField<
    "frame-helper-size-threshold", "int", "2",
    "The minimum number of instructions that are outlined in a frame " #
    "helper (default = 2)",
    /*hidden=*/1>;

// --- AArch64PromoteConstant.cpp ---

defm A64_StressPromoteConst : BoolField<
    "aarch64-stress-promote-const", "false",
    "Promote all vector constants",
    /*hidden=*/1>;

// --- AArch64SelectionDAGInfo.cpp ---

defm A64_LowerToSMERoutines : BoolField<
    "aarch64-lower-to-sme-routines", "true",
    "Enable AArch64 SME memory operations " #
    "to lower to librt functions",
    /*hidden=*/1>;

defm A64_UseMOPS : BoolField<
    "aarch64-use-mops", "true",
    "Enable AArch64 MOPS instructions " #
    "for memcpy/memset/memmove",
    /*hidden=*/1>;

// --- AArch64SpeculationHardening.cpp ---

defm A64_HardenLoads : BoolField<
    "aarch64-slh-loads", "true",
    "Sanitize loads from memory.",
    /*hidden=*/1>;

// --- AArch64StackTagging.cpp ---

defm A64_StackTaggingMergeInit : OptionalValueField<
    "stack-tagging-merge-init", "bool", "true",
    "merge stack variable initializers with tagging when possible",
    /*hidden=*/1>;

defm A64_StackTaggingUseStackSafety : OptionalValueField<
    "stack-tagging-use-stack-safety", "bool", "true",
    "Use Stack Safety analysis results",
    /*hidden=*/1>;

defm A64_StackTaggingScanLimit : ValueField<
    "stack-tagging-merge-init-scan-limit", "unsigned", "40",
    "",
    /*hidden=*/1>;

defm A64_StackTaggingMergeInitSizeLimit : ValueField<
    "stack-tagging-merge-init-size-limit", "unsigned", "272",
    "",
    /*hidden=*/1>;

// Hand-authored (not EnumField<>): std::optional<> field so the consumer
// can distinguish "not specified" from the "none" enumerator, matching
// AArch64StackTagging.cpp's getClRecordStackHistoryWasSpecified() check --
// same shape as X86_AsmSyntax/PPC_CmpInGPR.
def A64_RecordStackHistory : Joined<["-", "--"],
    "stack-tagging-record-stack-history=">,
    HelpText<"Record stack frames with tagged allocations in a thread-local "
             "ring buffer">,
    Flags<[HelpHidden]>,
    Field<"std::optional<A64StackTaggingRecordStackHistoryMode>",
          "A64_RecordStackHistory", "std::nullopt">,
    FieldEnum<A64StackTaggingRecordStackHistoryModeEnum>;
def A64_RecordStackHistory_sep : Separate<["-", "--"],
    "stack-tagging-record-stack-history">,
    Flags<[HelpHidden]>, Alias<!cast<Option>(A64_RecordStackHistory)>;

// --- AArch64StackTaggingPreRA.cpp ---

defm A64_UncheckedLdSt : EnumField<
    "stack-tagging-unchecked-ld-st", A64UncheckedLdStModeEnum,
    "A64UncheckedLdStMode::Safe",
    "Unconditionally apply unchecked-ld-st optimization (even for large "
    "stack frames, or in the presence of variable sized allocas).",
    /*hidden=*/1>;

defm A64_StackTaggingFirstSlotOpt : BoolField<
    "stack-tagging-first-slot-opt", "true",
    "Apply first slot optimization for stack tagging " #
    "(eliminate ADDG Rt, Rn, 0, 0).",
    /*hidden=*/1>;

// --- AArch64Subtarget.cpp ---

defm A64_EnableEarlyIfConvert : BoolField<
    "aarch64-early-ifcvt", "true",
    "Enable the early if converter pass",
    /*hidden=*/1>;

defm A64_UseAddressTopByteIgnored : BoolField<
    "aarch64-use-tbi", "false",
    "Assume that top byte of an address is ignored",
    /*hidden=*/1>;

defm A64_MachOUseNonLazyBind : BoolField<
    "aarch64-macho-enable-nonlazybind", "false",
    "Call nonlazybind functions via direct GOT load for Mach-O",
    /*hidden=*/1>;

defm A64_UseAA : BoolField<
    "aarch64-use-aa", "true",
    "Enable the use of AA during codegen.",
    /*hidden=*/0>;

defm A64_OverrideVectorInsertExtractBaseCost : OptionalField<
    "aarch64-insert-extract-base-cost", "unsigned",
    "Base cost of vector insert/extract element",
    /*hidden=*/1>;

defm A64_ReservedRegsForRA : ListField<
    "reserve-regs-for-regalloc", "std::string",
    "Reserve physical "
    "registers, so they can't be used by register allocator. "
    "Should only be used for testing register allocator.",
    /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/1>;

// A64AuthCheckMethodEnum is deliberately its own self-contained enum, NOT
// an External FieldEnum reusing AArch64PAuth::AuthCheckMethod: that type
// lives in the private lib/Target/AArch64/AArch64PointerAuth.h header, but
// the generated struct emits `FieldType KeyPath = Default;` as an in-class
// default member initializer, which requires the enum's complete definition
// in every TU that includes AArch64Options.h -- including 8 external
// driver tools (opt, llc, llvm-mca, llvm-lto, llvm-objdump, llvm-profgen,
// llvm-mc, clang's ExecuteCompilerInvocation.cpp) that have no include-path
// access to that private header. A closed-membership enum here plus an
// explicit switch-based conversion confined to AArch64Subtarget.cpp (the
// only place that needs an AArch64PAuth::AuthCheckMethod value) avoids the
// layering violation; see AArch64Subtarget.cpp's toAuthCheckMethod().
defm A64_AuthenticatedLRCheckMethod : EnumField<
    "aarch64-authenticated-lr-check-method", A64AuthCheckMethodEnum,
    "A64AuthCheckMethod::None",
    "Override the variant of check applied "
    "to authenticated LR during tail call",
    /*hidden=*/1>;

defm A64_MinimumJumpTableEntries : OptionalField<
    "aarch64-min-jump-table-entries", "unsigned",
    "Set minimum number of entries to use a jump table on AArch64",
    /*hidden=*/1>;

defm A64_StreamingHazardSize : OptionalField<
    "aarch64-stack-hazard-size", "unsigned",
    "Hazard size for streaming mode memory accesses. 0 = disabled.",
    /*hidden=*/1>;

def A64_StreamingHazardSizeAlias : Joined<["-", "--"],
    "aarch64-streaming-hazard-size=">,
    Flags<[HelpHidden]>, Alias<!cast<Option>(A64_StreamingHazardSize)>;
def A64_StreamingHazardSizeAlias_sep : Separate<["-", "--"],
    "aarch64-streaming-hazard-size">,
    Flags<[HelpHidden]>, Alias<!cast<Option>(A64_StreamingHazardSize)>;

defm A64_VScaleForTuning : OptionalField<
    "sve-vscale-for-tuning", "unsigned",
    "Force a vscale for tuning factor for SVE",
    /*hidden=*/1>;

defm A64_EnableSubregLivenessTracking : BoolField<
    "aarch64-enable-subreg-liveness-tracking", "false",
    "Enable subreg liveness tracking",
    /*hidden=*/1>;

defm A64_UseScalarIncVL : OptionalValueField<
    "sve-use-scalar-inc-vl", "bool", "true",
    "Prefer add+cnt over addvl/inc/dec",
    /*hidden=*/1>;

// --- AArch64TargetMachine.cpp ---

defm A64_EnableCCMP : BoolField<
    "aarch64-enable-ccmp", "true",
    "Enable the CCMP formation pass",
    /*hidden=*/1>;

defm A64_EnableCondBrTuning : BoolField<
    "aarch64-enable-cond-br-tune", "true",
    "Enable the conditional branch tuning pass",
    /*hidden=*/1>;

defm A64_EnableCopyPropagation : BoolField<
    "aarch64-enable-copy-propagation", "true",
    "Enable the copy propagation with AArch64 copy instr",
    /*hidden=*/1>;

defm A64_EnableMCR : BoolField<
    "aarch64-enable-mcr", "true",
    "Enable the machine combiner pass",
    /*hidden=*/1>;

defm A64_EnableStPairSuppress : BoolField<
    "aarch64-enable-stp-suppress", "true",
    "Suppress STP for AArch64",
    /*hidden=*/1>;

defm A64_EnableAdvSIMDScalar : BoolField<
    "aarch64-enable-simd-scalar", "false",
    "Enable use of AdvSIMD scalar integer instructions",
    /*hidden=*/1>;

defm A64_EnablePromoteConstant : BoolField<
    "aarch64-enable-promote-const", "true",
    "Enable the promote constant pass",
    /*hidden=*/1>;

defm A64_EnableCollectLOH : BoolField<
    "aarch64-enable-collect-loh", "true",
    "Enable the pass that emits the linker optimization hints (LOH)",
    /*hidden=*/1>;

defm A64_EnableDeadRegisterElimination : BoolField<
    "aarch64-enable-dead-defs", "true",
    "Enable the pass that removes dead" #
    " definitions and replaces stores to" #
    " them with stores to the zero" #
    " register",
    /*hidden=*/1>;

defm A64_EnableRedundantCopyElimination : BoolField<
    "aarch64-enable-copyelim", "true",
    "Enable the redundant copy elimination pass",
    /*hidden=*/1>;

defm A64_EnableLoadStoreOpt : BoolField<
    "aarch64-enable-ldst-opt", "true",
    "Enable the load/store pair" #
    " optimization pass",
    /*hidden=*/1>;

defm A64_EnableAtomicTidy : BoolField<
    "aarch64-enable-atomic-cfg-tidy", "true",
    "Run SimplifyCFG after expanding atomic operations" #
    " to make use of cmpxchg flow-based information",
    /*hidden=*/1>;

defm A64_EnableEarlyIfConversion : BoolField<
    "aarch64-enable-early-ifcvt", "true",
    "Run early if-conversion",
    /*hidden=*/1>;

defm A64_EnableCondOpt : BoolField<
    "aarch64-enable-condopt", "true",
    "Enable the condition optimizer pass",
    /*hidden=*/1>;

defm A64_EnableGEPOpt : BoolField<
    "aarch64-enable-gep-opt", "false",
    "Enable optimizations on complex GEPs",
    /*hidden=*/1>;

defm A64_EnableSelectOpt : BoolField<
    "aarch64-select-opt", "true",
    "Enable select to branch optimizations",
    /*hidden=*/1>;

defm A64_BranchRelaxation : BoolField<
    "aarch64-enable-branch-relax", "true",
    "Relax out of range conditional branches",
    /*hidden=*/1>;

defm A64_EnableCompressJumpTables : BoolField<
    "aarch64-enable-compress-jump-tables", "true",
    "Use smallest entry possible for jump tables",
    /*hidden=*/1>;

defm A64_EnableGlobalMerge : OptionalValueField<
    "aarch64-enable-global-merge", "bool", "true",
    "Enable the global merge pass",
    /*hidden=*/1>;

defm A64_EnableLoopDataPrefetch : BoolField<
    "aarch64-enable-loop-data-prefetch", "true",
    "Enable the loop data prefetch pass",
    /*hidden=*/1>;

defm A64_EnableGlobalISelAtO : ValueField<
    "aarch64-enable-global-isel-at-O", "int", "0",
    "Enable GlobalISel at or below an opt level (-1 to disable)",
    /*hidden=*/1>;

defm A64_EnableSMEPeepholeOpt : BoolField<
    "enable-aarch64-sme-peephole-opt", "true",
    "Perform SME peephole optimization",
    /*hidden=*/1>;

defm A64_EnableFalkorHWPFFix : BoolField<
    "aarch64-enable-falkor-hwpf-fix", "true",
    "",
    /*hidden=*/1>;

defm A64_EnableBranchTargets : BoolField<
    "aarch64-enable-branch-targets", "true",
    "Enable the AArch64 branch target pass",
    /*hidden=*/1>;

defm A64_SVEVectorBitsMax : ValueField<
    "aarch64-sve-vector-bits-max", "unsigned", "0",
    "Assume SVE vector registers are at most this big, " #
    "with zero meaning no maximum size is assumed.",
    /*hidden=*/1>;

defm A64_SVEVectorBitsMin : ValueField<
    "aarch64-sve-vector-bits-min", "unsigned", "0",
    "Assume SVE vector registers are at least this big, " #
    "with zero meaning no minimum size is assumed.",
    /*hidden=*/1>;

defm A64_ForceStreaming : BoolField<
    "force-streaming", "false",
    "Force the use of streaming code for all functions",
    /*hidden=*/1>;

defm A64_ForceStreamingCompatible : BoolField<
    "force-streaming-compatible", "false",
    "Force the use of streaming-compatible code for all functions",
    /*hidden=*/1>;

defm A64_EnableGISelLoadStoreOptPreLegal : BoolField<
    "aarch64-enable-gisel-ldst-prelegal", "true",
    "Enable GlobalISel's pre-legalizer load/store optimization pass",
    /*hidden=*/1>;

defm A64_EnableGISelLoadStoreOptPostLegal : BoolField<
    "aarch64-enable-gisel-ldst-postlegal", "false",
    "Enable GlobalISel's post-legalizer load/store optimization pass",
    /*hidden=*/1>;

defm A64_EnableSinkFold : BoolField<
    "aarch64-enable-sink-fold", "true",
    "Enable sinking and folding of instruction copies",
    /*hidden=*/1>;

defm A64_EnableMachinePipeliner : BoolField<
    "aarch64-enable-pipeliner", "false",
    "Enable Machine Pipeliner for AArch64",
    /*hidden=*/1>;

defm A64_EnableSRLTSubregToRegMitigation : BoolField<
    "aarch64-srlt-mitigate-sr2r", "true",
    "Enable SUBREG_TO_REG mitigation by adding 'implicit-def' for " #
    "super-regs when using Subreg Liveness Tracking",
    /*hidden=*/1>;

defm A64_EnableSVEShuffleOpt : BoolField<
    "aarch64-enable-sve-shuffle-opts", "true",
    "Enable pattern matching of shuffles that could make use of SVE " #
    "instructions like tbl or the bottom/top variants",
    /*hidden=*/1>;

// --- AArch64TargetObjectFile.cpp ---

defm A64_EmitDebugTLSLocation : BoolField<
    "aarch64-emit-debug-tls-location", "false",
    "Emit the TLS DWARF location with DTPREL relocation for AArch64",
    /*hidden=*/1>;

// --- AArch64TargetTransformInfo.cpp ---

defm A64_EnableFalkorHWPFUnrollFix : BoolField<
    "enable-falkor-hwpf-unroll-fix", "true",
    "",
    /*hidden=*/1>;

defm A64_SVEPreferFixedOverScalableIfEqualCost : OptionalValueField<
    "sve-prefer-fixed-over-scalable-if-equal", "bool", "true",
    "",
    /*hidden=*/1>;

defm A64_SVEGatherOverhead : OptionalField<
    "sve-gather-overhead", "unsigned",
    "",
    /*hidden=*/1>;

defm A64_SVEScatterOverhead : OptionalField<
    "sve-scatter-overhead", "unsigned",
    "",
    /*hidden=*/1>;

defm A64_SVETailFoldInsnThreshold : ValueField<
    "sve-tail-folding-insn-threshold", "unsigned", "15",
    "",
    /*hidden=*/1>;

defm A64_NeonNonConstStrideOverhead : ValueField<
    "neon-nonconst-stride-overhead", "unsigned", "10",
    "",
    /*hidden=*/1>;

defm A64_CallPenaltyChangeSM : ValueField<
    "call-penalty-sm-change", "unsigned", "5",
    "Penalty of calling a function that requires a change to PSTATE.SM",
    /*hidden=*/1>;

defm A64_InlineCallPenaltyChangeSM : ValueField<
    "inline-call-penalty-sm-change", "unsigned", "10",
    "Penalty of inlining a call that requires a change to PSTATE.SM",
    /*hidden=*/1>;

defm A64_EnableOrLikeSelectOpt : BoolField<
    "enable-aarch64-or-like-select", "true",
    "",
    /*hidden=*/1>;

defm A64_EnableLSRCostOpt : BoolField<
    "enable-aarch64-lsr-cost-opt", "true",
    "",
    /*hidden=*/1>;

defm A64_BaseHistCntCost : ValueField<
    "aarch64-base-histcnt-cost", "unsigned", "8",
    "The cost of a histcnt instruction",
    /*hidden=*/1>;

defm A64_DMBLookaheadThreshold : ValueField<
    "dmb-lookahead-threshold", "unsigned", "10",
    "The number of instructions to search for a redundant dmb",
    /*hidden=*/1>;

defm A64_ForceUnrollThreshold : ValueField<
    "aarch64-force-unroll-threshold", "int", "0",
    "Threshold for forced unrolling of small loops in AArch64",
    /*hidden=*/1>;

defm A64_SVETailFolding : ValueField<
    "sve-tail-folding", "std::string", "\"\"",
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
    "\nnoreverse     Inverse of above">;

defm A64_EnableFixedwidthAutovecInStreamingMode : BoolField<
    "enable-fixedwidth-autovec-in-streaming-mode", "false",
    "",
    /*hidden=*/1>;

defm A64_EnableScalableAutovecInStreamingMode : BoolField<
    "enable-scalable-autovec-in-streaming-mode", "false",
    "",
    /*hidden=*/1>;

// --- GISel/AArch64PostLegalizerCombiner.cpp ---

defm A64_EnableConsecutiveMemOpOpt : BoolField<
    "aarch64-postlegalizer-consecutive-memops", "true",
    "Enable consecutive memop optimization " #
    "in AArch64PostLegalizerCombiner",
    /*hidden=*/1>;

// --- MCTargetDesc/AArch64MCAsmInfo.cpp ---

defm A64_AsmWriterVariant : EnumField<
    "aarch64-neon-syntax", A64AsmWriterVariantTyEnum,
    "A64AsmWriterVariantTy::Default",
    "Choose style of NEON code to emit from AArch64 backend:">;

// --- MCTargetDesc/AArch64TargetStreamer.cpp ---

defm A64_MarkBTIProperty : BoolField<
    "aarch64-mark-bti-property", "false",
    "Add .note.gnu.property with BTI to assembly files",
    /*hidden=*/1>;

// --- MCTargetDesc/AArch64MCLFIRewriter.cpp ---

defm A64_LFIGuardElim : BoolField<
    "aarch64-lfi-guard-elim", "true",
    "Enable the LFI guard elimination optimization",
    /*hidden=*/1>;

// --- AArch64CodeLayoutOpt.cpp ---

defm A64_CodeLayoutOptEnable : EnumListField<
    "aarch64-code-layout-opt-enable", A64CodeLayoutOptEnum,
    "Enable code alignment optimization for instruction pairs",
    /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/1>;

defm A64_CodeLayoutOptAlignFunctions : ValueField<
    "aarch64-code-layout-opt-align-functions", "unsigned", "64",
    "Function alignment in bytes for code layout optimization " #
    "(must be a power of 2)",
    /*hidden=*/1>;

// --- AArch64PointerAuth.cpp ---

defm A64_CfiLlvmSetRaSignState : EnumField<
    "aarch64-cfi-llvm-set-ra-sign-state", A64SetRAStateModeEnum,
    "A64SetRAStateMode::PAuthLR",
    "Control emission of .cfi_set_ra_state for PAC return address "
    "signing CFI",
    /*hidden=*/1>;

// --- AArch64PTrueCoalescing.cpp ---

defm A64_EnablePTrueCoalescing : BoolField<
    "aarch64-enable-ptrue-coalescing", "false",
    "Enable coalescing of compatible AArch64 SVE PTRUE instructions",
    /*hidden=*/1>;

defm A64_UMOVFoldLimit : ValueField<
    "aarch64-umov-fold-scan-limit", "unsigned", "16",
    "How far back to scan from a GPR store to find a UMOV that can be folded " #
    "into a direct FPR store.",
    /*hidden=*/1>;

// --- AArch64ISelLowering.cpp ---

// Development flag to allow incremental bring up. Will be removed once the
// implementation is complete.
defm A64_EnableSVEFixedLengthBfloatSupport : BoolField<
    "aarch64-sve-vls-bfloat-support", "false",
    "Use SVE for fixed-length vector bfloat operations",
    /*hidden=*/1>;

// --- AArch64InstrInfo.cpp ---

defm A64_UseCompactUnwindFrameRecordForOutlinedFunctions : BoolField<
    "aarch64-outliner-compact-unwind-frame", "true",
    "Use a frame record for Mach-O non-leaf outlined functions",
    /*hidden=*/1>;
