//===- RISCVOptions.td - RISCV target options ------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def RISCVOptions : OptionsStruct<"llvm"> {}

// --- AsmParser/RISCVAsmParser.cpp ---

defm RV_AddBuildAttributes : BoolField<
    "riscv-add-build-attributes", "false", "">;

// --- MCTargetDesc/RISCVAsmBackend.cpp ---

defm RV_ULEB128Reloc : BoolField<
    "riscv-uleb128-reloc",
    "true",
    "Emit R_RISCV_SET_ULEB128/E_RISCV_SUB_ULEB128 if appropriate",
    /*hidden=*/1>;

defm RV_AlignRvc : BoolField<
    "riscv-align-rvc",
    "true",
    "When generating R_RISCV_ALIGN, insert $alignment-2 "
    "bytes of NOPs even in norvc code",
    /*hidden=*/1>;

// --- MCTargetDesc/RISCVInstPrinter.cpp ---

defm RV_NoAliases : BoolField<
    "riscv-no-aliases",
    "false",
    "Disable the emission of assembler pseudo instructions",
    /*hidden=*/1>;

defm RV_EmitX8AsFP : BoolField<
    "riscv-emit-x8-as-fp",
    "false",
    "Emit x8 as fp instead of s0",
    /*hidden=*/1>;

// --- MCTargetDesc/RISCVTargetStreamer.cpp ---

defm RV_AbiAttributes : BoolField<
    "riscv-abi-attributes",
    "false",
    "Enable emitting RISC-V ELF attributes for ABI features",
    /*hidden=*/1>;

// --- RISCVISelDAGToDAG.cpp ---

defm RV_UsePseudoMovImm : BoolField<
    "riscv-use-rematerializable-movimm",
    "false",
    "Use a rematerializable pseudoinstruction for 2 instruction "
    "constant materialization",
    /*hidden=*/1>;

// --- RISCVISelLowering.cpp ---

defm RV_ExtensionMaxWebSize : ValueField<
    "riscv-lower-ext-max-web-size", "unsigned", "18",
    "Give the maximum size (in number of nodes) of the web of "
    "instructions that we will consider for VW expansion",
    /*hidden=*/1>;

defm RV_AllowSplatInVW_W : BoolField<
    "riscv-lower-form-vw-w-with-splat",
    "false",
    "Allow the formation of VW_W operations (e.g., "
    "VWADD_W) with splat constants",
    /*hidden=*/1>;

defm RV_NumRepeatedDivisors : ValueField<
    "riscv-lower-fp-repeated-divisors", "unsigned", "2",
    "Set the minimum number of repetitions of a divisor to allow "
    "transformation to multiplications by the reciprocal",
    /*hidden=*/1>;

defm RV_FPImmCost : ValueField<
    "riscv-lower-fpimm-cost", "int", "3",
    "Give the maximum number of instructions that we will "
    "use for creating a floating-point immediate value",
    /*hidden=*/1>;

defm RV_ReassocShlAddiAdd : BoolField<
    "reassoc-shl-addi-add",
    "true",
    "Swap add and addi in cases where the add may "
    "be combined with a shift",
    /*hidden=*/1>;

// std::optional<int>: the real fallback used by the branch-merging pass
// (Subtarget.getMispredictionPenalty() / 4) is a runtime, non-literal
// value, so "specified or not" must survive as real tri-state rather than
// be folded into the clv2 schema's literal default of 2.
defm RV_BrMergingBaseCostThresh : OptionalField<
    "riscv-br-merging-base-cost", "int",
    "Sets the cost threshold for when multiple conditionals will be merged "
    "into one branch versus be split in multiple branches. Merging "
    "conditionals saves branches at the cost of additional instructions. "
    "This value sets the instruction cost limit, below which conditionals "
    "will be merged, and above which conditionals will be split. Set to -1 "
    "to never merge branches.",
    /*hidden=*/1>;

