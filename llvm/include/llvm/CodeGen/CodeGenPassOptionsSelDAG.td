//===- CodeGenPassOptionsSelDAG.td - SelDAG option defs -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def CodeGenSelDAGOptions : OptionsStruct<"llvm"> {}

// =========================================================================
// Options — SelectionDAG/DAGCombiner.cpp
// =========================================================================

// std::optional<bool>: DAGCombiner needs to distinguish "left unset" (fall
// back to the subtarget's useAA() query) from "explicitly specified"
// (including explicitly false).
defm CGPASS_CombinerGlobalAliasAnalysis : OptionalValueField<
    "combiner-global-alias-analysis", "bool", "true",
    "Enable DAG combiner's use of IR alias analysis", /*hidden=*/1>;

// std::optional<bool>: DAGCombiner needs to distinguish "left unset" (fall
// back to the target lowering's useTopologicalSorting() query) from
// "explicitly specified" (including explicitly false).
defm CGPASS_CombinerTopologicalSorting : OptionalValueField<
    "combiner-topological-sorting", "bool", "true",
    "DAG combiner nodes consistently processed in topological order",
    /*hidden=*/1>;

// std::optional<std::string>: DAGCombiner needs to distinguish "left unset"
// (apply alias-analysis-based combines to every function) from "explicitly
// specified" (restrict to the named function, even if that name is the
// empty string).
defm CGPASS_CombinerAaOnlyFunc : OptionalValueField<
    "combiner-aa-only-func", "std::string", "\"\"",
    "Only use DAG-combiner alias analysis in this function">;

defm CGPASS_CombinerUseTbaa : BoolField<"combiner-use-tbaa", "true",
    "Enable DAG combiner's use of TBAA", /*hidden=*/1>;

defm CGPASS_CombinerStressLoadSlicing : BoolField<
    "combiner-stress-load-slicing", "false",
    "Bypass the profitability model of load slicing", /*hidden=*/1>;

defm CGPASS_CombinerSplitLoadIndex : BoolField<"combiner-split-load-index",
    "true", "DAG combiner may split indexing from loads", /*hidden=*/1>;

defm CGPASS_CombinerStoreMerging : BoolField<"combiner-store-merging",
    "true", "DAG combiner enable merging multiple stores into a wider "
    "store", /*hidden=*/1>;

defm CGPASS_CombinerTokenfactorInlineLimit : ValueField<
    "combiner-tokenfactor-inline-limit", "unsigned", "2048u",
    "Limit the number of operands to inline for Token Factors",
    /*hidden=*/1>;

defm CGPASS_CombinerStoreMergeDependenceLimit : ValueField<
    "combiner-store-merge-dependence-limit", "unsigned", "10u",
    "Limit the number of times for the same StoreNode and RootNode to "
    "bail out in store merging dependence check", /*hidden=*/1>;

defm CGPASS_CombinerReduceLoadOpStoreWidth : BoolField<
    "combiner-reduce-load-op-store-width", "true",
    "DAG combiner enable reducing the width of load/op/store sequence",
    /*hidden=*/1>;

defm CGPASS_CombinerReduceLoadOpStoreWidthForceNarrowingProfitable : BoolField<
    "combiner-reduce-load-op-store-width-force-narrowing-profitable",
    "false", "DAG combiner force override the narrowing profitable check "
    "when reducing the width of load/op/store sequences", /*hidden=*/1>;

defm CGPASS_CombinerShrinkLoadReplaceStoreWithStore : BoolField<
    "combiner-shrink-load-replace-store-with-store", "true",
    "DAG combiner enable load/<replace bytes>/store with a narrower store",
    /*hidden=*/1>;

defm CGPASS_CombinerDisabled : BoolField<"combiner-disabled", "false",
    "Disable the DAG combiner", /*hidden=*/1>;

// =========================================================================
// Options — SelectionDAG/LegalizeTypes.cpp
// =========================================================================

defm CGPASS_EnableLegalizeTypesChecking : BoolField<
    "enable-legalize-types-checking", "false", "", /*hidden=*/1>;

// =========================================================================
// Options — SelectionDAG/ResourcePriorityQueue.cpp
// =========================================================================

