//===- CodeGenPassOptionsMachine1.td - Machine1 option defs -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def CodeGenMachine1Options : OptionsStruct<"llvm"> {}

// =========================================================================
// Enum: UseBFI (MachineLICM.cpp)
// =========================================================================

def UseBFIEnum : OptionEnum<"UseBFI", [
    EnumMember<"None", "none">,
    EnumMember<"PGO", "pgo">,
    EnumMember<"All", "all">]>;

// =========================================================================
// Options — MachineBasicBlock.cpp
// =========================================================================

defm CGPASS_PrintSlotindexes : BoolField<"print-slotindexes", "true",
    "When printing machine IR, annotate instructions and blocks with "
    "SlotIndexes when available", /*hidden=*/1>;

// =========================================================================
// Options — MachineBlockFrequencyInfo.cpp
// =========================================================================

defm CGPASS_PrintMachineBfi : BoolField<"print-machine-bfi", "false",
    "Print the machine block frequency info.", /*hidden=*/1>;

// =========================================================================
// Options — MachineBlockPlacement.cpp
// =========================================================================

// std::optional<unsigned>: MachineBlockPlacement.cpp falls back to a
// target-computed value (TLI->getMaxPermittedBytesForAlignment()) when this
// is left unspecified, which a plain always-present field couldn't
// distinguish from an explicit 0.
defm CGPASS_MaxBytesForAlignment : OptionalValueField<
    "max-bytes-for-alignment", "unsigned", "0u",
    "Forces the maximum bytes allowed to be emitted when padding for "
    "alignment", /*hidden=*/1>;

// std::optional<unsigned>: unspecified falls back to a target/heuristic
// computed threshold; see the interacting fallback logic in
// MachineBlockPlacement.cpp's tail-duplication cost model.
defm CGPASS_TailDupPlacementThreshold : OptionalValueField<
    "tail-dup-placement-threshold", "unsigned", "2u",
    "Instruction cutoff for tail duplication during layout. Tail merging "
    "during layout is forced to have a threshold that ...", /*hidden=*/1>;

defm CGPASS_TailDupPlacementAggressiveThreshold : OptionalValueField<
    "tail-dup-placement-aggressive-threshold", "unsigned", "4u",
    "Instruction cutoff for aggressive tail duplication during layout. "
    "Used at -O3. Tail merging during layout is forced t...", /*hidden=*/1>;

defm CGPASS_AlignAllBlocks : ValueField<"align-all-blocks", "unsigned", "0u",
    "Force the alignment of all blocks in the function in log2 format "
    "(e.g 4 means align on 16B boundaries).", /*hidden=*/1>;

defm CGPASS_AlignAllNofallthruBlocks : ValueField<
    "align-all-nofallthru-blocks", "unsigned", "0u",
    "Force the alignment of all blocks that have no fall-through "
    "predecessors (i.e. don't add nops that are executed). In ...",
    /*hidden=*/1>;

defm CGPASS_BlockPlacementPredecessorLimit : ValueField<
    "block-placement-predecessor-limit", "unsigned", "1000u",
    "For blocks with more predecessors, certain layout optimizations will "
    "be disabled to prevent quadratic compile time.", /*hidden=*/1>;

defm CGPASS_BlockPlacementExitBlockBias : ValueField<
    "block-placement-exit-block-bias", "unsigned", "0u",
    "Block frequency percentage a loop exit block needs over the original "
    "exit to be considered the new exit.", /*hidden=*/1>;

defm CGPASS_LoopToColdBlockRatio : ValueField<"loop-to-cold-block-ratio",
    "unsigned", "5u",
    "Outline loop blocks from loop chain if (frequency of loop) / "
    "(frequency of block) is greater than this ratio", /*hidden=*/1>;

defm CGPASS_ForceLoopColdBlock : BoolField<"force-loop-cold-block", "false",
    "Force outlining cold blocks from loops.", /*hidden=*/1>;

defm CGPASS_PreciseRotationCost : BoolField<"precise-rotation-cost", "false",
    "Model the cost of loop rotation more precisely by using profile data.",
    /*hidden=*/1>;

defm CGPASS_ForcePreciseRotationCost : BoolField<
    "force-precise-rotation-cost", "false",
    "Force the use of precise cost loop rotation strategy.", /*hidden=*/1>;

defm CGPASS_MisfetchCost : ValueField<"misfetch-cost", "unsigned", "1u",
    "Cost that models the probabilistic risk of an instruction misfetch "
    "due to a jump comparing to falling through, whose ...", /*hidden=*/1>;

defm CGPASS_JumpInstCost : ValueField<"jump-inst-cost", "unsigned", "1u",
    "Cost of jump instructions.", /*hidden=*/1>;

defm CGPASS_TailDupPlacement : BoolField<"tail-dup-placement", "true",
    "Perform tail duplication during placement. Creates more fallthrough "
    "opportunities in outline branches.", /*hidden=*/1>;

