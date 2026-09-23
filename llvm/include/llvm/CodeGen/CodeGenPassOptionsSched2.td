//===- CodeGenPassOptionsSched2.td - Sched2 option defs -*- tablegen -*-=====//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def CodeGenSched2Options : OptionsStruct<"llvm"> {}

// =========================================================================
// Options — TargetPassConfig.cpp
// =========================================================================

defm CGPASS_DisableBlockPlacement : BoolField<"disable-block-placement",
    "false", "Disable probability-driven block placement", /*hidden=*/1>;

defm CGPASS_EnableBlockPlacementStats : BoolField<
    "enable-block-placement-stats", "false",
    "Collect probability-driven block placement stats", /*hidden=*/1>;

defm CGPASS_DisableSsc : BoolField<"disable-ssc", "false",
    "Disable Stack Slot Coloring", /*hidden=*/1>;

defm CGPASS_DisableMachineDce : BoolField<"disable-machine-dce", "false",
    "Disable Machine Dead Code Elimination", /*hidden=*/1>;

defm CGPASS_DisableEarlyIfcvt : BoolField<"disable-early-ifcvt", "false",
    "Disable Early If-conversion", /*hidden=*/1>;

defm CGPASS_DisableMachineLicm : BoolField<"disable-machine-licm", "false",
    "Disable Machine LICM", /*hidden=*/1>;

defm CGPASS_DisableMachineCse : BoolField<"disable-machine-cse", "false",
    "Disable Machine Common Subexpression Elimination", /*hidden=*/1>;

defm CGPASS_DisablePostraMachineLicm : BoolField<
    "disable-postra-machine-licm", "false", "Disable Machine LICM",
    /*hidden=*/1>;

defm CGPASS_DisableMachineSink : BoolField<"disable-machine-sink", "false",
    "Disable Machine Sinking", /*hidden=*/1>;

defm CGPASS_DisablePostraMachineSink : BoolField<
    "disable-postra-machine-sink", "false", "Disable PostRA Machine Sinking",
    /*hidden=*/1>;

defm CGPASS_DisableLsr : BoolField<"disable-lsr", "false",
    "Disable Loop Strength Reduction Pass", /*hidden=*/1>;

defm CGPASS_DisableConstantHoisting : BoolField<"disable-constant-hoisting",
    "false", "Disable ConstantHoisting", /*hidden=*/1>;

defm CGPASS_DisableCgp : BoolField<"disable-cgp", "false",
    "Disable Codegen Prepare", /*hidden=*/1>;

defm CGPASS_DisableCopyprop : BoolField<"disable-copyprop", "false",
    "Disable Copy Propagation pass", /*hidden=*/1>;

defm CGPASS_DisablePartialLibcallInlining : BoolField<
    "disable-partial-libcall-inlining", "false",
    "Disable Partial Libcall Inlining", /*hidden=*/1>;

defm CGPASS_EnableImplicitNullChecks : BoolField<
    "enable-implicit-null-checks", "false",
    "Fold null checks into faulting memory operations", /*hidden=*/1>;

defm CGPASS_PrintIselInput : BoolField<"print-isel-input", "false",
    "Print LLVM IR input to isel pass", /*hidden=*/1>;

defm CGPASS_PrintRegusage : BoolField<"print-regusage", "false",
    "Print register usage details collected for IPRA", /*hidden=*/1>;

defm CGPASS_EnableGlobalMergeFunc : BoolField<"enable-global-merge-func",
    "false", "Enable global merge functions that are based on hash function",
    /*hidden=*/1>;

defm CGPASS_DisableCfiFixup : BoolField<"disable-cfi-fixup", "false",
    "Disable the CFI fixup pass", /*hidden=*/1>;

defm CGPASS_DisableRaFsprofileLoader : BoolField<
    "disable-ra-fsprofile-loader", "false",
    "Disable MIRProfileLoader before RegAlloc", /*hidden=*/1>;

defm CGPASS_DisableLayoutFsprofileLoader : BoolField<
    "disable-layout-fsprofile-loader", "false",
    "Disable MIRProfileLoader before BlockPlacement", /*hidden=*/1>;

defm CGPASS_FsRemappingFile : ValueField<"fs-remapping-file", "std::string",
    "", "Flow Sensitive profile remapping file name.", /*hidden=*/1>;

defm CGPASS_MischedPostra : BoolField<"misched-postra", "false",
    "Run MachineScheduler post regalloc (independent of preRA sched)",
    /*hidden=*/1>;

defm CGPASS_EarlyLiveIntervals : BoolField<"early-live-intervals", "false",
    "Run live interval analysis earlier in the pipeline", /*hidden=*/1>;

