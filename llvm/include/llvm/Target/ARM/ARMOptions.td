//===- ARMOptions.td - OptTable option defs for ARM -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def ARMOptions : OptionsStruct<"llvm"> {}

// --- ARMCallLowering.cpp ---

defm ARM_EnableGISelBigEndian : BoolField<"enable-arm-gisel-bigendian",
    "false", "Enable Global-ISel Big Endian Lowering", /*hidden=*/1>;

// --- ARMConstantIslandPass.cpp ---

defm ARM_AdjustJumpTableBlocks : BoolField<"arm-adjust-jump-tables",
    "true", "Adjust basic block layout to better use TB[BH]", /*hidden=*/1>;

defm ARM_CPMaxIteration : ValueField<"arm-constant-island-max-iteration",
    "unsigned", "30", "The max number of iteration for converge",
    /*hidden=*/1>;

// --- ARMExpandPseudoInsts.cpp ---

defm ARM_VerifyARMPseudo : BoolField<"verify-arm-pseudo-expand",
    "false", "Verify machine code after expanding ARM pseudos",
    /*hidden=*/1>;

// --- ARMFrameLowering.cpp ---

defm ARM_SpillAlignedNEONRegs : BoolField<"align-neon-spills",
    "true", "Align ARM NEON spills in prolog and epilog", /*hidden=*/1>;

// --- ARMHazardRecognizer.cpp ---

// Hand-authored (not ValueField) so the field is std::optional<int>: callers
// need to distinguish "never specified" from any concrete value, including
// the sentinel -1, which clv2's wasOptSpecified<> tracked separately from
// the resolved value. Mirrors ValueField's own Joined+Separate expansion.
def ARM_DataBankMask : Joined<["-", "--"], "arm-data-bank-mask=">,
    Flags<[HelpHidden]>,
    Field<"std::optional<int>", "ARM_DataBankMask", "std::nullopt">;
def ARM_DataBankMask_sep : Separate<["-", "--"], "arm-data-bank-mask">,
    Flags<[HelpHidden]>, Alias<ARM_DataBankMask>;

// OptionalValueField gives a std::optional<bool> field: absent means the
// flag was never specified (matching clv2's wasOptSpecified<> check), while
// still accepting the exact same "-arm-assume-itcm-bankconflict" (bare) and
// "-arm-assume-itcm-bankconflict=<bool>" spellings as before.
defm ARM_AssumeITCMConflict : OptionalValueField<
    "arm-assume-itcm-bankconflict", "bool", "true", "", /*hidden=*/1>;

// --- ARMISelDAGToDAG.cpp ---

defm ARM_DisableShifterOp : BoolField<"disable-shifter-op",
    "false", "Disable isel of shifter-op", /*hidden=*/1>;

// --- ARMISelLowering.cpp ---

defm ARM_Interworking : BoolField<"arm-interworking",
    "true", "Enable / disable ARM interworking (for debugging only)",
    /*hidden=*/1>;

defm ARM_EnableConstpoolPromotion : BoolField<"arm-promote-constant",
    "false", "Enable / disable promotion of unnamed_addr constants into "
    "constant pools", /*hidden=*/1>;

defm ARM_ConstpoolPromotionMaxSize : ValueField<
    "arm-promote-constant-max-size", "unsigned", "64",
    "Maximum size of constant to promote into a constant pool",
    /*hidden=*/1>;

defm ARM_ConstpoolPromotionMaxTotal : ValueField<
    "arm-promote-constant-max-total", "unsigned", "128",
    "Maximum size of ALL constants to promote into a constant pool",
    /*hidden=*/1>;

defm ARM_MVEMaxSupportedInterleaveFactor : ValueField<
    "mve-max-interleave-factor", "unsigned", "2",
    "Maximum interleave factor for MVE VLDn to generate.", /*hidden=*/1>;

defm ARM_MaxBaseUpdatesToCheck : ValueField<
    "arm-max-base-updates-to-check", "unsigned", "64",
    "Maximum number of base-updates to check generating postindex.",
    /*hidden=*/1>;

// --- ARMLoadStoreOptimizer.cpp ---

defm ARM_AssumeMisalignedLoadStores : BoolField<
    "arm-assume-misaligned-load-store", "false",
    "Be more conservative in ARM load/store opt", /*hidden=*/1>;

