//===- PowerPCOptions.td - CLV2 option defs for PPC ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def PowerPCOptsReg : Registry<"llvm::ppc_opts"> {
  // Getter bodies are compiled once, in PowerPCOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

// --- PPCAsmPrinter.cpp ---

def PPC_EnableSSPCanaryBitInTB : Option<
    "aix-ssp-tb-bit",
    "Enable Passing SSP Canary info in Trackback on AIX",
    "bool"> {
  let Default = "false";
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_IFuncLocalIfProven : Option<
    "ifunc-local-if-proven",
    "During ifunc lowering, the compiler assumes the resolver returns "
    "dso-local functions and bails out if non-local functions are "
    "detected; this flag flips the assumption: resolver returns "
    "preemptible functions unless the compiler can prove all paths "
    "return local functions.",
    "bool"> {
  let Default = "false";
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_IFuncWarnInsteadOfError : Option<
    "test-ifunc-warn-noerror", "",
    "bool"> {
  let Default = "false";
  let Registry = PowerPCOptsReg;
  let ReallyHidden = 1;
}

// --- PPCFrameLowering.cpp ---

def PPC_EnablePEVectorSpills : Option<
    "ppc-enable-pe-vector-spills",
    "Enable spills in prologue to vector registers.",
    "bool"> {
  let Default = "false";
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

// --- PPCInstrInfo.cpp ---

def PPC_DisableCTRLoopAnal : Option<
    "disable-ppc-ctrloop-analysis",
    "Disable analysis for CTR loops",
    "bool"> {
  let Default = "false";
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_DisableCmpOpt : Option<
    "disable-ppc-cmp-opt",
    "Disable compare instruction optimization",
    "bool"> {
  let Default = "false";
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_VSXSelfCopyCrash : Option<
    "crash-on-ppc-vsx-self-copy",
    "Causes the backend to crash instead of generating a nop VSX copy",
    "bool"> {
  let Default = "false";
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_UseOldLatencyCalc : Option<
    "ppc-old-latency-calc",
    "Use the old (incorrect) instruction latency calculation",
    "bool"> {
  let Default = "false";
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_FMARPFactor : Option<
    "ppc-fma-rp-factor",
    "register pressure factor for the transformations.",
    "float"> {
  let Registry = PowerPCOptsReg;
  let Default = "1.5f";
  let Hidden = 1;
}

def PPC_EnableFMARegPressureReduction : Option<
    "ppc-fma-rp-reduction",
    "enable register pressure reduce in machine combiner pass.",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- PPCISelDAGToDAG.cpp ---

def PPC_ANDIGlueBug : Option<
    "expose-ppc-andi-glue-bug",
    "expose the ANDI glue bug on PPC",
    "bool"> {
  let Default = "false";
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_UseBitPermRewriter : Option<
    "ppc-use-bit-perm-rewriter",
    "use aggressive ppc isel for bit permutations",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PPC_BPermRewriterNoMasking : Option<
    "ppc-bit-perm-rewriter-stress-rotates",
    "stress rotate selection in aggressive ppc isel for bit permutations",
    "bool"> {
  let Default = "false";
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_EnableBranchHint : Option<
    "ppc-use-branch-hint",
    "Enable static hinting of branches on ppc",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PPC_EnableTLSOpt : Option<
    "ppc-tls-opt",
    "Enable tls optimization peephole",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- PPCICmpInGPRType enum ---

def PPCICmpAll : EnumValue<"all",
    "All possible int comparisons in GPRs."> {
  let CppName = "All";
  let Value = 0;
}

def PPCICmpNone : EnumValue<"none",
    "Do not modify integer comparisons."> {
  let CppName = "None";
  let Value = 1;
}

def PPCICmpI32 : EnumValue<"i32",
    "Only i32 comparisons in GPRs."> {
  let CppName = "I32";
  let Value = 2;
}

def PPCICmpI64 : EnumValue<"i64",
    "Only i64 comparisons in GPRs."> {
  let CppName = "I64";
  let Value = 3;
}

def PPCICmpNonExtIn : EnumValue<"nonextin",
    "Only comparisons where inputs don't need [sz]ext."> {
  let CppName = "NonExtIn";
  let Value = 4;
}

def PPCICmpZext : EnumValue<"zext",
    "Only comparisons with zext result."> {
  let CppName = "Zext";
  let Value = 5;
}

def PPCICmpSext : EnumValue<"sext",
    "Only comparisons with sext result."> {
  let CppName = "Sext";
  let Value = 6;
}

def PPCICmpZextI32 : EnumValue<"zexti32",
    "Only i32 comparisons with zext result."> {
  let CppName = "ZextI32";
  let Value = 7;
}

def PPCICmpSextI32 : EnumValue<"sexti32",
    "Only i32 comparisons with sext result."> {
  let CppName = "SextI32";
  let Value = 8;
}

def PPCICmpZextI64 : EnumValue<"zexti64",
    "Only i64 comparisons with zext result."> {
  let CppName = "ZextI64";
  let Value = 9;
}

def PPCICmpSextI64 : EnumValue<"sexti64",
    "Only i64 comparisons with sext result."> {
  let CppName = "SextI64";
  let Value = 10;
}

def PPCICmpInGPRType : EnumType<"PPCICmpInGPRType",
    [PPCICmpAll, PPCICmpNone, PPCICmpI32, PPCICmpI64,
     PPCICmpNonExtIn, PPCICmpZext, PPCICmpSext, PPCICmpZextI32,
     PPCICmpSextI32, PPCICmpZextI64, PPCICmpSextI64]>;

def PPC_CmpInGPR : Option<
    "ppc-gpr-icmps",
    "Specify the types of comparisons to emit GPR-only code for.",
    "PPCICmpInGPRType"> {
  let Registry = PowerPCOptsReg;
  let Default = "all";
  let Hidden = 1;
}

// --- PPCISelLowering.cpp ---

def PPC_DisableP10StoreForward : Option<
    "disable-p10-store-forward",
    "disable P10 store forward-friendly conversion",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_DisablePreinc : Option<
    "disable-ppc-preinc",
    "disable preincrement load/store generation on PPC",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_DisableILPPref : Option<
    "disable-ppc-ilp-pref",
    "disable setting the node scheduling preference to ILP on PPC",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_DisableUnaligned : Option<
    "disable-ppc-unaligned",
    "disable unaligned load/store generation on PPC",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_DisableSCO : Option<
    "disable-ppc-sco",
    "disable sibling call optimization on ppc",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_DisableInnermostLoopAlign32 : Option<
    "disable-ppc-innermost-loop-align32",
    "don't always align innermost loop to 32 bytes on ppc",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_UseAbsoluteJumpTables : Option<
    "ppc-use-absolute-jumptables",
    "use absolute jump tables on ppc",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_DisablePerfectShuffle : Option<
    "ppc-disable-perfect-shuffle",
    "disable vector permute decomposition",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PPC_DisableAutoPairedVecSt : Option<
    "disable-auto-paired-vec-st",
    "disable automatically generated 32byte paired vector stores",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PPC_MinimumJumpTableEntries : Option<
    "ppc-min-jump-table-entries",
    "Set minimum number of entries to use a jump table on PPC",
    "unsigned"> {
  let Registry = PowerPCOptsReg;
  let Default = "64";
  let Hidden = 1;
}

def PPC_MinimumBitTestCmps : Option<
    "ppc-min-bit-test-cmps",
    "Set minimum of largest number of comparisons to use bit test for "
    "switch on PPC.",
    "unsigned"> {
  let Registry = PowerPCOptsReg;
  let Default = "3";
  let Hidden = 1;
}

def PPC_GatherAllAliasesMaxDepth : Option<
    "ppc-gather-alias-max-depth",
    "max depth when checking alias info in GatherAllAliases()",
    "unsigned"> {
  let Registry = PowerPCOptsReg;
  let Default = "18";
  let Hidden = 1;
}

def PPC_AIXTLSModelOptUseIEForLDLimit : Option<
    "ppc-aix-shared-lib-tls-model-opt-limit",
    "Set inclusive limit count of TLS local-dynamic access(es) in a "
    "function to use initial-exec",
    "unsigned"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- PPCTargetMachine.cpp ---

def PPC_EnableBranchCoalescing : Option<
    "enable-ppc-branch-coalesce",
    "enable coalescing of duplicate branches for PPC",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_DisableCTRLoops : Option<
    "disable-ppc-ctrloops",
    "Disable CTR loops for PPC",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_DisableInstrFormPrep : Option<
    "disable-ppc-instr-form-prep",
    "Disable PPC loop instr form prep",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_VSXFMAMutateEarly : Option<
    "schedule-ppc-vsx-fma-mutation-early",
    "Schedule VSX FMA instruction mutation early",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_DisableVSXSwapRemoval : Option<
    "disable-ppc-vsx-swap-removal",
    "Disable VSX Swap Removal for PPC",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_DisableMIPeephole : Option<
    "disable-ppc-peephole",
    "Disable machine peepholes for PPC",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_EnableGEPOpt : Option<
    "ppc-gep-opt",
    "Enable optimizations on complex GEPs",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PPC_EnablePrefetch : Option<
    "enable-ppc-prefetching",
    "enable software prefetching on PPC",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_EnableExtraTOCRegDeps : Option<
    "enable-ppc-extra-toc-reg-deps",
    "Add extra TOC register dependencies",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PPC_EnableMachineCombinerPass : Option<
    "ppc-machine-combiner",
    "Enable the machine combiner pass",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PPC_ReduceCRLogical : Option<
    "ppc-reduce-cr-logicals",
    "Expand eligible cr-logical binary ops to branches",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PPC_EnableGenScalarMASSEntries : Option<
    "enable-ppc-gen-scalar-mass",
    "Enable lowering math functions to their corresponding MASS "
    "(scalar) entries",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_EnableGlobalMerge : Option<
    "ppc-global-merge",
    "Enable the global merge pass",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_GlobalMergeMaxOffset : Option<
    "ppc-global-merge-max-offset",
    "Maximum global merge offset",
    "unsigned"> {
  let Registry = PowerPCOptsReg;
  let Default = "32767";
  let Hidden = 1;
}

// --- MCTargetDesc/PPCInstPrinter.cpp ---

def PPC_FullRegNames : Option<
    "ppc-asm-full-reg-names",
    "Use full register names when printing assembly",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_ShowVSRNumsAsVR : Option<
    "ppc-vsr-nums-as-vr",
    "Prints full register names with vs{31-63} as v{0-31}",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_FullRegNamesWithPercent : Option<
    "ppc-reg-with-percent-prefix",
    "Prints full register names with percent",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

// --- PPCMachineScheduler.cpp ---

def PPC_DisableAddiLoadHeuristic : Option<
    "disable-ppc-sched-addi-load",
    "Disable scheduling addi instruction before load for ppc",
    "bool"> {
  let Default = "false";
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_EnableAddiHeuristic : Option<
    "ppc-postra-bias-addi",
    "Enable scheduling addi instruction as early as possible post ra",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- PPCPreEmitPeephole.cpp ---

def PPC_EnablePCRelLinkerOpt : Option<
    "ppc-pcrel-linker-opt",
    "enable PC Relative linker optimization",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PPC_RunPreEmitPeephole : Option<
    "ppc-late-peephole",
    "Run pre-emit peephole optimizations.",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PPC_DSCRValue : Option<
    "ppc-set-dscr",
    "Set the Data Stream Control Register.",
    "uint64_t"> {
  let Default = "0";
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

// --- PPCSubtarget.cpp ---

def PPC_EnableMachinePipeliner : Option<
    "ppc-enable-pipeliner",
    "Enable Machine Pipeliner for PPC",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

// --- PPCLoopInstrFormPrep.cpp ---

def PPC_MaxVarsPrep : Option<
    "ppc-formprep-max-vars",
    "Potential common base number threshold per function for PPC loop prep",
    "unsigned"> {
  let Registry = PowerPCOptsReg;
  let Default = "24";
  let Hidden = 1;
}

def PPC_PreferUpdateForm : Option<
    "ppc-formprep-prefer-update",
    "prefer update form when ds form is also a update form",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PPC_EnableUpdateFormForNonConstInc : Option<
    "ppc-formprep-update-nonconst-inc",
    "prepare update form when the load/store increment is a loop "
    "invariant non-const value.",
    "bool"> {
  let Default = "false";
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_EnableChainCommoning : Option<
    "ppc-formprep-chain-commoning",
    "Enable chain commoning in PPC loop prepare pass.",
    "bool"> {
  let Default = "false";
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_MaxVarsUpdateForm : Option<
    "ppc-preinc-prep-max-vars",
    "Potential PHI threshold per loop for PPC loop prep of update form",
    "unsigned"> {
  let Registry = PowerPCOptsReg;
  let Default = "3";
  let Hidden = 1;
}

def PPC_MaxVarsDSForm : Option<
    "ppc-dsprep-max-vars",
    "Potential PHI threshold per loop for PPC loop prep of DS form",
    "unsigned"> {
  let Registry = PowerPCOptsReg;
  let Default = "3";
  let Hidden = 1;
}

def PPC_MaxVarsDQForm : Option<
    "ppc-dqprep-max-vars",
    "Potential PHI threshold per loop for PPC loop prep of DQ form",
    "unsigned"> {
  let Registry = PowerPCOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def PPC_MaxVarsChainCommon : Option<
    "ppc-chaincommon-max-vars",
    "Bucket number per loop for PPC loop chain common",
    "unsigned"> {
  let Registry = PowerPCOptsReg;
  let Default = "4";
  let Hidden = 1;
}

def PPC_DispFormPrepMinThreshold : Option<
    "ppc-dispprep-min-threshold",
    "Minimal common base load/store instructions triggering DS/DQ form "
    "preparation",
    "unsigned"> {
  let Registry = PowerPCOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def PPC_ChainCommonPrepMinThreshold : Option<
    "ppc-chaincommon-min-threshold",
    "Minimal common base load/store instructions triggering chain "
    "commoning preparation. Must be not smaller than 4",
    "unsigned"> {
  let Registry = PowerPCOptsReg;
  let Default = "4";
  let Hidden = 1;
}

// --- PPCMachineFunctionInfo.cpp ---

def PPC_DisableNonVolatileCR : Option<
    "ppc-disable-non-volatile-cr",
    "Disable the use of non-volatile CR register fields",
    "bool"> {
  let Default = "false";
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

// --- PPCTargetTransformInfo.cpp ---

def PPC_EVL : Option<
    "ppc-evl",
    "Allow EVL type vp.load/vp.store",
    "bool"> {
  let Default = "false";
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_Pwr9EVL : Option<
    "ppc-pwr9-evl",
    "Allow vp.load and vp.store for pwr9",
    "bool"> {
  let Default = "false";
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_VecMaskCost : Option<
    "ppc-vec-mask-cost",
    "add masking cost for i1 vectors",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PPC_DisableConstHoist : Option<
    "disable-ppc-constant-hoisting",
    "disable constant hoisting on PPC",
    "bool"> {
  let Default = "false";
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_EnableColdCC : Option<
    "ppc-enable-coldcc",
    "Enable using coldcc calling conv for cold internal functions",
    "bool"> {
  let Default = "false";
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_LsrNoInsnsCost : Option<
    "ppc-lsr-no-insns-cost",
    "Do not add instruction count to lsr cost model",
    "bool"> {
  let Default = "false";
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_SmallCTRLoopThreshold : Option<
    "min-ctr-loop-threshold",
    "Loops with a constant trip count smaller than "
    "this value will not use the count register.",
    "unsigned"> {
  let Registry = PowerPCOptsReg;
  let Default = "4";
  let Hidden = 1;
}

// --- PPCVSXFMAMutate.cpp ---

def PPC_DisableVSXFMAMutate : Option<
    "disable-ppc-vsx-fma-mutation",
    "Disable VSX FMA instruction mutation",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- PPCMIPeephole.cpp ---

def PPC_FixedPointRegToImm : Option<
    "ppc-reg-to-imm-fixed-point",
    "Iterate to a fixed point when attempting to "
    "convert reg-reg instructions to reg-imm",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PPC_ConvertRegReg : Option<
    "ppc-convert-rr-to-ri",
    "Convert eligible reg+reg instructions to reg+imm",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PPC_EnableSExtElimination : Option<
    "ppc-eliminate-signext",
    "enable elimination of sign-extensions",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PPC_EnableZExtElimination : Option<
    "ppc-eliminate-zeroext",
    "enable elimination of zero-extensions",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PPC_EnableTrapOptimization : Option<
    "ppc-opt-conditional-trap",
    "enable optimization of conditional traps",
    "bool"> {
  let Default = "false";
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

// --- PPCRegisterInfo.cpp ---

def PPC_EnableBasePointer : Option<
    "ppc-use-base-pointer",
    "Enable use of a base pointer for complex stack frames",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PPC_AlwaysBasePointer : Option<
    "ppc-always-use-base-pointer",
    "Force the use of a base pointer in every function",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_EnableGPRToVecSpills : Option<
    "ppc-enable-gpr-to-vsr-spills",
    "Enable spills from gpr to vsr rather than stack",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Hidden = 1;
}

def PPC_StackPtrConst : Option<
    "ppc-stack-ptr-caller-preserved",
    "Consider R1 caller preserved so stack saves of "
    "caller preserved registers can be LICM candidates",
    "bool"> {
  let Registry = PowerPCOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def PPC_MaxCRBitSpillDist : Option<
    "ppc-max-crbit-spill-dist",
    "Maximum search distance for definition of CR bit spill on ppc",
    "unsigned"> {
  let Registry = PowerPCOptsReg;
  let Default = "100";
  let Hidden = 1;
}
