//===- HexagonOptions.td - Option defs for Hexagon -------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def HexagonOptions : OptionsStruct<"llvm"> {}

// =========================================================================
// Enum types
// =========================================================================

// --- MCTargetDesc/HexagonMCTargetDesc.cpp ---

// Self-contained (not External): the real Hexagon::ArchEnum lives in the
// private lib/Target/Hexagon/HexagonDepArch.h header, unreachable from the
// driver tools that include HexagonOptions.h. HexagonMCTargetDesc.cpp's
// toArchEnum() maps each enumerator by name to Hexagon::ArchEnum. Only the
// spellings upstream's -mhvx cl::opt accepted are listed; Generic's empty
// spelling is what "-mhvx=" (explicitly empty value) parses to.
def HexHVXArchV60 : EnumMember<"V60", "v60">;
def HexHVXArchV62 : EnumMember<"V62", "v62">;
def HexHVXArchV65 : EnumMember<"V65", "v65">;
def HexHVXArchV66 : EnumMember<"V66", "v66">;
def HexHVXArchV67 : EnumMember<"V67", "v67">;
def HexHVXArchV68 : EnumMember<"V68", "v68">;
def HexHVXArchV69 : EnumMember<"V69", "v69">;
def HexHVXArchV71 : EnumMember<"V71", "v71">;
def HexHVXArchV73 : EnumMember<"V73", "v73">;
def HexHVXArchV75 : EnumMember<"V75", "v75">;
def HexHVXArchV79 : EnumMember<"V79", "v79">;
def HexHVXArchV81 : EnumMember<"V81", "v81">;
def HexHVXArchGeneric : EnumMember<"Generic", "">;

def HexHVXArchEnum : OptionEnum<"HexHVXArch", [
    HexHVXArchV60, HexHVXArchV62, HexHVXArchV65, HexHVXArchV66,
    HexHVXArchV67, HexHVXArchV68, HexHVXArchV69, HexHVXArchV71,
    HexHVXArchV73, HexHVXArchV75, HexHVXArchV79, HexHVXArchV81,
    HexHVXArchGeneric]>;

// --- HexagonTargetMachine.cpp ---

// Generated as llvm::QFloatMode, replacing the identical
// `enum class QFloatMode` HexagonTargetMachine.h used to declare.
def QFloatModeStrictIEEE : EnumMember<"StrictIEEE", "strict-ieee">;
def QFloatModeIEEE : EnumMember<"IEEE", "ieee">;
def QFloatModeLossy : EnumMember<"Lossy", "lossy">;
def QFloatModeLegacy : EnumMember<"Legacy", "legacy">;

def QFloatModeEnum : OptionEnum<"QFloatMode", [
    QFloatModeStrictIEEE, QFloatModeIEEE, QFloatModeLossy,
    QFloatModeLegacy]>;

// =========================================================================
// Options
// =========================================================================

// --- HexagonBitSimplify.cpp ---

defm HEX_PreserveTiedOps : BoolField<
    "hexbit-keep-tied", "true",
    "Preserve subregisters in tied operands",
    /*hidden=*/1>;

defm HEX_GenExtractBit : BoolField<
    "hexbit-extract", "true",
    "Generate extract instructions",
    /*hidden=*/1>;

defm HEX_GenBitSplit : BoolField<
    "hexbit-bitsplit", "true",
    "Generate bitsplit instructions",
    /*hidden=*/1>;

defm HEX_MaxExtract : OptionalField<
    "hexbit-max-extract", "unsigned",
    "",
    /*hidden=*/1>;

defm HEX_MaxBitSplit : OptionalField<
    "hexbit-max-bitsplit", "unsigned",
    "",
    /*hidden=*/1>;

defm HEX_RegisterSetLimit : ValueField<
    "hexbit-registerset-limit", "unsigned", "1000",
    "",
    /*hidden=*/1>;

// --- HexagonBranchRelaxation.cpp ---

defm HEX_BranchRelaxSafetyBuffer : ValueField<
    "branch-relax-safety-buffer", "unsigned", "200",
    "safety buffer size",
    /*hidden=*/1>;

// --- HexagonCommonGEP.cpp ---

defm HEX_OptSpeculate : BoolField<
    "commgep-speculate", "true",
    "",
    /*hidden=*/1>;

defm HEX_OptEnableInv : BoolField<
    "commgep-inv", "true",
    "",
    /*hidden=*/1>;

defm HEX_OptEnableConst : BoolField<
    "commgep-const", "true",
    "",
    /*hidden=*/1>;

// --- HexagonConstExtenders.cpp ---

defm HEX_CExtThreshold : ValueField<
    "hexagon-cext-threshold", "unsigned", "3",
    "Minimum number of extenders to trigger replacement",
    /*hidden=*/1>;

defm HEX_CExtLimit : OptionalField<
    "hexagon-cext-limit", "unsigned",
    "Maximum number of replacements",
    /*hidden=*/1>;

// --- HexagonCopyHoisting.cpp ---

defm HEX_CPHoistFn : ValueField<
    "cphoistfn", "std::string", "\"\"",
    "",
    /*hidden=*/1>;

// --- HexagonCopyToCombine.cpp ---

defm HEX_DisableMergeIntoCombines : BoolField<
    "disable-merge-into-combines", "false",
    "Disable merging into combines",
    /*hidden=*/1>;

defm HEX_DisableConst64 : BoolField<
    "disable-const64", "false",
    "Disable generation of const64",
    /*hidden=*/1>;

defm HEX_MaxNumInstBetweenNewValueStoreAndTFR : ValueField<
    "max-num-inst-between-tfr-and-nv-store", "unsigned", "4",
    "Maximum distance between a tfr feeding a store we "
    "consider the store still to be newifiable",
    /*hidden=*/1>;

// --- HexagonEarlyIfConv.cpp ---

