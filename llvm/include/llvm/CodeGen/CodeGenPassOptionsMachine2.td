//===- CodeGenPassOptionsMachine2.td - Machine2 option defs -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def CodeGenMachine2Options : OptionsStruct<"llvm"> {}

// =========================================================================
// Enum: MISchedDirection (MachineScheduler.cpp) -- reuses the existing
// llvm::MISched::Direction enum (MachineScheduler.h), not a new type.
// =========================================================================

def MISchedDirectionEnum : OptionEnum<"llvm::MISched::Direction", [
    EnumMember<"Unspecified", "__unspecified">,
    EnumMember<"TopDown", "topdown">,
    EnumMember<"BottomUp", "bottomup">,
    EnumMember<"Bidirectional", "bidirectional">]> {
  let External = 1;
  let Ident = "MISchedDirection";
}

// =========================================================================
// Enum: WindowSchedulingFlag (MachinePipeliner.cpp) -- reuses the existing
// llvm::WindowSchedulingFlag enum (WindowScheduler.h), not a new type.
// =========================================================================

def WindowSchedulingFlagEnum : OptionEnum<"llvm::WindowSchedulingFlag", [
    EnumMember<"WS_Off", "off">,
    EnumMember<"WS_On", "on">,
    EnumMember<"WS_Force", "force">]> {
  let External = 1;
  let Ident = "WindowSchedulingFlag";
}

// =========================================================================
// Options — MachineOutliner.cpp
// =========================================================================

defm CGPASS_OutlinerLeafDescendants : BoolField<"outliner-leaf-descendants",
    "true", "Consider all leaf descendants of internal nodes of the suffix "
    "tree as candidates for outlining (if false, only leaf c...",
    /*hidden=*/1>;

defm CGPASS_DisableGlobalOutlining : BoolField<"disable-global-outlining",
    "false", "Disable global outlining only by ignoring the codegen data "
    "generation or use", /*hidden=*/1>;

defm CGPASS_AppendContentHashOutlinedName : BoolField<
    "append-content-hash-outlined-name", "true",
    "This appends the content hash to the globally outlined function name. "
    "It's beneficial for enhancing the precision of ...", /*hidden=*/1>;

// =========================================================================
// Options — MachinePipeliner.cpp
// =========================================================================

defm CGPASS_EnablePipeliner : BoolField<"enable-pipeliner", "true",
    "Enable Software Pipelining", /*hidden=*/1>;

// std::optional<bool>: MachinePipeliner needs to distinguish "left unset"
// (fall back to -Os/-Oz on the current function) from "explicitly
// specified" (including explicitly false).
defm CGPASS_EnablePipelinerOptSize : OptionalValueField<
    "enable-pipeliner-opt-size", "bool", "true", "Enable SWP at Os.",
    /*hidden=*/1>;

defm CGPASS_PipelinerMaxMii : ValueField<"pipeliner-max-mii", "int", "27",
    "Size limit for the MII.", /*hidden=*/1>;

defm CGPASS_PipelinerForceIi : ValueField<"pipeliner-force-ii", "int", "-1",
    "Force pipeliner to use specified II.", /*hidden=*/1>;

defm CGPASS_PipelinerMaxStages : ValueField<"pipeliner-max-stages", "int",
    "3", "Maximum stages allowed in the generated scheduled.", /*hidden=*/1>;

defm CGPASS_PipelinerPruneDeps : BoolField<"pipeliner-prune-deps", "true",
    "Prune dependences between unrelated Phi nodes.", /*hidden=*/1>;

defm CGPASS_PipelinerPruneLoopCarried : BoolField<
    "pipeliner-prune-loop-carried", "true",
    "Prune loop carried order dependences.", /*hidden=*/1>;

defm CGPASS_PipelinerMax : ValueField<"pipeliner-max", "int", "-1",
    "Limit pipeliner to N loops">;

defm CGPASS_PipelinerIgnoreRecmii : BoolField<"pipeliner-ignore-recmii",
    "false", "Ignore RecMII">;

defm CGPASS_PipelinerShowMask : BoolField<"pipeliner-show-mask", "false",
    "Show pipeliner resource mask", /*hidden=*/1>;

defm CGPASS_PipelinerDbgRes : BoolField<"pipeliner-dbg-res", "false",
    "Debug pipeliner resources", /*hidden=*/1>;

defm CGPASS_PipelinerAnnotateForTesting : BoolField<
    "pipeliner-annotate-for-testing", "false",
    "Instead of emitting the pipelined code, annotate instructions with "
    "the generated schedule for feeding into the -modul...", /*hidden=*/1>;