defm CGPASS_DisableDfaSched : BoolField<"disable-dfa-sched", "false",
    "Disable use of DFA during scheduling", /*hidden=*/1>;

defm CGPASS_DfaSchedRegPressureThreshold : ValueField<
    "dfa-sched-reg-pressure-threshold", "int", "5",
    "Track reg pressure and switch priority to in-depth", /*hidden=*/1>;

// =========================================================================
// Options — SelectionDAG/ScheduleDAGRRList.cpp
// =========================================================================

defm CGPASS_DisableSchedCycles : BoolField<"disable-sched-cycles", "false",
    "Disable cycle-level precision during preRA scheduling", /*hidden=*/1>;

defm CGPASS_DisableSchedRegPressure : BoolField<
    "disable-sched-reg-pressure", "false",
    "Disable regpressure priority in sched=list-ilp", /*hidden=*/1>;

defm CGPASS_DisableSchedLiveUses : BoolField<"disable-sched-live-uses",
    "true", "Disable live use priority in sched=list-ilp", /*hidden=*/1>;

defm CGPASS_DisableSchedVrcycle : BoolField<"disable-sched-vrcycle",
    "false", "Disable virtual register cycle interference checks",
    /*hidden=*/1>;

defm CGPASS_DisableSchedPhysregJoin : BoolField<
    "disable-sched-physreg-join", "false",
    "Disable physreg def-use affinity", /*hidden=*/1>;

defm CGPASS_DisableSchedStalls : BoolField<"disable-sched-stalls", "true",
    "Disable no-stall priority in sched=list-ilp", /*hidden=*/1>;

defm CGPASS_DisableSchedCriticalPath : BoolField<
    "disable-sched-critical-path", "false",
    "Disable critical path priority in sched=list-ilp", /*hidden=*/1>;

defm CGPASS_DisableSchedHeight : BoolField<"disable-sched-height", "false",
    "Disable scheduled-height priority in sched=list-ilp", /*hidden=*/1>;

defm CGPASS_Disable2addrHack : BoolField<"disable-2addr-hack", "true",
    "Disable scheduler's two-address hack", /*hidden=*/1>;

defm CGPASS_MaxSchedReorder : ValueField<"max-sched-reorder", "int", "6",
    "Number of instructions to allow ahead of the critical path in "
    "sched=list-ilp", /*hidden=*/1>;

defm CGPASS_SchedAvgIpc : ValueField<"sched-avg-ipc", "unsigned", "1u",
    "Average inst/cycle when no target itinerary exists.", /*hidden=*/1>;

// =========================================================================
// Options — SelectionDAG/ScheduleDAGSDNodes.cpp
// =========================================================================

defm CGPASS_SchedHighLatencyCycles : ValueField<
    "sched-high-latency-cycles", "int", "10",
    "Roughly estimate the number of cycles that 'long latency' "
    "instructions take for targets with no itinerary", /*hidden=*/1>;

// =========================================================================
// Options — SelectionDAG/SelectionDAG.cpp
// =========================================================================

defm CGPASS_EnableMemcpyDagOpt : BoolField<"enable-memcpy-dag-opt", "true",
    "Gang up loads and stores generated by inlining of memcpy",
    /*hidden=*/1>;

defm CGPASS_LdstmemcpyGlueMax : ValueField<"ldstmemcpy-glue-max", "int",
    "0", "Number limit for gluing ld/st of memcpy.", /*hidden=*/1>;

defm CGPASS_HasPredecessorMaxSteps : ValueField<
    "has-predecessor-max-steps", "unsigned", "8192u",
    "DAG combiner limit number of steps when searching DAG for "
    "predecessor nodes", /*hidden=*/1>;

defm CGPASS_VScaleUnrollLimit : ValueField<"vscale-unroll-limit", "int",
    "64", "Maximum vscale for which vector unrolling is allowed.",
    /*hidden=*/1>;

// =========================================================================
// Options — SelectionDAG/SelectionDAGBuilder.cpp
// =========================================================================

defm CGPASS_InsertAssertAlign : BoolField<"insert-assert-align", "true",
    "Insert the experimental `assertalign` node.", /*hidden=*/1>;

defm CGPASS_LimitFloatPrecision : ValueField<"limit-float-precision",
    "unsigned", "0",
    "Generate low-precision inline sequences for some float libcalls",
    /*hidden=*/1>;