defm CGPASS_BranchFoldPlacement : BoolField<"branch-fold-placement", "true",
    "Perform branch folding during placement. Reduces code size.",
    /*hidden=*/1>;

defm CGPASS_TailDupPlacementPenalty : ValueField<
    "tail-dup-placement-penalty", "unsigned", "2u",
    "Cost penalty for blocks that can avoid breaking CFG by copying. "
    "Percent as integer.", /*hidden=*/1>;

defm CGPASS_TailDupProfilePercentThreshold : ValueField<
    "tail-dup-profile-percent-threshold", "unsigned", "50u",
    "If profile count information is used in tail duplication cost model, "
    "the gained fall through number from tail duplica...", /*hidden=*/1>;

defm CGPASS_TriangleChainCount : ValueField<"triangle-chain-count",
    "unsigned", "2u",
    "Number of triangle-shaped-CFG's that need to be in a row for the "
    "triangle tail duplication heuristic to kick in. 0 to...", /*hidden=*/1>;

defm CGPASS_RenumberBlocksBeforeView : BoolField<
    "renumber-blocks-before-view", "false",
    "If true, basic blocks are re-numbered before MBP layout is printed "
    "into a dot graph. Only used when a function is bei...", /*hidden=*/1>;

defm CGPASS_ExtTspBlockPlacementMaxBlocks : ValueField<
    "ext-tsp-block-placement-max-blocks", "unsigned", "UINT_MAX",
    "Maximum number of basic blocks in a function to run ext-TSP block "
    "placement.", /*hidden=*/1>;

defm CGPASS_ApplyExtTspForSize : BoolField<"apply-ext-tsp-for-size", "false",
    "Use ext-tsp for size-aware block placement.", /*hidden=*/1>;

// =========================================================================
// Options — MachineBranchProbabilityInfo.cpp
// =========================================================================

defm CGPASS_StaticLikelyProb : ValueField<"static-likely-prob", "unsigned",
    "80u", "branch probability threshold in percentage to be considered "
    "very likely", /*hidden=*/1>;

defm CGPASS_ProfileLikelyProb : ValueField<"profile-likely-prob", "unsigned",
    "51u", "branch probability threshold in percentage to be considered "
    "very likely when profile is available", /*hidden=*/1>;

// =========================================================================
// Options — MachineCFGPrinter.cpp
// =========================================================================

defm CGPASS_McfgFuncName : ValueField<"mcfg-func-name", "std::string", "",
    "The name of a function (or its substring) whose CFG is "
    "viewed/printed.", /*hidden=*/1>;

defm CGPASS_McfgDotFilenamePrefix : ValueField<"mcfg-dot-filename-prefix",
    "std::string", "",
    "The prefix used for the Machine CFG dot file names.", /*hidden=*/1>;

defm CGPASS_DotMcfgOnly : BoolField<"dot-mcfg-only", "false",
    "Print only the CFG without blocks body", /*hidden=*/1>;

// =========================================================================
// Options — MachineCSE.cpp
// =========================================================================

defm CGPASS_CsusesThreshold : ValueField<"csuses-threshold", "int", "1024",
    "Threshold for the size of CSUses", /*hidden=*/1>;

defm CGPASS_AggressiveMachineCse : BoolField<"aggressive-machine-cse",
    "false", "Override the profitability heuristics for Machine CSE",
    /*hidden=*/1>;

// =========================================================================
// Options — MachineCombiner.cpp
// =========================================================================

defm CGPASS_MachineCombinerIncThreshold : ValueField<
    "machine-combiner-inc-threshold", "unsigned", "500u",
    "Incremental depth computation will be used for basic blocks with "
    "more instructions.", /*hidden=*/1>;

defm CGPASS_MachineCombinerDumpSubstIntrs : BoolField<
    "machine-combiner-dump-subst-intrs", "false", "Dump all substituted "
    "intrs", /*hidden=*/1>;

// NOTE: pre-migration, this getter fell back on a getView<>()-null check to
// an EXPENSIVE_CHECKS-conditional compile-time default (true in
// EXPENSIVE_CHECKS builds, false otherwise) when the registry had never been
// parsed into the current OptionsContext at all. LLVMContext::getOptions<T>()
// has no equivalent "never parsed" signal -- it always yields either a
// per-context override or T::Current (itself defaulted from this field).
// This is a narrow, disclosed behavior change: it can only be observed by
// code that constructs an LLVMContext without ever running it through a
// driver's normal option-parsing, in an EXPENSIVE_CHECKS build.
defm CGPASS_MachineCombinerVerifyPatternOrder : BoolField<
    "machine-combiner-verify-pattern-order", "false",
    "Verify that the generated patterns are ordered by increasing latency",
    /*hidden=*/1>;

// =========================================================================
// Options — MachineCopyPropagation.cpp
// =========================================================================

defm CGPASS_McpUseIsCopyInstr : BoolField<"mcp-use-is-copy-instr", "false",
    "", /*hidden=*/1>;

