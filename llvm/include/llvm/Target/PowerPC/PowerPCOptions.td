//===- PowerPCOptions.td - OptTable option defs for PowerPC ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def PowerPCOptions : OptionsStruct<"llvm"> {}

// --- PPCAsmPrinter.cpp ---

defm PPC_EnableSSPCanaryBitInTB : BoolField<"aix-ssp-tb-bit", "false",
    "Enable Passing SSP Canary info in Trackback on AIX", /*hidden=*/1>;

defm PPC_IFuncLocalIfProven : BoolField<"ifunc-local-if-proven", "false",
    "During ifunc lowering, the compiler assumes the resolver returns "
    "dso-local functions and bails out if non-local functions are "
    "detected; this flag flips the assumption: resolver returns "
    "preemptible functions unless the compiler can prove all paths "
    "return local functions.", /*hidden=*/1>;

defm PPC_IFuncWarnInsteadOfError : BoolField<"test-ifunc-warn-noerror",
    "false", "", /*hidden=*/1>;

// --- PPCFrameLowering.cpp ---

defm PPC_EnablePEVectorSpills : BoolField<"ppc-enable-pe-vector-spills",
    "false", "Enable spills in prologue to vector registers.", /*hidden=*/1>;

// --- PPCInstrInfo.cpp ---

defm PPC_DisableCTRLoopAnal : BoolField<"disable-ppc-ctrloop-analysis",
    "false", "Disable analysis for CTR loops", /*hidden=*/1>;

defm PPC_DisableCmpOpt : BoolField<"disable-ppc-cmp-opt", "false",
    "Disable compare instruction optimization", /*hidden=*/1>;

defm PPC_VSXSelfCopyCrash : BoolField<"crash-on-ppc-vsx-self-copy", "false",
    "Causes the backend to crash instead of generating a nop VSX copy",
    /*hidden=*/1>;

defm PPC_UseOldLatencyCalc : BoolField<"ppc-old-latency-calc", "false",
    "Use the old (incorrect) instruction latency calculation", /*hidden=*/1>;

defm PPC_FMARPFactor : ValueField<"ppc-fma-rp-factor", "float", "1.5f",
    "register pressure factor for the transformations.", /*hidden=*/1>;

defm PPC_EnableFMARegPressureReduction : BoolField<"ppc-fma-rp-reduction",
    "true", "enable register pressure reduce in machine combiner pass.",
    /*hidden=*/1>;

// --- PPCISelDAGToDAG.cpp ---

defm PPC_ANDIGlueBug : BoolField<"expose-ppc-andi-glue-bug", "false",
    "expose the ANDI glue bug on PPC", /*hidden=*/1>;

defm PPC_UseBitPermRewriter : BoolField<"ppc-use-bit-perm-rewriter", "true",
    "use aggressive ppc isel for bit permutations", /*hidden=*/1>;

defm PPC_BPermRewriterNoMasking : BoolField<
    "ppc-bit-perm-rewriter-stress-rotates", "false",
    "stress rotate selection in aggressive ppc isel for bit permutations",
    /*hidden=*/1>;

defm PPC_EnableBranchHint : BoolField<"ppc-use-branch-hint", "true",
    "Enable static hinting of branches on ppc", /*hidden=*/1>;

defm PPC_EnableTLSOpt : BoolField<"ppc-tls-opt", "true",
    "Enable tls optimization peephole", /*hidden=*/1>;

def PPCICmpInGPRTypeEnum : OptionEnum<"PPCICmpInGPRType", [
    EnumMember<"All", "all">,
    EnumMember<"None", "none">,
    EnumMember<"I32", "i32">,
    EnumMember<"I64", "i64">,
    EnumMember<"NonExtIn", "nonextin">,
    EnumMember<"Zext", "zext">,
    EnumMember<"Sext", "sext">,
    EnumMember<"ZextI32", "zexti32">,
    EnumMember<"SextI32", "sexti32">,
    EnumMember<"ZextI64", "zexti64">,
    EnumMember<"SextI64", "sexti64">]>;