defm CGPASS_SwitchPeelThreshold : ValueField<"switch-peel-threshold",
    "unsigned", "66u",
    "Set the case probability threshold for peeling the case from a "
    "switch statement. A value greater than 100 will void t...",
    /*hidden=*/1>;

// =========================================================================
// Options — SelectionDAG/SelectionDAGDumper.cpp
// =========================================================================

defm CGPASS_DagDumpVerbose : BoolField<"dag-dump-verbose", "false",
    "Display more information when dumping selection DAG nodes.",
    /*hidden=*/1>;

defm CGPASS_PrintSdnodeAddrs : BoolField<"print-sdnode-addrs", "false",
    "Print addresses of SDNodes when dumping", /*hidden=*/1>;

// =========================================================================
// Options — SelectionDAG/SelectionDAGISel.cpp
// =========================================================================

defm CGPASS_FastIselAbort : ValueField<"fast-isel-abort", "int", "0",
    "Enable abort calls when 'fast' instruction selection fails to lower "
    "an instruction: 0 disable the abort, 1 will abort...", /*hidden=*/1>;

defm CGPASS_FastIselReportOnFallback : BoolField<
    "fast-isel-report-on-fallback", "false",
    "Emit a diagnostic when 'fast' instruction selection falls back to "
    "SelectionDAG.", /*hidden=*/1>;

defm CGPASS_UseMbpi : BoolField<"use-mbpi", "true",
    "use Machine Branch Probability Info", /*hidden=*/1>;

defm CGPASS_DumpSortedDags : BoolField<"dump-sorted-dags", "false",
    "Print DAGs with sorted nodes in debug dump", /*hidden=*/1>;

defm CGPASS_FilterViewDags : ValueField<"filter-view-dags", "std::string",
    "", "Only display the basic block whose name matches this for all "
    "view-*-dags options", /*hidden=*/1>;

defm CGPASS_ViewDagCombine1Dags : BoolField<"view-dag-combine1-dags",
    "false", "Pop up a window to show dags before the first dag combine "
    "pass", /*hidden=*/1>;

defm CGPASS_ViewLegalizeTypesDags : BoolField<"view-legalize-types-dags",
    "false", "Pop up a window to show dags before legalize types",
    /*hidden=*/1>;

defm CGPASS_ViewDagCombineLtDags : BoolField<"view-dag-combine-lt-dags",
    "false", "Pop up a window to show dags before the post legalize "
    "types dag combine pass", /*hidden=*/1>;

defm CGPASS_ViewLegalizeDags : BoolField<"view-legalize-dags", "false",
    "Pop up a window to show dags before legalize", /*hidden=*/1>;

defm CGPASS_ViewDagCombine2Dags : BoolField<"view-dag-combine2-dags",
    "false", "Pop up a window to show dags before the second dag combine "
    "pass", /*hidden=*/1>;

defm CGPASS_ViewIselDags : BoolField<"view-isel-dags", "false",
    "Pop up a window to show isel dags as they are selected",
    /*hidden=*/1>;

defm CGPASS_ViewSchedDags : BoolField<"view-sched-dags", "false",
    "Pop up a window to show sched dags as they are processed",
    /*hidden=*/1>;

defm CGPASS_ViewSunitDags : BoolField<"view-sunit-dags", "false",
    "Pop up a window to show SUnit dags after they are processed",
    /*hidden=*/1>;

// =========================================================================
// Options — SelectionDAG/StatepointLowering.cpp
// =========================================================================

defm CGPASS_UseRegistersForDeoptValues : BoolField<
    "use-registers-for-deopt-values", "false",
    "Allow using registers for non pointer deopt args", /*hidden=*/1>;

defm CGPASS_UseRegistersForGcValuesInLandingPad : BoolField<
    "use-registers-for-gc-values-in-landing-pad", "false",
    "Allow using registers for gc pointer in landing pad", /*hidden=*/1>;

defm CGPASS_MaxRegistersForGcValues : ValueField<
    "max-registers-for-gc-values", "unsigned", "0u",
    "Max number of VRegs allowed to pass GC pointer meta args in",
    /*hidden=*/1>;
