//===- CodeGenPassOptionsRegAlloc.td - RegAlloc option defs -*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def CodeGenRegAllocOptions : OptionsStruct<"llvm"> {}

// =========================================================================
// Enum: EvictionAdvisorMode (RegAllocEvictionAdvisor.cpp)
// =========================================================================

def EvictionAdvisorModeEnum : OptionEnum<"EvictionAdvisorMode", [
    EnumMember<"Default", "default">,
    EnumMember<"Release", "release">,
    EnumMember<"Development", "development">]>;

// =========================================================================
// Enum: PriorityAdvisorMode (RegAllocPriorityAdvisor.cpp)
// =========================================================================

def PriorityAdvisorModeEnum : OptionEnum<"PriorityAdvisorMode", [
    EnumMember<"Default", "default">,
    EnumMember<"Release", "release">,
    EnumMember<"Development", "development">,
    EnumMember<"Dummy", "dummy">]>;

// =========================================================================
// Options — InlineSpiller.cpp
// =========================================================================

defm CGPASS_RestrictStatepointRemat : BoolField<"restrict-statepoint-remat",
    "false", "Restrict remat for statepoint operands", /*hidden=*/1>;

// =========================================================================
// Options — RegAllocBase.cpp
// =========================================================================

defm CGPASS_VerifyRegalloc : BoolField<"verify-regalloc", "false",
    "Verify during register allocation", /*hidden=*/1>;

// =========================================================================
// Options — RegAllocEvictionAdvisor.cpp
// =========================================================================

// std::optional<bool>, not a plain BoolField: RegAllocEvictionAdvisor.cpp
// needs to distinguish "left unset" (fall back to the subtarget default)
// from "explicitly specified" (even when explicitly false, which must
// override the subtarget default).
defm CGPASS_EnableLocalReassign : OptionalValueField<"enable-local-reassign",
    "bool", "true",
    "Local reassignment can yield better allocation decisions, but may be "
    "compile time intensive", /*hidden=*/1>;

defm CGPASS_RegallocEvictionMaxInterferenceCutoff : ValueField<
    "regalloc-eviction-max-interference-cutoff", "unsigned", "10u",
    "Number of interferences after which we declare an interference "
    "unevictable and bail out.", /*hidden=*/1>;

// =========================================================================
// Options — RegAllocFast.cpp
// =========================================================================

defm CGPASS_RafastIgnoreMissingDefs : BoolField<"rafast-ignore-missing-defs",
    "false", "", /*hidden=*/1>;

// =========================================================================
// Options — RegAllocGreedy.cpp
// =========================================================================

// std::optional<unsigned>: unspecified falls back to a target-computed
// default (TRI->getCSRCost()/getCSRCostScale()), which an always-present
// field couldn't distinguish from an explicit 0/80.
defm CGPASS_RegallocCsrFirstTimeCost : OptionalValueField<
    "regalloc-csr-first-time-cost", "unsigned", "0u",
    "Cost for first time use of callee-saved register.", /*hidden=*/1>;

defm CGPASS_RegallocCsrCostScale : OptionalValueField<
    "regalloc-csr-cost-scale", "unsigned", "80u",
    "Scale for the callee-saved register cost, in percentage.",
    /*hidden=*/1>;

// std::optional<bool>: unspecified falls back to a target query
// (TRI->regClassPriorityTrumpsGlobalness()/reverseLocalAssignment()).
defm CGPASS_GreedyRegclassPriorityTrumpsGlobalness : OptionalValueField<
    "greedy-regclass-priority-trumps-globalness", "bool", "true",
    "Change the greedy register allocator's live range priority calculation "
    "to make the AllocationPriority of the register...", /*hidden=*/1>;

defm CGPASS_GreedyReverseLocalAssignment : OptionalValueField<
    "greedy-reverse-local-assignment", "bool", "true",
    "Reverse allocation order of local live ranges, such that shorter local "
    "live ranges will tend to be allocated first", /*hidden=*/1>;

defm CGPASS_LcrMaxDepth : ValueField<"lcr-max-depth", "unsigned", "5u",
    "Last chance recoloring max depth", /*hidden=*/1>;

defm CGPASS_LcrMaxInterf : ValueField<"lcr-max-interf", "unsigned", "8u",
    "Last chance recoloring maximum number of considered interference "
    "at a time", /*hidden=*/1>;

defm CGPASS_ExhaustiveRegisterSearch : BoolField<
    "exhaustive-register-search", "false",
    "Exhaustive Search for registers bypassing the depth and interference "
    "cutoffs of last chance recoloring", /*hidden=*/1>;