// std::optional<PPCICmpInGPRType>: getCmpInGPRWasSpecified() needs a
// genuine tri-state (clv2's specified<>() check) distinct from the actual
// value, so this is hand-authored (not EnumField<>) like X86_AsmSyntax --
// no multiclass composes std::optional<> field storage with FieldEnum<>
// tracking.
def PPC_CmpInGPR : Joined<["-", "--"], "ppc-gpr-icmps=">,
    HelpText<"Specify the types of comparisons to emit GPR-only code for.">,
    Flags<[HelpHidden]>,
    Field<"std::optional<PPCICmpInGPRType>", "PPC_CmpInGPR", "std::nullopt">,
    FieldEnum<PPCICmpInGPRTypeEnum>;
def PPC_CmpInGPR_sep : Separate<["-", "--"], "ppc-gpr-icmps">,
    Flags<[HelpHidden]>, Alias<!cast<Option>(PPC_CmpInGPR)>;

// --- PPCISelLowering.cpp ---

defm PPC_DisableP10StoreForward : BoolField<"disable-p10-store-forward",
    "false", "disable P10 store forward-friendly conversion", /*hidden=*/1>;

defm PPC_DisablePreinc : BoolField<"disable-ppc-preinc", "false",
    "disable preincrement load/store generation on PPC", /*hidden=*/1>;

defm PPC_DisableILPPref : BoolField<"disable-ppc-ilp-pref", "false",
    "disable setting the node scheduling preference to ILP on PPC",
    /*hidden=*/1>;

defm PPC_DisableUnaligned : BoolField<"disable-ppc-unaligned", "false",
    "disable unaligned load/store generation on PPC", /*hidden=*/1>;

defm PPC_DisableSCO : BoolField<"disable-ppc-sco", "false",
    "disable sibling call optimization on ppc", /*hidden=*/1>;

defm PPC_DisableInnermostLoopAlign32 : BoolField<
    "disable-ppc-innermost-loop-align32", "false",
    "don't always align innermost loop to 32 bytes on ppc", /*hidden=*/1>;

defm PPC_UseAbsoluteJumpTables : BoolField<"ppc-use-absolute-jumptables",
    "false", "use absolute jump tables on ppc", /*hidden=*/1>;

defm PPC_DisablePerfectShuffle : BoolField<"ppc-disable-perfect-shuffle",
    "true", "disable vector permute decomposition", /*hidden=*/1>;

// OptionalValueField gives a std::optional<bool> field: the fallback when
// unspecified is computed at the call site from Subtarget.isISAFuture()
// (clv2's getOptValIfSpecified<>(Ctx, !IsFutureCPU) runtime-computed
// default), so "unspecified" must survive as tri-state.
defm PPC_DisableAutoPairedVecSt : OptionalValueField<
    "disable-auto-paired-vec-st", "bool", "true",
    "disable automatically generated 32byte paired vector stores",
    /*hidden=*/1>;

defm PPC_MinimumJumpTableEntries : ValueField<"ppc-min-jump-table-entries",
    "unsigned", "64",
    "Set minimum number of entries to use a jump table on PPC",
    /*hidden=*/1>;

defm PPC_MinimumBitTestCmps : ValueField<"ppc-min-bit-test-cmps",
    "unsigned", "3",
    "Set minimum of largest number of comparisons to use bit test for "
    "switch on PPC.", /*hidden=*/1>;

defm PPC_GatherAllAliasesMaxDepth : ValueField<
    "ppc-gather-alias-max-depth", "unsigned", "18",
    "max depth when checking alias info in GatherAllAliases()",
    /*hidden=*/1>;