defm HEX_EnableHexagonBP : BoolField<
    "enable-hexagon-br-prob", "true",
    "",
    /*hidden=*/1>;

defm HEX_EIFSizeLimit : ValueField<
    "eif-limit", "unsigned", "6",
    "",
    /*hidden=*/1>;

defm HEX_SkipExitBranches : BoolField<
    "eif-no-loop-exit", "false",
    "",
    /*hidden=*/1>;

// --- HexagonExpandCondsets.cpp ---

defm HEX_ExpandCondsetsTfrLimit : OptionalField<
    "expand-condsets-tfr-limit", "unsigned",
    "",
    /*hidden=*/1>;

defm HEX_ExpandCondsetsCoaLimit : OptionalField<
    "expand-condsets-coa-limit", "unsigned",
    "",
    /*hidden=*/1>;

// --- HexagonFixupHwLoops.cpp ---

defm HEX_MaxLoopRange : ValueField<
    "hexagon-loop-range", "unsigned", "200",
    "",
    /*hidden=*/1>;

// --- HexagonFrameLowering.cpp ---

defm HEX_DisableDeallocRet : BoolField<
    "disable-hexagon-dealloc-ret", "false",
    "Disable Dealloc Return for Hexagon target",
    /*hidden=*/1>;

defm HEX_NumberScavengerSlots : ValueField<
    "number-scavenger-slots", "unsigned", "2",
    "Set the number of scavenger slots",
    /*hidden=*/1>;

defm HEX_SpillFuncThreshold : ValueField<
    "spill-func-threshold", "int", "6",
    "Specify O2(not Os) spill func threshold",
    /*hidden=*/1>;

defm HEX_SpillFuncThresholdOs : ValueField<
    "spill-func-threshold-Os", "int", "1",
    "Specify Os spill func threshold",
    /*hidden=*/1>;

defm HEX_EnableStackOVFSanitizer : BoolField<
    "enable-stackovf-sanitizer", "false",
    "Enable runtime checks for stack overflow.",
    /*hidden=*/1>;

defm HEX_EnableShrinkWrapping : BoolField<
    "hexagon-shrink-frame", "true",
    "Enable stack frame shrink wrapping",
    /*hidden=*/1>;

defm HEX_ShrinkLimit : OptionalField<
    "shrink-frame-limit", "unsigned",
    "Max count of stack frame shrink-wraps",
    /*hidden=*/1>;

defm HEX_EnableSaveRestoreLong : BoolField<
    "enable-save-restore-long", "false",
    "Enable long calls for save-restore stubs.",
    /*hidden=*/1>;

defm HEX_EliminateFramePointer : BoolField<
    "hexagon-fp-elim", "true",
    "Refrain from using FP whenever possible",
    /*hidden=*/1>;

defm HEX_OptimizeSpillSlots : BoolField<
    "hexagon-opt-spill", "true",
    "Optimize spill slots",
    /*hidden=*/1>;

// --- HexagonGenExtract.cpp ---

defm HEX_ExtractCutoff : OptionalField<
    "extract-cutoff", "unsigned",
    "",
    /*hidden=*/1>;

defm HEX_NoSR0 : BoolField<
    "extract-nosr0", "true",
    "",
    /*hidden=*/1>;

// --- HexagonGenInsert.cpp ---

defm HEX_VRegDistCutoff : ValueField<
    "insert-dist-cutoff", "unsigned", "30",
    "",
    /*hidden=*/1>;

defm HEX_MaxIFMSize : ValueField<
    "insert-max-ifmap", "unsigned", "1024",
    "",
    /*hidden=*/1>;

defm HEX_OptTiming : BoolField<
    "insert-timing", "false",
    "",
    /*hidden=*/1>;

defm HEX_OptSelectAll0 : BoolField<
    "insert-all0", "false",
    "",
    /*hidden=*/1>;

defm HEX_OptSelectHas0 : BoolField<
    "insert-has0", "false",
    "",
    /*hidden=*/1>;

defm HEX_OptInsertConst : BoolField<
    "insert-const", "false",
    "",
    /*hidden=*/1>;

// --- HexagonGenMux.cpp ---

defm HEX_MinPredDist : ValueField<
    "hexagon-gen-mux-threshold", "unsigned", "0",
    "Minimum distance between predicate definition and furthest use",
    /*hidden=*/1>;

// --- HexagonGlobalScheduler.cpp ---

defm HEX_DisablePullUp : OptionalValueField<
    "disable-pull-up", "bool", "true",
    "Disable the pull up of instructions optimization",
    /*hidden=*/1>;

defm HEX_EnableSpeculativePullUp : BoolField<
    "enable-speculative-pull-up", "false",
    "Enable speculative pull up scheduling",
    /*hidden=*/1>;

defm HEX_EnableLocalPullUp : BoolField<
    "enable-local-pull-up", "true",
    "Enable local scheduling of pull up candidates",
    /*hidden=*/1>;

defm HEX_AllowSpeculateLoads : BoolField<
    "speculate-loads-on-pull-up", "true",
    "Allow speculative scheduling of loads",
    /*hidden=*/1>;

defm HEX_AllowCmpBranchLoads : BoolField<
    "cmp-branch-loads-pull-up", "true",
    "Allow speculative scheduling of loads past cmp/branch",
    /*hidden=*/1>;

defm HEX_AllowUnlikelyPath : BoolField<
    "unlikely-path-pull-up", "true",
    "Allow pull up of unlikely path instructions",
    /*hidden=*/1>;

defm HEX_AllowDependentPullUp : BoolField<
    "enable-dependent-pull-up", "true",
    "Allow dependent pull up",
    /*hidden=*/1>;

defm HEX_PreventCompoundSeparation : OptionalValueField<
    "prevent-compound-separation", "bool", "true",
    "Prevent separation of compound instructions",
    /*hidden=*/1>;

