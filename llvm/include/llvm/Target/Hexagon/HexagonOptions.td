//===- HexagonOptions.td - CLV2 option defs for Hexagon ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def HexagonOptsReg : Registry<"llvm::hex_opts"> { let OutOfLineGetters = 1; }

// --- HexagonBitSimplify.cpp ---

def HEX_PreserveTiedOps : Option<
    "hexbit-keep-tied",
    "Preserve subregisters in tied operands",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_GenExtractBit : Option<
    "hexbit-extract",
    "Generate extract instructions",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_GenBitSplit : Option<
    "hexbit-bitsplit",
    "Generate bitsplit instructions",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_MaxExtract : Option<
    "hexbit-max-extract", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "4294967295";
  let Hidden = 1;
}

def HEX_MaxBitSplit : Option<
    "hexbit-max-bitsplit", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "4294967295";
  let Hidden = 1;
}

def HEX_RegisterSetLimit : Option<
    "hexbit-registerset-limit", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "1000";
  let Hidden = 1;
}

// --- HexagonBranchRelaxation.cpp ---

def HEX_BranchRelaxSafetyBuffer : Option<
    "branch-relax-safety-buffer",
    "safety buffer size",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "200";
  let Hidden = 1;
}

// --- HexagonCommonGEP.cpp ---

def HEX_OptSpeculate : Option<
    "commgep-speculate", "",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_OptEnableInv : Option<
    "commgep-inv", "",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_OptEnableConst : Option<
    "commgep-const", "",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- HexagonConstExtenders.cpp ---

def HEX_CExtThreshold : Option<
    "hexagon-cext-threshold",
    "Minimum number of extenders to trigger replacement",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "3";
  let Hidden = 1;
}

def HEX_CExtLimit : Option<
    "hexagon-cext-limit",
    "Maximum number of replacements",
    "unsigned"> {
  let Default = "0";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonCopyHoisting.cpp ---

def HEX_CPHoistFn : Option<
    "cphoistfn", "",
    "std::string"> {
  let Default = "\"\"";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonCopyToCombine.cpp ---

def HEX_DisableMergeIntoCombines : Option<
    "disable-merge-into-combines",
    "Disable merging into combines",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_DisableConst64 : Option<
    "disable-const64",
    "Disable generation of const64",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_MaxNumInstBetweenNewValueStoreAndTFR : Option<
    "max-num-inst-between-tfr-and-nv-store",
    "Maximum distance between a tfr feeding a store we "
    "consider the store still to be newifiable",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "4";
  let Hidden = 1;
}

// --- HexagonEarlyIfConv.cpp ---

def HEX_EnableHexagonBP : Option<
    "enable-hexagon-br-prob", "",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EIFSizeLimit : Option<
    "eif-limit", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "6";
  let Hidden = 1;
}

def HEX_SkipExitBranches : Option<
    "eif-no-loop-exit", "",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonExpandCondsets.cpp ---

def HEX_ExpandCondsetsTfrLimit : Option<
    "expand-condsets-tfr-limit", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "4294967295";
  let Hidden = 1;
}

def HEX_ExpandCondsetsCoaLimit : Option<
    "expand-condsets-coa-limit", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "4294967295";
  let Hidden = 1;
}

// --- HexagonFixupHwLoops.cpp ---

def HEX_MaxLoopRange : Option<
    "hexagon-loop-range", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "200";
  let Hidden = 1;
}

// --- HexagonFrameLowering.cpp ---

def HEX_DisableDeallocRet : Option<
    "disable-hexagon-dealloc-ret",
    "Disable Dealloc Return for Hexagon target",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_NumberScavengerSlots : Option<
    "number-scavenger-slots",
    "Set the number of scavenger slots",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def HEX_SpillFuncThreshold : Option<
    "spill-func-threshold",
    "Specify O2(not Os) spill func threshold",
    "int"> {
  let Registry = HexagonOptsReg;
  let Default = "6";
  let Hidden = 1;
}

def HEX_SpillFuncThresholdOs : Option<
    "spill-func-threshold-Os",
    "Specify Os spill func threshold",
    "int"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EnableStackOVFSanitizer : Option<
    "enable-stackovf-sanitizer",
    "Enable runtime checks for stack overflow.",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EnableShrinkWrapping : Option<
    "hexagon-shrink-frame",
    "Enable stack frame shrink wrapping",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_ShrinkLimit : Option<
    "shrink-frame-limit",
    "Max count of stack frame shrink-wraps",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "4294967295";
  let Hidden = 1;
}

