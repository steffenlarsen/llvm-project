//===- CodeGenPassOptionsCore2.td - Core2 option defs -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def CodeGenCore2Options : OptionsStruct<"llvm"> {}

// =========================================================================
// Options — HardwareLoops.cpp (continued; the other 4 tri-state options in
// this same source file belong to CGPassCore1Reg, still unmigrated)
// =========================================================================

// std::optional<unsigned>: HardwareLoopsLegacy needs to distinguish "left
// unset" (keep the pass's default counter bitwidth) from "explicitly
// specified".
defm CGPASS_HardwareLoopCounterBitwidth : OptionalValueField<
    "hardware-loop-counter-bitwidth", "unsigned", "32u",
    "Set the loop counter bitwidth", /*hidden=*/1>;

// std::optional<bool>: HardwareLoopsLegacy needs to distinguish "left
// unset" (keep the pass's default guard behavior) from "explicitly
// specified" (including explicitly false).
defm CGPASS_ForceHardwareLoopGuard : OptionalValueField<
    "force-hardware-loop-guard", "bool", "true",
    "Force generation of loop guard intrinsic", /*hidden=*/1>;

// =========================================================================
// Options — IfConversion.cpp
// =========================================================================

defm CGPASS_IfcvtFnStart : ValueField<"ifcvt-fn-start", "int", "-1", "",
    /*hidden=*/1>;

defm CGPASS_IfcvtFnStop : ValueField<"ifcvt-fn-stop", "int", "-1", "",
    /*hidden=*/1>;

defm CGPASS_IfcvtLimit : ValueField<"ifcvt-limit", "int", "-1", "",
    /*hidden=*/1>;

defm CGPASS_DisableIfcvtSimple : BoolField<"disable-ifcvt-simple", "false",
    "", /*hidden=*/1>;

defm CGPASS_DisableIfcvtSimpleFalse : BoolField<
    "disable-ifcvt-simple-false", "false", "", /*hidden=*/1>;

defm CGPASS_DisableIfcvtTriangle : BoolField<"disable-ifcvt-triangle",
    "false", "", /*hidden=*/1>;

defm CGPASS_DisableIfcvtTriangleRev : BoolField<
    "disable-ifcvt-triangle-rev", "false", "", /*hidden=*/1>;

defm CGPASS_DisableIfcvtTriangleFalse : BoolField<
    "disable-ifcvt-triangle-false", "false", "", /*hidden=*/1>;

defm CGPASS_DisableIfcvtDiamond : BoolField<"disable-ifcvt-diamond",
    "false", "", /*hidden=*/1>;

defm CGPASS_DisableIfcvtForkedDiamond : BoolField<
    "disable-ifcvt-forked-diamond", "false", "", /*hidden=*/1>;

defm CGPASS_IfcvtBranchFold : BoolField<"ifcvt-branch-fold", "true", "",
    /*hidden=*/1>;

// =========================================================================
// Options — ImplicitNullChecks.cpp
// =========================================================================

defm CGPASS_ImpNullCheckPageSize : ValueField<"imp-null-check-page-size",
    "int", "4096", "The page size of the target in bytes", /*hidden=*/1>;

defm CGPASS_ImpNullMaxInstsToConsider : ValueField<
    "imp-null-max-insts-to-consider", "unsigned", "8u",
    "The max number of instructions to consider hoisting loads over (the "
    "algorithm is quadratic over this number)", /*hidden=*/1>;

// =========================================================================
// Options — InterleavedAccessPass.cpp
// =========================================================================

defm CGPASS_LowerInterleavedAccesses : BoolField<
    "lower-interleaved-accesses", "true",
    "Enable lowering interleaved accesses to intrinsics", /*hidden=*/1>;

// =========================================================================
// Options — InterleavedLoadCombinePass.cpp
// =========================================================================

defm CGPASS_DisableInterleavedLoadCombine : BoolField<
    "disable-interleaved-load-combine", "false",
    "Disable combining of interleaved loads", /*hidden=*/1>;

// =========================================================================
// Options — MLRegAllocEvictAdvisor.cpp
// =========================================================================

defm CGPASS_RegallocEvictInteractiveChannelBase : ValueField<
    "regalloc-evict-interactive-channel-base", "std::string", "\"\"",
    "Base file path for the interactive mode.", /*hidden=*/1>;