defm HEX_PreventDuplexSeparation : BoolField<
    "prevent-duplex-separation", "true",
    "Prevent separation of duplex instructions",
    /*hidden=*/1>;

defm HEX_MainCandidateQueueSize : ValueField<
    "pull-up-main-queue-size", "unsigned", "8",
    "",
    /*hidden=*/1>;

defm HEX_SecondaryCandidateQueueSize : ValueField<
    "pull-up-sec-queue-size", "unsigned", "2",
    "",
    /*hidden=*/1>;

defm HEX_PostPullUpOpt : BoolField<
    "post-pull-up-opt", "true",
    "Enable post pull up optimization pass",
    /*hidden=*/1>;

defm HEX_SpeculateNonPredInsn : BoolField<
    "speculate-non-pred-insn", "true",
    "Allow speculate non-predicated instructions",
    /*hidden=*/1>;

defm HEX_OneFloatPerPacket : BoolField<
    "single-float-packet", "false",
    "Only allow one float instruction per packet",
    /*hidden=*/1>;

defm HEX_OneComplexPerPacket : BoolField<
    "single-complex-packet", "false",
    "Only allow one complex instruction per packet",
    /*hidden=*/1>;

// --- HexagonHardwareLoops.cpp ---

defm HEX_HWLoopLimit : ValueField<
    "hexagon-max-hwloop", "int", "-1",
    "",
    /*hidden=*/1>;

defm HEX_PHFn : ValueField<
    "hexagon-hwloop-phfn", "std::string", "\"\"",
    "",
    /*hidden=*/1>;

defm HEX_HWCreatePreheader : BoolField<
    "hexagon-hwloop-preheader", "true",
    "Automatically create preheader for hardware loops",
    /*hidden=*/1>;

defm HEX_SpecPreheader : BoolField<
    "hwloop-spec-preheader", "false",
    "Allow speculative preheader",
    /*hidden=*/1>;

// --- HexagonInstrInfo.cpp ---

defm HEX_ScheduleInlineAsm : BoolField<
    "hexagon-sched-inline-asm", "false",
    "Schedule inline assembly",
    /*hidden=*/1>;

defm HEX_EnableBranchPrediction : BoolField<
    "hexagon-enable-branch-prediction", "true",
    "Enable branch prediction",
    /*hidden=*/1>;

defm HEX_DisableNVSchedule : BoolField<
    "disable-hexagon-nv-schedule", "false",
    "Disable schedule adjustment for new value stores",
    /*hidden=*/1>;

defm HEX_EnableTimingClassLatency : BoolField<
    "enable-timing-class-latency", "false",
    "Enable timing class latency",
    /*hidden=*/1>;

defm HEX_EnableALUForwarding : BoolField<
    "enable-alu-forwarding", "true",
    "Enable ALU forwarding",
    /*hidden=*/1>;

defm HEX_EnableACCForwarding : BoolField<
    "enable-acc-forwarding", "true",
    "Enable ACC forwarding",
    /*hidden=*/1>;

defm HEX_BranchRelaxAsmLarge : BoolField<
    "branch-relax-asm-large", "true",
    "Branch relaxation for asm large",
    /*hidden=*/1>;

// --- HexagonISelDAGToDAG.cpp ---

defm HEX_EnableAddressRebalancing : BoolField<
    "isel-rebalance-addr", "true",
    "",
    /*hidden=*/1>;

defm HEX_RebalanceOnlyForOptimizations : BoolField<
    "rebalance-only-opt", "false",
    "",
    /*hidden=*/1>;

defm HEX_RebalanceOnlyImbalanced : BoolField<
    "rebalance-only-imbal", "false",
    "",
    /*hidden=*/1>;

defm HEX_CheckSingleUse : BoolField<
    "hexagon-isel-su", "true",
    "Check single use",
    /*hidden=*/1>;

// --- HexagonISelLowering.cpp ---

defm HEX_EmitJumpTables : BoolField<
    "hexagon-emit-jump-tables", "true",
    "Control whether jump tables are emitted or not",
    /*hidden=*/1>;

defm HEX_MinimumJumpTables : ValueField<
    "minimum-jump-tables", "int", "5",
    "Set minimum jump tables",
    /*hidden=*/1>;

defm HEX_EnableHexSDNodeSched : BoolField<
    "enable-hexagon-sdnode-sched", "false",
    "",
    /*hidden=*/1>;

defm HEX_AlignLoads : BoolField<
    "hexagon-align-loads", "false",
    "Rewrite unaligned loads as a pair of aligned loads",
    /*hidden=*/1>;

// --- HexagonISelLoweringHVX.cpp ---

defm HEX_HvxWidenThreshold : OptionalField<
    "hexagon-hvx-widen", "unsigned",
    "Lower threshold (in bytes) for widening to HVX vectors",
    /*hidden=*/1>;

// --- HexagonLoadStoreWidening.cpp ---

defm HEX_MaxMBBSizeForLoadStoreWidening : ValueField<
    "max-bb-size-for-load-store-widening", "unsigned", "1000",
    "",
    /*hidden=*/1>;

// --- HexagonLoopAlign.cpp ---

defm HEX_DisableLoopAlign : BoolField<
    "disable-hexagon-loop-align", "false",
    "Disable Hexagon loop alignment",
    /*hidden=*/1>;

defm HEX_HVXLoopAlignLimitUB : ValueField<
    "hexagon-hvx-loop-align-limit-ub", "unsigned", "16",
    "Upper bound for HVX loop alignment",
    /*hidden=*/1>;

defm HEX_TinyLoopAlignLimitUB : ValueField<
    "hexagon-tiny-loop-align-limit-ub", "unsigned", "16",
    "Upper bound for tiny loop alignment",
    /*hidden=*/1>;

defm HEX_LoopAlignLimitUB : ValueField<
    "hexagon-loop-align-limit-ub", "unsigned", "8",
    "Upper bound for loop alignment",
    /*hidden=*/1>;

