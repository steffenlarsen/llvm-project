//===- IROptions.td - IR options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def IROptions : OptionsStruct<"llvm"> {}

// =========================================================================
// External enum: ChangePrinter (from PrintPasses.h)
// =========================================================================

// ChangePrinter::None (the compile-time default, never explicitly
// selectable) is deliberately not an EnumMember: it has no
// "-print-changed=<spelling>" form of its own.
//
// ChangePrinter::Verbose DOES need an EnumMember with an empty spelling:
// upstream's cl::opt<ChangePrinter> used cl::ValueOptional plus a sentinel
// clEnumValN(ChangePrinter::Verbose, "", "") entry, so both the bare
// "-print-changed" flag AND the explicit-but-empty "-print-changed=" spelling
// resolve to Verbose (see llvm/test/Other/change-printer.ll's two identical
// CHECK-SIMPLE RUN lines). IR_PrintChangedBare's Assign<> below covers the
// bare spelling; this empty-string member covers "-print-changed=" so the
// Joined option's enum lookup doesn't reject it as an unknown value.
def ChangePrinterEnum : OptionEnum<"llvm::ChangePrinter", [
    EnumMember<"Verbose", "">,
    EnumMember<"Quiet", "quiet">,
    EnumMember<"DiffVerbose", "diff">,
    EnumMember<"DiffQuiet", "diff-quiet">,
    EnumMember<"ColourDiffVerbose", "cdiff">,
    EnumMember<"ColourDiffQuiet", "cdiff-quiet">,
    EnumMember<"DotCfgVerbose", "dot-cfg">,
    EnumMember<"DotCfgQuiet", "dot-cfg-quiet">]> {
  let External = 1;
  let Ident = "ChangePrinter";
}

// =========================================================================
// Inline enum: PassDebugLevel
// =========================================================================

def PassDebugLevelEnum : OptionEnum<"PassDebugLevel", [
    EnumMember<"Disabled", "Disabled">,
    EnumMember<"Arguments", "Arguments">,
    EnumMember<"Structure", "Structure">,
    EnumMember<"Executions", "Executions">,
    EnumMember<"Details", "Details">]>;

// =========================================================================
// Options — PrintPasses.cpp
// =========================================================================

defm IR_PrintBefore : ListField<
    "print-before", "std::string", "Print IR before specified passes",
    /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/1>;

defm IR_PrintAfter : ListField<
    "print-after", "std::string", "Print IR after specified passes",
    /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/1>;

defm IR_PrintBeforeAll : BoolField<
    "print-before-all", "false", "Print IR before each pass", /*hidden=*/1>;

defm IR_PrintAfterAll : BoolField<
    "print-after-all", "false", "Print IR after each pass", /*hidden=*/1>;

// Not EnumField<>: that multiclass also emits an always-hidden Separate
// ("-print-changed value") alias, which OptTable rejects as equivalent to
// the bare Flag<> spelling below (both would match a lone "-print-changed"
// token) -- same reasoning as CGPASS_EnableMachineOutliner in
// CodeGenPassOptionsSched1.td. The old clv2 option's ValueOptional bit only
// ever supported Joined ("=value") and the bare flag, never a
// separate-token spelling, so omitting the Separate alias here matches its
// actual behavior, not just a design accident.
def IR_PrintChanged : Joined<["-", "--"], "print-changed=">,
    HelpText<"Print changed IRs">, Flags<[HelpHidden]>,
    Field<"llvm::ChangePrinter", "IR_PrintChanged", "llvm::ChangePrinter::None">,
    FieldEnum<ChangePrinterEnum>;

// Bare "-print-changed" (no "=value") is equivalent to
// "-print-changed=verbose", matching the old clv2 ValueOptional spelling.
def IR_PrintChangedBare : Flag<["-", "--"], "print-changed">,
    Flags<[HelpHidden]>,
    Assign<"IR_PrintChanged", "llvm::ChangePrinter::Verbose">;

defm IR_DiffBinary : ValueField<
    "print-changed-diff-path", "std::string", "\"diff\"",
    "system diff used by change reporters", /*hidden=*/1>;

defm IR_PrintModuleScope : BoolField<
    "print-module-scope", "false",
    "When printing IR for print-[before|after]{-all} "
    "always print a module IR", /*hidden=*/1>;

defm IR_LoopPrintFuncScope : BoolField<
    "print-loop-func-scope", "false",
    "When printing IR for print-[before|after]{-all} "
    "for a loop pass, always print function IR", /*hidden=*/1>;

defm IR_FilterPasses : ListField<
    "filter-passes", "std::string",
    "Only consider IR changes for passes whose names "
    "match the specified value. No-op without -print-changed",
    /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/1,
    /*metaVar=*/"pass names">;

defm IR_PrintFuncsList : ListField<
    "filter-print-funcs", "std::string",
    "Only print IR for functions whose name "
    "match this for all print-[before|after][-all] options",
    /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/1,
    /*metaVar=*/"function names">;