defm CGPASS_DisableReplaceWithVecLib : BoolField<
    "disable-replace-with-vec-lib", "false",
    "Disable replace with vector math call pass", /*hidden=*/1>;

defm CGPASS_StartAfter : ValueField<"start-after", "std::string", "",
    "Resume compilation after a specific pass", /*hidden=*/1>;

defm CGPASS_StartBefore : ValueField<"start-before", "std::string", "",
    "Resume compilation before a specific pass", /*hidden=*/1>;

defm CGPASS_StopAfter : ValueField<"stop-after", "std::string", "",
    "Stop compilation after a specific pass", /*hidden=*/1>;

defm CGPASS_StopBefore : ValueField<"stop-before", "std::string", "",
    "Stop compilation before a specific pass", /*hidden=*/1>;

defm CGPASS_EnableSplitMachineFunctions : BoolField<
    "enable-split-machine-functions", "false",
    "Split out cold blocks from machine functions based on profile "
    "information.", /*hidden=*/1>;

defm CGPASS_DisableSelectOptimize : BoolField<"disable-select-optimize",
    "true", "Disable the select-optimization pass from running",
    /*hidden=*/1>;

defm CGPASS_SplitStaticData : BoolField<"split-static-data", "false",
    "Split static data sections into hot and cold sections using profile "
    "information", /*hidden=*/1>;

// Not Hidden: matches the old clv2 option, which had no Hidden bit set.
defm CGPASS_BasicBlockSectionMatchInfer : BoolField<
    "basic-block-section-match-infer", "false",
    "Enable matching and inference when generating basic block sections">;

// Not Hidden: matches the old clv2 option, which had no Hidden bit set.
// No explicit Default in the old clv2 option either; "false" reproduces the
// value clv2 fell back to when unspecified.
defm CGPASS_EmitBbHash : BoolField<"emit-bb-hash", "false",
    "Emit the hash of basic block in the SHT_LLVM_BB_ADDR_MAP section.">;

// =========================================================================
// Options — TargetRegisterInfo.cpp
// =========================================================================

defm CGPASS_HugeSizeForSplit : ValueField<"huge-size-for-split", "unsigned",
    "5000u", "A threshold of live range size which may cause high compile "
    "time cost in global splitting.", /*hidden=*/1>;

// =========================================================================
// Options — TargetSchedule.cpp
// =========================================================================

defm CGPASS_SchedModelForceEnableIntervals : BoolField<
    "sched-model-force-enable-intervals", "false",
    "Force the use of resource intervals in the schedule model",
    /*hidden=*/1>;

// =========================================================================
// Options — VLIWMachineScheduler.cpp
// =========================================================================

defm CGPASS_IgnoreBbRegPressure : BoolField<"ignore-bb-reg-pressure",
    "false", "", /*hidden=*/1>;

defm CGPASS_UseNewerCandidate : BoolField<"use-newer-candidate", "true", "",
    /*hidden=*/1>;

defm CGPASS_MischedVerboseLevel : ValueField<"misched-verbose-level",
    "unsigned", "1u", "", /*hidden=*/1>;

defm CGPASS_CheckEarlyAvail : BoolField<"check-early-avail", "true", "",
    /*hidden=*/1>;

defm CGPASS_VliwMischedRegPressure : ValueField<"vliw-misched-reg-pressure",
    "float", "0.75f", "High register pressure threhold.", /*hidden=*/1>;

// =========================================================================
// Options — WindowScheduler.cpp
// =========================================================================

defm CGPASS_WindowSearchNum : ValueField<"window-search-num", "unsigned",
    "6u", "The number of searches per loop in the window algorithm. 0 means "
    "no search number limit.", /*hidden=*/1>;

defm CGPASS_WindowSearchRatio : ValueField<"window-search-ratio", "unsigned",
    "40u", "The ratio of searches per loop in the window algorithm. 100 "
    "means search all positions in the loop, while 0 means not...",
    /*hidden=*/1>;

defm CGPASS_WindowIiCoeff : ValueField<"window-ii-coeff", "unsigned", "5u",
    "The coefficient used when initializing II in the window algorithm.",
    /*hidden=*/1>;

defm CGPASS_WindowRegionLimit : ValueField<"window-region-limit", "unsigned",
    "3u", "The lower limit of the scheduling region in the window "
    "algorithm.", /*hidden=*/1>;

defm CGPASS_WindowDiffLimit : ValueField<"window-diff-limit", "unsigned",
    "2u", "The lower limit of the difference between best II and base II "
    "in the window algorithm. If the difference is smaller t...",
    /*hidden=*/1>;

defm CGPASS_WindowIiLimit : ValueField<"window-ii-limit", "unsigned",
    "1000u", "The upper limit of II in the window algorithm.",
    /*hidden=*/1>;
