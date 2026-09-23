//===- X86Options.td - X86 target options ----------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def X86Options : OptionsStruct<"llvm"> {}

// --- X86AvoidStoreForwardingBlocks.cpp ---

defm X86_DisableAvoidSFB : BoolField<
    "x86-disable-avoid-SFB", "false",
    "X86: Disable Store Forwarding Blocks fixup.",
    /*hidden=*/1>;

defm X86_SFBInspectionLimit : ValueField<
    "x86-sfb-inspection-limit", "unsigned", "20",
    "X86: Number of instructions backward to "
    "inspect for store forwarding blocks.",
    /*hidden=*/1>;

// --- X86CallFrameOptimization.cpp ---

defm X86_NoCallFrameOpt : BoolField<
    "no-x86-call-frame-opt", "false",
    "Avoid optimizing x86 call frames for size",
    /*hidden=*/1>;

// --- X86CmovConversion.cpp ---

defm X86_CmovConverter : BoolField<
    "x86-cmov-converter", "true",
    "Enable the X86 cmov-to-branch optimization.",
    /*hidden=*/1>;

defm X86_CmovConverterThreshold : ValueField<
    "x86-cmov-converter-threshold", "unsigned", "4",
    "Minimum gain per loop (in cycles) threshold.",
    /*hidden=*/1>;

defm X86_CmovConverterForceMemOperand : BoolField<
    "x86-cmov-converter-force-mem-operand", "true",
    "Convert cmovs to branches whenever they have memory operands.",
    /*hidden=*/1>;

defm X86_CmovConverterForceAll : BoolField<
    "x86-cmov-converter-force-all", "false",
    "Convert all cmovs to branches.",
    /*hidden=*/1>;

// --- X86DomainReassignment.cpp ---

defm X86_DisableDomainReassignment : BoolField<
    "disable-x86-domain-reassignment", "false",
    "X86: Disable Virtual Register Reassignment.",
    /*hidden=*/1>;

// --- X86FixupBWInsts.cpp ---

defm X86_FixupBWInsts : BoolField<
    "fixup-byte-word-insts", "true",
    "Change byte and word instructions to larger sizes",
    /*hidden=*/1>;

// --- X86IndirectBranchTracking.cpp ---

defm X86_IndirectBranchTracking : BoolField<
    "x86-indirect-branch-tracking", "false",
    "Enable X86 indirect branch tracking pass.",
    /*hidden=*/1>;

// --- X86InsertVZeroUpper.cpp ---

defm X86_UseVZeroUpper : BoolField<
    "x86-use-vzeroupper", "true",
    "Minimize AVX to SSE transition penalty",
    /*hidden=*/1>;

// --- X86InstrInfo.cpp ---

defm X86_DisableSpillFusing : BoolField<
    "disable-spill-fusing", "false",
    "Disable fusing of spill code into instructions",
    /*hidden=*/1>;

defm X86_PrintFailedFusing : BoolField<
    "print-failed-fuse-candidates", "false",
    "Print instructions that the allocator wants to"
    " fuse, but the X86 backend currently can't",
    /*hidden=*/1>;

defm X86_ReMatPICStubLoad : BoolField<
    "remat-pic-stub-load", "false",
    "Re-materialize load from stub in PIC mode",
    /*hidden=*/1>;

defm X86_PartialRegUpdateClearance : ValueField<
    "partial-reg-update-clearance", "unsigned", "64",
    "Clearance between two register writes "
    "for inserting XOR to avoid partial "
    "register update",
    /*hidden=*/1>;

defm X86_UndefRegClearance : ValueField<
    "undef-reg-clearance", "unsigned", "128",
    "How many idle instructions we would like before "
    "certain undef register reads",
    /*hidden=*/1>;

defm X86_MaxNFConversions : ValueField<
    "x86-max-nf-conversions-for-cmp-reuse", "unsigned", "6",
    "Maximum number of NF conversions allowed to reuse EFLAGS from a "
    "producer dominating a multi-predecessor block",
    /*hidden=*/1>;