def HEX_EnableSaveRestoreLong : Option<
    "enable-save-restore-long",
    "Enable long calls for save-restore stubs.",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EliminateFramePointer : Option<
    "hexagon-fp-elim",
    "Refrain from using FP whenever possible",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_OptimizeSpillSlots : Option<
    "hexagon-opt-spill",
    "Optimize spill slots",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- HexagonGenExtract.cpp ---

def HEX_ExtractCutoff : Option<
    "extract-cutoff", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "4294967295";
  let Hidden = 1;
}

def HEX_NoSR0 : Option<
    "extract-nosr0", "",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- HexagonGenInsert.cpp ---

def HEX_VRegDistCutoff : Option<
    "insert-dist-cutoff", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "30";
  let Hidden = 1;
}

def HEX_MaxIFMSize : Option<
    "insert-max-ifmap", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "1024";
  let Hidden = 1;
}

def HEX_OptTiming : Option<
    "insert-timing", "",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_OptSelectAll0 : Option<
    "insert-all0", "",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_OptSelectHas0 : Option<
    "insert-has0", "",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_OptInsertConst : Option<
    "insert-const", "",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonGenMux.cpp ---

def HEX_MinPredDist : Option<
    "hexagon-gen-mux-threshold",
    "Minimum distance between predicate definition and furthest use",
    "unsigned"> {
  let Default = "0";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonGlobalScheduler.cpp ---

def HEX_DisablePullUp : Option<
    "disable-pull-up",
    "Disable the pull up of instructions optimization",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EnableSpeculativePullUp : Option<
    "enable-speculative-pull-up",
    "Enable speculative pull up scheduling",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EnableLocalPullUp : Option<
    "enable-local-pull-up",
    "Enable local scheduling of pull up candidates",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_AllowSpeculateLoads : Option<
    "speculate-loads-on-pull-up",
    "Allow speculative scheduling of loads",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_AllowCmpBranchLoads : Option<
    "cmp-branch-loads-pull-up",
    "Allow speculative scheduling of loads past cmp/branch",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_AllowUnlikelyPath : Option<
    "unlikely-path-pull-up",
    "Allow pull up of unlikely path instructions",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_AllowDependentPullUp : Option<
    "enable-dependent-pull-up",
    "Allow dependent pull up",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_PreventCompoundSeparation : Option<
    "prevent-compound-separation",
    "Prevent separation of compound instructions",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_PreventDuplexSeparation : Option<
    "prevent-duplex-separation",
    "Prevent separation of duplex instructions",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_MainCandidateQueueSize : Option<
    "pull-up-main-queue-size", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def HEX_SecondaryCandidateQueueSize : Option<
    "pull-up-sec-queue-size", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def HEX_PostPullUpOpt : Option<
    "post-pull-up-opt",
    "Enable post pull up optimization pass",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_SpeculateNonPredInsn : Option<
    "speculate-non-pred-insn",
    "Allow speculate non-predicated instructions",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_OneFloatPerPacket : Option<
    "single-float-packet",
    "Only allow one float instruction per packet",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_OneComplexPerPacket : Option<
    "single-complex-packet",
    "Only allow one complex instruction per packet",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonHardwareLoops.cpp ---

def HEX_HWLoopLimit : Option<
    "hexagon-max-hwloop", "",
    "int"> {
  let Registry = HexagonOptsReg;
  let Default = "-1";
  let Hidden = 1;
}

def HEX_PHFn : Option<
    "hexagon-hwloop-phfn", "",
    "std::string"> {
  let Default = "\"\"";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_HWCreatePreheader : Option<
    "hexagon-hwloop-preheader",
    "Automatically create preheader for hardware loops",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_SpecPreheader : Option<
    "hwloop-spec-preheader",
    "Allow speculative preheader",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonInstrInfo.cpp ---

def HEX_ScheduleInlineAsm : Option<
    "hexagon-sched-inline-asm",
    "Schedule inline assembly",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EnableBranchPrediction : Option<
    "hexagon-enable-branch-prediction",
    "Enable branch prediction",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_DisableNVSchedule : Option<
    "disable-hexagon-nv-schedule",
    "Disable schedule adjustment for new value stores",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EnableTimingClassLatency : Option<
    "enable-timing-class-latency",
    "Enable timing class latency",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EnableALUForwarding : Option<
    "enable-alu-forwarding",
    "Enable ALU forwarding",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EnableACCForwarding : Option<
    "enable-acc-forwarding",
    "Enable ACC forwarding",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_BranchRelaxAsmLarge : Option<
    "branch-relax-asm-large",
    "Branch relaxation for asm large",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- HexagonISelDAGToDAG.cpp ---

def HEX_EnableAddressRebalancing : Option<
    "isel-rebalance-addr", "",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_RebalanceOnlyForOptimizations : Option<
    "rebalance-only-opt", "",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_RebalanceOnlyImbalanced : Option<
    "rebalance-only-imbal", "",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_CheckSingleUse : Option<
    "hexagon-isel-su",
    "Check single use",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- HexagonISelLowering.cpp ---

def HEX_EmitJumpTables : Option<
    "hexagon-emit-jump-tables",
    "Control whether jump tables are emitted or not",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_MinimumJumpTables : Option<
    "minimum-jump-tables",
    "Set minimum jump tables",
    "int"> {
  let Registry = HexagonOptsReg;
  let Default = "5";
  let Hidden = 1;
}

def HEX_EnableHexSDNodeSched : Option<
    "enable-hexagon-sdnode-sched", "",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_AlignLoads : Option<
    "hexagon-align-loads",
    "Rewrite unaligned loads as a pair of aligned loads",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonISelLoweringHVX.cpp ---

def HEX_HvxWidenThreshold : Option<
    "hexagon-hvx-widen",
    "Lower threshold (in bytes) for widening to HVX vectors",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "16";
  let Hidden = 1;
}

// --- HexagonLoadStoreWidening.cpp ---

def HEX_MaxMBBSizeForLoadStoreWidening : Option<
    "max-bb-size-for-load-store-widening", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "1000";
  let Hidden = 1;
}