// std::optional<bool>: was cl::opt<cl::boolOrDefault>; MachineCopyPropagation
// needs to distinguish "left unset" (fall back to a target query) from
// "explicitly specified" (including explicitly false).
defm CGPASS_EnableSpillCopyElim : OptionalValueField<
    "enable-spill-copy-elim", "bool", "true", "", /*hidden=*/1>;

// =========================================================================
// Options — MachineDominators.cpp
// =========================================================================

// See the CGPASS_MachineCombinerVerifyPatternOrder note above: the
// getView<>()-null / EXPENSIVE_CHECKS fallback this used to have pre-
// migration is not representable with LLVMContext::getOptions<T>() and is
// dropped here (narrow, disclosed behavior change).
defm CGPASS_VerifyMachineDomInfo : BoolField<"verify-machine-dom-info",
    "false", "Verify machine dominator info (time consuming)",
    /*hidden=*/1>;

// =========================================================================
// Options — MachineFunction.cpp
// =========================================================================

defm CGPASS_AlignAllFunctions : ValueField<"align-all-functions", "unsigned",
    "0u", "Force the alignment of all functions in log2 format (e.g. 4 "
    "means align on 16B boundaries).", /*hidden=*/1>;

// =========================================================================
// Options — MachineFunctionPass.cpp
// =========================================================================

defm CGPASS_DroppedVariableStatsMir : BoolField<
    "dropped-variable-stats-mir", "false",
    "Dump dropped debug variables stats for MIR passes", /*hidden=*/1>;

// =========================================================================
// Options — MachineFunctionSplitter.cpp
// =========================================================================

defm CGPASS_MfsPsiCutoff : ValueField<"mfs-psi-cutoff", "unsigned",
    "999950u", "Percentile profile summary cutoff used to determine cold "
    "blocks. Unused if set to zero.", /*hidden=*/1>;

defm CGPASS_MfsCountThreshold : ValueField<"mfs-count-threshold", "unsigned",
    "1u", "Minimum number of times a block must be executed to be "
    "retained.", /*hidden=*/1>;

defm CGPASS_MfsSplitEhcode : BoolField<"mfs-split-ehcode", "false",
    "Splits all EH code and it's descendants by default.", /*hidden=*/1>;

// =========================================================================
// Options — MachineInstr.cpp
// =========================================================================

defm CGPASS_PrintMiAddrs : BoolField<"print-mi-addrs", "false",
    "Print addresses of MachineInstrs when dumping", /*hidden=*/1>;

// =========================================================================
// Options — MachineLICM.cpp
// =========================================================================

defm CGPASS_AvoidSpeculation : BoolField<"avoid-speculation", "true",
    "MachineLICM should avoid speculation", /*hidden=*/1>;

defm CGPASS_HoistCheapInsts : BoolField<"hoist-cheap-insts", "false",
    "MachineLICM should hoist even cheap instructions", /*hidden=*/1>;

defm CGPASS_HoistConstStores : BoolField<"hoist-const-stores", "true",
    "Hoist invariant stores", /*hidden=*/1>;

defm CGPASS_HoistConstLoads : BoolField<"hoist-const-loads", "true",
    "Hoist invariant loads", /*hidden=*/1>;

defm CGPASS_BlockFreqRatioThreshold : ValueField<
    "block-freq-ratio-threshold", "unsigned", "100u",
    "Do not hoist instructions if targetblock is N times hotter than "
    "the source.", /*hidden=*/1>;

// =========================================================================
// Options — MachineOperand.cpp
// =========================================================================

defm CGPASS_PrintRegmaskNumRegs : ValueField<"print-regmask-num-regs", "int",
    "32", "Number of registers to limit to when printing regmask operands "
    "in IR dumps. unlimited = -1", /*hidden=*/1>;

// =========================================================================
// Options — MachineOutliner.cpp
// =========================================================================

defm CGPASS_EnableLinkonceodrOutlining : BoolField<
    "enable-linkonceodr-outlining", "false",
    "Enable the machine outliner on linkonceodr functions", /*hidden=*/1>;

defm CGPASS_MachineOutlinerReruns : ValueField<"machine-outliner-reruns",
    "unsigned", "0u",
    "Number of times to rerun the outliner after the initial outline",
    /*hidden=*/1>;

defm CGPASS_OutlinerBenefitThreshold : ValueField<
    "outliner-benefit-threshold", "unsigned", "1u",
    "The minimum size in bytes before an outlining candidate is accepted",
    /*hidden=*/1>;

// =========================================================================
// Options — MachineLICM.cpp (enum option)
// =========================================================================

defm CGPASS_DisableHoistingToHotterBlocks : EnumField<
    "disable-hoisting-to-hotter-blocks", UseBFIEnum, "UseBFI::PGO",
    "Disable hoisting instructions to hotter blocks", /*hidden=*/1>;