// --- X86ISelDAGToDAG.cpp ---

defm X86_AndImmShrink : BoolField<
    "x86-and-imm-shrink", "true",
    "Enable setting constant bits to reduce size of mask immediates",
    /*hidden=*/1>;

defm X86_PromoteAnyextLoad : BoolField<
    "x86-promote-anyext-load", "true",
    "Enable promoting aligned anyext load to wider load",
    /*hidden=*/1>;

// --- X86ISelLowering.cpp ---

// std::optional<int>: getPrefLoopAlignment() only overrides its computed
// result when the option was actually specified, so "unspecified" must
// survive as tri-state rather than be folded into the 4 literal default.
defm X86_ExperimentalPrefInnermostLoopAlignment : OptionalField<
    "x86-experimental-pref-innermost-loop-alignment", "int",
    "Sets the preferable loop alignment for experiments (as log2 bytes) "
    "for innermost loops only. If specified, this option overrides "
    "alignment set by x86-experimental-pref-loop-alignment.",
    /*hidden=*/1>;

defm X86_BrMergingBaseCost : ValueField<
    "x86-br-merging-base-cost", "int", "2",
    "Sets the cost threshold for when multiple conditionals will be merged "
    "into one branch versus be split in multiple branches. Merging "
    "conditionals saves branches at the cost of additional instructions. "
    "This value sets the instruction cost limit, below which conditionals "
    "will be merged, and above which conditionals will be split. Set to -1 "
    "to never merge branches.",
    /*hidden=*/1>;

defm X86_BrMergingCcmpBias : ValueField<
    "x86-br-merging-ccmp-bias", "int", "6",
    "Increases 'x86-br-merging-base-cost' in cases that the target "
    "supports conditional compare instructions.",
    /*hidden=*/1>;

defm X86_WidenShift : BoolField<
    "x86-widen-shift", "true",
    "Replace narrow shifts with wider shifts.",
    /*hidden=*/1>;

defm X86_BrMergingLikelyBias : ValueField<
    "x86-br-merging-likely-bias", "int", "0",
    "Increases 'x86-br-merging-base-cost' in cases that it is likely "
    "that all conditionals will be executed. For example for merging "
    "the conditionals (a == b && c > d), if its known that a == b is "
    "likely, then it is likely that if the conditionals are split "
    "both sides will be executed, so it may be desirable to increase "
    "the instruction cost threshold. Set to -1 to never merge likely "
    "branches.",
    /*hidden=*/1>;

defm X86_BrMergingUnlikelyBias : ValueField<
    "x86-br-merging-unlikely-bias", "int", "-1",
    "Decreases 'x86-br-merging-base-cost' in cases that it is unlikely "
    "that all conditionals will be executed. For example for merging "
    "the conditionals (a == b && c > d), if its known that a == b is "
    "unlikely, then it is unlikely that if the conditionals are split "
    "both sides will be executed, so it may be desirable to decrease "
    "the instruction cost threshold. Set to -1 to never merge unlikely "
    "branches.",
    /*hidden=*/1>;

defm X86_MulConstantOptimization : BoolField<
    "mul-constant-optimization", "true",
    "Replace 'mul x, Const' with more effective instructions like "
    "SHIFT, LEA, etc.",
    /*hidden=*/1>;

// --- X86LoadValueInjectionLoadHardening.cpp ---

defm X86_LVILoadOptPlugin : ValueField<
    "x86-lvi-load-opt-plugin", "std::string", "\"\"",
    "Specify a plugin to optimize LFENCE insertion",
    /*hidden=*/1>;

defm X86_LVILoadNoCBranch : BoolField<
    "x86-lvi-load-no-cbranch", "false",
    "Don't treat conditional branches as disclosure gadgets. This "
    "may improve performance, at the cost of security.",
    /*hidden=*/1>;