defm PPC_AIXTLSModelOptUseIEForLDLimit : ValueField<
    "ppc-aix-shared-lib-tls-model-opt-limit", "unsigned", "1",
    "Set inclusive limit count of TLS local-dynamic access(es) in a "
    "function to use initial-exec", /*hidden=*/1>;

// --- PPCTargetMachine.cpp ---

defm PPC_EnableBranchCoalescing : BoolField<"enable-ppc-branch-coalesce",
    "false", "enable coalescing of duplicate branches for PPC",
    /*hidden=*/1>;

defm PPC_DisableCTRLoops : BoolField<"disable-ppc-ctrloops", "false",
    "Disable CTR loops for PPC", /*hidden=*/1>;

defm PPC_DisableInstrFormPrep : BoolField<"disable-ppc-instr-form-prep",
    "false", "Disable PPC loop instr form prep", /*hidden=*/1>;

defm PPC_VSXFMAMutateEarly : BoolField<
    "schedule-ppc-vsx-fma-mutation-early", "false",
    "Schedule VSX FMA instruction mutation early", /*hidden=*/1>;

defm PPC_DisableVSXSwapRemoval : BoolField<"disable-ppc-vsx-swap-removal",
    "false", "Disable VSX Swap Removal for PPC", /*hidden=*/1>;

defm PPC_DisableMIPeephole : BoolField<"disable-ppc-peephole", "false",
    "Disable machine peepholes for PPC", /*hidden=*/1>;

defm PPC_EnableGEPOpt : BoolField<"ppc-gep-opt", "true",
    "Enable optimizations on complex GEPs", /*hidden=*/1>;

// OptionalValueField gives a std::optional<bool> field so
// getEnablePrefetchWasSpecified() can tell "never specified" (clv2's
// wasOptSpecified<> check -- the pass is only added when this flag was
// explicitly passed, regardless of true/false) apart from the default.
defm PPC_EnablePrefetch : OptionalValueField<"enable-ppc-prefetching",
    "bool", "true", "enable software prefetching on PPC", /*hidden=*/1>;

defm PPC_EnableExtraTOCRegDeps : BoolField<
    "enable-ppc-extra-toc-reg-deps", "true",
    "Add extra TOC register dependencies", /*hidden=*/1>;

defm PPC_EnableMachineCombinerPass : BoolField<"ppc-machine-combiner",
    "true", "Enable the machine combiner pass", /*hidden=*/1>;

defm PPC_ReduceCRLogical : BoolField<"ppc-reduce-cr-logicals", "true",
    "Expand eligible cr-logical binary ops to branches", /*hidden=*/1>;

defm PPC_EnableGenScalarMASSEntries : BoolField<
    "enable-ppc-gen-scalar-mass", "false",
    "Enable lowering math functions to their corresponding MASS "
    "(scalar) entries", /*hidden=*/1>;

// OptionalValueField gives a std::optional<bool> field so callers can tell
// "-ppc-global-merge was never given" (clv2's wasOptSpecified<> check,
// used to fall back to the optimization-level-based AIX default) apart
// from an explicit false.
defm PPC_EnableGlobalMerge : OptionalValueField<"ppc-global-merge", "bool",
    "true", "Enable the global merge pass", /*hidden=*/1>;

defm PPC_GlobalMergeMaxOffset : ValueField<"ppc-global-merge-max-offset",
    "unsigned", "32767", "Maximum global merge offset", /*hidden=*/1>;

// --- MCTargetDesc/PPCInstPrinter.cpp ---

defm PPC_FullRegNames : BoolField<"ppc-asm-full-reg-names", "false",
    "Use full register names when printing assembly", /*hidden=*/1>;

defm PPC_ShowVSRNumsAsVR : BoolField<"ppc-vsr-nums-as-vr", "false",
    "Prints full register names with vs{31-63} as v{0-31}", /*hidden=*/1>;

defm PPC_FullRegNamesWithPercent : BoolField<
    "ppc-reg-with-percent-prefix", "false",
    "Prints full register names with percent", /*hidden=*/1>;