// --- HexagonLoopAlign.cpp ---

def HEX_DisableLoopAlign : Option<
    "disable-hexagon-loop-align",
    "Disable Hexagon loop alignment",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_HVXLoopAlignLimitUB : Option<
    "hexagon-hvx-loop-align-limit-ub",
    "Upper bound for HVX loop alignment",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "16";
  let Hidden = 1;
}

def HEX_TinyLoopAlignLimitUB : Option<
    "hexagon-tiny-loop-align-limit-ub",
    "Upper bound for tiny loop alignment",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "16";
  let Hidden = 1;
}

def HEX_LoopAlignLimitUB : Option<
    "hexagon-loop-align-limit-ub",
    "Upper bound for loop alignment",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def HEX_LoopAlignLimitLB : Option<
    "hexagon-loop-align-limit-lb",
    "Lower bound for loop alignment",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "4";
  let Hidden = 1;
}

def HEX_LoopBndlAlignLimit : Option<
    "hexagon-loop-bundle-align-limit",
    "Bundle alignment limit for loop alignment",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "4";
  let Hidden = 1;
}

def HEX_TinyLoopBndlAlignLimit : Option<
    "hexagon-tiny-loop-bundle-align-limit",
    "Bundle alignment limit for tiny loop alignment",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "8";
  let Hidden = 1;
}

// --- HexagonLoopIdiomRecognition.cpp ---

def HEX_DisableMemcpyIdiom : Option<
    "disable-memcpy-idiom",
    "Disable memcpy idiom recognition",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_DisableMemmoveIdiom : Option<
    "disable-memmove-idiom",
    "Disable memmove idiom recognition",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_RuntimeMemSizeThreshold : Option<
    "runtime-mem-idiom-threshold",
    "Runtime memory idiom threshold",
    "unsigned"> {
  let Default = "0";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_CompileTimeMemSizeThreshold : Option<
    "compile-time-mem-idiom-threshold",
    "Compile time memory idiom threshold",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "64";
  let Hidden = 1;
}

def HEX_OnlyNonNestedMemmove : Option<
    "only-nonnested-memmove-idiom",
    "Only non-nested memmove idiom",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_VolatileMemcpy : Option<
    "disable-hexagon-volatile-memcpy",
    "Hexagon volatile memcpy",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_HLIRSimplifyLimit : Option<
    "hlir-simplify-limit", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "10000";
  let Hidden = 1;
}

// --- HexagonNewValueJump.cpp ---

def HEX_DbgNVJCount : Option<
    "nvj-count", "",
    "int"> {
  let Registry = HexagonOptsReg;
  let Default = "-1";
  let Hidden = 1;
}

def HEX_DisableNewValueJumps : Option<
    "disable-nvjump",
    "Disable new value jumps",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonOptAddrMode.cpp ---

