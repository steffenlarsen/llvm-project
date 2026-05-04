//===- X86Options.td - CLV2 option defs for X86 -----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def X86OptsReg : Registry<"llvm::x86_opts"> {
  // Getter bodies are compiled once, in X86OptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

// --- X86AvoidStoreForwardingBlocks.cpp ---

def X86_DisableAvoidSFB : Option<
    "x86-disable-avoid-SFB",
    "X86: Disable Store Forwarding Blocks fixup.",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

def X86_SFBInspectionLimit : Option<
    "x86-sfb-inspection-limit",
    "X86: Number of instructions backward to "
    "inspect for store forwarding blocks.",
    "unsigned"> {
  let Registry = X86OptsReg;
  let Default = "20";
  let Hidden = 1;
}

// --- X86CallFrameOptimization.cpp ---

def X86_NoCallFrameOpt : Option<
    "no-x86-call-frame-opt",
    "Avoid optimizing x86 call frames for size",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

// --- X86CmovConversion.cpp ---

def X86_CmovConverter : Option<
    "x86-cmov-converter",
    "Enable the X86 cmov-to-branch optimization.",
    "bool"> {
  let Registry = X86OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def X86_CmovConverterThreshold : Option<
    "x86-cmov-converter-threshold",
    "Minimum gain per loop (in cycles) threshold.",
    "unsigned"> {
  let Registry = X86OptsReg;
  let Default = "4";
  let Hidden = 1;
}

def X86_CmovConverterForceMemOperand : Option<
    "x86-cmov-converter-force-mem-operand",
    "Convert cmovs to branches whenever they have memory operands.",
    "bool"> {
  let Registry = X86OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def X86_CmovConverterForceAll : Option<
    "x86-cmov-converter-force-all",
    "Convert all cmovs to branches.",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

// --- X86DomainReassignment.cpp ---

def X86_DisableDomainReassignment : Option<
    "disable-x86-domain-reassignment",
    "X86: Disable Virtual Register Reassignment.",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

// --- X86FixupBWInsts.cpp ---

def X86_FixupBWInsts : Option<
    "fixup-byte-word-insts",
    "Change byte and word instructions to larger sizes",
    "bool"> {
  let Registry = X86OptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- X86IndirectBranchTracking.cpp ---

def X86_IndirectBranchTracking : Option<
    "x86-indirect-branch-tracking",
    "Enable X86 indirect branch tracking pass.",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

// --- X86InsertVZeroUpper.cpp ---

def X86_UseVZeroUpper : Option<
    "x86-use-vzeroupper",
    "Minimize AVX to SSE transition penalty",
    "bool"> {
  let Registry = X86OptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- X86InstrInfo.cpp ---

def X86_DisableSpillFusing : Option<
    "disable-spill-fusing",
    "Disable fusing of spill code into instructions",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

def X86_PrintFailedFusing : Option<
    "print-failed-fuse-candidates",
    "Print instructions that the allocator wants to"
    " fuse, but the X86 backend currently can't",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

def X86_ReMatPICStubLoad : Option<
    "remat-pic-stub-load",
    "Re-materialize load from stub in PIC mode",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

def X86_PartialRegUpdateClearance : Option<
    "partial-reg-update-clearance",
    "Clearance between two register writes "
    "for inserting XOR to avoid partial "
    "register update",
    "unsigned"> {
  let Registry = X86OptsReg;
  let Default = "64";
  let Hidden = 1;
}

def X86_UndefRegClearance : Option<
    "undef-reg-clearance",
    "How many idle instructions we would like before "
    "certain undef register reads",
    "unsigned"> {
  let Registry = X86OptsReg;
  let Default = "128";
  let Hidden = 1;
}

def X86_MaxNFConversions : Option<
    "x86-max-nf-conversions-for-cmp-reuse",
    "Maximum number of NF conversions allowed to reuse EFLAGS from a "
    "producer dominating a multi-predecessor block",
    "unsigned"> {
  let Registry = X86OptsReg;
  let Default = "6";
  let Hidden = 1;
}

// --- X86ISelDAGToDAG.cpp ---

def X86_AndImmShrink : Option<
    "x86-and-imm-shrink",
    "Enable setting constant bits to reduce size of mask immediates",
    "bool"> {
  let Registry = X86OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def X86_PromoteAnyextLoad : Option<
    "x86-promote-anyext-load",
    "Enable promoting aligned anyext load to wider load",
    "bool"> {
  let Registry = X86OptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- X86ISelLowering.cpp ---

def X86_ExperimentalPrefInnermostLoopAlignment : Option<
    "x86-experimental-pref-innermost-loop-alignment",
    "Sets the preferable loop alignment for experiments (as log2 bytes) "
    "for innermost loops only. If specified, this option overrides "
    "alignment set by x86-experimental-pref-loop-alignment.",
    "int"> {
  let Registry = X86OptsReg;
  let Default = "4";
  let Hidden = 1;
}

def X86_BrMergingBaseCost : Option<
    "x86-br-merging-base-cost",
    "Sets the cost threshold for when multiple conditionals will be merged "
    "into one branch versus be split in multiple branches. Merging "
    "conditionals saves branches at the cost of additional instructions. "
    "This value sets the instruction cost limit, below which conditionals "
    "will be merged, and above which conditionals will be split. Set to -1 "
    "to never merge branches.",
    "int"> {
  let Registry = X86OptsReg;
  let Default = "2";
  let Hidden = 1;
}

def X86_BrMergingCcmpBias : Option<
    "x86-br-merging-ccmp-bias",
    "Increases 'x86-br-merging-base-cost' in cases that the target "
    "supports conditional compare instructions.",
    "int"> {
  let Registry = X86OptsReg;
  let Default = "6";
  let Hidden = 1;
}

def X86_WidenShift : Option<
    "x86-widen-shift",
    "Replace narrow shifts with wider shifts.",
    "bool"> {
  let Registry = X86OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def X86_BrMergingLikelyBias : Option<
    "x86-br-merging-likely-bias",
    "Increases 'x86-br-merging-base-cost' in cases that it is likely "
    "that all conditionals will be executed. For example for merging "
    "the conditionals (a == b && c > d), if its known that a == b is "
    "likely, then it is likely that if the conditionals are split "
    "both sides will be executed, so it may be desirable to increase "
    "the instruction cost threshold. Set to -1 to never merge likely "
    "branches.",
    "int"> {
  let Default = "0";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

def X86_BrMergingUnlikelyBias : Option<
    "x86-br-merging-unlikely-bias",
    "Decreases 'x86-br-merging-base-cost' in cases that it is unlikely "
    "that all conditionals will be executed. For example for merging "
    "the conditionals (a == b && c > d), if its known that a == b is "
    "unlikely, then it is unlikely that if the conditionals are split "
    "both sides will be executed, so it may be desirable to decrease "
    "the instruction cost threshold. Set to -1 to never merge unlikely "
    "branches.",
    "int"> {
  let Registry = X86OptsReg;
  let Default = "-1";
  let Hidden = 1;
}

def X86_MulConstantOptimization : Option<
    "mul-constant-optimization",
    "Replace 'mul x, Const' with more effective instructions like "
    "SHIFT, LEA, etc.",
    "bool"> {
  let Registry = X86OptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- X86LoadValueInjectionLoadHardening.cpp ---

def X86_LVILoadOptPlugin : Option<
    "x86-lvi-load-opt-plugin",
    "Specify a plugin to optimize LFENCE insertion",
    "std::string"> {
  let Default = "\"\"";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

def X86_LVILoadNoCBranch : Option<
    "x86-lvi-load-no-cbranch",
    "Don't treat conditional branches as disclosure gadgets. This "
    "may improve performance, at the cost of security.",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

def X86_LVILoadDot : Option<
    "x86-lvi-load-dot",
    "For each function, emit a dot graph depicting potential LVI gadgets",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

def X86_LVILoadDotOnly : Option<
    "x86-lvi-load-dot-only",
    "For each function, emit a dot graph depicting potential LVI "
    "gadgets, and do not insert any fences",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

def X86_LVILoadDotVerify : Option<
    "x86-lvi-load-dot-verify",
    "For each function, emit a dot graph to stdout depicting "
    "potential LVI gadgets, used for testing purposes only",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

// --- X86LowerAMXIntrinsics.cpp ---

def X86_ScalarizeAMX : Option<
    "enable-x86-scalar-amx",
    "X86: enable AMX scalarizition.",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

// --- X86MCInstLower.cpp ---

def X86_EnableBranchHint : Option<
    "enable-branch-hint",
    "Enable branch hint.",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

def X86_BranchHintProbabilityThreshold : Option<
    "branch-hint-probability-threshold",
    "The probability threshold of enabling branch hint.",
    "unsigned"> {
  let Registry = X86OptsReg;
  let Default = "50";
  let Hidden = 1;
}

// --- X86OptimizeLEAs.cpp ---

def X86_DisableLEAOpt : Option<
    "disable-x86-lea-opt",
    "X86: Disable LEA optimizations.",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

// --- X86RegisterInfo.cpp ---

def X86_UseBasePointer : Option<
    "x86-use-base-pointer",
    "Enable use of a base pointer for complex stack frames",
    "bool"> {
  let Registry = X86OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def X86_DisableRegAllocNDDHints : Option<
    "x86-disable-regalloc-hints-for-ndd",
    "Disable two address hints for register "
    "allocation",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

def X86_SetjmpCSRWarningThreshold : Option<
    "x86-setjmp-csr-warning-threshold",
    "Basic block count threshold for emitting a warning about "
    "callee-saved registers reserved due to setjmp",
    "unsigned"> {
  let Registry = X86OptsReg;
  let Default = "50";
  let Hidden = 1;
}

// --- X86SelectionDAGInfo.cpp ---

def X86_UseFSRMForMemcpy : Option<
    "x86-use-fsrm-for-memcpy",
    "Use fast short rep mov in memcpy lowering",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

// --- X86SpeculativeExecutionSideEffectSuppression.cpp ---

def X86_SESESEnableWithoutLVICFI : Option<
    "x86-seses-enable-without-lvi-cfi",
    "Force enable speculative execution side effect suppression. "
    "(Note: User must pass -mlvi-cfi in order to mitigate indirect "
    "branches and returns.)",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

def X86_SESESOneLFENCEPerBB : Option<
    "x86-seses-one-lfence-per-bb",
    "Omit all lfences other than the first to be placed in a basic block.",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

def X86_SESESOnlyLFENCENonConst : Option<
    "x86-seses-only-lfence-non-const",
    "Only lfence before groups of terminators where at least one "
    "branch instruction has an input to the addressing mode that is a "
    "register other than %rip.",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

def X86_SESESOmitBranchLFENCEs : Option<
    "x86-seses-omit-branch-lfences",
    "Omit all lfences before branch instructions.",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

// --- X86SpeculativeLoadHardening.cpp ---

def X86_SpeculativeLoadHardening : Option<
    "x86-speculative-load-hardening",
    "Force enable speculative load hardening",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

def X86_SLHLfence : Option<
    "x86-slh-lfence",
    "Use LFENCE along each conditional edge to harden against speculative "
    "loads rather than conditional movs and poisoned pointers.",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

def X86_SLHPostLoad : Option<
    "x86-slh-post-load",
    "Harden the value loaded *after* it is loaded by "
    "flushing the loaded bits to 1. This is hard to do "
    "in general but can be done easily for GPRs.",
    "bool"> {
  let Registry = X86OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def X86_SLHFenceCallAndRet : Option<
    "x86-slh-fence-call-and-ret",
    "Use a full speculation fence to harden both call and ret edges "
    "rather than a lighter weight mitigation.",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

def X86_SLHInterprocedural : Option<
    "x86-slh-ip",
    "Harden interprocedurally by passing our state in and out of "
    "functions in the high bits of the stack pointer.",
    "bool"> {
  let Registry = X86OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def X86_SLHLoads : Option<
    "x86-slh-loads",
    "Sanitize loads from memory. When disable, no "
    "significant security is provided.",
    "bool"> {
  let Registry = X86OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def X86_SLHIndirect : Option<
    "x86-slh-indirect",
    "Harden indirect calls and jumps against using speculatively "
    "stored attacker controlled addresses. This is designed to "
    "mitigate Spectre v1.2 style attacks.",
    "bool"> {
  let Registry = X86OptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- X86Subtarget.cpp ---

def X86_EarlyIfConv : Option<
    "x86-early-ifcvt",
    "Enable early if-conversion on X86",
    "bool"> {
  let Registry = X86OptsReg;
  let Hidden = 1;
}

// --- X86SuppressAPXForReloc.cpp ---

def X86_EnableAPXForRelocation : Option<
    "x86-enable-apx-for-relocation",
    "Enable APX features (EGPR, NDD and NF) for instructions with "
    "relocations on x86-64 ELF",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
}

// --- X86TargetMachine.cpp ---

def X86_MachineCombiner : Option<
    "x86-machine-combiner",
    "Enable the machine combiner pass",
    "bool"> {
  let Registry = X86OptsReg;
  let Default = "1";
  let Hidden = 1;
}

def X86_TileRA : Option<
    "x86-tile-ra",
    "Enable the tile register allocation pass",
    "bool"> {
  let Registry = X86OptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- X86WinEHUnwindV2.cpp ---

def X86_WinEHUnwindV2UnwindCodesThreshold : Option<
    "x86-wineh-unwindv2-unwind-codes-threshold",
    "Maximum number of unwind codes before "
    "splitting into a new unwind info.",
    "unsigned"> {
  let Registry = X86OptsReg;
  let Default = "255";
  let Hidden = 1;
}

def X86_WinEHUnwindV2ForceMode : Option<
    "x86-wineh-unwindv2-force-mode",
    "Overwrites the Unwind v2 mode for testing purposes.",
    "unsigned"> {
  let Default = "0";
  let Registry = X86OptsReg;
  let Hidden = 1;
}

def X86_WinEHUnwindV2InstructionCountThreshold : Option<
    "x86-wineh-unwindv2-instruction-count-threshold",
    "Maximum number of (approximate) instructions before splitting "
    "into a new unwind info.",
    "unsigned"> {
  let Registry = X86OptsReg;
  let Default = "600";
  let Hidden = 1;
}

// --- AsmParser/X86AsmParser.cpp ---

def X86_LVIInlineAsmHardening : Option<
    "x86-experimental-lvi-inline-asm-hardening",
    "Harden inline assembly code that may be vulnerable to Load Value"
    " Injection (LVI). This feature is experimental.",
    "bool"> {
  let Registry = X86OptsReg;
  let Hidden = 1;
}

// --- MCTargetDesc/X86AsmBackend.cpp ---

def X86_AlignBranchBoundary : Option<
    "x86-align-branch-boundary",
    "Control how the assembler should align branches with NOP. If the "
    "boundary's size is not 0, it should be a power of 2 and no less "
    "than 32. Branches will be aligned to prevent from being across or "
    "against the boundary of specified size. The default value 0 does not "
    "align branches.",
    "unsigned"> {
  let Default = "0";
  let Registry = X86OptsReg;
}

def X86_AlignBranch : Option<
    "x86-align-branch",
    "Specify types of branches to align (plus separated list of types):"
    "\njcc      indicates conditional jumps"
    "\nfused    indicates fused conditional jumps"
    "\njmp      indicates direct unconditional jumps"
    "\ncall     indicates direct and indirect calls"
    "\nret      indicates rets"
    "\nindirect indicates indirect unconditional jumps",
    "std::string"> {
  let Registry = X86OptsReg;
}

def X86_BranchesWithin32BBoundaries : Option<
    "x86-branches-within-32B-boundaries",
    "Align selected instructions to mitigate negative performance impact "
    "of Intel's micro code update for errata skx102.  May break "
    "assumptions about labels corresponding to particular instructions, "
    "and should be used with caution.",
    "bool"> {
  let Default = "false";
  let Registry = X86OptsReg;
}

def X86_PadMaxPrefixSize : Option<
    "x86-pad-max-prefix-size",
    "Maximum number of prefixes to use for padding",
    "unsigned"> {
  let Registry = X86OptsReg;
}

def X86_PadForAlign : Option<
    "x86-pad-for-align",
    "Pad previous instructions to implement align directives",
    "bool"> {
  let Registry = X86OptsReg;
  let Hidden = 1;
}

def X86_PadForBranchAlign : Option<
    "x86-pad-for-branch-align",
    "Pad previous instructions to implement branch alignment",
    "bool"> {
  let Registry = X86OptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- MCTargetDesc/X86MCAsmInfo.cpp ---

def X86AsmSyntaxATT : EnumValue<"att", "Emit AT&T-style assembly"> {
  let CppName = "ATT";
  let Value = 0;
}

def X86AsmSyntaxIntel : EnumValue<"intel", "Emit Intel-style assembly"> {
  let CppName = "Intel";
  let Value = 1;
}

def X86AsmSyntaxKind : EnumType<"X86AsmSyntaxKind",
    [X86AsmSyntaxATT, X86AsmSyntaxIntel]>;

def X86_AsmSyntax : Option<
    "x86-asm-syntax", "Select the assembly style for input",
    "X86AsmSyntaxKind"> {
  let Registry = X86OptsReg;
  let Default = "att";
  let Hidden = 1;
}

def X86_MarkDataRegions : Option<
    "mark-data-regions",
    "Mark code section jump table data regions.",
    "bool"> {
  let Registry = X86OptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- X86WinEHUnwindV3.cpp ---

def X86_ApproxBytesPerInstr : Option<
    "x86-wineh-unwindv3-instr-avg-size",
    "Average size of an instruction. This value is used in determining "
    "split points for chained unwinder info",
    "unsigned"> {
  let Registry = X86OptsReg;
  let Default = "7";
  let Hidden = 1;
}