defm HEX_LoopAlignLimitLB : ValueField<
    "hexagon-loop-align-limit-lb", "unsigned", "4",
    "Lower bound for loop alignment",
    /*hidden=*/1>;

defm HEX_LoopBndlAlignLimit : ValueField<
    "hexagon-loop-bundle-align-limit", "unsigned", "4",
    "Bundle alignment limit for loop alignment",
    /*hidden=*/1>;

defm HEX_TinyLoopBndlAlignLimit : ValueField<
    "hexagon-tiny-loop-bundle-align-limit", "unsigned", "8",
    "Bundle alignment limit for tiny loop alignment",
    /*hidden=*/1>;

// --- HexagonLoopIdiomRecognition.cpp ---

defm HEX_DisableMemcpyIdiom : BoolField<
    "disable-memcpy-idiom", "false",
    "Disable memcpy idiom recognition",
    /*hidden=*/1>;

defm HEX_DisableMemmoveIdiom : BoolField<
    "disable-memmove-idiom", "false",
    "Disable memmove idiom recognition",
    /*hidden=*/1>;

defm HEX_RuntimeMemSizeThreshold : ValueField<
    "runtime-mem-idiom-threshold", "unsigned", "0",
    "Runtime memory idiom threshold",
    /*hidden=*/1>;

defm HEX_CompileTimeMemSizeThreshold : ValueField<
    "compile-time-mem-idiom-threshold", "unsigned", "64",
    "Compile time memory idiom threshold",
    /*hidden=*/1>;

defm HEX_OnlyNonNestedMemmove : BoolField<
    "only-nonnested-memmove-idiom", "true",
    "Only non-nested memmove idiom",
    /*hidden=*/1>;

defm HEX_VolatileMemcpy : BoolField<
    "disable-hexagon-volatile-memcpy", "false",
    "Hexagon volatile memcpy",
    /*hidden=*/1>;

defm HEX_HLIRSimplifyLimit : ValueField<
    "hlir-simplify-limit", "unsigned", "10000",
    "",
    /*hidden=*/1>;

// --- HexagonNewValueJump.cpp ---

defm HEX_DbgNVJCount : ValueField<
    "nvj-count", "int", "-1",
    "",
    /*hidden=*/1>;

defm HEX_DisableNewValueJumps : BoolField<
    "disable-nvjump", "false",
    "Disable new value jumps",
    /*hidden=*/1>;

// --- HexagonOptAddrMode.cpp ---

defm HEX_CodeGrowthLimit : ValueField<
    "hexagon-amode-growth-limit", "int", "0",
    "Address mode growth limit",
    /*hidden=*/1>;

// --- HexagonPeephole.cpp ---

defm HEX_DisableOptSZExt : BoolField<
    "disable-hexagon-optszext", "true",
    "Disable Hexagon opt sign/zero extend",
    /*hidden=*/1>;

defm HEX_DisablePNotP : BoolField<
    "disable-hexagon-pnotp", "false",
    "Disable Hexagon PNOT P",
    /*hidden=*/1>;

defm HEX_DisableOptExtTo64 : BoolField<
    "disable-hexagon-opt-ext-to-64", "true",
    "Disable Hexagon opt ext to 64",
    /*hidden=*/1>;

// --- HexagonQFPOptimizer.cpp ---

defm HEX_DisableQFPOptimizer : BoolField<
    "disable-qfp-opt", "false",
    "Disable optimization of Qfloat operations.",
    /*hidden=*/0>;

defm HEX_DisableQFOptForMul : BoolField<
    "disable-qfp-opt-mul", "true",
    "Disable optimization of Qfloat operations for multiply.",
    /*hidden=*/0>;

// --- HexagonRDFOpt.cpp ---

defm HEX_RDFLimit : OptionalField<
    "hexagon-rdf-limit", "unsigned",
    "",
    /*hidden=*/0>;

defm HEX_RDFDump : BoolField<
    "hexagon-rdf-dump", "false",
    "",
    /*hidden=*/1>;

defm HEX_RDFTrackReserved : BoolField<
    "hexagon-rdf-track-reserved", "false",
    "",
    /*hidden=*/1>;

defm HEX_EnableAggressiveRDFCopy : BoolField<
    "hexagon-aggressive-rdf-copy", "false",
    "Enable aggressive RDF copy propagation with super-register support",
    /*hidden=*/1>;

// --- HexagonRegisterInfo.cpp ---

defm HEX_FrameIndexSearchRange : ValueField<
    "hexagon-frame-index-search-range", "unsigned", "32",
    "",
    /*hidden=*/1>;

defm HEX_FrameIndexReuseLimit : ValueField<
    "hexagon-frame-index-reuse-limit", "unsigned", "4294967295",
    "",
    /*hidden=*/1>;

// --- HexagonSplitDouble.cpp ---

defm HEX_MaxHSDR : ValueField<
    "max-hsdr", "int", "-1",
    "",
    /*hidden=*/1>;

defm HEX_MemRefsFixed : BoolField<
    "hsdr-no-mem", "true",
    "",
    /*hidden=*/1>;

// --- HexagonSubtarget.cpp ---

defm HEX_EnableBSBSched : OptionalValueField<
    "enable-bsb-sched", "bool", "true",
    "Enable BSB scheduling",
    /*hidden=*/1>;

defm HEX_EnableTCLatencySched : BoolField<
    "enable-tc-latency-sched", "false",
    "Enable TC latency scheduling",
    /*hidden=*/1>;

defm HEX_EnableDotCurSched : BoolField<
    "enable-cur-sched", "true",
    "Enable dot cur scheduling",
    /*hidden=*/1>;

defm HEX_OverrideLongCalls : OptionalValueField<
    "hexagon-long-calls", "bool", "true",
    "If set, always generate long calls",
    /*hidden=*/1>;