def HEX_CodeGrowthLimit : Option<
    "hexagon-amode-growth-limit",
    "Address mode growth limit",
    "int"> {
  let Default = "0";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonPeephole.cpp ---

def HEX_DisableOptSZExt : Option<
    "disable-hexagon-optszext",
    "Disable Hexagon opt sign/zero extend",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_DisablePNotP : Option<
    "disable-hexagon-pnotp",
    "Disable Hexagon PNOT P",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_DisableOptExtTo64 : Option<
    "disable-hexagon-opt-ext-to-64",
    "Disable Hexagon opt ext to 64",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- HexagonQFPOptimizer.cpp ---

def HEX_DisableQFPOptimizer : Option<
    "disable-qfp-opt",
    "Disable optimization of Qfloat operations.",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
}

def HEX_DisableQFOptForMul : Option<
    "disable-qfp-opt-mul",
    "Disable optimization of Qfloat operations for multiply.",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
}

// --- HexagonRDFOpt.cpp ---

def HEX_RDFLimit : Option<
    "hexagon-rdf-limit", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "4294967295";
}

def HEX_RDFDump : Option<
    "hexagon-rdf-dump", "",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_RDFTrackReserved : Option<
    "hexagon-rdf-track-reserved", "",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EnableAggressiveRDFCopy : Option<
    "hexagon-aggressive-rdf-copy",
    "Enable aggressive RDF copy propagation with super-register support",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonRegisterInfo.cpp ---

def HEX_FrameIndexSearchRange : Option<
    "hexagon-frame-index-search-range", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "32";
  let Hidden = 1;
}

def HEX_FrameIndexReuseLimit : Option<
    "hexagon-frame-index-reuse-limit", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "4294967295";
  let Hidden = 1;
}

// --- HexagonSplitDouble.cpp ---

def HEX_MaxHSDR : Option<
    "max-hsdr", "",
    "int"> {
  let Registry = HexagonOptsReg;
  let Default = "-1";
  let Hidden = 1;
}

def HEX_MemRefsFixed : Option<
    "hsdr-no-mem", "",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- HexagonSubtarget.cpp ---

def HEX_EnableBSBSched : Option<
    "enable-bsb-sched",
    "Enable BSB scheduling",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EnableTCLatencySched : Option<
    "enable-tc-latency-sched",
    "Enable TC latency scheduling",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EnableDotCurSched : Option<
    "enable-cur-sched",
    "Enable dot cur scheduling",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_OverrideLongCalls : Option<
    "hexagon-long-calls",
    "If set, always generate long calls",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EnablePredicatedCalls : Option<
    "hexagon-pred-calls",
    "Consider calls to be predicable",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_SchedPredsCloser : Option<
    "sched-preds-closer",
    "Schedule predicate definitions closer to uses",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_SchedRetvalOptimization : Option<
    "sched-retval-optimization",
    "Enable retval optimization",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EnableCheckBankConflict : Option<
    "hexagon-check-bank-conflict",
    "Enable bank conflict checking",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- HexagonTargetMachine.cpp ---

def HEX_EnableCExtOpt : Option<
    "hexagon-cext",
    "Enable Hexagon constant-extender optimization",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EnableRDFOpt : Option<
    "rdf-opt",
    "Enable RDF-based optimizations",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EnablePostRAHandleQFP : Option<
    "hexagon-handle-qfloat",
    "Fix up QFloat spills and reloads after register allocation",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_RDFFuncBlockLimit : Option<
    "rdf-bb-limit",
    "Basic block limit for a function for RDF optimizations",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "1000";
  let Hidden = 1;
}