defm X86_LVILoadDot : BoolField<
    "x86-lvi-load-dot", "false",
    "For each function, emit a dot graph depicting potential LVI gadgets",
    /*hidden=*/1>;

defm X86_LVILoadDotOnly : BoolField<
    "x86-lvi-load-dot-only", "false",
    "For each function, emit a dot graph depicting potential LVI "
    "gadgets, and do not insert any fences",
    /*hidden=*/1>;

defm X86_LVILoadDotVerify : BoolField<
    "x86-lvi-load-dot-verify", "false",
    "For each function, emit a dot graph to stdout depicting "
    "potential LVI gadgets, used for testing purposes only",
    /*hidden=*/1>;

// --- X86LowerAMXIntrinsics.cpp ---

defm X86_ScalarizeAMX : BoolField<
    "enable-x86-scalar-amx", "false",
    "X86: enable AMX scalarizition.",
    /*hidden=*/1>;

// --- X86MCInstLower.cpp ---

defm X86_EnableBranchHint : BoolField<
    "enable-branch-hint", "false",
    "Enable branch hint.",
    /*hidden=*/1>;

defm X86_BranchHintProbabilityThreshold : ValueField<
    "branch-hint-probability-threshold", "unsigned", "50",
    "The probability threshold of enabling branch hint.",
    /*hidden=*/1>;

// --- X86OptimizeLEAs.cpp ---

defm X86_DisableLEAOpt : BoolField<
    "disable-x86-lea-opt", "false",
    "X86: Disable LEA optimizations.",
    /*hidden=*/1>;

// --- X86RegisterInfo.cpp ---

defm X86_UseBasePointer : BoolField<
    "x86-use-base-pointer", "true",
    "Enable use of a base pointer for complex stack frames",
    /*hidden=*/1>;

defm X86_DisableRegAllocNDDHints : BoolField<
    "x86-disable-regalloc-hints-for-ndd", "false",
    "Disable two address hints for register "
    "allocation",
    /*hidden=*/1>;

defm X86_SetjmpCSRWarningThreshold : ValueField<
    "x86-setjmp-csr-warning-threshold", "unsigned", "50",
    "Basic block count threshold for emitting a warning about "
    "callee-saved registers reserved due to setjmp",
    /*hidden=*/1>;

// --- X86SelectionDAGInfo.cpp ---

defm X86_UseFSRMForMemcpy : BoolField<
    "x86-use-fsrm-for-memcpy", "false",
    "Use fast short rep mov in memcpy lowering",
    /*hidden=*/1>;

// --- X86SpeculativeExecutionSideEffectSuppression.cpp ---

defm X86_SESESEnableWithoutLVICFI : BoolField<
    "x86-seses-enable-without-lvi-cfi", "false",
    "Force enable speculative execution side effect suppression. "
    "(Note: User must pass -mlvi-cfi in order to mitigate indirect "
    "branches and returns.)",
    /*hidden=*/1>;

defm X86_SESESOneLFENCEPerBB : BoolField<
    "x86-seses-one-lfence-per-bb", "false",
    "Omit all lfences other than the first to be placed in a basic block.",
    /*hidden=*/1>;

defm X86_SESESOnlyLFENCENonConst : BoolField<
    "x86-seses-only-lfence-non-const", "false",
    "Only lfence before groups of terminators where at least one "
    "branch instruction has an input to the addressing mode that is a "
    "register other than %rip.",
    /*hidden=*/1>;

defm X86_SESESOmitBranchLFENCEs : BoolField<
    "x86-seses-omit-branch-lfences", "false",
    "Omit all lfences before branch instructions.",
    /*hidden=*/1>;

// --- X86SpeculativeLoadHardening.cpp ---

defm X86_SpeculativeLoadHardening : BoolField<
    "x86-speculative-load-hardening", "false",
    "Force enable speculative load hardening",
    /*hidden=*/1>;