defm RV_BrMergingLikelyBias : ValueField<
    "riscv-br-merging-likely-bias", "int", "0",
    "Increases 'riscv-br-merging-base-cost' in cases that it is "
    "likely that all conditionals will be executed. For example for "
    "merging the conditionals (a == b && c > d), if its known that "
    "a == b is likely, then it is likely that if the conditionals are "
    "split both sides will be executed, so it may be desirable to "
    "increase the instruction cost threshold. Set to -1 to never merge "
    "likely branches.",
    /*hidden=*/1>;

defm RV_BrMergingUnlikelyBias : ValueField<
    "riscv-br-merging-unlikely-bias", "int", "-1",
    "Decreases 'riscv-br-merging-base-cost' in cases that it is unlikely "
    "that all conditionals will be executed. For example for merging "
    "the conditionals (a == b && c > d), if its known that a == b is "
    "unlikely, then it is unlikely that if the conditionals are split "
    "both sides will be executed, so it may be desirable to decrease "
    "the instruction cost threshold. Set to -1 to never merge unlikely "
    "branches.",
    /*hidden=*/1>;

// --- RISCVIndirectBranchTracking.cpp ---

// std::optional<unsigned>: "unspecified" is distinct from any concrete
// label value (including 0), unlike a plain field with a literal default.
// (clv2's ReallyHidden collapses to plain hidden=1, per prior migrations.)
defm RV_PreferredLandingPadLabel : OptionalField<
    "riscv-landing-pad-label", "unsigned",
    "Use preferred fixed label for all labels",
    /*hidden=*/1>;

// --- RISCVInsertReadWriteCSR.cpp ---

defm RV_DisableFRMInsertOpt : BoolField<
    "riscv-disable-frm-insert-opt",
    "false",
    "Disable optimized frm insertion.",
    /*hidden=*/1>;

// --- RISCVInsertVSETVLI.cpp ---

defm RV_EnsureWholeVectorRegisterMoveValidVTYPE : BoolField<
    "riscv-insert-vsetvli-whole-vector-register-move-valid-vtype",
    "true",
    "Insert vsetvlis before vmvNr.vs to ensure vtype is valid and "
    "vill is cleared",
    /*hidden=*/1>;

// --- RISCVInstrInfo.cpp ---

defm RV_PreferWholeRegisterMove : BoolField<
    "riscv-prefer-whole-register-move",
    "false",
    "Prefer whole register move for vector registers.",
    /*hidden=*/1>;

// External enum: MachineTraceStrategy (from CodeGen/MachineTraceMetrics.h).
// TS_NumStrategies is the compile-time default and deliberately not an
// EnumMember: it has no "-riscv-force-machine-combiner-strategy=<spelling>"
// form of its own -- it's a sentinel unreachable from the CLI, so
// `Field != MachineTraceStrategy::TS_NumStrategies` reproduces clv2's
// wasOptSpecified<> check without needing a std::optional<> field.
def RVMachineTraceStrategyEnum : OptionEnum<"MachineTraceStrategy", [
    EnumMember<"TS_Local", "local">,
    EnumMember<"TS_MinInstrCount", "min-instr">]> {
  let External = 1;
}

defm RV_ForceMachineCombinerStrategy : EnumField<
    "riscv-force-machine-combiner-strategy", RVMachineTraceStrategyEnum,
    "MachineTraceStrategy::TS_NumStrategies",
    "Force machine combiner to use a specific strategy for machine "
    "trace metrics evaluation.",
    /*hidden=*/1>;

defm RV_OutlinerEnableRegSave : BoolField<
    "riscv-outliner-regsave",
    "true",
    "Enable RegSave strategy in machine outliner (save X5 to a "
    "temporary register when X5 is live across outlined calls).",
    /*hidden=*/1>;

// --- RISCVLoadStoreOptimizer.cpp ---

defm RV_LdStLimit : ValueField<
    "riscv-load-store-scan-limit", "unsigned", "128", "",
    /*hidden=*/1>;

// --- RISCVOptWInstrs.cpp ---