defm CGPASS_MlregallocMaxEvictionCount : ValueField<
    "mlregalloc-max-eviction-count", "unsigned", "100u",
    "The maximum number of times a live range can be evicted before "
    "preventing it from being evicted", /*hidden=*/1>;

defm CGPASS_RegallocTrainingLog : ValueField<"regalloc-training-log",
    "std::string", "",
    "Training log for the register allocator eviction model",
    /*hidden=*/1>;

defm CGPASS_RegallocModel : ValueField<"regalloc-model", "std::string", "",
    "The model being trained for register allocation eviction",
    /*hidden=*/1>;

// =========================================================================
// Options — MLRegAllocPriorityAdvisor.cpp
// =========================================================================

defm CGPASS_RegallocPriorityInteractiveChannelBase : ValueField<
    "regalloc-priority-interactive-channel-base", "std::string", "",
    "Base file path for the interactive mode.", /*hidden=*/1>;

defm CGPASS_RegallocPriorityTrainingLog : ValueField<
    "regalloc-priority-training-log", "std::string", "",
    "Training log for the register allocator priority model",
    /*hidden=*/1>;

defm CGPASS_RegallocPriorityModel : ValueField<"regalloc-priority-model",
    "std::string", "",
    "The model being trained for register allocation priority",
    /*hidden=*/1>;

// =========================================================================
// Options — PHIElimination.cpp
// =========================================================================

defm CGPASS_DisablePhiElimEdgeSplitting : BoolField<
    "disable-phi-elim-edge-splitting", "false",
    "Disable critical edge splitting during PHI elimination",
    /*hidden=*/1>;

defm CGPASS_PhiElimSplitAllCriticalEdges : BoolField<
    "phi-elim-split-all-critical-edges", "false",
    "Split all critical edges during PHI elimination", /*hidden=*/1>;

defm CGPASS_NoPhiElimLiveOutEarlyExit : BoolField<
    "no-phi-elim-live-out-early-exit", "false",
    "Do not use an early exit if isLiveOutPastPHIs returns true.",
    /*hidden=*/1>;

// =========================================================================
// Options — PeepholeOptimizer.cpp
// =========================================================================

defm CGPASS_AggressiveExtOpt : BoolField<"aggressive-ext-opt", "false",
    "Aggressive extension optimization", /*hidden=*/1>;

defm CGPASS_DisablePeephole : BoolField<"disable-peephole", "false",
    "Disable the peephole optimizer", /*hidden=*/1>;

defm CGPASS_DisableAdvCopyOpt : BoolField<"disable-adv-copy-opt", "false",
    "Disable advanced copy optimization", /*hidden=*/1>;

defm CGPASS_DisableNonAllocatablePhysCopyOpt : BoolField<
    "disable-non-allocatable-phys-copy-opt", "false",
    "Disable non-allocatable physical register copy optimization",
    /*hidden=*/1>;

defm CGPASS_RewritePhiLimit : ValueField<"rewrite-phi-limit", "unsigned",
    "10u", "Limit the length of PHI chains to lookup", /*hidden=*/1>;

defm CGPASS_RecurrenceChainLimit : ValueField<"recurrence-chain-limit",
    "unsigned", "3u",
    "Maximum length of recurrence chain when evaluating the benefit of "
    "commuting operands", /*hidden=*/1>;

// =========================================================================
// Options — PreISelIntrinsicLowering.cpp
// =========================================================================

// std::optional<int64_t>: PreISelIntrinsicLowering needs to distinguish
// "left unset" (fall back to the subtarget's max mem intrinsic inline size
// threshold) from "explicitly specified".
defm CGPASS_MemIntrinsicExpandSize : OptionalValueField<
    "mem-intrinsic-expand-size", "int64_t", "-1",
    "Set minimum mem intrinsic size to expand in IR", /*hidden=*/1>;

// =========================================================================
// Options — RDFLiveness.cpp
// =========================================================================

defm CGPASS_RdfLivenessMaxRec : ValueField<"rdf-liveness-max-rec",
    "unsigned", "25u", "Maximum recursion level", /*hidden=*/1>;

// =========================================================================
// Options — SafeStack.cpp
// =========================================================================