// Deliberately not comma-split (commaJoined = 0): the old clv2 ListOption
// had no CommaSeparated bit, so a single occurrence's value is kept whole
// (a "file:line[,line-line][,line]" filter can itself contain commas) and
// only repeated flags accumulate additional entries.
defm IR_PrintSourceLocs : ListField<
    "filter-print-source-locs", "std::string",
    "Only print IR containing matching source locations",
    /*commaJoined=*/0, /*appendEquals=*/1, /*hidden=*/1,
    /*metaVar=*/"file:line[,line-line][,line]">;

// =========================================================================
// Options — AsmWriter.cpp
// =========================================================================

defm IR_PrintInstAddrs : BoolField<
    "print-inst-addrs", "false",
    "Add an address after each instruction", /*hidden=*/1>;

// std::optional<bool>: AssemblyWriter's constructor only overrides its own
// caller-supplied ShouldPreserveUseListOrder default when the flag was
// actually specified on the command line -- hence hand-authored (mirroring
// CGPASS_PostRaScheduler in CodeGenPassOptionsSched1.td) instead of a plain
// BoolField, whose resolved-or-default "false" can't distinguish the two.
def IR_PreserveAssemblyUseListOrder : Flag<
    ["-", "--"], "preserve-ll-uselistorder">,
    HelpText<"Preserve use-list order when writing LLVM assembly.">,
    Flags<[HelpHidden]>,
    Field<"std::optional<bool>", "IR_PreserveAssemblyUseListOrder",
          "std::nullopt">,
    Assign<"IR_PreserveAssemblyUseListOrder", "true">;
def IR_PreserveAssemblyUseListOrder_no : Flag<
    ["-", "--"], "no-preserve-ll-uselistorder">,
    Flags<[HelpHidden]>,
    Assign<"IR_PreserveAssemblyUseListOrder", "false">;
def IR_PreserveAssemblyUseListOrder_eq : Joined<
    ["-", "--"], "preserve-ll-uselistorder=">,
    Flags<[HelpHidden]>, BoolAssign<"IR_PreserveAssemblyUseListOrder">;

defm IR_PrintInstDebugLocs : BoolField<
    "print-inst-debug-locs", "false",
    "Add a debug location after each instruction, if available",
    /*hidden=*/1>;

defm IR_PrintProfData : BoolField<
    "print-prof-data", "false",
    "Print profile data attached to instructions", /*hidden=*/1>;

defm IR_PrintAddrspaceName : BoolField<
    "print-addrspace-name", "false",
    "Print the name of addrspace, if available", /*hidden=*/1>;

// =========================================================================
// Options — OptBisect.cpp
// =========================================================================

// std::optional<int>: initOptBisectFromOptions() must distinguish "never
// specified" (leave bisection disabled) from an explicit "-opt-bisect-limit"
// value (including an explicit -1, which means "run all passes" and thereby
// enables verbose bisect output) -- a plain ValueField's resolved-or-default
// "-1" can't tell those two cases apart. Hand-authored (not OptionalValueField)
// since this option has never had a bare-flag spelling.
def IR_OptBisectLimit : Joined<["-", "--"], "opt-bisect-limit=">,
    HelpText<"Maximum optimization to perform (equivalent to -opt-bisect=1-N)">,
    Flags<[HelpHidden]>,
    Field<"std::optional<int>", "IR_OptBisectLimit", "std::nullopt">;
def IR_OptBisectLimit_sep : Separate<["-", "--"], "opt-bisect-limit">,
    Flags<[HelpHidden]>, Alias<IR_OptBisectLimit>;

// Same "specified" rationale as IR_OptBisectLimit above.
def IR_OptBisectIntervals : Joined<["-", "--"], "opt-bisect=">,
    HelpText<"Run optimization passes only for the specified intervals. "
    "Format: '1-10,20-30,45' runs passes 1-10, 20-30, and 45, where "
    "index 1 is the first pass. Supply '0' to run no passes and -1 to "
    "run all passes.">,
    Flags<[HelpHidden]>,
    Field<"std::optional<std::string>", "IR_OptBisectIntervals",
          "std::nullopt">;
def IR_OptBisectIntervals_sep : Separate<["-", "--"], "opt-bisect">,
    Flags<[HelpHidden]>, Alias<IR_OptBisectIntervals>;

defm IR_OptBisectVerbose : BoolField<
    "opt-bisect-verbose", "true",
    "Show verbose output when opt-bisect-limit and/or opt-disable are set",
    /*hidden=*/1>;

defm IR_OptDisablePasses : ListField<
    "opt-disable", "std::string",
    "Optimization pass(es) to disable (comma-separated list)",
    /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/1>;

// =========================================================================
// Options — Constants.cpp
// =========================================================================

defm IR_UseConstantIntForFixedLengthSplat : BoolField<
    "use-constant-int-for-fixed-length-splat", "false",
    "Use ConstantInt's native fixed-length vector splat support.",
    /*hidden=*/1>;

defm IR_UseConstantIntForScalableSplat : BoolField<
    "use-constant-int-for-scalable-splat", "false",
    "Use ConstantInt's native scalable vector splat support.",
    /*hidden=*/1>;

// =========================================================================
// Options — ModuleSummaryIndex.cpp
// =========================================================================