defm HEX_EnablePredicatedCalls : BoolField<
    "hexagon-pred-calls", "false",
    "Consider calls to be predicable",
    /*hidden=*/1>;

defm HEX_SchedPredsCloser : BoolField<
    "sched-preds-closer", "true",
    "Schedule predicate definitions closer to uses",
    /*hidden=*/1>;

defm HEX_SchedRetvalOptimization : BoolField<
    "sched-retval-optimization", "true",
    "Enable retval optimization",
    /*hidden=*/1>;

defm HEX_EnableCheckBankConflict : BoolField<
    "hexagon-check-bank-conflict", "true",
    "Enable bank conflict checking",
    /*hidden=*/1>;

// --- HexagonTargetMachine.cpp ---

defm HEX_EnableCExtOpt : BoolField<
    "hexagon-cext", "true",
    "Enable Hexagon constant-extender optimization",
    /*hidden=*/1>;

defm HEX_EnableRDFOpt : BoolField<
    "rdf-opt", "true",
    "Enable RDF-based optimizations",
    /*hidden=*/1>;

defm HEX_EnablePostRAHandleQFP : BoolField<
    "hexagon-handle-qfloat", "true",
    "Fix up QFloat spills and reloads after register allocation",
    /*hidden=*/1>;

defm HEX_RDFFuncBlockLimit : ValueField<
    "rdf-bb-limit", "unsigned", "1000",
    "Basic block limit for a function for RDF optimizations",
    /*hidden=*/1>;

defm HEX_DisableHardwareLoops : BoolField<
    "disable-hexagon-hwloops", "false",
    "Disable Hardware Loops for Hexagon target",
    /*hidden=*/1>;

defm HEX_EnableMCR : BoolField<
    "hexagon-mcr", "true",
    "Enable the machine combiner pass",
    /*hidden=*/1>;

defm HEX_DisableAModeOpt : BoolField<
    "disable-hexagon-amodeopt", "false",
    "Disable Hexagon Addressing Mode Optimization",
    /*hidden=*/1>;

defm HEX_DisableCFGOpt : BoolField<
    "disable-hexagon-cfgopt", "false",
    "Disable Hexagon CFG Optimization",
    /*hidden=*/1>;

defm HEX_DisableHCP : BoolField<
    "disable-hcp", "false",
    "Disable Hexagon constant propagation",
    /*hidden=*/1>;

defm HEX_DisableMask : BoolField<
    "disable-mask", "false",
    "Disable Hexagon specific Mask generation pass",
    /*hidden=*/1>;

defm HEX_DisableLiveVars : BoolField<
    "disable-hlv", "false",
    "Disable Hexagon specific post-RA live-variable analysis",
    /*hidden=*/1>;

defm HEX_DisableStoreWidening : BoolField<
    "disable-store-widen", "false",
    "Disable store widening",
    /*hidden=*/1>;

defm HEX_DisableLoadWidening : BoolField<
    "disable-load-widen", "false",
    "Disable load widening",
    /*hidden=*/1>;

defm HEX_EnableExpandCondsets : BoolField<
    "hexagon-expand-condsets", "true",
    "Early expansion of MUX",
    /*hidden=*/1>;

defm HEX_EnableTfrCleanup : BoolField<
    "hexagon-tfr-cleanup", "true",
    "Cleanup of TFRs/COPYs",
    /*hidden=*/1>;

defm HEX_EnableEarlyIf : BoolField<
    "hexagon-eif", "true",
    "Enable early if-conversion",
    /*hidden=*/1>;

defm HEX_EnableCopyHoist : BoolField<
    "hexagon-copy-hoist", "true",
    "Enable Hexagon copy hoisting",
    /*hidden=*/1>;

defm HEX_EnableGenInsert : BoolField<
    "hexagon-insert", "true",
    "Generate \"insert\" instructions",
    /*hidden=*/1>;

defm HEX_EnableCommGEP : BoolField<
    "hexagon-commgep", "true",
    "Enable commoning of GEP instructions",
    /*hidden=*/1>;

defm HEX_EnableGenExtract : BoolField<
    "hexagon-extract", "true",
    "Generate \"extract\" instructions",
    /*hidden=*/1>;

defm HEX_EnableGenMux : BoolField<
    "hexagon-mux", "true",
    "Enable converting conditional transfers into MUX instructions",
    /*hidden=*/1>;

defm HEX_EnableGenPred : BoolField<
    "hexagon-gen-pred", "true",
    "Enable conversion of arithmetic operations to predicate instructions",
    /*hidden=*/1>;

defm HEX_EnableLoopPrefetch : BoolField<
    "hexagon-loop-prefetch", "false",
    "Enable loop data prefetch on Hexagon",
    /*hidden=*/1>;

defm HEX_DisableHSDR : BoolField<
    "disable-hsdr", "false",
    "Disable splitting double registers",
    /*hidden=*/1>;

defm HEX_EnableGenMemAbs : BoolField<
    "hexagon-mem-abs", "true",
    "Generate absolute set instructions",
    /*hidden=*/1>;

defm HEX_EnableBitSimplify : BoolField<
    "hexagon-bit", "true",
    "Bit simplification",
    /*hidden=*/1>;

defm HEX_EnableLoopResched : BoolField<
    "hexagon-loop-resched", "true",
    "Loop rescheduling",
    /*hidden=*/1>;

defm HEX_NoOpt : BoolField<
    "hexagon-noopt", "false",
    "Disable backend optimizations",
    /*hidden=*/1>;

defm HEX_EnableVectorPrint : BoolField<
    "enable-hexagon-vector-print", "false",
    "Enable Hexagon Vector print instr pass",
    /*hidden=*/1>;

defm HEX_EnableVExtractOpt : BoolField<
    "hexagon-opt-vextract", "true",
    "Enable vextract optimization",
    /*hidden=*/1>;