defm CGPASS_PipelinerExperimentalCg : BoolField<"pipeliner-experimental-cg",
    "false", "Use the experimental peeling code generator for software "
    "pipelining", /*hidden=*/1>;

defm CGPASS_PipelinerIiSearchRange : ValueField<
    "pipeliner-ii-search-range", "int", "10", "Range to search for II",
    /*hidden=*/1>;

// std::optional<bool>: MachinePipeliner needs to distinguish "left unset"
// (fall back to a target/heuristic-computed default) from "explicitly
// specified" (including explicitly false).
defm CGPASS_PipelinerRegisterPressure : OptionalValueField<
    "pipeliner-register-pressure", "bool", "true",
    "Limit register pressure of scheduled loop", /*hidden=*/1>;

defm CGPASS_PipelinerRegisterPressureMargin : ValueField<
    "pipeliner-register-pressure-margin", "int", "5",
    "Margin representing the unused percentage of the register pressure "
    "limit", /*hidden=*/1>;

defm CGPASS_PipelinerMveCg : BoolField<"pipeliner-mve-cg", "false",
    "Use the MVE code generator for software pipelining", /*hidden=*/1>;

defm CGPASS_PipelinerMaxNumStores : ValueField<"pipeliner-max-num-stores",
    "unsigned", "200u",
    "Maximum number of stores allowed in the target loop.", /*hidden=*/1>;

// =========================================================================
// Options — MachineRegisterInfo.cpp
// =========================================================================

// std::optional<bool>: MachineRegisterInfo needs to distinguish "left
// unset" (fall back to the subtarget's enableSubRegLiveness() query) from
// "explicitly specified" (including explicitly false).
defm CGPASS_EnableSubregLiveness : OptionalValueField<
    "enable-subreg-liveness", "bool", "true",
    "Enable subregister liveness tracking.", /*hidden=*/1>;

// =========================================================================
// Options — MachineScheduler.cpp
// =========================================================================

// std::optional<bool>: MachineScheduler needs to distinguish "left unset"
// (fall back to the subtarget's enableMachineScheduler() query) from
// "explicitly specified" (including explicitly false).
defm CGPASS_EnableMisched : OptionalValueField<"enable-misched", "bool",
    "true", "Enable the machine instruction scheduling pass.",
    /*hidden=*/1>;

// std::optional<bool>: MachineScheduler needs to distinguish "left unset"
// (fall back to the subtarget's enablePostRAMachineScheduler() query) from
// "explicitly specified" (including explicitly false).
defm CGPASS_EnablePostMisched : OptionalValueField<"enable-post-misched",
    "bool", "true", "Enable the post-ra machine instruction scheduling "
    "pass.", /*hidden=*/1>;

// std::optional<std::string>: MachineScheduler needs to distinguish "left
// unset" (schedule every function) from "explicitly specified" (restrict
// scheduling to the named function, even if that name is the empty
// string).
defm CGPASS_MischedOnlyFunc : OptionalValueField<"misched-only-func",
    "std::string", "\"\"", "Only schedule this function">;

// std::optional<unsigned>: MachineScheduler needs to distinguish "left
// unset" (schedule every block) from "explicitly specified" (restrict
// scheduling to the named MBB#, even if that number is 0).
defm CGPASS_MischedOnlyBlock : OptionalValueField<"misched-only-block",
    "unsigned", "0u", "Only schedule this MBB#">;

defm CGPASS_ViewMischedCutoff : ValueField<"view-misched-cutoff",
    "unsigned", "0u",
    "Hide nodes with more predecessor/successor than cutoff">;

defm CGPASS_MischedCutoff : ValueField<"misched-cutoff", "unsigned", "~0U",
    "Stop scheduling after N instructions">;

defm CGPASS_MischedDumpReservedCycles : BoolField<
    "misched-dump-reserved-cycles", "false",
    "Dump resource usage at schedule boundary.">;

defm CGPASS_MischedDetailResourceBooking : BoolField<
    "misched-detail-resource-booking", "false",
    "Show details of invoking getNextResoufceCycle.">;

defm CGPASS_MischedDcpl : BoolField<"misched-dcpl", "false",
    "Print critical path length to stdout", /*hidden=*/1>;

defm CGPASS_MischedLimit : ValueField<"misched-limit", "unsigned", "256u",
    "Limit ready list to N instructions", /*hidden=*/1>;

defm CGPASS_MischedRegpressure : BoolField<"misched-regpressure", "true",
    "Enable register pressure scheduling.", /*hidden=*/1>;

defm CGPASS_MischedCyclicpath : BoolField<"misched-cyclicpath", "true",
    "Enable cyclic critical path analysis.", /*hidden=*/1>;