defm RV_DisableSExtWRemoval : BoolField<
    "riscv-disable-sextw-removal",
    "false",
    "Disable removal of sext.w",
    /*hidden=*/1>;

defm RV_DisableStripWSuffix : BoolField<
    "riscv-disable-strip-w-suffix",
    "false",
    "Disable strip W suffix",
    /*hidden=*/1>;

// --- RISCVRegisterInfo.cpp ---

defm RV_DisableCostPerUse : BoolField<
    "riscv-disable-cost-per-use", "false", "",
    /*hidden=*/1>;

defm RV_DisableRegAllocHints : BoolField<
    "riscv-disable-regalloc-hints",
    "false",
    "Disable two address hints for register allocation",
    /*hidden=*/1>;

// --- RISCVSubtarget.cpp ---

defm RV_DisableUsingConstantPoolForLargeInts : BoolField<
    "riscv-disable-using-constant-pool-for-large-ints",
    "false",
    "Disable using constant pool for large integers.",
    /*hidden=*/1>;

// Plain field, default 0: the consumer (getMaxBuildIntsCost()) already
// treats 0 as its own "unset" sentinel independently of clv2's
// specified-tracking, so no std::optional<> is needed here.
defm RV_MaxBuildIntsCost : ValueField<
    "riscv-max-build-ints-cost", "unsigned", "0",
    "The maximum cost used for building integers.",
    /*hidden=*/1>;

defm RV_UseAA : BoolField<
    "riscv-use-aa",
    "true",
    "Enable the use of AA during codegen.">;

// std::optional<unsigned>: the real fallback (getSchedModel().
// MispredictPenalty) is a runtime, non-literal value.
defm RV_SchedMispredictPenalty : OptionalField<
    "riscv-sched-mispredict-penalty", "unsigned",
    "Override the mispredict penalty (in cycles) in the scheduler "
    "model. A non-negative value overrides the target default.",
    /*hidden=*/1>;

// std::optional<unsigned>: the real fallback (getSchedModel().LoadLatency)
// is a runtime, non-literal value.
defm RV_SchedLoadLatency : OptionalField<
    "riscv-sched-load-latency", "unsigned",
    "Override the load latency (in cycles) in the scheduler model. "
    "A non-negative value overrides the target default.",
    /*hidden=*/1>;

// std::optional<unsigned>: the real fallback (TuneInfo->
// MinimumJumpTableEntries) is a runtime, non-literal value.
defm RV_MinimumJumpTableEntries : OptionalField<
    "riscv-min-jump-table-entries", "unsigned",
    "Set minimum number of entries to use a jump table on RISCV",
    /*hidden=*/1>;

defm RV_UseMIPSLoadStorePairs : BoolField<
    "use-riscv-mips-load-store-pairs",
    "false",
    "Enable the load/store pair optimization pass",
    /*hidden=*/1>;

defm RV_UseMIPSCCMovInsn : BoolField<
    "use-riscv-mips-ccmov",
    "true",
    "Use 'mips.ccmov' instruction",
    /*hidden=*/1>;

// --- RISCVTargetMachine.cpp ---

defm RV_EnableRedundantCopyElimination : BoolField<
    "riscv-enable-copyelim",
    "true",
    "Enable the redundant copy elimination pass",
    /*hidden=*/1>;

// std::optional<bool>: addPreISel() branches on wasOptSpecified<> itself
// (not just the resolved value), so "unspecified" must survive as tri-state.
defm RV_EnableGlobalMerge : OptionalValueField<
    "riscv-enable-global-merge", "bool", "true",
    "Enable the global merge pass",
    /*hidden=*/1>;

defm RV_EnableMachineCombiner : BoolField<
    "riscv-enable-machine-combiner",
    "true",
    "Enable the machine combiner pass",
    /*hidden=*/1>;