defm HEX_EnableVectorCombine : BoolField<
    "hexagon-vector-combine", "true",
    "Enable HVX vector combining",
    /*hidden=*/1>;

defm HEX_EnableInitialCFGCleanup : BoolField<
    "hexagon-initial-cfg-cleanup", "true",
    "Simplify the CFG after atomic expansion pass",
    /*hidden=*/1>;

defm HEX_EnableInstSimplify : BoolField<
    "hexagon-instsimplify", "true",
    "Enable instsimplify",
    /*hidden=*/1>;

// --- HexagonTargetObjectFile.cpp ---

defm HEX_SmallDataThreshold : ValueField<
    "hexagon-small-data-threshold", "unsigned", "8",
    "Maximum size of an object in the sdata section",
    /*hidden=*/1>;

defm HEX_NoSmallDataSorting : BoolField<
    "mno-sort-sda", "false",
    "",
    /*hidden=*/1>;

defm HEX_StaticInSData : BoolField<
    "hexagon-statics-in-small-data", "false",
    "",
    /*hidden=*/1>;

defm HEX_TraceGVPlacement : BoolField<
    "trace-gv-placement", "false",
    "Trace GV placement",
    /*hidden=*/1>;

defm HEX_EmitJTInText : BoolField<
    "hexagon-emit-jt-text", "false",
    "",
    /*hidden=*/1>;

defm HEX_EmitLUTInText : BoolField<
    "hexagon-emit-lut-text", "false",
    "",
    /*hidden=*/1>;

// --- HexagonTargetTransformInfo.cpp ---

defm HEX_AutoHVX : BoolField<
    "hexagon-autohvx", "false",
    "Enable auto-vectorization for HVX",
    /*hidden=*/1>;

defm HEX_AllowScatterGatherHVX : BoolField<
    "hexagon-allow-scatter-gather-hvx", "false",
    "Allow gather/scatter intrinsics for Hexagon",
    /*hidden=*/1>;

defm HEX_EnableV68FloatAutoHVX : BoolField<
    "force-hvx-float", "false",
    "Enable auto-vectorization of floating point types on v68.",
    /*hidden=*/1>;

defm HEX_EmitLookupTables : BoolField<
    "hexagon-emit-lookup-tables", "true",
    "Enable emission of lookup tables",
    /*hidden=*/1>;

defm HEX_MaskedVMem : BoolField<
    "hexagon-masked-vmem", "true",
    "Enable masked vector memory operations",
    /*hidden=*/1>;

// --- HexagonVExtract.cpp ---

defm HEX_VExtractThreshold : ValueField<
    "hexagon-vextract-threshold", "unsigned", "1",
    "",
    /*hidden=*/1>;

// --- HexagonVLIWPacketizer.cpp ---

defm HEX_DisablePacketizer : BoolField<
    "disable-packetizer", "false",
    "Disable Hexagon packetizer pass",
    /*hidden=*/1>;

defm HEX_Slot1Store : BoolField<
    "slot1-store-slot0-load", "true",
    "Allow slot1 store slot0 load",
    /*hidden=*/1>;

defm HEX_PacketizeVolatiles : BoolField<
    "hexagon-packetize-volatiles", "true",
    "Allow packetizing volatile memory accesses",
    /*hidden=*/1>;

defm HEX_DisableVecDblNVStores : BoolField<
    "disable-vecdbl-nv-stores", "false",
    "",
    /*hidden=*/1>;

// --- HexagonVectorCombine.cpp ---

defm HEX_HVCDumpModule : BoolField<
    "hvc-dump-module", "false",
    "",
    /*hidden=*/1>;

defm HEX_HVCVAEnabled : BoolField<
    "hvc-va", "true",
    "",
    /*hidden=*/1>;

defm HEX_HVCVIEnabled : BoolField<
    "hvc-vi", "true",
    "",
    /*hidden=*/1>;

defm HEX_HVCVADoFullStores : BoolField<
    "hvc-va-full-stores", "false",
    "",
    /*hidden=*/1>;

defm HEX_HVCVAGroupCountLimit : ValueField<
    "hvc-va-group-count-limit", "unsigned", "4294967295",
    "",
    /*hidden=*/1>;

defm HEX_HVCVAGroupSizeLimit : ValueField<
    "hvc-va-group-size-limit", "unsigned", "4294967295",
    "",
    /*hidden=*/1>;

// --- HexagonVectorLoopCarriedReuse.cpp ---

defm HEX_VLCRIterationLim : ValueField<
    "hexagon-vlcr-iteration-lim", "int", "2",
    "",
    /*hidden=*/1>;

// --- HexagonVectorPrint.cpp ---

defm HEX_TraceHexVectorStoresOnly : BoolField<
    "trace-hex-vector-stores-only", "false",
    "Trace hex vector stores only",
    /*hidden=*/1>;

// --- RDFCopy.cpp ---

defm HEX_RDFCpLimit : OptionalField<
    "rdf-cp-limit", "unsigned",
    "",
    /*hidden=*/1>;

// --- MCTargetDesc/HexagonMCTargetDesc.cpp ---

defm HEX_DisableCompound : BoolField<
    "mno-compound", "false",
    "Disable looking for compound instructions for Hexagon",
    /*hidden=*/0>;

defm HEX_DisableDuplex : BoolField<
    "mno-pairing", "false",
    "Disable looking for duplex instructions for Hexagon",
    /*hidden=*/0>;

defm HEX_MV5 : BoolField<
    "mv5", "false",
    "Build for Hexagon V5",
    /*hidden=*/1>;

defm HEX_MV55 : BoolField<
    "mv55", "false",
    "Build for Hexagon V55",
    /*hidden=*/1>;

defm HEX_MV60 : BoolField<
    "mv60", "false",
    "Build for Hexagon V60",
    /*hidden=*/1>;

defm HEX_MV62 : BoolField<
    "mv62", "false",
    "Build for Hexagon V62",
    /*hidden=*/1>;