defm CGPASS_SafestackUsePointerAddress : BoolField<
    "safestack-use-pointer-address", "false", "", /*hidden=*/1>;

defm CGPASS_SafeStackColoring : BoolField<"safe-stack-coloring", "true",
    "enable safe stack coloring", /*hidden=*/1>;

// =========================================================================
// Options — SafeStackLayout.cpp
// =========================================================================

defm CGPASS_SafeStackLayout : BoolField<"safe-stack-layout", "true",
    "enable safe stack layout", /*hidden=*/1>;

// =========================================================================
// Options — SelectOptimize.cpp
// =========================================================================

defm CGPASS_ColdOperandThreshold : ValueField<"cold-operand-threshold",
    "unsigned", "20u",
    "Maximum frequency of path for an operand to be considered cold.",
    /*hidden=*/1>;

defm CGPASS_ColdOperandMaxCostMultiplier : ValueField<
    "cold-operand-max-cost-multiplier", "unsigned", "1u",
    "Maximum cost multiplier of TCC_expensive for the dependence slice of "
    "a cold operand to be considered inexpensive.", /*hidden=*/1>;

defm CGPASS_SelectOptiLoopGradientGainThreshold : ValueField<
    "select-opti-loop-gradient-gain-threshold", "unsigned", "25u",
    "Gradient gain threshold (%).", /*hidden=*/1>;

defm CGPASS_SelectOptiLoopCycleGainThreshold : ValueField<
    "select-opti-loop-cycle-gain-threshold", "unsigned", "4u",
    "Minimum gain per loop (in cycles) threshold.", /*hidden=*/1>;

defm CGPASS_SelectOptiLoopRelativeGainThreshold : ValueField<
    "select-opti-loop-relative-gain-threshold", "unsigned", "8u",
    "Minimum relative gain per loop threshold (1/X). Defaults to 12.5%",
    /*hidden=*/1>;

defm CGPASS_MispredictDefaultRate : ValueField<"mispredict-default-rate",
    "unsigned", "25u", "Default mispredict rate (initialized to 25%).",
    /*hidden=*/1>;

defm CGPASS_DisableLoopLevelHeuristics : BoolField<
    "disable-loop-level-heuristics", "false",
    "Disable loop-level heuristics.", /*hidden=*/1>;

// =========================================================================
// Options — ShrinkWrap.cpp
// =========================================================================

// std::optional<bool>: ShrinkWrap needs to distinguish "left unset" (defer
// to the target's own preference) from "explicitly specified" (including
// explicitly false) -- the same tri-state cl::boolOrDefault semantics as
// clv2's original CGPASS_EnableShrinkWrap.
defm CGPASS_EnableShrinkWrap : OptionalValueField<"enable-shrink-wrap",
    "bool", "true", "enable the shrink-wrapping pass", /*hidden=*/1>;

defm CGPASS_EnableShrinkWrapRegionSplit : BoolField<
    "enable-shrink-wrap-region-split", "true",
    "enable splitting of the restore block if possible", /*hidden=*/1>;

// =========================================================================
// Options — StackColoring.cpp
// =========================================================================

defm CGPASS_NoStackColoring : BoolField<"no-stack-coloring", "false",
    "Disable stack coloring", /*hidden=*/1>;

defm CGPASS_ProtectFromEscapedAllocas : BoolField<
    "protect-from-escaped-allocas", "false",
    "Do not optimize lifetime zones that are broken", /*hidden=*/1>;

defm CGPASS_StackcoloringLifetimeStartOnFirstUse : BoolField<
    "stackcoloring-lifetime-start-on-first-use", "true",
    "Treat stack lifetimes as starting on first use, not on START marker.",
    /*hidden=*/1>;

// =========================================================================
// Options — StackMapLivenessAnalysis.cpp
// =========================================================================

defm CGPASS_EnablePatchpointLiveness : BoolField<
    "enable-patchpoint-liveness", "true",
    "Enable PatchPoint Liveness Analysis Pass", /*hidden=*/1>;

// =========================================================================
// Options — StackMaps.cpp
// =========================================================================

defm CGPASS_StackmapVersion : ValueField<"stackmap-version", "int", "3",
    "Specify the stackmap encoding version (default = 3)", /*hidden=*/1>;

// =========================================================================
// Options — StackProtector.cpp
// =========================================================================

