//===- CodeGenPassOptionsSched1.td - Sched1 option defs -*- tablegen -*-=====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def CodeGenSched1Options : OptionsStruct<"llvm"> {}

// =========================================================================
// Enum: CGRunOutliner (TargetPassConfig.cpp)
// =========================================================================

// TargetDefault is a sentinel (never matched by real command-line input --
// its "" spelling only appears as the field's own default expression) that
// lets the consumer detect "the user didn't specify this option at all",
// mirroring the old clv2::wasOptSpecified() check that used to gate whether
// CGPassBuilderOption::EnableMachineOutliner got overwritten.
def CGRunOutlinerEnum : OptionEnum<"CGRunOutliner", [
    EnumMember<"TargetDefault", "">,
    EnumMember<"AlwaysOutline", "always">,
    EnumMember<"OptimisticPGO", "optimistic-pgo">,
    EnumMember<"ConservativePGO", "conservative-pgo">,
    EnumMember<"NeverOutline", "never">]>;

// =========================================================================
// Enum: CGGlobalISelAbortMode (TargetPassConfig.cpp)
// =========================================================================

// TargetDefault is a sentinel, not a real spelling (the old clv2 option had
// no bare-flag/ValueOptional form -- only "=0"/"=1"/"=2" were ever accepted).
// It lets the consumer distinguish "not specified" from "specified", which
// is load-bearing: TargetPassConfig::getCGPassBuilderOption() and its
// constructor only assign into CGPassBuilderOption::EnableGlobalISelAbort /
// TM.Options.GlobalISelAbort when the option was explicitly specified,
// otherwise leaving a target's own already-configured GlobalISelAbort value
// (e.g. AArch64TargetMachine's constructor calls
// setGlobalISelAbort(GlobalISelAbortMode::Disable)) untouched. A plain,
// always-present EnumField here would silently clobber that with the
// schema's default on every read.
def CGGlobalISelAbortModeEnum : OptionEnum<"CGGlobalISelAbortMode", [
    EnumMember<"TargetDefault", "">,
    EnumMember<"Disable", "0">,
    EnumMember<"Enable", "1">,
    EnumMember<"DisableWithDiag", "2">]>;

// =========================================================================
// Options — AggressiveAntiDepBreaker.cpp
// =========================================================================

defm CGPASS_AggAntidepDebugdiv : ValueField<"agg-antidep-debugdiv", "int",
    "0", "", /*hidden=*/1>;

defm CGPASS_AggAntidepDebugmod : ValueField<"agg-antidep-debugmod", "int",
    "0", "", /*hidden=*/1>;

// =========================================================================
// Options — DFAPacketizer.cpp
// =========================================================================

// std::optional<unsigned>: the old clv2 option used wasOptSpecified() to
// distinguish "not given" (no limit) from an explicit "0" (stop immediately),
// which a plain always-present field couldn't represent.
defm CGPASS_DfaInstrLimit : OptionalValueField<"dfa-instr-limit", "unsigned",
    "0u", "If present, stops packetizing after N instructions",
    /*hidden=*/1>;

// =========================================================================
// Options — ModuloSchedule.cpp
// =========================================================================

defm CGPASS_PipelinerSwapBranchTargetsMve : BoolField<
    "pipeliner-swap-branch-targets-mve", "false",
    "Swap target blocks of a conditional branch for MVE expander",
    /*hidden=*/1>;

// =========================================================================
// Options — PostRASchedulerList.cpp
// =========================================================================

// Not BoolField<>/ValueField<>: both fields here are read through
// wasOptSpecified() in PostRASchedulerList.cpp, falling back to a
// target/subtarget query (ST.enablePostRAScheduler() /
// Subtarget.getAntiDepBreakMode()) when unspecified -- a plain
// always-present field couldn't distinguish "unspecified" from an explicit
// value equal to the .td default. Hand-written (rather than
// OptionalValueField<>) to keep the original spellings: the bool keeps its
// "-post-RA-scheduler" / "-no-post-RA-scheduler" / "-post-RA-scheduler=bool"
// trio (mirroring BoolField's shape), and the string keeps its Joined +
// Separate ("-break-anti-dependencies value") trio (mirroring ValueField's
// shape), neither of which OptionalValueField<> would preserve.
def CGPASS_PostRaScheduler : Flag<["-", "--"], "post-RA-scheduler">,
    HelpText<"Enable scheduling after register allocation">,
    Flags<[HelpHidden]>,
    Field<"std::optional<bool>", "CGPASS_PostRaScheduler", "std::nullopt">,
    Assign<"CGPASS_PostRaScheduler", "true">;