// --- PPCMachineScheduler.cpp ---

defm PPC_DisableAddiLoadHeuristic : BoolField<
    "disable-ppc-sched-addi-load", "false",
    "Disable scheduling addi instruction before load for ppc",
    /*hidden=*/1>;

defm PPC_EnableAddiHeuristic : BoolField<"ppc-postra-bias-addi", "true",
    "Enable scheduling addi instruction as early as possible post ra",
    /*hidden=*/1>;

// --- PPCPreEmitPeephole.cpp ---

defm PPC_EnablePCRelLinkerOpt : BoolField<"ppc-pcrel-linker-opt", "true",
    "enable PC Relative linker optimization", /*hidden=*/1>;

defm PPC_RunPreEmitPeephole : BoolField<"ppc-late-peephole", "true",
    "Run pre-emit peephole optimizations.", /*hidden=*/1>;

// OptionalField gives a std::optional<uint64_t> field: getDSCRValueWasSpecified()
// needs a genuine tri-state (clv2's specified<>() check) distinct from the
// value itself, since the DSCR-setting code only runs when this was
// actually passed on the command line.
defm PPC_DSCRValue : OptionalField<"ppc-set-dscr", "uint64_t",
    "Set the Data Stream Control Register.", /*hidden=*/1>;

// --- PPCSubtarget.cpp ---

defm PPC_EnableMachinePipeliner : BoolField<"ppc-enable-pipeliner", "false",
    "Enable Machine Pipeliner for PPC", /*hidden=*/1>;

// --- PPCLoopInstrFormPrep.cpp ---

defm PPC_MaxVarsPrep : ValueField<"ppc-formprep-max-vars", "unsigned", "24",
    "Potential common base number threshold per function for PPC loop prep",
    /*hidden=*/1>;

defm PPC_PreferUpdateForm : BoolField<"ppc-formprep-prefer-update", "true",
    "prefer update form when ds form is also a update form", /*hidden=*/1>;

defm PPC_EnableUpdateFormForNonConstInc : BoolField<
    "ppc-formprep-update-nonconst-inc", "false",
    "prepare update form when the load/store increment is a loop "
    "invariant non-const value.", /*hidden=*/1>;

defm PPC_EnableChainCommoning : BoolField<
    "ppc-formprep-chain-commoning", "false",
    "Enable chain commoning in PPC loop prepare pass.", /*hidden=*/1>;

defm PPC_MaxVarsUpdateForm : ValueField<"ppc-preinc-prep-max-vars",
    "unsigned", "3",
    "Potential PHI threshold per loop for PPC loop prep of update form",
    /*hidden=*/1>;

defm PPC_MaxVarsDSForm : ValueField<"ppc-dsprep-max-vars", "unsigned", "3",
    "Potential PHI threshold per loop for PPC loop prep of DS form",
    /*hidden=*/1>;

defm PPC_MaxVarsDQForm : ValueField<"ppc-dqprep-max-vars", "unsigned", "8",
    "Potential PHI threshold per loop for PPC loop prep of DQ form",
    /*hidden=*/1>;

defm PPC_MaxVarsChainCommon : ValueField<"ppc-chaincommon-max-vars",
    "unsigned", "4",
    "Bucket number per loop for PPC loop chain common", /*hidden=*/1>;

defm PPC_DispFormPrepMinThreshold : ValueField<
    "ppc-dispprep-min-threshold", "unsigned", "2",
    "Minimal common base load/store instructions triggering DS/DQ form "
    "preparation", /*hidden=*/1>;

defm PPC_ChainCommonPrepMinThreshold : ValueField<
    "ppc-chaincommon-min-threshold", "unsigned", "4",
    "Minimal common base load/store instructions triggering chain "
    "commoning preparation. Must be not smaller than 4", /*hidden=*/1>;