defm CGPASS_EnableSelectiondagSp : BoolField<"enable-selectiondag-sp",
    "true", "", /*hidden=*/1>;

defm CGPASS_DisableCheckNoreturnCall : BoolField<
    "disable-check-noreturn-call", "false", "", /*hidden=*/1>;

// =========================================================================
// Options — StackSlotColoring.cpp
// =========================================================================

defm CGPASS_NoStackSlotSharing : BoolField<"no-stack-slot-sharing",
    "false", "Suppress slot sharing during stack coloring", /*hidden=*/1>;

defm CGPASS_SscDceLimit : ValueField<"ssc-dce-limit", "int", "-1", "",
    /*hidden=*/1>;

// =========================================================================
// Options — TailDuplicator.cpp
// =========================================================================

defm CGPASS_TailDupSize : ValueField<"tail-dup-size", "unsigned", "2u",
    "Maximum instructions to consider tail duplicating", /*hidden=*/1>;

defm CGPASS_TailDupIndirectSize : ValueField<"tail-dup-indirect-size",
    "unsigned", "20u",
    "Maximum instructions to consider tail duplicating blocks that end "
    "with indirect branches.", /*hidden=*/1>;

defm CGPASS_TailDupPredSize : ValueField<"tail-dup-pred-size", "unsigned",
    "16u",
    "Maximum predecessors (maximum successors at the same time) to "
    "consider tail duplicating blocks.", /*hidden=*/1>;

defm CGPASS_TailDupSuccSize : ValueField<"tail-dup-succ-size", "unsigned",
    "16u",
    "Maximum successors (maximum predecessors at the same time) to "
    "consider tail duplicating blocks.", /*hidden=*/1>;

defm CGPASS_TailDupVerify : BoolField<"tail-dup-verify", "false",
    "Verify sanity of PHI instructions during taildup", /*hidden=*/1>;

defm CGPASS_TailDupLimit : ValueField<"tail-dup-limit", "unsigned", "~0U",
    "", /*hidden=*/1>;

// =========================================================================
// Options — TwoAddressInstructionPass.cpp
// =========================================================================

defm CGPASS_TwoaddrReschedule : BoolField<"twoaddr-reschedule", "true",
    "Coalesce copies by rescheduling (default=true)", /*hidden=*/1>;

defm CGPASS_TwoaddrAnalyzeRevcopyTied : BoolField<
    "twoaddr-analyze-revcopy-tied", "true",
    "Analyze tied operands when looking for reversed copy chain",
    /*hidden=*/1>;

defm CGPASS_DataflowEdgeLimit : ValueField<"dataflow-edge-limit",
    "unsigned", "10u",
    "Maximum number of dataflow edges to traverse when evaluating the "
    "benefit of commuting operands", /*hidden=*/1>;

// =========================================================================
// Options — TypePromotion.cpp
// =========================================================================

defm CGPASS_DisableTypePromotion : BoolField<"disable-type-promotion",
    "false", "Disable type promotion pass", /*hidden=*/1>;

// =========================================================================
// Options — WinEHPrepare.cpp
// =========================================================================

defm CGPASS_DisableDemotion : BoolField<"disable-demotion", "false",
    "Clone multicolor basic blocks but do not demote cross scopes",
    /*hidden=*/1>;

defm CGPASS_DisableCleanups : BoolField<"disable-cleanups", "false",
    "Do not remove implausible terminators or other similar cleanups",
    /*hidden=*/1>;

defm CGPASS_DemoteCatchswitchOnly : BoolField<"demote-catchswitch-only",
    "false", "Demote catchswitch BBs only (for wasm EH)", /*hidden=*/1>;

// =========================================================================
// Options — WindowsSecureHotPatching.cpp
// =========================================================================

defm CGPASS_MsSecureHotpatchFunctionsFile : ValueField<
    "ms-secure-hotpatch-functions-file", "std::string", "\"\"",
    "A file containing list of mangled function names to mark for Windows "
    "Secure Hot-Patching", /*hidden=*/0, /*metaVar=*/"filename">;

defm CGPASS_MsSecureHotpatchFunctionsList : ListField<
    "ms-secure-hotpatch-functions-list", "std::string",
    "A list of mangled function names to mark for Windows Secure "
    "Hot-Patching", /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/0,
    /*metaVar=*/"list">;