// std::optional<unsigned>: an explicit "riscv-v-vector-bits-max" must take
// priority over a function's VScaleRange attribute, which a same-typed
// literal default (indistinguishable from "unspecified") cannot express.
defm RV_VVectorBitsMax : OptionalField<
    "riscv-v-vector-bits-max", "unsigned",
    "Assume V extension vector registers are at most this big, "
    "with zero meaning no maximum size is assumed.",
    /*hidden=*/1>;

// std::optional<int>: same VScaleRange-attribute-priority reasoning as
// RV_VVectorBitsMax above.
defm RV_VVectorBitsMin : OptionalField<
    "riscv-v-vector-bits-min", "int",
    "Assume V extension vector registers are at least this big, "
    "with zero meaning no minimum size is assumed. A value of -1 "
    "means use Zvl*b extension. This is primarily used to enable "
    "autovectorization with fixed width vectors.",
    /*hidden=*/1>;

defm RV_EnableCopyPropagation : BoolField<
    "riscv-enable-copy-propagation",
    "true",
    "Enable the copy propagation with RISC-V copy instr",
    /*hidden=*/1>;

defm RV_EnableDeadRegisterElimination : BoolField<
    "riscv-enable-dead-defs",
    "true",
    "Enable the pass that removes dead"
    " definitions and replaces stores to"
    " them with stores to x0",
    /*hidden=*/1>;

defm RV_EnableSinkFold : BoolField<
    "riscv-enable-sink-fold",
    "true",
    "Enable sinking and folding of instruction copies",
    /*hidden=*/1>;

defm RV_EnableLoopDataPrefetch : BoolField<
    "riscv-enable-loop-data-prefetch",
    "true",
    "Enable the loop data prefetch pass",
    /*hidden=*/1>;

defm RV_DisableVectorMaskMutation : BoolField<
    "riscv-disable-vector-mask-mutation",
    "false",
    "Disable the vector mask scheduling mutation",
    /*hidden=*/1>;

defm RV_EnableMachinePipeliner : BoolField<
    "riscv-enable-pipeliner",
    "false",
    "Enable Machine Pipeliner for RISC-V",
    /*hidden=*/1>;

defm RV_EnableCFIInstrInserter : BoolField<
    "riscv-enable-cfi-instr-inserter",
    "false",
    "Enable CFI Instruction Inserter for RISC-V",
    /*hidden=*/1>;

defm RV_EnableSelectOpt : BoolField<
    "riscv-select-opt",
    "true",
    "Enable select to branch optimizations",
    /*hidden=*/1>;

// --- RISCVTargetTransformInfo.cpp ---

defm RV_RegisterWidthLMUL : ValueField<
    "riscv-v-register-bit-width-lmul", "unsigned", "2",
    "The LMUL to use for getRegisterBitWidth queries. Affects LMUL used "
    "by autovectorized code. Fractional LMULs are not supported.",
    /*hidden=*/1>;

// std::optional<unsigned>: getMaximumVF() only overrides its computed
// result when the option was actually specified, so "unspecified" must
// survive as tri-state rather than be folded into the 0 literal default.
defm RV_SLPMaxVF : OptionalField<
    "riscv-v-slp-max-vf", "unsigned",
    "Overrides result used for getMaximumVF query which is used "
    "exclusively by SLP vectorizer.",
    /*hidden=*/1>;

defm RV_MinTripCount : ValueField<
    "riscv-v-min-trip-count", "unsigned", "5",
    "Set the lower bound of a trip count to decide on "
    "vectorization while tail-folding.",
    /*hidden=*/1>;

defm RV_EnableOrLikeSelectOpt : BoolField<
    "enable-riscv-or-like-select", "true", "",
    /*hidden=*/1>;

// --- RISCVZilsdOptimizer.cpp ---

defm RV_DisableZilsdOpt : BoolField<
    "disable-riscv-zilsd-opt",
    "false",
    "Disable Zilsd load/store optimization",
    /*hidden=*/1>;

defm RV_ZilsdMaxRescheduleDistance : ValueField<
    "riscv-zilsd-max-reschedule-distance", "unsigned", "10",
    "Maximum distance for rescheduling load/store instructions",
    /*hidden=*/1>;