def CGPASS_PostRaScheduler_no : Flag<["-", "--"], "no-post-RA-scheduler">,
    Flags<[HelpHidden]>, Assign<"CGPASS_PostRaScheduler", "false">;
def CGPASS_PostRaScheduler_eq : Joined<["-", "--"], "post-RA-scheduler=">,
    Flags<[HelpHidden]>, BoolAssign<"CGPASS_PostRaScheduler">;

def CGPASS_BreakAntiDependencies : Joined<["-", "--"],
    "break-anti-dependencies=">,
    HelpText<"Break post-RA scheduling anti-dependencies: 'critical', 'all', "
             "or 'none'">,
    Flags<[HelpHidden]>,
    Field<"std::optional<std::string>", "CGPASS_BreakAntiDependencies",
          "std::nullopt">;
def CGPASS_BreakAntiDependencies_sep : Separate<["-", "--"],
    "break-anti-dependencies">, Flags<[HelpHidden]>,
    Alias<CGPASS_BreakAntiDependencies>;

defm CGPASS_PostraSchedDebugdiv : ValueField<"postra-sched-debugdiv", "int",
    "0", "Debug control MBBs that are scheduled", /*hidden=*/1>;

defm CGPASS_PostraSchedDebugmod : ValueField<"postra-sched-debugmod", "int",
    "0", "Debug control MBBs that are scheduled", /*hidden=*/1>;

// =========================================================================
// Options — ScheduleDAG.cpp
// =========================================================================

defm CGPASS_StressSched : BoolField<"stress-sched", "false",
    "Stress test instruction scheduling", /*hidden=*/1>;

// =========================================================================
// Options — ScheduleDAGInstrs.cpp
// =========================================================================

// Not BoolField<>: read through wasOptSpecified() in ScheduleDAGInstrs.cpp,
// falling back to a target query (ST.useAA()) when unspecified -- a plain
// always-present field couldn't distinguish "unspecified" from an explicit
// "false". Hand-written (rather than OptionalValueField<>) to keep the
// original "-enable-aa-sched-mi" / "-no-enable-aa-sched-mi" /
// "-enable-aa-sched-mi=bool" trio, mirroring BoolField's shape (see the
// analogous CGPASS_PostRaScheduler comment above, in the
// PostRASchedulerList.cpp section of this file).
def CGPASS_EnableAaSchedMi : Flag<["-", "--"], "enable-aa-sched-mi">,
    HelpText<"Enable use of AA during MI DAG construction">,
    Flags<[HelpHidden]>,
    Field<"std::optional<bool>", "CGPASS_EnableAaSchedMi", "std::nullopt">,
    Assign<"CGPASS_EnableAaSchedMi", "true">;
def CGPASS_EnableAaSchedMi_no : Flag<["-", "--"], "no-enable-aa-sched-mi">,
    Flags<[HelpHidden]>, Assign<"CGPASS_EnableAaSchedMi", "false">;
def CGPASS_EnableAaSchedMi_eq : Joined<["-", "--"], "enable-aa-sched-mi=">,
    Flags<[HelpHidden]>, BoolAssign<"CGPASS_EnableAaSchedMi">;

defm CGPASS_UseTbaaInSchedMi : BoolField<"use-tbaa-in-sched-mi", "true",
    "Enable use of TBAA during MI DAG construction", /*hidden=*/1>;

defm CGPASS_Schedmodel : BoolField<"schedmodel", "true",
    "Use TargetSchedModel for latency lookup", /*hidden=*/1>;

defm CGPASS_Scheditins : BoolField<"scheditins", "true",
    "Use InstrItineraryData for latency lookup", /*hidden=*/1>;

defm CGPASS_DagMapsHugeRegion : ValueField<"dag-maps-huge-region", "unsigned",
    "500u",
    "The limit to use while constructing the DAG prior to scheduling, at "
    "which point a trade-off is made to avoid excessiv...", /*hidden=*/1>;

defm CGPASS_SchedPrintCycles : BoolField<"sched-print-cycles", "false",
    "Report top/bottom cycles when dumping SUnit instances", /*hidden=*/1>;

