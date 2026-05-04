//===- RISCVOptions.td - CLV2 option defs for RISCV ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def RISCVOptsReg : Registry<"llvm::rv_opts"> {
  // Getter bodies are compiled once, in RISCVOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

// --- AsmParser/RISCVAsmParser.cpp ---

def RV_AddBuildAttributes : Option<
    "riscv-add-build-attributes", "",
    "bool"> {
  let Registry = RISCVOptsReg;
}

// --- MCTargetDesc/RISCVAsmBackend.cpp ---

def RV_ULEB128Reloc : Option<
    "riscv-uleb128-reloc",
    "Emit R_RISCV_SET_ULEB128/E_RISCV_SUB_ULEB128 if appropriate",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def RV_AlignRvc : Option<
    "riscv-align-rvc",
    "When generating R_RISCV_ALIGN, insert $alignment-2 "
    "bytes of NOPs even in norvc code",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- MCTargetDesc/RISCVInstPrinter.cpp ---

def RV_NoAliases : Option<
    "riscv-no-aliases",
    "Disable the emission of assembler pseudo instructions",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

def RV_EmitX8AsFP : Option<
    "riscv-emit-x8-as-fp",
    "Emit x8 as fp instead of s0",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

// --- MCTargetDesc/RISCVTargetStreamer.cpp ---

def RV_AbiAttributes : Option<
    "riscv-abi-attributes",
    "Enable emitting RISC-V ELF attributes for ABI features",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

// --- RISCVISelDAGToDAG.cpp ---