def HEX_DisableHardwareLoops : Option<
    "disable-hexagon-hwloops",
    "Disable Hardware Loops for Hexagon target",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EnableMCR : Option<
    "hexagon-mcr",
    "Enable the machine combiner pass",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_DisableAModeOpt : Option<
    "disable-hexagon-amodeopt",
    "Disable Hexagon Addressing Mode Optimization",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_DisableCFGOpt : Option<
    "disable-hexagon-cfgopt",
    "Disable Hexagon CFG Optimization",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_DisableHCP : Option<
    "disable-hcp",
    "Disable Hexagon constant propagation",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_DisableMask : Option<
    "disable-mask",
    "Disable Hexagon specific Mask generation pass",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_DisableLiveVars : Option<
    "disable-hlv",
    "Disable Hexagon specific post-RA live-variable analysis",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_DisableStoreWidening : Option<
    "disable-store-widen",
    "Disable store widening",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_DisableLoadWidening : Option<
    "disable-load-widen",
    "Disable load widening",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EnableExpandCondsets : Option<
    "hexagon-expand-condsets",
    "Early expansion of MUX",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EnableTfrCleanup : Option<
    "hexagon-tfr-cleanup",
    "Cleanup of TFRs/COPYs",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EnableEarlyIf : Option<
    "hexagon-eif",
    "Enable early if-conversion",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EnableCopyHoist : Option<
    "hexagon-copy-hoist",
    "Enable Hexagon copy hoisting",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EnableGenInsert : Option<
    "hexagon-insert",
    "Generate \"insert\" instructions",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EnableCommGEP : Option<
    "hexagon-commgep",
    "Enable commoning of GEP instructions",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EnableGenExtract : Option<
    "hexagon-extract",
    "Generate \"extract\" instructions",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EnableGenMux : Option<
    "hexagon-mux",
    "Enable converting conditional transfers into MUX instructions",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EnableGenPred : Option<
    "hexagon-gen-pred",
    "Enable conversion of arithmetic operations to predicate instructions",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EnableLoopPrefetch : Option<
    "hexagon-loop-prefetch",
    "Enable loop data prefetch on Hexagon",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_DisableHSDR : Option<
    "disable-hsdr",
    "Disable splitting double registers",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EnableGenMemAbs : Option<
    "hexagon-mem-abs",
    "Generate absolute set instructions",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EnableBitSimplify : Option<
    "hexagon-bit",
    "Bit simplification",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EnableLoopResched : Option<
    "hexagon-loop-resched",
    "Loop rescheduling",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_NoOpt : Option<
    "hexagon-noopt",
    "Disable backend optimizations",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EnableVectorPrint : Option<
    "enable-hexagon-vector-print",
    "Enable Hexagon Vector print instr pass",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EnableVExtractOpt : Option<
    "hexagon-opt-vextract",
    "Enable vextract optimization",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EnableVectorCombine : Option<
    "hexagon-vector-combine",
    "Enable HVX vector combining",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EnableInitialCFGCleanup : Option<
    "hexagon-initial-cfg-cleanup",
    "Simplify the CFG after atomic expansion pass",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_EnableInstSimplify : Option<
    "hexagon-instsimplify",
    "Enable instsimplify",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- HexagonTargetObjectFile.cpp ---

def HEX_SmallDataThreshold : Option<
    "hexagon-small-data-threshold",
    "Maximum size of an object in the sdata section",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def HEX_NoSmallDataSorting : Option<
    "mno-sort-sda", "",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_StaticInSData : Option<
    "hexagon-statics-in-small-data", "",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_TraceGVPlacement : Option<
    "trace-gv-placement",
    "Trace GV placement",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EmitJTInText : Option<
    "hexagon-emit-jt-text", "",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EmitLUTInText : Option<
    "hexagon-emit-lut-text", "",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonTargetTransformInfo.cpp ---

def HEX_AutoHVX : Option<
    "hexagon-autohvx",
    "Enable auto-vectorization for HVX",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_AllowScatterGatherHVX : Option<
    "hexagon-allow-scatter-gather-hvx",
    "Allow gather/scatter intrinsics for Hexagon",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EnableV68FloatAutoHVX : Option<
    "force-hvx-float",
    "Enable auto-vectorization of floating point types on v68.",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EmitLookupTables : Option<
    "hexagon-emit-lookup-tables",
    "Enable emission of lookup tables",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_MaskedVMem : Option<
    "hexagon-masked-vmem",
    "Enable masked vector memory operations",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- HexagonVExtract.cpp ---

def HEX_VExtractThreshold : Option<
    "hexagon-vextract-threshold", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- HexagonVLIWPacketizer.cpp ---

def HEX_DisablePacketizer : Option<
    "disable-packetizer",
    "Disable Hexagon packetizer pass",
    "bool"> {
  let Default = "0";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_Slot1Store : Option<
    "slot1-store-slot0-load",
    "Allow slot1 store slot0 load",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_PacketizeVolatiles : Option<
    "hexagon-packetize-volatiles",
    "Allow packetizing volatile memory accesses",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_DisableVecDblNVStores : Option<
    "disable-vecdbl-nv-stores", "",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonVectorCombine.cpp ---

def HEX_HVCDumpModule : Option<
    "hvc-dump-module", "",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_HVCVAEnabled : Option<
    "hvc-va", "",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_HVCVIEnabled : Option<
    "hvc-vi", "",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_HVCVADoFullStores : Option<
    "hvc-va-full-stores", "",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_HVCVAGroupCountLimit : Option<
    "hvc-va-group-count-limit", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "4294967295";
  let Hidden = 1;
}

def HEX_HVCVAGroupSizeLimit : Option<
    "hvc-va-group-size-limit", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "4294967295";
  let Hidden = 1;
}

// --- HexagonVectorLoopCarriedReuse.cpp ---

def HEX_VLCRIterationLim : Option<
    "hexagon-vlcr-iteration-lim", "",
    "int"> {
  let Registry = HexagonOptsReg;
  let Default = "2";
  let Hidden = 1;
}