defm X86_SLHLfence : BoolField<
    "x86-slh-lfence", "false",
    "Use LFENCE along each conditional edge to harden against speculative "
    "loads rather than conditional movs and poisoned pointers.",
    /*hidden=*/1>;

defm X86_SLHPostLoad : BoolField<
    "x86-slh-post-load", "true",
    "Harden the value loaded *after* it is loaded by "
    "flushing the loaded bits to 1. This is hard to do "
    "in general but can be done easily for GPRs.",
    /*hidden=*/1>;

defm X86_SLHFenceCallAndRet : BoolField<
    "x86-slh-fence-call-and-ret", "false",
    "Use a full speculation fence to harden both call and ret edges "
    "rather than a lighter weight mitigation.",
    /*hidden=*/1>;

defm X86_SLHInterprocedural : BoolField<
    "x86-slh-ip", "true",
    "Harden interprocedurally by passing our state in and out of "
    "functions in the high bits of the stack pointer.",
    /*hidden=*/1>;

defm X86_SLHLoads : BoolField<
    "x86-slh-loads", "true",
    "Sanitize loads from memory. When disable, no "
    "significant security is provided.",
    /*hidden=*/1>;

defm X86_SLHIndirect : BoolField<
    "x86-slh-indirect", "true",
    "Harden indirect calls and jumps against using speculatively "
    "stored attacker controlled addresses. This is designed to "
    "mitigate Spectre v1.2 style attacks.",
    /*hidden=*/1>;

// --- X86Subtarget.cpp ---

defm X86_EarlyIfConv : BoolField<
    "x86-early-ifcvt", "false",
    "Enable early if-conversion on X86",
    /*hidden=*/1>;

// --- X86SuppressAPXForReloc.cpp ---

defm X86_EnableAPXForRelocation : BoolField<
    "x86-enable-apx-for-relocation", "false",
    "Enable APX features (EGPR, NDD and NF) for instructions with "
    "relocations on x86-64 ELF">;

// --- X86TargetMachine.cpp ---

defm X86_MachineCombiner : BoolField<
    "x86-machine-combiner", "true",
    "Enable the machine combiner pass",
    /*hidden=*/1>;

defm X86_TileRA : BoolField<
    "x86-tile-ra", "true",
    "Enable the tile register allocation pass",
    /*hidden=*/1>;

// --- X86WinEHUnwindV2.cpp ---

defm X86_WinEHUnwindV2UnwindCodesThreshold : ValueField<
    "x86-wineh-unwindv2-unwind-codes-threshold", "unsigned", "255",
    "Maximum number of unwind codes before "
    "splitting into a new unwind info.",
    /*hidden=*/1>;

// std::optional<unsigned>: runAnalysisOnFuncOrFunclet() branches on
// wasOptSpecified<> itself (not just the resolved value), so "unspecified"
// must survive as tri-state rather than be folded into the 0 literal
// default.
defm X86_WinEHUnwindV2ForceMode : OptionalField<
    "x86-wineh-unwindv2-force-mode", "unsigned",
    "Overwrites the Unwind v2 mode for testing purposes.",
    /*hidden=*/1>;

defm X86_WinEHUnwindV2InstructionCountThreshold : ValueField<
    "x86-wineh-unwindv2-instruction-count-threshold", "unsigned", "600",
    "Maximum number of (approximate) instructions before splitting "
    "into a new unwind info.",
    /*hidden=*/1>;

// --- AsmParser/X86AsmParser.cpp ---

defm X86_LVIInlineAsmHardening : BoolField<
    "x86-experimental-lvi-inline-asm-hardening", "false",
    "Harden inline assembly code that may be vulnerable to Load Value"
    " Injection (LVI). This feature is experimental.",
    /*hidden=*/1>;

// --- MCTargetDesc/X86AsmBackend.cpp ---