def RV_UsePseudoMovImm : Option<
    "riscv-use-rematerializable-movimm",
    "Use a rematerializable pseudoinstruction for 2 instruction "
    "constant materialization",
    "bool"> {
  let Default = "false";
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

// --- RISCVISelLowering.cpp ---

def RV_ExtensionMaxWebSize : Option<
    "riscv-lower-ext-max-web-size",
    "Give the maximum size (in number of nodes) of the web of "
    "instructions that we will consider for VW expansion",
    "unsigned"> {
  let Registry = RISCVOptsReg;
  let Default = "18";
  let Hidden = 1;
}

def RV_AllowSplatInVW_W : Option<
    "riscv-lower-form-vw-w-with-splat",
    "Allow the formation of VW_W operations (e.g., "
    "VWADD_W) with splat constants",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

def RV_NumRepeatedDivisors : Option<
    "riscv-lower-fp-repeated-divisors",
    "Set the minimum number of repetitions of a divisor to allow "
    "transformation to multiplications by the reciprocal",
    "unsigned"> {
  let Registry = RISCVOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def RV_FPImmCost : Option<
    "riscv-lower-fpimm-cost",
    "Give the maximum number of instructions that we will "
    "use for creating a floating-point immediate value",
    "int"> {
  let Registry = RISCVOptsReg;
  let Default = "3";
  let Hidden = 1;
}

def RV_ReassocShlAddiAdd : Option<
    "reassoc-shl-addi-add",
    "Swap add and addi in cases where the add may "
    "be combined with a shift",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def RV_BrMergingBaseCostThresh : Option<
    "riscv-br-merging-base-cost",
    "Sets the cost threshold for when multiple conditionals will be merged "
    "into one branch versus be split in multiple branches. Merging "
    "conditionals saves branches at the cost of additional instructions. "
    "This value sets the instruction cost limit, below which conditionals "
    "will be merged, and above which conditionals will be split. Set to -1 "
    "to never merge branches.",
    "int"> {
  let Registry = RISCVOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def RV_BrMergingLikelyBias : Option<
    "riscv-br-merging-likely-bias",
    "Increases 'riscv-br-merging-base-cost' in cases that it is "
    "likely that all conditionals will be executed. For example for "
    "merging the conditionals (a == b && c > d), if its known that "
    "a == b is likely, then it is likely that if the conditionals are "
    "split both sides will be executed, so it may be desirable to "
    "increase the instruction cost threshold. Set to -1 to never merge "
    "likely branches.",
    "int"> {
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

def RV_BrMergingUnlikelyBias : Option<
    "riscv-br-merging-unlikely-bias",
    "Decreases 'riscv-br-merging-base-cost' in cases that it is unlikely "
    "that all conditionals will be executed. For example for merging "
    "the conditionals (a == b && c > d), if its known that a == b is "
    "unlikely, then it is unlikely that if the conditionals are split "
    "both sides will be executed, so it may be desirable to decrease "
    "the instruction cost threshold. Set to -1 to never merge unlikely "
    "branches.",
    "int"> {
  let Registry = RISCVOptsReg;
  let Default = "-1";
  let Hidden = 1;
}

// --- RISCVIndirectBranchTracking.cpp ---

def RV_PreferredLandingPadLabel : Option<
    "riscv-landing-pad-label",
    "Use preferred fixed label for all labels",
    "unsigned"> {
  let Default = "0";
  let Registry = RISCVOptsReg;
  let ReallyHidden = 1;
}

// --- RISCVInsertReadWriteCSR.cpp ---

def RV_DisableFRMInsertOpt : Option<
    "riscv-disable-frm-insert-opt",
    "Disable optimized frm insertion.",
    "bool"> {
  let Default = "false";
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

// --- RISCVInsertVSETVLI.cpp ---

def RV_EnsureWholeVectorRegisterMoveValidVTYPE : Option<
    "riscv-insert-vsetvli-whole-vector-register-move-valid-vtype",
    "Insert vsetvlis before vmvNr.vs to ensure vtype is valid and "
    "vill is cleared",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- RISCVInstrInfo.cpp ---

def RV_PreferWholeRegisterMove : Option<
    "riscv-prefer-whole-register-move",
    "Prefer whole register move for vector registers.",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

def MachineTraceStrategyLocal : EnumValue<"local",
    "Local strategy."> {
  let CppName = "TS_Local";
}

def MachineTraceStrategyMinInstrCount : EnumValue<"min-instr",
    "MinInstrCount strategy."> {
  let CppName = "TS_MinInstrCount";
}

def MachineTraceStrategyEnum : EnumType<"MachineTraceStrategy",
    [MachineTraceStrategyLocal, MachineTraceStrategyMinInstrCount]> {
  let External = 1;
}

def RV_ForceMachineCombinerStrategy : Option<
    "riscv-force-machine-combiner-strategy",
    "Force machine combiner to use a specific strategy for machine "
    "trace metrics evaluation.",
    "MachineTraceStrategy"> {
  let Registry = RISCVOptsReg;
  let Default = "MachineTraceStrategy::TS_NumStrategies";
  let Hidden = 1;
}

def RV_OutlinerEnableRegSave : Option<
    "riscv-outliner-regsave",
    "Enable RegSave strategy in machine outliner (save X5 to a "
    "temporary register when X5 is live across outlined calls).",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- RISCVLoadStoreOptimizer.cpp ---

def RV_LdStLimit : Option<
    "riscv-load-store-scan-limit", "",
    "unsigned"> {
  let Registry = RISCVOptsReg;
  let Default = "128";
  let Hidden = 1;
}

// --- RISCVOptWInstrs.cpp ---

def RV_DisableSExtWRemoval : Option<
    "riscv-disable-sextw-removal",
    "Disable removal of sext.w",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

def RV_DisableStripWSuffix : Option<
    "riscv-disable-strip-w-suffix",
    "Disable strip W suffix",
    "bool"> {
  let Default = "false";
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

// --- RISCVRegisterInfo.cpp ---

def RV_DisableCostPerUse : Option<
    "riscv-disable-cost-per-use", "",
    "bool"> {
  let Default = "false";
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

def RV_DisableRegAllocHints : Option<
    "riscv-disable-regalloc-hints",
    "Disable two address hints for register allocation",
    "bool"> {
  let Default = "false";
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

// --- RISCVSubtarget.cpp ---

def RV_DisableUsingConstantPoolForLargeInts : Option<
    "riscv-disable-using-constant-pool-for-large-ints",
    "Disable using constant pool for large integers.",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

def RV_MaxBuildIntsCost : Option<
    "riscv-max-build-ints-cost",
    "The maximum cost used for building integers.",
    "unsigned"> {
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

def RV_UseAA : Option<
    "riscv-use-aa",
    "Enable the use of AA during codegen.",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Default = "1";
}

def RV_SchedMispredictPenalty : Option<
    "riscv-sched-mispredict-penalty",
    "Override the mispredict penalty (in cycles) in the scheduler "
    "model. A non-negative value overrides the target default.",
    "unsigned"> {
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

def RV_SchedLoadLatency : Option<
    "riscv-sched-load-latency",
    "Override the load latency (in cycles) in the scheduler model. "
    "A non-negative value overrides the target default.",
    "unsigned"> {
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

def RV_MinimumJumpTableEntries : Option<
    "riscv-min-jump-table-entries",
    "Set minimum number of entries to use a jump table on RISCV",
    "unsigned"> {
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

def RV_UseMIPSLoadStorePairs : Option<
    "use-riscv-mips-load-store-pairs",
    "Enable the load/store pair optimization pass",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

def RV_UseMIPSCCMovInsn : Option<
    "use-riscv-mips-ccmov",
    "Use 'mips.ccmov' instruction",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- RISCVTargetMachine.cpp ---

def RV_EnableRedundantCopyElimination : Option<
    "riscv-enable-copyelim",
    "Enable the redundant copy elimination pass",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def RV_EnableGlobalMerge : Option<
    "riscv-enable-global-merge",
    "Enable the global merge pass",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

def RV_EnableMachineCombiner : Option<
    "riscv-enable-machine-combiner",
    "Enable the machine combiner pass",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def RV_VVectorBitsMax : Option<
    "riscv-v-vector-bits-max",
    "Assume V extension vector registers are at most this big, "
    "with zero meaning no maximum size is assumed.",
    "unsigned"> {
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

def RV_VVectorBitsMin : Option<
    "riscv-v-vector-bits-min",
    "Assume V extension vector registers are at least this big, "
    "with zero meaning no minimum size is assumed. A value of -1 "
    "means use Zvl*b extension. This is primarily used to enable "
    "autovectorization with fixed width vectors.",
    "int"> {
  let Registry = RISCVOptsReg;
  let Default = "-1";
  let Hidden = 1;
}

def RV_EnableCopyPropagation : Option<
    "riscv-enable-copy-propagation",
    "Enable the copy propagation with RISC-V copy instr",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def RV_EnableDeadRegisterElimination : Option<
    "riscv-enable-dead-defs",
    "Enable the pass that removes dead"
    " definitions and replaces stores to"
    " them with stores to x0",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def RV_EnableSinkFold : Option<
    "riscv-enable-sink-fold",
    "Enable sinking and folding of instruction copies",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def RV_EnableLoopDataPrefetch : Option<
    "riscv-enable-loop-data-prefetch",
    "Enable the loop data prefetch pass",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def RV_DisableVectorMaskMutation : Option<
    "riscv-disable-vector-mask-mutation",
    "Disable the vector mask scheduling mutation",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

def RV_EnableMachinePipeliner : Option<
    "riscv-enable-pipeliner",
    "Enable Machine Pipeliner for RISC-V",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

def RV_EnableCFIInstrInserter : Option<
    "riscv-enable-cfi-instr-inserter",
    "Enable CFI Instruction Inserter for RISC-V",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

def RV_EnableSelectOpt : Option<
    "riscv-select-opt",
    "Enable select to branch optimizations",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- RISCVTargetTransformInfo.cpp ---

def RV_RegisterWidthLMUL : Option<
    "riscv-v-register-bit-width-lmul",
    "The LMUL to use for getRegisterBitWidth queries. Affects LMUL used "
    "by autovectorized code. Fractional LMULs are not supported.",
    "unsigned"> {
  let Registry = RISCVOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def RV_SLPMaxVF : Option<
    "riscv-v-slp-max-vf",
    "Overrides result used for getMaximumVF query which is used "
    "exclusively by SLP vectorizer.",
    "unsigned"> {
  let Default = "0";
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

def RV_MinTripCount : Option<
    "riscv-v-min-trip-count",
    "Set the lower bound of a trip count to decide on "
    "vectorization while tail-folding.",
    "unsigned"> {
  let Registry = RISCVOptsReg;
  let Default = "5";
  let Hidden = 1;
}

def RV_EnableOrLikeSelectOpt : Option<
    "enable-riscv-or-like-select", "",
    "bool"> {
  let Registry = RISCVOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- RISCVZilsdOptimizer.cpp ---

def RV_DisableZilsdOpt : Option<
    "disable-riscv-zilsd-opt",
    "Disable Zilsd load/store optimization",
    "bool"> {
  let Default = "false";
  let Registry = RISCVOptsReg;
  let Hidden = 1;
}

def RV_ZilsdMaxRescheduleDistance : Option<
    "riscv-zilsd-max-reschedule-distance",
    "Maximum distance for rescheduling load/store instructions",
    "unsigned"> {
  let Registry = RISCVOptsReg;
  let Default = "10";
  let Hidden = 1;
}