// --- HexagonVectorPrint.cpp ---

def HEX_TraceHexVectorStoresOnly : Option<
    "trace-hex-vector-stores-only",
    "Trace hex vector stores only",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- RDFCopy.cpp ---

def HEX_RDFCpLimit : Option<
    "rdf-cp-limit", "",
    "unsigned"> {
  let Default = "0";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- MCTargetDesc/HexagonMCTargetDesc.cpp ---

def HEX_DisableCompound : Option<
    "mno-compound",
    "Disable looking for compound instructions for Hexagon",
    "bool"> {
  let Registry = HexagonOptsReg;
}

def HEX_DisableDuplex : Option<
    "mno-pairing",
    "Disable looking for duplex instructions for Hexagon",
    "bool"> {
  let Registry = HexagonOptsReg;
}

def HEX_MV5 : Option<
    "mv5", "Build for Hexagon V5",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_MV55 : Option<
    "mv55", "Build for Hexagon V55",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_MV60 : Option<
    "mv60", "Build for Hexagon V60",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_MV62 : Option<
    "mv62", "Build for Hexagon V62",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_MV65 : Option<
    "mv65", "Build for Hexagon V65",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_MV66 : Option<
    "mv66", "Build for Hexagon V66",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_MV67 : Option<
    "mv67", "Build for Hexagon V67",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_MV67T : Option<
    "mv67t", "Build for Hexagon V67T",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_MV68 : Option<
    "mv68", "Build for Hexagon V68",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_MV69 : Option<
    "mv69", "Build for Hexagon V69",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_MV71 : Option<
    "mv71", "Build for Hexagon V71",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_MV71T : Option<
    "mv71t", "Build for Hexagon V71T",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_MV73 : Option<
    "mv73", "Build for Hexagon V73",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_MV75 : Option<
    "mv75", "Build for Hexagon V75",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_MV79 : Option<
    "mv79", "Build for Hexagon V79",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_MV81 : Option<
    "mv81", "Build for Hexagon V81",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexArchEnum for HEX_MHVX ---

def HexArchNoArch : EnumValue<"none", ""> {
  let CppName = "NoArch";
  let Value = 0;
}

def HexArchGeneric : EnumValue<"", ""> {
  let CppName = "Generic";
  let Value = 1;
}

def HexArchV5 : EnumValue<"v5", ""> {
  let CppName = "V5";
  let Value = 2;
}

def HexArchV55 : EnumValue<"v55", ""> {
  let CppName = "V55";
  let Value = 3;
}

def HexArchV60 : EnumValue<"v60", "Build for HVX v60"> {
  let CppName = "V60";
  let Value = 4;
}

def HexArchV62 : EnumValue<"v62", "Build for HVX v62"> {
  let CppName = "V62";
  let Value = 5;
}

def HexArchV65 : EnumValue<"v65", "Build for HVX v65"> {
  let CppName = "V65";
  let Value = 6;
}

def HexArchV66 : EnumValue<"v66", "Build for HVX v66"> {
  let CppName = "V66";
  let Value = 7;
}

def HexArchV67 : EnumValue<"v67", "Build for HVX v67"> {
  let CppName = "V67";
  let Value = 8;
}

def HexArchV68 : EnumValue<"v68", "Build for HVX v68"> {
  let CppName = "V68";
  let Value = 9;
}

def HexArchV69 : EnumValue<"v69", "Build for HVX v69"> {
  let CppName = "V69";
  let Value = 10;
}

def HexArchV71 : EnumValue<"v71", "Build for HVX v71"> {
  let CppName = "V71";
  let Value = 11;
}

def HexArchV73 : EnumValue<"v73", "Build for HVX v73"> {
  let CppName = "V73";
  let Value = 12;
}

def HexArchV75 : EnumValue<"v75", "Build for HVX v75"> {
  let CppName = "V75";
  let Value = 13;
}

def HexArchV79 : EnumValue<"v79", "Build for HVX v79"> {
  let CppName = "V79";
  let Value = 14;
}

def HexArchV81 : EnumValue<"v81", "Build for HVX v81"> {
  let CppName = "V81";
  let Value = 15;
}

def HexArchEnum : EnumType<"HexArchEnum",
    [HexArchNoArch, HexArchGeneric, HexArchV5, HexArchV55,
     HexArchV60, HexArchV62, HexArchV65, HexArchV66,
     HexArchV67, HexArchV68, HexArchV69, HexArchV71,
     HexArchV73, HexArchV75, HexArchV79, HexArchV81]>;

def HEX_MHVX : Option<
    "mhvx", "Enable Hexagon Vector eXtensions",
    "HexArchEnum"> {
  let Registry = HexagonOptsReg;
  let Default = "none";
  let ValueOptional = 1;
}

def HEX_EnableHVXIEEEFP : Option<
    "mhvx-ieee-fp",
    "Enable HVX IEEE floating point",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EnableHVXQFloat : Option<
    "mhvx-qfloat",
    "Enable HVX QFloat",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EnableCabac : Option<
    "mcabac",
    "tbd",
    "bool"> {
  let Registry = HexagonOptsReg;
}

// --- MCTargetDesc/HexagonMCChecker.cpp ---

def HEX_RelaxNVChecks : Option<
    "relax-nv-checks", "",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- MCTargetDesc/HexagonMCELFStreamer.cpp ---

def HEX_GPSize : Option<
    "gpsize",
    "Global Pointer Addressing Size.  The default size is 8.",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "8";
  let Prefix = 1;
}