// --- PPCMachineFunctionInfo.cpp ---

defm PPC_DisableNonVolatileCR : BoolField<"ppc-disable-non-volatile-cr",
    "false", "Disable the use of non-volatile CR register fields",
    /*hidden=*/1>;

// --- PPCTargetTransformInfo.cpp ---

defm PPC_EVL : BoolField<"ppc-evl", "false",
    "Allow EVL type vp.load/vp.store", /*hidden=*/1>;

defm PPC_Pwr9EVL : BoolField<"ppc-pwr9-evl", "false",
    "Allow vp.load and vp.store for pwr9", /*hidden=*/1>;

defm PPC_VecMaskCost : BoolField<"ppc-vec-mask-cost", "true",
    "add masking cost for i1 vectors", /*hidden=*/1>;

defm PPC_DisableConstHoist : BoolField<"disable-ppc-constant-hoisting",
    "false", "disable constant hoisting on PPC", /*hidden=*/1>;

defm PPC_EnableColdCC : BoolField<"ppc-enable-coldcc", "false",
    "Enable using coldcc calling conv for cold internal functions",
    /*hidden=*/1>;

defm PPC_LsrNoInsnsCost : BoolField<"ppc-lsr-no-insns-cost", "false",
    "Do not add instruction count to lsr cost model", /*hidden=*/1>;

defm PPC_SmallCTRLoopThreshold : ValueField<"min-ctr-loop-threshold",
    "unsigned", "4",
    "Loops with a constant trip count smaller than "
    "this value will not use the count register.", /*hidden=*/1>;

// --- PPCVSXFMAMutate.cpp ---

defm PPC_DisableVSXFMAMutate : BoolField<"disable-ppc-vsx-fma-mutation",
    "true", "Disable VSX FMA instruction mutation", /*hidden=*/1>;

// --- PPCMIPeephole.cpp ---

defm PPC_FixedPointRegToImm : BoolField<"ppc-reg-to-imm-fixed-point",
    "true", "Iterate to a fixed point when attempting to "
    "convert reg-reg instructions to reg-imm", /*hidden=*/1>;

defm PPC_ConvertRegReg : BoolField<"ppc-convert-rr-to-ri", "true",
    "Convert eligible reg+reg instructions to reg+imm", /*hidden=*/1>;

defm PPC_EnableSExtElimination : BoolField<"ppc-eliminate-signext", "true",
    "enable elimination of sign-extensions", /*hidden=*/1>;

defm PPC_EnableZExtElimination : BoolField<"ppc-eliminate-zeroext", "true",
    "enable elimination of zero-extensions", /*hidden=*/1>;

defm PPC_EnableTrapOptimization : BoolField<"ppc-opt-conditional-trap",
    "false", "enable optimization of conditional traps", /*hidden=*/1>;

// --- PPCRegisterInfo.cpp ---

defm PPC_EnableBasePointer : BoolField<"ppc-use-base-pointer", "true",
    "Enable use of a base pointer for complex stack frames", /*hidden=*/1>;

defm PPC_AlwaysBasePointer : BoolField<"ppc-always-use-base-pointer",
    "false", "Force the use of a base pointer in every function",
    /*hidden=*/1>;

defm PPC_EnableGPRToVecSpills : BoolField<"ppc-enable-gpr-to-vsr-spills",
    "false", "Enable spills from gpr to vsr rather than stack",
    /*hidden=*/1>;

defm PPC_StackPtrConst : BoolField<"ppc-stack-ptr-caller-preserved", "true",
    "Consider R1 caller preserved so stack saves of "
    "caller preserved registers can be LICM candidates", /*hidden=*/1>;

defm PPC_MaxCRBitSpillDist : ValueField<"ppc-max-crbit-spill-dist",
    "unsigned", "100",
    "Maximum search distance for definition of CR bit spill on ppc",
    /*hidden=*/1>;