// std::optional<unsigned>: the X86AsmBackend constructor only overrides its
// computed AlignBoundary when the option was actually specified, so
// "unspecified" must survive as tri-state rather than be folded into the 0
// literal default.
defm X86_AlignBranchBoundary : OptionalField<
    "x86-align-branch-boundary", "unsigned",
    "Control how the assembler should align branches with NOP. If the "
    "boundary's size is not 0, it should be a power of 2 and no less "
    "than 32. Branches will be aligned to prevent from being across or "
    "against the boundary of specified size. The default value 0 does not "
    "align branches.">;

// std::optional<std::string>: same "override only if specified" reasoning
// as X86_AlignBranchBoundary above; the plus-separated spelling list is
// parsed application-side into an X86AlignBranchKind bitmask.
defm X86_AlignBranch : OptionalField<
    "x86-align-branch", "std::string",
    "Specify types of branches to align (plus separated list of types):"
    "\njcc      indicates conditional jumps"
    "\nfused    indicates fused conditional jumps"
    "\njmp      indicates direct unconditional jumps"
    "\ncall     indicates direct and indirect calls"
    "\nret      indicates rets"
    "\nindirect indicates indirect unconditional jumps">;

defm X86_BranchesWithin32BBoundaries : BoolField<
    "x86-branches-within-32B-boundaries", "false",
    "Align selected instructions to mitigate negative performance impact "
    "of Intel's micro code update for errata skx102.  May break "
    "assumptions about labels corresponding to particular instructions, "
    "and should be used with caution.">;

// std::optional<unsigned>: same "override only if specified" reasoning as
// X86_AlignBranchBoundary above.
defm X86_PadMaxPrefixSize : OptionalField<
    "x86-pad-max-prefix-size", "unsigned",
    "Maximum number of prefixes to use for padding">;

defm X86_PadForAlign : BoolField<
    "x86-pad-for-align", "false",
    "Pad previous instructions to implement align directives",
    /*hidden=*/1>;

defm X86_PadForBranchAlign : BoolField<
    "x86-pad-for-branch-align", "true",
    "Pad previous instructions to implement branch alignment",
    /*hidden=*/1>;

// --- MCTargetDesc/X86MCAsmInfo.cpp ---

def X86AsmSyntaxEnum : OptionEnum<"X86AsmSyntaxKind", [
    EnumMember<"ATT", "att">,
    EnumMember<"Intel", "intel">]>;

// std::optional<X86AsmSyntaxKind>: getX86AsmSyntax() only overrides the
// process-wide runtime default (set via setX86AsmSyntax(), itself driven by
// e.g. llvm-mc's own --x86-asm-syntax-shaped flag) when this option was
// actually specified, so "unspecified" must survive as tri-state rather
// than be folded into a literal "att" default. Hand-authored (not
// EnumField<>) because no multiclass composes std::optional<> field
// storage with FieldEnum<> tracking.
def X86_AsmSyntax : Joined<["-", "--"], "x86-asm-syntax=">,
    HelpText<"Select the assembly style for input">,
    Flags<[HelpHidden]>,
    Field<"std::optional<X86AsmSyntaxKind>", "X86_AsmSyntax", "std::nullopt">,
    FieldEnum<X86AsmSyntaxEnum>;
def X86_AsmSyntax_sep : Separate<["-", "--"], "x86-asm-syntax">,
    Flags<[HelpHidden]>, Alias<!cast<Option>(X86_AsmSyntax)>;

defm X86_MarkDataRegions : BoolField<
    "mark-data-regions", "true",
    "Mark code section jump table data regions.",
    /*hidden=*/1>;

// --- X86WinEHUnwindV3.cpp ---

defm X86_ApproxBytesPerInstr : ValueField<
    "x86-wineh-unwindv3-instr-avg-size", "unsigned", "7",
    "Average size of an instruction. This value is used in determining "
    "split points for chained unwinder info",
    /*hidden=*/1>;