defm HEX_MV65 : BoolField<
    "mv65", "false",
    "Build for Hexagon V65",
    /*hidden=*/1>;

defm HEX_MV66 : BoolField<
    "mv66", "false",
    "Build for Hexagon V66",
    /*hidden=*/1>;

defm HEX_MV67 : BoolField<
    "mv67", "false",
    "Build for Hexagon V67",
    /*hidden=*/1>;

defm HEX_MV67T : BoolField<
    "mv67t", "false",
    "Build for Hexagon V67T",
    /*hidden=*/1>;

defm HEX_MV68 : BoolField<
    "mv68", "false",
    "Build for Hexagon V68",
    /*hidden=*/1>;

defm HEX_MV69 : BoolField<
    "mv69", "false",
    "Build for Hexagon V69",
    /*hidden=*/1>;

defm HEX_MV71 : BoolField<
    "mv71", "false",
    "Build for Hexagon V71",
    /*hidden=*/1>;

defm HEX_MV71T : BoolField<
    "mv71t", "false",
    "Build for Hexagon V71T",
    /*hidden=*/1>;

defm HEX_MV73 : BoolField<
    "mv73", "false",
    "Build for Hexagon V73",
    /*hidden=*/1>;

defm HEX_MV75 : BoolField<
    "mv75", "false",
    "Build for Hexagon V75",
    /*hidden=*/1>;

defm HEX_MV79 : BoolField<
    "mv79", "false",
    "Build for Hexagon V79",
    /*hidden=*/1>;

defm HEX_MV81 : BoolField<
    "mv81", "false",
    "Build for Hexagon V81",
    /*hidden=*/1>;

// --- MCTargetDesc/HexagonMCTargetDesc.cpp (-mhvx) ---

// Hand-authored: -mhvx is ValueOptional. Bare "-mhvx" selects Generic,
// "-mhvx=vNN" selects that version, and when absent the field stays
// std::nullopt (upstream's NoArch sentinel). This is OptionalValueField<>'s
// shape spelled out by hand so the Joined def can carry a FieldEnum<>. Like
// upstream's cl::ValueOptional, there is no "-mhvx vNN" Separate spelling:
// the bare Flag already owns the "-mhvx" name.
def HEX_MHVX : Joined<["-", "--"], "mhvx=">,
    HelpText<"Enable Hexagon Vector eXtensions">,
    Field<"std::optional<HexHVXArch>", "HEX_MHVX", "std::nullopt">,
    FieldEnum<HexHVXArchEnum>;
def HEX_MHVX_bare : Flag<["-", "--"], "mhvx">, Flags<[HelpHidden]>,
    Assign<"HEX_MHVX", "HexHVXArch::Generic">;

defm HEX_EnableHVXIEEEFP : BoolField<
    "mhvx-ieee-fp", "false",
    "Enable HVX IEEE floating point",
    /*hidden=*/1>;

defm HEX_EnableCabac : BoolField<
    "mcabac", "false",
    "tbd",
    /*hidden=*/0>;

// --- MCTargetDesc/HexagonMCChecker.cpp ---

defm HEX_RelaxNVChecks : BoolField<
    "relax-nv-checks", "false",
    "",
    /*hidden=*/1>;

// --- MCTargetDesc/HexagonMCELFStreamer.cpp ---

defm HEX_GPSize : ValueField<
    "gpsize", "unsigned", "8",
    "Global Pointer Addressing Size.  The default size is 8.",
    /*hidden=*/0>;

// --- MCTargetDesc/HexagonMCShuffler.cpp ---

defm HEX_DisableShuffle : BoolField<
    "disable-hexagon-shuffle", "false",
    "Disable Hexagon instruction shuffling",
    /*hidden=*/1>;

// --- MCTargetDesc/HexagonAsmBackend.cpp ---

defm HEX_DisableFixup : BoolField<
    "mno-fixup", "false",
    "Disable fixing up resolved relocations for Hexagon",
    /*hidden=*/0>;

// --- AsmParser/HexagonAsmParser.cpp ---

defm HEX_WarnMissingParenthesis : BoolField<
    "mwarn-missing-parenthesis", "true",
    "Warn for missing parenthesis around predicate registers",
    /*hidden=*/0>;

defm HEX_ErrorMissingParenthesis : BoolField<
    "merror-missing-parenthesis", "false",
    "Error for missing parenthesis around predicate registers",
    /*hidden=*/0>;

defm HEX_WarnSignedMismatch : BoolField<
    "mwarn-sign-mismatch", "false",
    "Warn for mismatching a signed and unsigned value",
    /*hidden=*/0>;

defm HEX_WarnNoncontigiousRegister : BoolField<
    "mwarn-noncontigious-register", "true",
    "Warn for register names that aren't contigious",
    /*hidden=*/0>;

defm HEX_ErrorNoncontigiousRegister : BoolField<
    "merror-noncontigious-register", "false",
    "Error for register names that aren't contigious",
    /*hidden=*/0>;

defm HEX_AddBuildAttributes : BoolField<
    "hexagon-add-build-attributes", "false",
    "",
    /*hidden=*/0>;

// --- HexagonSubtarget.cpp (additional) ---

defm HEX_DisableHexagonMISched : OptionalValueField<
    "disable-hexagon-misched", "bool", "true",
    "Disable Hexagon MI Scheduling",
    /*hidden=*/1>;

// --- HexagonPeephole.cpp (additional) ---

defm HEX_DisableHexagonPeephole : BoolField<
    "disable-hexagon-peephole", "false",
    "Disable Peephole Optimization",
    /*hidden=*/1>;

// --- HexagonVLIWPacketizer.cpp (additional) ---

defm HEX_EnableGenAllInsnClass : BoolField<
    "enable-gen-insn", "false",
    "Generate all instruction with TC",
    /*hidden=*/1>;