defm ARM_InstReorderLimit : ValueField<
    "arm-prera-ldst-opt-reorder-limit", "unsigned", "8", "", /*hidden=*/1>;

// --- ARMLowOverheadLoops.cpp ---

defm ARM_DisableTailPredication : BoolField<"arm-loloops-disable-tailpred",
    "false", "Disable tail-predication in the ARM LowOverheadLoop pass",
    /*hidden=*/1>;

defm ARM_DisableOmitDLS : BoolField<"arm-disable-omit-dls",
    "false", "Disable omitting 'dls lr, lr' instructions", /*hidden=*/1>;

// --- ARMParallelDSP.cpp ---

defm ARM_DisableParallelDSP : BoolField<"disable-arm-parallel-dsp",
    "false", "", /*hidden=*/1>;

defm ARM_NumLoadLimit : ValueField<"arm-parallel-dsp-load-limit",
    "unsigned", "16", "Limit the number of loads analysed", /*hidden=*/1>;

// --- ARMSelectionDAGInfo.cpp ---

def ARMTPLoopMemTransfer : OptionEnum<"ARMTPLoopMemTransfer", [
    EnumMember<"ForceDisabled", "force-disabled">,
    EnumMember<"ForceEnabled", "force-enabled">,
    EnumMember<"Allow", "allow">]>;

defm ARM_EnableMemtransferTPLoop : EnumField<"arm-memtransfer-tploop",
    ARMTPLoopMemTransfer, "ARMTPLoopMemTransfer::ForceDisabled",
    "Control conversion of memcpy to Tail predicated loops (WLSTP)",
    /*hidden=*/1>;

// --- ARMSubtarget.cpp ---

defm ARM_UseFusedMulOps : BoolField<"arm-use-mulops",
    "true", "", /*hidden=*/1>;

// ARM_ITDefaultIT and ARM_ITRestrictedIT share a single field: the two flags
// are mutually exclusive and, when both are given, clv2 used whichever came
// last on the command line. The generated apply() already processes Args in
// encounter order (see OptionParserEmitter.cpp), so two Flags that Assign
// into the same KeyPath naturally reproduce "last flag wins" without needing
// any position/specified tracking of their own.
def ARMITMode : OptionEnum<"ARMITMode", [
    EnumMember<"DefaultIT", "default-it">,
    EnumMember<"RestrictedIT", "restrict-it">]>;

def ARM_ITDefaultIT : Flag<["-", "--"], "arm-default-it">,
    HelpText<"Generate any type of IT block">, Flags<[HelpHidden]>,
    Field<"ARMITMode", "ARM_ITMode", "ARMITMode::DefaultIT">,
    Assign<"ARM_ITMode", "ARMITMode::DefaultIT">;
def ARM_ITRestrictedIT : Flag<["-", "--"], "arm-restrict-it">,
    HelpText<"Disallow complex IT blocks">, Flags<[HelpHidden]>,
    Assign<"ARM_ITMode", "ARMITMode::RestrictedIT">;

defm ARM_ForceFastISel : BoolField<"arm-force-fast-isel",
    "false", "", /*hidden=*/1>;

// --- ARMTargetMachine.cpp ---

defm ARM_DisableA15SDOptimization : BoolField<"disable-a15-sd-optimization",
    "false", "Inhibit optimization of S->D register accesses on A15",
    /*hidden=*/1>;

defm ARM_EnableAtomicTidy : BoolField<"arm-atomic-cfg-tidy",
    "true", "Run SimplifyCFG after expanding atomic operations to make use "
    "of cmpxchg flow-based information", /*hidden=*/1>;

defm ARM_EnableARMLoadStoreOpt : BoolField<"arm-load-store-opt",
    "true", "Enable ARM load/store optimization pass", /*hidden=*/1>;

// OptionalValueField gives a std::optional<bool> field so callers can tell
// "-arm-global-merge was never given" (clv2's wasOptSpecified<> check) apart
// from an explicit false; keeps the same bare and "=<bool>" spellings that
// clang's driver and existing lit tests already rely on.
defm ARM_EnableGlobalMerge : OptionalValueField<"arm-global-merge",
    "bool", "true", "Enable the global merge pass", /*hidden=*/1>;

// --- ARMTargetTransformInfo.cpp ---

defm ARM_EnableMaskedLoadStores : BoolField<"enable-arm-maskedldst",
    "true", "Enable the generation of masked loads and stores",
    /*hidden=*/1>;