defm CGPASS_MischedCluster : BoolField<"misched-cluster", "true",
    "Enable memop clustering.", /*hidden=*/1>;

defm CGPASS_ForceFastCluster : BoolField<"force-fast-cluster", "false",
    "Switch to fast cluster algorithm with the lost of some fusion "
    "opportunities", /*hidden=*/1>;

defm CGPASS_FastClusterThreshold : ValueField<"fast-cluster-threshold",
    "unsigned", "1000u", "The threshold for fast cluster", /*hidden=*/1>;

defm CGPASS_MischedDumpScheduleTrace : BoolField<
    "misched-dump-schedule-trace", "false",
    "Dump resource usage at schedule boundary.">;

defm CGPASS_MischedDumpScheduleTraceColHeaderWidth : ValueField<
    "misched-dump-schedule-trace-col-header-width", "unsigned", "19u",
    "Set width of the columns with the resources and schedule units">;

defm CGPASS_MischedDumpScheduleTraceColWidth : ValueField<
    "misched-dump-schedule-trace-col-width", "unsigned", "5u",
    "Set width of the columns showing resource booking.">;

defm CGPASS_MischedSortResourcesInTrace : BoolField<
    "misched-sort-resources-in-trace", "true",
    "Sort the resources printed in the dump trace">;

defm CGPASS_MischedResourceCutoff : ValueField<"misched-resource-cutoff",
    "unsigned", "10u", "Number of intervals to track", /*hidden=*/1>;

// =========================================================================
// Options — MachineSink.cpp
// =========================================================================

defm CGPASS_MachineSinkSplit : BoolField<"machine-sink-split", "true",
    "Split critical edges during machine sinking", /*hidden=*/1>;

defm CGPASS_MachineSinkBfi : BoolField<"machine-sink-bfi", "true",
    "Use block frequency info to find successors to sink", /*hidden=*/1>;

defm CGPASS_MachineSinkSplitProbabilityThreshold : ValueField<
    "machine-sink-split-probability-threshold", "unsigned", "40u",
    "Percentage threshold for splitting single-instruction critical edge. "
    "If the branch threshold is higher than this thre...", /*hidden=*/1>;

defm CGPASS_MachineSinkLoadInstrsThreshold : ValueField<
    "machine-sink-load-instrs-threshold", "unsigned", "2000u",
    "Do not try to find alias store for a load if there is a in-path "
    "block whose instruction number is higher than this th...",
    /*hidden=*/1>;

defm CGPASS_MachineSinkLoadBlocksThreshold : ValueField<
    "machine-sink-load-blocks-threshold", "unsigned", "20u",
    "Do not try to find alias store for a load if the block number in "
    "the straight line is higher than this threshold.", /*hidden=*/1>;

defm CGPASS_SinkInstsToAvoidSpills : BoolField<
    "sink-insts-to-avoid-spills", "false",
    "Sink instructions into cycles to avoid register spills",
    /*hidden=*/1>;

defm CGPASS_MachineSinkCycleLimit : ValueField<"machine-sink-cycle-limit",
    "unsigned", "50u",
    "The maximum number of instructions considered for cycle sinking.",
    /*hidden=*/1>;

// =========================================================================
// Options — MachineStripDebug.cpp
// =========================================================================

defm CGPASS_MirStripDebugifyOnly : BoolField<"mir-strip-debugify-only",
    "true", "Should mir-strip-debug only strip debug info from "
    "debugified modules by default">;

// =========================================================================
// Options — MacroFusion.cpp
// =========================================================================

defm CGPASS_MischedFusion : BoolField<"misched-fusion", "true",
    "Enable scheduling for macro fusion.", /*hidden=*/1>;

// =========================================================================
// Options — MachineScheduler.cpp (enum options)
// =========================================================================

defm CGPASS_MischedPreraDirection : EnumField<"misched-prera-direction",
    MISchedDirectionEnum, "llvm::MISched::Direction::Unspecified",
    "Machine instruction scheduling direction for pre-RA scheduler",
    /*hidden=*/1>;

defm CGPASS_MischedPostraDirection : EnumField<"misched-postra-direction",
    MISchedDirectionEnum, "llvm::MISched::Direction::Unspecified",
    "Machine instruction scheduling direction for post-RA scheduler",
    /*hidden=*/1>;

// =========================================================================
// Options — MachinePipeliner.cpp (enum option)
// =========================================================================

defm CGPASS_WindowSched : EnumField<"window-sched", WindowSchedulingFlagEnum,
    "llvm::WindowSchedulingFlag::WS_On", "Window scheduling flag",
    /*hidden=*/1>;