// --- MCTargetDesc/HexagonMCShuffler.cpp ---

def HEX_DisableShuffle : Option<
    "disable-hexagon-shuffle",
    "Disable Hexagon instruction shuffling",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- MCTargetDesc/HexagonAsmBackend.cpp ---

def HEX_DisableFixup : Option<
    "mno-fixup",
    "Disable fixing up resolved relocations for Hexagon",
    "bool"> {
  let Registry = HexagonOptsReg;
}

// --- AsmParser/HexagonAsmParser.cpp ---

def HEX_WarnMissingParenthesis : Option<
    "mwarn-missing-parenthesis",
    "Warn for missing parenthesis around predicate registers",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
}

def HEX_ErrorMissingParenthesis : Option<
    "merror-missing-parenthesis",
    "Error for missing parenthesis around predicate registers",
    "bool"> {
  let Registry = HexagonOptsReg;
}

def HEX_WarnSignedMismatch : Option<
    "mwarn-sign-mismatch",
    "Warn for mismatching a signed and unsigned value",
    "bool"> {
  let Registry = HexagonOptsReg;
}

def HEX_WarnNoncontigiousRegister : Option<
    "mwarn-noncontigious-register",
    "Warn for register names that aren't contigious",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
}

def HEX_ErrorNoncontigiousRegister : Option<
    "merror-noncontigious-register",
    "Error for register names that aren't contigious",
    "bool"> {
  let Registry = HexagonOptsReg;
}

def HEX_AddBuildAttributes : Option<
    "hexagon-add-build-attributes", "",
    "bool"> {
  let Registry = HexagonOptsReg;
}

// --- HexagonSubtarget.cpp (additional) ---

def HEX_DisableHexagonMISched : Option<
    "disable-hexagon-misched",
    "Disable Hexagon MI Scheduling",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonPeephole.cpp (additional) ---

def HEX_DisableHexagonPeephole : Option<
    "disable-hexagon-peephole",
    "Disable Peephole Optimization",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonVLIWPacketizer.cpp (additional) ---

def HEX_EnableGenAllInsnClass : Option<
    "enable-gen-insn",
    "Generate all instruction with TC",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonSplitDouble.cpp (additional) ---

def HEX_SplitAll : Option<
    "hsdr-split-all",
    "Split all partitions",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonLoopAlign.cpp (additional) ---

def HEX_LoopEdgeThreshold : Option<
    "hexagon-loop-edge-threshold",
    "Set hexagon loop align edge threshold",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "7500";
  let Hidden = 1;
}

// --- HexagonGlobalScheduler.cpp (additional) ---

def HEX_PerformDualJumps : Option<
    "dual-jump-in-pull-up",
    "Perform dual jump formation during pull up",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_AllowBBPeelPullUp : Option<
    "enable-bb-peel-pull-up",
    "Peel a reg copy out of a BBloop",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_DisableCheckBundles : Option<
    "disable-hexagon-check-bundles",
    "Disable Hexagon check bundles pass",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_WarnOnBundleSize : Option<
    "warn-on-bundle-size",
    "Hexagon check bundles and warn on size",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_ForceNoopHazards : Option<
    "force-noop-hazards",
    "Force noop hazards in scheduler",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonInstrInfo.cpp (additional) ---

def HEX_UseDFAHazardRec : Option<
    "dfa-hazard-rec",
    "Use the DFA based hazard recognizer.",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- HexagonISelLowering.cpp (additional) ---