// =========================================================================
// Options — TargetInstrInfo.cpp
// =========================================================================

defm CGPASS_DisableSchedHazard : BoolField<"disable-sched-hazard", "false",
    "Disable hazard detection during preRA scheduling", /*hidden=*/1>;

defm CGPASS_AccReassoc : BoolField<"acc-reassoc", "true",
    "Enable reassociation of accumulation chains", /*hidden=*/1>;

defm CGPASS_AccMinDepth : ValueField<"acc-min-depth", "unsigned", "8u",
    "Minimum length of accumulator chains required for the optimization "
    "to kick in", /*hidden=*/1>;

defm CGPASS_AccMaxWidth : ValueField<"acc-max-width", "unsigned", "3u",
    "Maximum number of branches in the accumulator tree", /*hidden=*/1>;

// =========================================================================
// Options — TargetLoweringBase.cpp
// =========================================================================

defm CGPASS_JumpIsExpensive : BoolField<"jump-is-expensive", "false",
    "Do not create extra branches to split comparison logic.",
    /*hidden=*/1>;

defm CGPASS_MinJumpTableEntries : ValueField<"min-jump-table-entries",
    "unsigned", "4u", "Set minimum number of entries to use a jump table.",
    /*hidden=*/1>;

defm CGPASS_MaxJumpTableSize : ValueField<"max-jump-table-size", "unsigned",
    "UINT_MAX", "Set maximum size of jump tables.", /*hidden=*/1>;

defm CGPASS_JumpTableDensity : ValueField<"jump-table-density", "unsigned",
    "10u", "Minimum density for building a jump table in a normal function",
    /*hidden=*/1>;

defm CGPASS_OptsizeJumpTableDensity : ValueField<
    "optsize-jump-table-density", "unsigned", "40u",
    "Minimum density for building a jump table in an optsize function",
    /*hidden=*/1>;

defm CGPASS_MinBitTestCmps : ValueField<"min-bit-test-cmps", "unsigned",
    "2u",
    "Set minimum of largest number of comparisons to use bit test for "
    "switch.", /*hidden=*/1>;

defm CGPASS_MaxStoreMemset : ValueField<"max-store-memset", "unsigned",
    "0u",
    "Override target's MaxStoresPerMemset and MaxStoresPerMemsetOptSize. "
    "Set to 0 to use the target default.", /*hidden=*/1>;

defm CGPASS_MaxStoreMemcpy : ValueField<"max-store-memcpy", "unsigned",
    "0u",
    "Override target's MaxStoresPerMemcpy and MaxStoresPerMemcpyOptSize. "
    "Set to 0 to use the target default.", /*hidden=*/1>;

defm CGPASS_MaxStoreMemmove : ValueField<"max-store-memmove", "unsigned",
    "0u",
    "Override target's MaxStoresPerMemmove and MaxStoresPerMemmoveOptSize. "
    "Set to 0 to use the target default.", /*hidden=*/1>;

defm CGPASS_DisableStrictnodeMutation : BoolField<
    "disable-strictnode-mutation", "false",
    "Don't mutate strict-float node to a legalize node", /*hidden=*/1>;

// =========================================================================
// Options — TargetLoweringObjectFileImpl.cpp
// =========================================================================

defm CGPASS_JumptableInFunctionSection : BoolField<
    "jumptable-in-function-section", "false",
    "Putting Jump Table in function section", /*hidden=*/1>;

// =========================================================================
// Options — TargetPassConfig.cpp
// =========================================================================

// std::optional<bool>, not a plain BoolField: TargetPassConfig.cpp
// distinguishes "left unset" (fall back to TM.useIPRA(), or leave a
// TPCOverride/target-installed value alone) from "explicitly specified"
// (even when explicitly false, which must win over that fallback). See
// CGPassBuilderOption::EnableIPRA (std::optional<bool>) and its two
// consumers in TargetPassConfig.cpp plus CodeGenPassBuilder.cpp.
defm CGPASS_EnableIpra : OptionalValueField<"enable-ipra", "bool", "true",
    "Enable interprocedural register allocation to reduce load/store at "
    "procedure calls.", /*hidden=*/1>;

defm CGPASS_VerifyMachineinstrs : OptionalValueField<"verify-machineinstrs",
    "bool", "true", "Verify generated machine code", /*hidden=*/1>;