defm ARM_DisableLowOverheadLoops : BoolField<"disable-arm-loloops",
    "false", "Disable the generation of low-overhead loops", /*hidden=*/1>;

defm ARM_AllowWLSLoops : BoolField<"allow-arm-wlsloops",
    "true", "Enable the generation of WLS loops", /*hidden=*/1>;

defm ARM_UseWidenGlobalArrays : BoolField<"widen-global-strings",
    "true", "Enable the widening of global strings to alignment boundaries",
    /*hidden=*/1>;

defm ARM_ForceUnrollThreshold : ValueField<"arm-force-unroll-threshold",
    "int", "12",
    "Threshold for forced unrolling of small loops in Arm architecture",
    /*hidden=*/1>;

// --- AsmParser/ARMAsmParser.cpp ---

def ARMImplicitItMode : OptionEnum<"ARMImplicitItMode", [
    EnumMember<"Always", "always">,
    EnumMember<"Never", "never">,
    EnumMember<"ARMOnly", "arm">,
    EnumMember<"ThumbOnly", "thumb">]>;

defm ARM_ImplicitItMode : EnumField<"arm-implicit-it", ARMImplicitItMode,
    "ARMImplicitItMode::ARMOnly",
    "Allow conditional instructions outside of an IT block">;

defm ARM_AddBuildAttributes : BoolField<"arm-add-build-attributes",
    "false", "">;

// --- MLxExpansionPass.cpp ---

defm ARM_ForceExpand : BoolField<"expand-all-fp-mlx", "false", "",
    /*hidden=*/1>;

defm ARM_ExpandLimit : ValueField<"expand-limit", "unsigned", "4294967295",
    "", /*hidden=*/1>;

// --- MVEGatherScatterLowering.cpp ---

defm ARM_EnableMaskedGatherScatters : BoolField<"enable-arm-maskedgatscat",
    "true", "Enable the generation of masked gathers and scatters",
    /*hidden=*/1>;

// --- MVELaneInterleavingPass.cpp ---

defm ARM_EnableInterleave : BoolField<"enable-mve-interleave",
    "true", "Enable interleave MVE vector operation lowering",
    /*hidden=*/1>;

// --- MVETailPredication.cpp ---

def ARMTailPredicationMode : OptionEnum<"ARMTailPredicationMode", [
    EnumMember<"Disabled", "disabled">,
    EnumMember<"EnabledNoReductions", "enabled-no-reductions">,
    EnumMember<"Enabled", "enabled">,
    EnumMember<"ForceEnabledNoReductions", "force-enabled-no-reductions">,
    EnumMember<"ForceEnabled", "force-enabled">]>;

defm ARM_EnableTailPredication : EnumField<"tail-predication",
    ARMTailPredicationMode, "ARMTailPredicationMode::Enabled",
    "MVE tail-predication pass options">;

// --- MVETPAndVPTOptimisationsPass.cpp ---

defm ARM_MergeEndDec : BoolField<"arm-enable-merge-loopenddec",
    "true", "Enable merging Loop End and Dec instructions.", /*hidden=*/1>;

defm ARM_SetLRPredicate : BoolField<"arm-set-lr-predicate",
    "true", "Enable setting lr as a predicate in tail predication regions.",
    /*hidden=*/1>;

// --- Thumb2InstrInfo.cpp ---

// Dead in the original clv2 registry too (no getter anywhere ever read it);
// preserved as-is rather than silently dropped.
defm ARM_OldT2IfCvt : BoolField<"old-thumb2-ifcvt",
    "false", "Use old-style Thumb2 if-conversion heuristics", /*hidden=*/1>;

defm ARM_PreferNoCSEL : BoolField<"prefer-no-csel",
    "false", "Prefer predicated Move to CSEL", /*hidden=*/1>;

// --- Thumb2SizeReduction.cpp ---

defm ARM_ReduceLimit : ValueField<"t2-reduce-limit", "int", "-1", "",
    /*hidden=*/1>;

defm ARM_ReduceLimit2Addr : ValueField<"t2-reduce-limit2", "int", "-1", "",
    /*hidden=*/1>;

defm ARM_ReduceLimitLdSt : ValueField<"t2-reduce-limit3", "int", "-1", "",
    /*hidden=*/1>;
