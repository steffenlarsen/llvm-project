//===- ARMOptions.td - CLV2 option defs for ARM ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def ARMOptsReg : Registry<"llvm::arm_opts"> {
  // Getter bodies are compiled once, in ARMOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

// --- ARMCallLowering.cpp ---

def ARM_EnableGISelBigEndian : Option<
    "enable-arm-gisel-bigendian",
    "Enable Global-ISel Big Endian Lowering",
    "bool"> {
  let Registry = ARMOptsReg;
  let Hidden = 1;
}

// --- ARMConstantIslandPass.cpp ---

def ARM_AdjustJumpTableBlocks : Option<
    "arm-adjust-jump-tables",
    "Adjust basic block layout to better use TB[BH]",
    "bool"> {
  let Registry = ARMOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def ARM_CPMaxIteration : Option<
    "arm-constant-island-max-iteration",
    "The max number of iteration for converge",
    "unsigned"> {
  let Registry = ARMOptsReg;
  let Default = "30";
  let Hidden = 1;
}

// --- ARMExpandPseudoInsts.cpp ---

def ARM_VerifyARMPseudo : Option<
    "verify-arm-pseudo-expand",
    "Verify machine code after expanding ARM pseudos",
    "bool"> {
  let Registry = ARMOptsReg;
  let Hidden = 1;
}

// --- ARMFrameLowering.cpp ---

def ARM_SpillAlignedNEONRegs : Option<
    "align-neon-spills",
    "Align ARM NEON spills in prolog and epilog",
    "bool"> {
  let Registry = ARMOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- ARMHazardRecognizer.cpp ---

def ARM_DataBankMask : Option<
    "arm-data-bank-mask", "",
    "int"> {
  let Registry = ARMOptsReg;
  let Default = "-1";
  let Hidden = 1;
}

def ARM_AssumeITCMConflict : Option<
    "arm-assume-itcm-bankconflict", "",
    "bool"> {
  let Registry = ARMOptsReg;
  let Hidden = 1;
}

// --- ARMISelDAGToDAG.cpp ---

def ARM_DisableShifterOp : Option<
    "disable-shifter-op",
    "Disable isel of shifter-op",
    "bool"> {
  let Registry = ARMOptsReg;
  let Hidden = 1;
}

// --- ARMISelLowering.cpp ---

def ARM_Interworking : Option<
    "arm-interworking",
    "Enable / disable ARM interworking (for debugging only)",
    "bool"> {
  let Registry = ARMOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def ARM_EnableConstpoolPromotion : Option<
    "arm-promote-constant",
    "Enable / disable promotion of unnamed_addr constants into "
    "constant pools",
    "bool"> {
  let Registry = ARMOptsReg;
  let Hidden = 1;
}

def ARM_ConstpoolPromotionMaxSize : Option<
    "arm-promote-constant-max-size",
    "Maximum size of constant to promote into a constant pool",
    "unsigned"> {
  let Registry = ARMOptsReg;
  let Default = "64";
  let Hidden = 1;
}

def ARM_ConstpoolPromotionMaxTotal : Option<
    "arm-promote-constant-max-total",
    "Maximum size of ALL constants to promote into a constant pool",
    "unsigned"> {
  let Registry = ARMOptsReg;
  let Default = "128";
  let Hidden = 1;
}

def ARM_MVEMaxSupportedInterleaveFactor : Option<
    "mve-max-interleave-factor",
    "Maximum interleave factor for MVE VLDn to generate.",
    "unsigned"> {
  let Registry = ARMOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def ARM_MaxBaseUpdatesToCheck : Option<
    "arm-max-base-updates-to-check",
    "Maximum number of base-updates to check generating postindex.",
    "unsigned"> {
  let Registry = ARMOptsReg;
  let Default = "64";
  let Hidden = 1;
}

// --- ARMLoadStoreOptimizer.cpp ---

def ARM_AssumeMisalignedLoadStores : Option<
    "arm-assume-misaligned-load-store",
    "Be more conservative in ARM load/store opt",
    "bool"> {
  let Registry = ARMOptsReg;
  let Hidden = 1;
}

def ARM_InstReorderLimit : Option<
    "arm-prera-ldst-opt-reorder-limit", "",
    "unsigned"> {
  let Registry = ARMOptsReg;
  let Default = "8";
  let Hidden = 1;
}

// --- ARMLowOverheadLoops.cpp ---

def ARM_DisableTailPredication : Option<
    "arm-loloops-disable-tailpred",
    "Disable tail-predication in the ARM LowOverheadLoop pass",
    "bool"> {
  let Registry = ARMOptsReg;
  let Hidden = 1;
}

def ARM_DisableOmitDLS : Option<
    "arm-disable-omit-dls",
    "Disable omitting 'dls lr, lr' instructions",
    "bool"> {
  let Registry = ARMOptsReg;
  let Hidden = 1;
}

// --- ARMParallelDSP.cpp ---

def ARM_DisableParallelDSP : Option<
    "disable-arm-parallel-dsp", "",
    "bool"> {
  let Registry = ARMOptsReg;
  let Hidden = 1;
}

def ARM_NumLoadLimit : Option<
    "arm-parallel-dsp-load-limit",
    "Limit the number of loads analysed",
    "unsigned"> {
  let Registry = ARMOptsReg;
  let Default = "16";
  let Hidden = 1;
}

// --- ARMSelectionDAGInfo.cpp ---

def ARMTPLoopMemTransferForceDisabled : EnumValue<"force-disabled",
    "Don't convert memcpy to TP loop."> {
  let CppName = "ForceDisabled";
  let Value = 0;
}

def ARMTPLoopMemTransferForceEnabled : EnumValue<"force-enabled",
    "Always convert memcpy to TP loop."> {
  let CppName = "ForceEnabled";
}

def ARMTPLoopMemTransferAllow : EnumValue<"allow",
    "Allow (may be subject to certain conditions) "
    "conversion of memcpy to TP loop."> {
  let CppName = "Allow";
}

def ARMTPLoopMemTransferEnum : EnumType<"ARMTPLoopMemTransfer",
    [ARMTPLoopMemTransferForceDisabled, ARMTPLoopMemTransferForceEnabled,
     ARMTPLoopMemTransferAllow]>;

def ARM_EnableMemtransferTPLoop : Option<
    "arm-memtransfer-tploop",
    "Control conversion of memcpy to "
    "Tail predicated loops (WLSTP)",
    "ARMTPLoopMemTransfer"> {
  let Registry = ARMOptsReg;
  let Default = "force-disabled";
  let Hidden = 1;
}

// --- ARMSubtarget.cpp ---

def ARM_UseFusedMulOps : Option<
    "arm-use-mulops", "",
    "bool"> {
  let Registry = ARMOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def ARMITModeDefaultIT : EnumValue<"arm-default-it",
    "Generate any type of IT block"> {
  let CppName = "DefaultIT";
}

def ARMITModeRestrictedIT : EnumValue<"arm-restrict-it",
    "Disallow complex IT blocks"> {
  let CppName = "RestrictedIT";
}

def ARMITModeEnum : EnumType<"ARMITMode",
    [ARMITModeDefaultIT, ARMITModeRestrictedIT]>;

def ARM_IT : Option<
    "", "IT block support",
    "ARMITMode"> {
  let Registry = ARMOptsReg;
  let Default = "arm-default-it";
  let Hidden = 1;
}

def ARM_ForceFastISel : Option<
    "arm-force-fast-isel", "",
    "bool"> {
  let Registry = ARMOptsReg;
  let Hidden = 1;
}

// --- ARMTargetMachine.cpp ---

def ARM_DisableA15SDOptimization : Option<
    "disable-a15-sd-optimization",
    "Inhibit optimization of S->D register accesses on A15",
    "bool"> {
  let Registry = ARMOptsReg;
  let Hidden = 1;
}

def ARM_EnableAtomicTidy : Option<
    "arm-atomic-cfg-tidy",
    "Run SimplifyCFG after expanding atomic operations"
    " to make use of cmpxchg flow-based information",
    "bool"> {
  let Registry = ARMOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def ARM_EnableARMLoadStoreOpt : Option<
    "arm-load-store-opt",
    "Enable ARM load/store optimization pass",
    "bool"> {
  let Registry = ARMOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def ARM_EnableGlobalMerge : Option<
    "arm-global-merge",
    "Enable the global merge pass",
    "bool"> {
  let Registry = ARMOptsReg;
  let Hidden = 1;
}

// --- ARMTargetTransformInfo.cpp ---

def ARM_EnableMaskedLoadStores : Option<
    "enable-arm-maskedldst",
    "Enable the generation of masked loads and stores",
    "bool"> {
  let Registry = ARMOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def ARM_DisableLowOverheadLoops : Option<
    "disable-arm-loloops",
    "Disable the generation of low-overhead loops",
    "bool"> {
  let Registry = ARMOptsReg;
  let Hidden = 1;
}

def ARM_AllowWLSLoops : Option<
    "allow-arm-wlsloops",
    "Enable the generation of WLS loops",
    "bool"> {
  let Registry = ARMOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def ARM_UseWidenGlobalArrays : Option<
    "widen-global-strings",
    "Enable the widening of global strings to alignment boundaries",
    "bool"> {
  let Registry = ARMOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def ARM_ForceUnrollThreshold : Option<
    "arm-force-unroll-threshold",
    "Threshold for forced unrolling of small loops in Arm architecture",
    "int"> {
  let Registry = ARMOptsReg;
  let Default = "12";
  let Hidden = 1;
}

// --- AsmParser/ARMAsmParser.cpp ---

def ARMImplicitItModeAlways : EnumValue<"always",
    "Accept in both ISAs, emit implicit ITs in Thumb"> {
  let CppName = "Always";
}

def ARMImplicitItModeNever : EnumValue<"never",
    "Warn in ARM, reject in Thumb"> {
  let CppName = "Never";
}

def ARMImplicitItModeARMOnly : EnumValue<"arm",
    "Accept in ARM, reject in Thumb"> {
  let CppName = "ARMOnly";
}

def ARMImplicitItModeThumbOnly : EnumValue<"thumb",
    "Warn in ARM, emit implicit ITs in Thumb"> {
  let CppName = "ThumbOnly";
}

def ARMImplicitItModeEnum : EnumType<"ARMImplicitItMode",
    [ARMImplicitItModeAlways, ARMImplicitItModeNever,
     ARMImplicitItModeARMOnly, ARMImplicitItModeThumbOnly]>;

def ARM_ImplicitItMode : Option<
    "arm-implicit-it",
    "Allow conditional instructions outside of an IT block",
    "ARMImplicitItMode"> {
  let Registry = ARMOptsReg;
  let Default = "arm";
}

def ARM_AddBuildAttributes : Option<
    "arm-add-build-attributes", "",
    "bool"> {
  let Registry = ARMOptsReg;
}

// --- MLxExpansionPass.cpp ---

def ARM_ForceExpand : Option<
    "expand-all-fp-mlx", "",
    "bool"> {
  let Registry = ARMOptsReg;
  let Hidden = 1;
}

def ARM_ExpandLimit : Option<
    "expand-limit", "",
    "unsigned"> {
  let Registry = ARMOptsReg;
  let Default = "4294967295";
  let Hidden = 1;
}

// --- MVEGatherScatterLowering.cpp ---

def ARM_EnableMaskedGatherScatters : Option<
    "enable-arm-maskedgatscat",
    "Enable the generation of masked gathers and scatters",
    "bool"> {
  let Registry = ARMOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- MVELaneInterleavingPass.cpp ---

def ARM_EnableInterleave : Option<
    "enable-mve-interleave",
    "Enable interleave MVE vector operation lowering",
    "bool"> {
  let Registry = ARMOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- MVETailPredication.cpp ---

def ARMTailPredicationModeDisabled : EnumValue<"disabled",
    "Don't tail-predicate loops"> {
  let CppName = "Disabled";
  let Value = 0;
}

def ARMTailPredicationModeEnabledNoReductions : EnumValue<
    "enabled-no-reductions",
    "Enable tail-predication, but not for reduction loops"> {
  let CppName = "EnabledNoReductions";
}

def ARMTailPredicationModeEnabled : EnumValue<"enabled",
    "Enable tail-predication, including reduction loops"> {
  let CppName = "Enabled";
}

def ARMTailPredicationModeForceEnabledNoReductions : EnumValue<
    "force-enabled-no-reductions",
    "Enable tail-predication, but not for reduction loops, "
    "and force this which might be unsafe"> {
  let CppName = "ForceEnabledNoReductions";
}

def ARMTailPredicationModeForceEnabled : EnumValue<"force-enabled",
    "Enable tail-predication, including reduction loops, "
    "and force this which might be unsafe"> {
  let CppName = "ForceEnabled";
}

def ARMTailPredicationModeEnum : EnumType<"ARMTailPredicationMode",
    [ARMTailPredicationModeDisabled,
     ARMTailPredicationModeEnabledNoReductions,
     ARMTailPredicationModeEnabled,
     ARMTailPredicationModeForceEnabledNoReductions,
     ARMTailPredicationModeForceEnabled]>;

def ARM_EnableTailPredication : Option<
    "tail-predication", "MVE tail-predication pass options",
    "ARMTailPredicationMode"> {
  let Registry = ARMOptsReg;
  let Default = "enabled";
}

// --- MVETPAndVPTOptimisationsPass.cpp ---

def ARM_MergeEndDec : Option<
    "arm-enable-merge-loopenddec",
    "Enable merging Loop End and Dec instructions.",
    "bool"> {
  let Registry = ARMOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def ARM_SetLRPredicate : Option<
    "arm-set-lr-predicate",
    "Enable setting lr as a predicate in tail predication regions.",
    "bool"> {
  let Registry = ARMOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- Thumb2InstrInfo.cpp ---

def ARM_OldT2IfCvt : Option<
    "old-thumb2-ifcvt",
    "Use old-style Thumb2 if-conversion heuristics",
    "bool"> {
  let Registry = ARMOptsReg;
  let Hidden = 1;
}

def ARM_PreferNoCSEL : Option<
    "prefer-no-csel",
    "Prefer predicated Move to CSEL",
    "bool"> {
  let Registry = ARMOptsReg;
  let Hidden = 1;
}

// --- Thumb2SizeReduction.cpp ---

def ARM_ReduceLimit : Option<
    "t2-reduce-limit", "",
    "int"> {
  let Registry = ARMOptsReg;
  let Default = "-1";
  let Hidden = 1;
}

def ARM_ReduceLimit2Addr : Option<
    "t2-reduce-limit2", "",
    "int"> {
  let Registry = ARMOptsReg;
  let Default = "-1";
  let Hidden = 1;
}

def ARM_ReduceLimitLdSt : Option<
    "t2-reduce-limit3", "",
    "int"> {
  let Registry = ARMOptsReg;
  let Default = "-1";
  let Hidden = 1;
}