defm CGPASS_DisableAtexitBasedGlobalDtorLowering : BoolField<
    "disable-atexit-based-global-dtor-lowering", "false",
    "For MachO, disable atexit()-based global destructor lowering",
    /*hidden=*/1>;

defm CGPASS_DebugifyAndStripAllSafe : OptionalValueField<
    "debugify-and-strip-all-safe", "bool", "true",
    "Debugify MIR before and Strip debug after each pass except those "
    "known to be unsafe when debug info is present", /*hidden=*/1>;

defm CGPASS_DebugifyCheckAndStripAllSafe : OptionalValueField<
    "debugify-check-and-strip-all-safe", "bool", "true",
    "Debugify MIR before, by checking and stripping the debug info after, "
    "each pass except those known to be unsafe when d...", /*hidden=*/1>;

defm CGPASS_FastIsel : OptionalValueField<"fast-isel", "bool", "true",
    "Enable the fast instruction selector", /*hidden=*/1>;

defm CGPASS_GlobalIsel : OptionalValueField<"global-isel", "bool", "true",
    "Enable the global instruction selector", /*hidden=*/1>;

defm CGPASS_PrintAfterIsel : BoolField<"print-after-isel", "false",
    "Print machine instrs after ISel", /*hidden=*/1>;

defm CGPASS_DisableExpandReductions : BoolField<"disable-expand-reductions",
    "false", "Disable the expand reduction intrinsics pass from running",
    /*hidden=*/1>;

defm CGPASS_EnableGcEmptyBasicBlocks : BoolField<
    "enable-gc-empty-basic-blocks", "false",
    "Enable garbage-collecting empty basic blocks", /*hidden=*/1>;

defm CGPASS_FsProfileFile : ValueField<"fs-profile-file", "std::string",
    "", "Flow Sensitive profile file name.", /*hidden=*/1>;

defm CGPASS_DisablePostRa : BoolField<"disable-post-ra", "false",
    "Disable Post Regalloc Scheduler", /*hidden=*/1>;

defm CGPASS_DisableBranchFold : BoolField<"disable-branch-fold", "false",
    "Disable branch folding", /*hidden=*/1>;

defm CGPASS_DisableTailDuplicate : BoolField<"disable-tail-duplicate",
    "false", "Disable tail duplication", /*hidden=*/1>;

defm CGPASS_DisableEarlyTaildup : BoolField<"disable-early-taildup",
    "false", "Disable pre-register allocation tail duplication",
    /*hidden=*/1>;

// =========================================================================
// Options — TargetPassConfig.cpp (enum options)
// =========================================================================

// Not EnumField<>: that multiclass also emits an always-hidden Separate
// ("-enable-machine-outliner value") alias, which OptTable rejects as
// equivalent to the bare Flag<> spelling below (both would match a lone
// "-enable-machine-outliner" token). The old clv2 option's ValueOptional
// bit only ever supported Joined ("=value") and the bare flag, never a
// separate-token spelling, so omitting the Separate alias here matches its
// actual behavior, not just a design accident.
def CGPASS_EnableMachineOutliner : Joined<["-", "--"],
    "enable-machine-outliner=">,
    HelpText<"Enable the machine outliner">, Flags<[HelpHidden]>,
    Field<"CGRunOutliner", "CGPASS_EnableMachineOutliner",
          "CGRunOutliner::TargetDefault">,
    FieldEnum<CGRunOutlinerEnum>;

// Bare "-enable-machine-outliner" (no "=value") is equivalent to
// "-enable-machine-outliner=always", matching the old clv2 ValueOptional
// spelling.
def CGPASS_EnableMachineOutlinerBare : Flag<["-", "--"],
    "enable-machine-outliner">, Flags<[HelpHidden]>,
    Assign<"CGPASS_EnableMachineOutliner", "CGRunOutliner::AlwaysOutline">;

defm CGPASS_GlobalISelAbort : EnumField<"global-isel-abort",
    CGGlobalISelAbortModeEnum, "CGGlobalISelAbortMode::TargetDefault",
    "Enable abort calls when global instruction selection fails to "
    "lower/select an instruction", /*hidden=*/1>;

// =========================================================================
// Options — TargetMachine.cpp
// =========================================================================

defm CGPASS_NoKernelInfoEndLto : BoolField<"no-kernel-info-end-lto",
    "false", "remove the kernel-info pass at the end of the full LTO "
    "pipeline", /*hidden=*/1>;