defm IR_AlwaysRenamePromotedLocals : BoolField<
    "always-rename-promoted-locals", "true",
    "Always rename promoted locals.", /*hidden=*/1>;

defm IR_PropagateAttrs : BoolField<
    "propagate-attrs", "true", "Propagate attributes in index",
    /*hidden=*/1>;

defm IR_ImportConstantsWithRefs : BoolField<
    "import-constants-with-refs", "true",
    "Import constant global variables with references", /*hidden=*/1>;

// =========================================================================
// Options — DiagnosticHandler.cpp
// =========================================================================

// The old clv2 Validate callbacks (validateRegexOption) have no OptTable
// schema equivalent and are dropped here, matching every prior clv2 ->
// OptTable migration that had per-option Validate callbacks (see e.g. the
// ProfileData / BPF migrations): an invalid regex is still caught, just
// later, when DiagnosticHandler.cpp itself compiles the pattern with
// llvm::Regex rather than at CLI-parse time.

defm IR_PassRemarks : ListField<
    "pass-remarks", "std::string",
    "Enable optimization remarks from passes whose name match "
    "the given regular expression",
    /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/1,
    /*metaVar=*/"pattern">;

defm IR_PassRemarksMissed : ListField<
    "pass-remarks-missed", "std::string",
    "Enable missed optimization remarks from passes whose name match "
    "the given regular expression",
    /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/1,
    /*metaVar=*/"pattern">;

defm IR_PassRemarksAnalysis : ListField<
    "pass-remarks-analysis", "std::string",
    "Enable optimization analysis remarks from passes whose name match "
    "the given regular expression",
    /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/1,
    /*metaVar=*/"pattern">;

// =========================================================================
// Options — ProfDataUtils.cpp
// =========================================================================

defm IR_ElideAllZeroBranchWeights : BoolField<
    "elide-all-zero-branch-weights", "true", "">;

// =========================================================================
// Options — Instruction.cpp
// =========================================================================

defm IR_ProfcheckDisableMetadataFixes : BoolField<
    "profcheck-disable-metadata-fixes", "false",
    "Disable metadata propagation fixes discovered through Issue #147390",
    /*hidden=*/1>;

// =========================================================================
// Options — PassTimingInfo.cpp
// =========================================================================

defm IR_TimePasses : BoolField<
    "time-passes",
    "false", "Time each pass, printing elapsed time for each on exit",
    /*hidden=*/1>;

defm IR_TimePassesPerRun : BoolField<
    "time-passes-per-run", "false",
    "Time each pass run, printing elapsed time for each run on exit",
    /*hidden=*/1>;

// =========================================================================
// Options — DebugInfoMetadata.cpp
// =========================================================================

defm IR_EnableFSDiscriminator : BoolField<
    "enable-fs-discriminator", "false",
    "Enable adding flow sensitive discriminators", /*hidden=*/1>;

defm IR_PickMergedSourceLocations : BoolField<
    "pick-merged-source-locations", "false",
    "Preserve line and column number when merging locations.",
    /*hidden=*/1>;

// =========================================================================
// Options — Verifier.cpp
// =========================================================================

defm IR_VerifyNoAliasScopeDomination : BoolField<
    "verify-noalias-scope-decl-dom", "false",
    "Ensure that llvm.experimental.noalias.scope.decl for identical "
    "scopes are not dominating", /*hidden=*/1>;

// =========================================================================
// Options — Value.cpp
// =========================================================================

defm IR_UseDerefAtPointSemantics : BoolField<
    "use-dereferenceable-at-point-semantics", "true",
    "Deref attributes and metadata infer facts at definition only",
    /*hidden=*/1>;

// =========================================================================
// Options — SafepointIRVerifier.cpp
// =========================================================================

defm IR_SafepointIRVerifierPrintOnly : BoolField<
    "safepoint-ir-verifier-print-only", "false", "">;

// =========================================================================
// Options — LegacyPassManager.cpp
// =========================================================================

defm IR_PassDebugging : EnumField<
    "debug-pass", PassDebugLevelEnum, "PassDebugLevel::Disabled",
    "Print legacy PassManager debugging information", /*hidden=*/1>;

// =========================================================================
// Options — Instructions.cpp
// =========================================================================

defm IR_DisableI2pP2iOpt : BoolField<
    "disable-i2p-p2i-opt", "false",
    "Disables inttoptr/ptrtoint roundtrip optimization">;

// =========================================================================
// Options — Function.cpp
// =========================================================================

defm IR_NonGlobalValueMaxNameSize : ValueField<
    "non-global-value-max-name-size", "int", "1024",
    "Maximum size for the name of non-global values.", /*hidden=*/1>;

// =========================================================================
// Options — Dominators.cpp
// =========================================================================

defm IR_VerifyDomInfo : BoolField<
    "verify-dom-info", "false",
    "Verify dominator info (time consuming)", /*hidden=*/1>;

// =========================================================================
// Options — AutoUpgrade.cpp
// =========================================================================

defm IR_DisableAutoUpgradeDebugInfo : BoolField<
    "disable-auto-upgrade-debug-info", "false",
    "Disable autoupgrade of debug info">;