// --- HexagonSplitDouble.cpp (additional) ---

defm HEX_SplitAll : BoolField<
    "hsdr-split-all", "false",
    "Split all partitions",
    /*hidden=*/1>;

// --- HexagonLoopAlign.cpp (additional) ---

defm HEX_LoopEdgeThreshold : ValueField<
    "hexagon-loop-edge-threshold", "unsigned", "7500",
    "Set hexagon loop align edge threshold",
    /*hidden=*/1>;

// --- HexagonGlobalScheduler.cpp (additional) ---

defm HEX_PerformDualJumps : BoolField<
    "dual-jump-in-pull-up", "true",
    "Perform dual jump formation during pull up",
    /*hidden=*/1>;

defm HEX_AllowBBPeelPullUp : BoolField<
    "enable-bb-peel-pull-up", "true",
    "Peel a reg copy out of a BBloop",
    /*hidden=*/1>;

defm HEX_DisableCheckBundles : BoolField<
    "disable-hexagon-check-bundles", "true",
    "Disable Hexagon check bundles pass",
    /*hidden=*/1>;

defm HEX_WarnOnBundleSize : BoolField<
    "warn-on-bundle-size", "false",
    "Hexagon check bundles and warn on size",
    /*hidden=*/1>;

defm HEX_ForceNoopHazards : BoolField<
    "force-noop-hazards", "false",
    "Force noop hazards in scheduler",
    /*hidden=*/1>;

// --- HexagonInstrInfo.cpp (additional) ---

defm HEX_UseDFAHazardRec : BoolField<
    "dfa-hazard-rec", "true",
    "Use the DFA based hazard recognizer.",
    /*hidden=*/1>;

// --- HexagonISelLowering.cpp (additional) ---

defm HEX_ConstantLoadsToImm : BoolField<
    "constant-loads-to-imm", "true",
    "Convert constant loads to immediate values.",
    /*hidden=*/1>;

defm HEX_DisableArgsMinAlignment : BoolField<
    "hexagon-disable-args-min-alignment", "false",
    "Disable minimum alignment of 1 for "
    "arguments passed by value on stack",
    /*hidden=*/1>;

// --- HexagonISelLoweringHVX.cpp (additional) ---

defm HEX_EnableFpFastConvert : BoolField<
    "hexagon-fp-fast-convert", "false",
    "Enable FP fast conversion routine.",
    /*hidden=*/1>;

// --- HexagonFrameLowering.cpp (additional) ---

defm HEX_SpillOptMax : OptionalField<
    "spill-opt-max", "unsigned",
    "",
    /*hidden=*/1>;

// --- HexagonGenInsert.cpp (additional) ---

defm HEX_VRegIndexCutoff : OptionalField<
    "insert-vreg-cutoff", "unsigned",
    "Vreg# cutoff for insert generation.",
    /*hidden=*/1>;

defm HEX_MaxORLSize : ValueField<
    "insert-max-orl", "unsigned", "4096",
    "Maximum size of OrderedRegisterList",
    /*hidden=*/1>;

defm HEX_OptTimingDetail : BoolField<
    "insert-timing-detail", "false",
    "Enable detailed timing of insert generation",
    /*hidden=*/1>;

// --- HexagonVectorCombine.cpp (additional) ---

defm HEX_MinLoadGroupSizeForAlignment : ValueField<
    "hvc-ld-min-group-size-for-alignment", "unsigned", "4",
    "",
    /*hidden=*/1>;

// --- MCTargetDesc/HexagonMCTargetDesc.cpp (additional) ---

defm HEX_DisableHVX : BoolField<
    "mno-hvx", "false",
    "Disable Hexagon Vector eXtensions",
    /*hidden=*/1>;

// --- HexagonHVXSaveRemark.cpp ---

defm HEX_HVXSaveThreshold : ValueField<
    "hexagon-hvx-save-threshold", "unsigned", "1024",
    "Minimum number of bytes of HVX caller-saved register data live "
    "across a call to trigger a remark (default: 8 x 128-byte vectors)",
    /*hidden=*/1>;

// --- HexagonXQFloatGenerator.cpp ---

defm HEX_EnableHVXXQFloat : BoolField<
    "enable-xqf-gen", "false",
    "Enable HVX XQFloat Generator",
    /*hidden=*/0>;

defm HEX_EnableConversionsRemoval : BoolField<
    "enable-rem-conv", "false",
    "Enable extraneous conversions removal",
    /*hidden=*/0>;

defm HEX_PrintDebugXQF : BoolField<
    "debug-print", "false",
    "Print debug",
    /*hidden=*/0>;

// --- HexagonPostRAHandleQFP.cpp ---

defm HEX_DisablePostRAHandleQFloat : BoolField<
    "disable-handle-qfp", "false",
    "Disable handling of Qfloat spills/refills after register allocation.",
    /*hidden=*/0>;

defm HEX_EnablePostRAXqfCompliance : BoolField<
    "enable-postra-xqf-check", "false",
    "Enable ABI compliance for xqf operands post regalloc.",
    /*hidden=*/0>;

// --- HexagonTargetMachine.cpp (QFloatMode enum) ---

defm HEX_QFloatMode : EnumField<
    "hexagon-qfloat-mode", QFloatModeEnum, "QFloatMode::Legacy",
    "Specify the qfloat mode to operate on.",
    /*hidden=*/1>;

// --- HexagonAlignGlobalArrays.cpp ---

defm HEX_DisableGlobalArrayAlignment : BoolField<
    "hexagon-disable-global-array-align", "false",
    "Disable aligning global integer arrays to an 8-byte boundary",
    /*hidden=*/1>;

defm HEX_DisableAlignOptByteHalf : BoolField<
    "hexagon-disable-align-opt-byte-half", "false",
    "Disable keeping byte and half-word arrays at their natural alignment "
    "when reducing .rodata size",
    /*hidden=*/1>;