def HEX_ConstantLoadsToImm : Option<
    "constant-loads-to-imm",
    "Convert constant loads to immediate values.",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def HEX_DisableArgsMinAlignment : Option<
    "hexagon-disable-args-min-alignment",
    "Disable minimum alignment of 1 for "
    "arguments passed by value on stack",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonISelLoweringHVX.cpp (additional) ---

def HEX_EnableFpFastConvert : Option<
    "hexagon-fp-fast-convert",
    "Enable FP fast conversion routine.",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonFrameLowering.cpp (additional) ---

def HEX_SpillOptMax : Option<
    "spill-opt-max", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "4294967295";
  let Hidden = 1;
}

// --- HexagonGenInsert.cpp (additional) ---

def HEX_VRegIndexCutoff : Option<
    "insert-vreg-cutoff",
    "Vreg# cutoff for insert generation.",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "4294967295";
  let Hidden = 1;
}

def HEX_MaxORLSize : Option<
    "insert-max-orl",
    "Maximum size of OrderedRegisterList",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "4096";
  let Hidden = 1;
}

def HEX_OptTimingDetail : Option<
    "insert-timing-detail",
    "Enable detailed timing of insert generation",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonVectorCombine.cpp (additional) ---

def HEX_MinLoadGroupSizeForAlignment : Option<
    "hvc-ld-min-group-size-for-alignment", "",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "4";
  let Hidden = 1;
}

// --- MCTargetDesc/HexagonMCTargetDesc.cpp (additional) ---

def HEX_DisableHVX : Option<
    "mno-hvx",
    "Disable Hexagon Vector eXtensions",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

// --- HexagonHVXSaveRemark.cpp ---

def HEX_HVXSaveThreshold : Option<
    "hexagon-hvx-save-threshold",
    "Minimum number of bytes of HVX caller-saved register data live "
    "across a call to trigger a remark (default: 8 x 128-byte vectors)",
    "unsigned"> {
  let Registry = HexagonOptsReg;
  let Default = "1024";
  let Hidden = 1;
}

// --- HexagonXQFloatGenerator.cpp ---

def HEX_EnableHVXXQFloat : Option<
    "enable-xqf-gen",
    "Enable HVX XQFloat Generator",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
}

def HEX_EnableConversionsRemoval : Option<
    "enable-rem-conv",
    "Enable extraneous conversions removal",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
}

def HEX_PrintDebugXQF : Option<
    "debug-print",
    "Print debug",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
}

// --- HexagonPostRAHandleQFP.cpp ---

def HEX_DisablePostRAHandleQFloat : Option<
    "disable-handle-qfp",
    "Disable handling of Qfloat spills/refills after register allocation.",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
  let Hidden = 1;
}

def HEX_EnablePostRAXqfCompliance : Option<
    "enable-postra-xqf-check",
    "Enable ABI compliance for xqf operands post regalloc.",
    "bool"> {
  let Default = "false";
  let Registry = HexagonOptsReg;
}

// --- HexagonTargetMachine.cpp (QFloatMode enum) ---

def QFloatStrictIEEE : EnumValue<"strict-ieee",
    "Enable code generation for qfloat strict IEEE-754 mode"> {
  let CppName = "StrictIEEE";
  let Value = 0;
}

def QFloatIEEE : EnumValue<"ieee",
    "Enable code generation for qfloat IEEE-754 mode"> {
  let CppName = "IEEE";
  let Value = 1;
}

def QFloatLossy : EnumValue<"lossy",
    "Enable code generation for qfloat lossy-subnormals mode"> {
  let CppName = "Lossy";
  let Value = 2;
}

def QFloatLegacy : EnumValue<"legacy",
    "Enable code generation for qfloat legacy mode"> {
  let CppName = "Legacy";
  let Value = 3;
}

def QFloatMode : EnumType<"QFloatMode",
    [QFloatStrictIEEE, QFloatIEEE, QFloatLossy, QFloatLegacy]>;

def HEX_QFloatMode : Option<
    "hexagon-qfloat-mode",
    "Specify the qfloat mode to operate on.",
    "QFloatMode"> {
  let Registry = HexagonOptsReg;
  let Default = "legacy";
  let Hidden = 1;
}

// --- HexagonAlignGlobalArrays.cpp ---

def HEX_DisableGlobalArrayAlignment : Option<
    "hexagon-disable-global-array-align",
    "Disable aligning global integer arrays to an 8-byte boundary",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def HEX_DisableAlignOptByteHalf : Option<
    "hexagon-disable-align-opt-byte-half",
    "Disable keeping byte and half-word arrays at their natural alignment "
    "when reducing .rodata size",
    "bool"> {
  let Registry = HexagonOptsReg;
  let Default = "false";
  let Hidden = 1;
}