defm CGPASS_GrowRegionComplexityBudget : ValueField<
    "grow-region-complexity-budget", "uint64_t", "10000",
    "growRegion() does not scale with the number of BB edges, so limit its "
    "budget and bail out once we reach the limit.", /*hidden=*/1>;

defm CGPASS_SplitThresholdForRegWithHint : ValueField<
    "split-threshold-for-reg-with-hint", "unsigned", "75u",
    "The threshold for splitting a virtual register with a hint, in "
    "percentage", /*hidden=*/1>;

// =========================================================================
// Options — RegAllocPBQP.cpp
// =========================================================================

defm CGPASS_PbqpCoalescing : BoolField<"pbqp-coalescing", "false",
    "Attempt coalescing during PBQP register allocation.", /*hidden=*/1>;

defm CGPASS_PbqpDumpGraphs : BoolField<"pbqp-dump-graphs", "false",
    "Dump graphs for each function/round in the compilation unit.",
    /*hidden=*/1>;

// =========================================================================
// Options — RegAllocScore.cpp
// =========================================================================

defm CGPASS_RegallocCopyWeight : ValueField<"regalloc-copy-weight", "double",
    "0.2", "", /*hidden=*/1>;

defm CGPASS_RegallocLoadWeight : ValueField<"regalloc-load-weight", "double",
    "4.0", "", /*hidden=*/1>;

defm CGPASS_RegallocStoreWeight : ValueField<"regalloc-store-weight",
    "double", "1.0", "", /*hidden=*/1>;

defm CGPASS_RegallocCheapRematWeight : ValueField<
    "regalloc-cheap-remat-weight", "double", "0.2", "", /*hidden=*/1>;

defm CGPASS_RegallocExpensiveRematWeight : ValueField<
    "regalloc-expensive-remat-weight", "double", "1.0", "", /*hidden=*/1>;

// =========================================================================
// Options — RegisterClassInfo.cpp
// =========================================================================

defm CGPASS_StressRegalloc : ValueField<"stress-regalloc", "unsigned", "0u",
    "Limit all regclasses to N registers", /*hidden=*/1>;

// =========================================================================
// Options — RegisterCoalescer.cpp
// =========================================================================

defm CGPASS_JoinLiveintervals : BoolField<"join-liveintervals", "true",
    "Coalesce copies (default=true)", /*hidden=*/1>;

defm CGPASS_TerminalRule : BoolField<"terminal-rule", "true",
    "Apply the terminal rule", /*hidden=*/1>;

defm CGPASS_JoinSplitedges : BoolField<"join-splitedges", "false",
    "Coalesce copies on split edges (default=subtarget)", /*hidden=*/1>;

defm CGPASS_VerifyCoalescing : BoolField<"verify-coalescing", "false",
    "Verify machine instrs before and after register coalescing",
    /*hidden=*/1>;

defm CGPASS_LateRematUpdateThreshold : ValueField<
    "late-remat-update-threshold", "unsigned", "100u",
    "During rematerialization for a copy, if the def instruction has many "
    "other copy uses to be rematerialized, delay the ...", /*hidden=*/1>;

defm CGPASS_LargeIntervalSizeThreshold : ValueField<
    "large-interval-size-threshold", "unsigned", "100u",
    "If the valnos size of an interval is larger than the threshold, it is "
    "regarded as a large interval.", /*hidden=*/1>;

defm CGPASS_LargeIntervalFreqThreshold : ValueField<
    "large-interval-freq-threshold", "unsigned", "256u",
    "For a large interval, if it is coalesced with other live intervals "
    "many times more than the threshold, stop its coale...", /*hidden=*/1>;

defm CGPASS_JoinGlobalcopies : OptionalValueField<"join-globalcopies", "bool",
    "true", "Coalesce copies that span blocks (default=subtarget)",
    /*hidden=*/1>;

// =========================================================================
// Options — SplitKit.cpp
// =========================================================================

defm CGPASS_EnableSplitLoopivHeuristic : BoolField<
    "enable-split-loopiv-heuristic", "true",
    "Enable loop iv regalloc heuristic">;

// =========================================================================
// Options — RegAllocEvictionAdvisor.cpp (enum option)
// =========================================================================

defm CGPASS_RegallocEnableAdvisor : EnumField<
    "regalloc-enable-advisor", EvictionAdvisorModeEnum,
    "EvictionAdvisorMode::Default", "Enable regalloc advisor mode",
    /*hidden=*/1>;

// =========================================================================
// Options — RegAllocPriorityAdvisor.cpp (enum option)
// =========================================================================

defm CGPASS_RegallocEnablePriorityAdvisor : EnumField<
    "regalloc-enable-priority-advisor", PriorityAdvisorModeEnum,
    "PriorityAdvisorMode::Default", "Enable regalloc priority advisor mode",
    /*hidden=*/1>;
