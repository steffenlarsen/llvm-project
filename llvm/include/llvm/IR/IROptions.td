//===- IROptions.td - CLV2 option defs for IR ---------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def IROptsReg : Registry<"llvm::ir_opts"> {
  // Getter bodies are compiled once, in IROptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

// =========================================================================
// External enum: ChangePrinter (from PrintPasses.h)
// =========================================================================

def ChangePrinterQuiet : EnumValue<"quiet", "Run in quiet mode"> {
  let CppName = "Quiet";
}

def ChangePrinterDiffVerbose : EnumValue<"diff", "Display patch-like changes"> {
  let CppName = "DiffVerbose";
}

def ChangePrinterDiffQuiet : EnumValue<"diff-quiet",
    "Display patch-like changes in quiet mode"> {
  let CppName = "DiffQuiet";
}

def ChangePrinterColourDiffVerbose : EnumValue<"cdiff",
    "Display patch-like changes with color"> {
  let CppName = "ColourDiffVerbose";
}

def ChangePrinterColourDiffQuiet : EnumValue<"cdiff-quiet",
    "Display patch-like changes in quiet mode with color"> {
  let CppName = "ColourDiffQuiet";
}

def ChangePrinterDotCfgVerbose : EnumValue<"dot-cfg",
    "Create a website with graphical changes"> {
  let CppName = "DotCfgVerbose";
}

def ChangePrinterDotCfgQuiet : EnumValue<"dot-cfg-quiet",
    "Create a website with graphical changes in quiet mode"> {
  let CppName = "DotCfgQuiet";
}

def ChangePrinterVerbose : EnumValue<"", ""> {
  let CppName = "Verbose";
}

def ChangePrinterEnum : EnumType<"ChangePrinter",
    [ChangePrinterQuiet, ChangePrinterDiffVerbose, ChangePrinterDiffQuiet,
     ChangePrinterColourDiffVerbose, ChangePrinterColourDiffQuiet,
     ChangePrinterDotCfgVerbose, ChangePrinterDotCfgQuiet,
     ChangePrinterVerbose]> {
  let External = 1;
}

// =========================================================================
// Inline enum: PassDebugLevel
// =========================================================================

def PassDebugLevelDisabled : EnumValue<"Disabled", "disable debug output"> {
  let CppName = "Disabled";
}

def PassDebugLevelArguments : EnumValue<"Arguments",
    "print pass arguments to pass to 'opt'"> {
  let CppName = "Arguments";
}

def PassDebugLevelStructure : EnumValue<"Structure",
    "print pass structure before run()"> {
  let CppName = "Structure";
}

def PassDebugLevelExecutions : EnumValue<"Executions",
    "print pass name before it is executed"> {
  let CppName = "Executions";
}

def PassDebugLevelDetails : EnumValue<"Details",
    "print pass details when it is executed"> {
  let CppName = "Details";
}

def PassDebugLevelEnum : EnumType<"PassDebugLevel",
    [PassDebugLevelDisabled, PassDebugLevelArguments,
     PassDebugLevelStructure, PassDebugLevelExecutions,
     PassDebugLevelDetails]>;

// =========================================================================
// Options — PrintPasses.cpp
// =========================================================================

def IR_PrintBefore : ListOption<
    "print-before", "Print IR before specified passes",
    "std::string"> {
  let Registry = IROptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

def IR_PrintAfter : ListOption<
    "print-after", "Print IR after specified passes",
    "std::string"> {
  let Registry = IROptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

def IR_PrintBeforeAll : Option<
    "print-before-all", "Print IR before each pass",
    "bool"> {
  let Registry = IROptsReg;
  let Hidden = 1;
}

def IR_PrintAfterAll : Option<
    "print-after-all", "Print IR after each pass",
    "bool"> {
  let Registry = IROptsReg;
  let Hidden = 1;
}

def IR_PrintChanged : Option<
    "print-changed", "Print changed IRs",
    "ChangePrinter"> {
  let Registry = IROptsReg;
  let Hidden = 1;
  let ValueOptional = 1;
}

def IR_DiffBinary : Option<
    "print-changed-diff-path", "system diff used by change reporters",
    "std::string"> {
  let Registry = IROptsReg;
  let Default = "\"diff\"";
  let Hidden = 1;
}

def IR_PrintModuleScope : Option<
    "print-module-scope",
    "When printing IR for print-[before|after]{-all} "
    "always print a module IR",
    "bool"> {
  let Registry = IROptsReg;
  let Hidden = 1;
}

def IR_LoopPrintFuncScope : Option<
    "print-loop-func-scope",
    "When printing IR for print-[before|after]{-all} "
    "for a loop pass, always print function IR",
    "bool"> {
  let Registry = IROptsReg;
  let Hidden = 1;
}

def IR_FilterPasses : ListOption<
    "filter-passes",
    "Only consider IR changes for passes whose names "
    "match the specified value. No-op without -print-changed",
    "std::string"> {
  let Registry = IROptsReg;
  let ValueDesc = "pass names";
  let Hidden = 1;
  let CommaSeparated = 1;
}

def IR_PrintFuncsList : ListOption<
    "filter-print-funcs",
    "Only print IR for functions whose name "
    "match this for all print-[before|after][-all] options",
    "std::string"> {
  let Registry = IROptsReg;
  let ValueDesc = "function names";
  let Hidden = 1;
  let CommaSeparated = 1;
}

// =========================================================================
// Options — AsmWriter.cpp
// =========================================================================

def IR_PrintInstAddrs : Option<
    "print-inst-addrs", "Add an address after each instruction",
    "bool"> {
  let Registry = IROptsReg;
  let Hidden = 1;
}

def IR_PreserveAssemblyUseListOrder : Option<
    "preserve-ll-uselistorder",
    "Preserve use-list order when writing LLVM assembly.",
    "bool"> {
  let Registry = IROptsReg;
  let Hidden = 1;
}

def IR_PrintInstDebugLocs : Option<
    "print-inst-debug-locs",
    "Add a debug location after each instruction, if available",
    "bool"> {
  let Registry = IROptsReg;
  let Hidden = 1;
}

def IR_PrintProfData : Option<
    "print-prof-data",
    "Print profile data attached to instructions",
    "bool"> {
  let Registry = IROptsReg;
  let Hidden = 1;
}

def IR_PrintAddrspaceName : Option<
    "print-addrspace-name",
    "Print the name of addrspace, if available",
    "bool"> {
  let Registry = IROptsReg;
  let Hidden = 1;
}

// =========================================================================
// Options — OptBisect.cpp
// =========================================================================

def IR_OptBisectLimit : Option<
    "opt-bisect-limit",
    "Maximum optimization to perform (equivalent to -opt-bisect=1-N)",
    "int"> {
  let Registry = IROptsReg;
  let Default = "-1";
  let Hidden = 1;
}

def IR_OptBisectIntervals : Option<
    "opt-bisect",
    "Run optimization passes only for the specified intervals. "
    "Format: '1-10,20-30,45' runs passes 1-10, 20-30, and 45, where "
    "index 1 is the first pass. Supply '0' to run no passes and -1 to "
    "run all passes.",
    "std::string"> {
  let Registry = IROptsReg;
  let Hidden = 1;
}

def IR_OptBisectVerbose : Option<
    "opt-bisect-verbose",
    "Show verbose output when opt-bisect-limit and/or opt-disable are set",
    "bool"> {
  let Registry = IROptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IR_OptDisablePasses : ListOption<
    "opt-disable", "Optimization pass(es) to disable (comma-separated list)",
    "std::string"> {
  let Registry = IROptsReg;
  let Hidden = 1;
  let CommaSeparated = 1;
}

// =========================================================================
// Options — Constants.cpp
// =========================================================================

def IR_UseConstantIntForFixedLengthSplat : Option<
    "use-constant-int-for-fixed-length-splat",
    "Use ConstantInt's native fixed-length vector splat support.",
    "bool"> {
  let Registry = IROptsReg;
  let Hidden = 1;
}

def IR_UseConstantIntForScalableSplat : Option<
    "use-constant-int-for-scalable-splat",
    "Use ConstantInt's native scalable vector splat support.",
    "bool"> {
  let Registry = IROptsReg;
  let Hidden = 1;
}

// =========================================================================
// Options — ModuleSummaryIndex.cpp
// =========================================================================

def IR_AlwaysRenamePromotedLocals : Option<
    "always-rename-promoted-locals",
    "Always rename promoted locals.",
    "bool"> {
  let Registry = IROptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IR_PropagateAttrs : Option<
    "propagate-attrs", "Propagate attributes in index",
    "bool"> {
  let Registry = IROptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IR_ImportConstantsWithRefs : Option<
    "import-constants-with-refs",
    "Import constant global variables with references",
    "bool"> {
  let Registry = IROptsReg;
  let Default = "1";
  let Hidden = 1;
}

// =========================================================================
// Options — DiagnosticHandler.cpp
// =========================================================================

def IR_PassRemarks : ListOption<
    "pass-remarks",
    "Enable optimization remarks from passes whose name match "
    "the given regular expression",
    "std::string"> {
  let Registry = IROptsReg;
  let ValueDesc = "pattern";
  let Hidden = 1;
  let CommaSeparated = 1;
  let Validate = [{
    return clv2::detail::validateRegexOption(Value, OptName, Diag);
  }];
}

def IR_PassRemarksMissed : ListOption<
    "pass-remarks-missed",
    "Enable missed optimization remarks from passes whose name match "
    "the given regular expression",
    "std::string"> {
  let Registry = IROptsReg;
  let ValueDesc = "pattern";
  let Hidden = 1;
  let CommaSeparated = 1;
  let Validate = [{
    return clv2::detail::validateRegexOption(Value, OptName, Diag);
  }];
}

def IR_PassRemarksAnalysis : ListOption<
    "pass-remarks-analysis",
    "Enable optimization analysis remarks from passes whose name match "
    "the given regular expression",
    "std::string"> {
  let Registry = IROptsReg;
  let ValueDesc = "pattern";
  let Hidden = 1;
  let CommaSeparated = 1;
  let Validate = [{
    return clv2::detail::validateRegexOption(Value, OptName, Diag);
  }];
}

// =========================================================================
// Options — ProfDataUtils.cpp
// =========================================================================

def IR_ElideAllZeroBranchWeights : Option<
    "elide-all-zero-branch-weights", "",
    "bool"> {
  let Registry = IROptsReg;
  let Default = "1";
}

// =========================================================================
// Options — Instruction.cpp
// =========================================================================

def IR_ProfcheckDisableMetadataFixes : Option<
    "profcheck-disable-metadata-fixes",
    "Disable metadata propagation fixes discovered through Issue #147390",
    "bool"> {
  let Registry = IROptsReg;
  let Hidden = 1;
}

// =========================================================================
// Options — PassTimingInfo.cpp
// =========================================================================

def IR_TimePasses : Option<
    "time-passes",
    "Time each pass, printing elapsed time for each on exit",
    "bool"> {
  let Registry = IROptsReg;
  let Hidden = 1;
}

def IR_TimePassesPerRun : Option<
    "time-passes-per-run",
    "Time each pass run, printing elapsed time for each run on exit",
    "bool"> {
  let Registry = IROptsReg;
  let Hidden = 1;
}

// =========================================================================
// Options — DebugInfoMetadata.cpp
// =========================================================================

def IR_EnableFSDiscriminator : Option<
    "enable-fs-discriminator",
    "Enable adding flow sensitive discriminators",
    "bool"> {
  let Registry = IROptsReg;
  let Hidden = 1;
}

def IR_PickMergedSourceLocations : Option<
    "pick-merged-source-locations",
    "Preserve line and column number when merging locations.",
    "bool"> {
  let Registry = IROptsReg;
  let Hidden = 1;
}

// =========================================================================
// Options — Verifier.cpp
// =========================================================================

def IR_VerifyNoAliasScopeDomination : Option<
    "verify-noalias-scope-decl-dom",
    "Ensure that llvm.experimental.noalias.scope.decl for identical "
    "scopes are not dominating",
    "bool"> {
  let Registry = IROptsReg;
  let Hidden = 1;
}

// =========================================================================
// Options — Value.cpp
// =========================================================================

def IR_UseDerefAtPointSemantics : Option<
    "use-dereferenceable-at-point-semantics",
    "Deref attributes and metadata infer facts at definition only",
    "bool"> {
  let Registry = IROptsReg;
  let Default = "1";
  let Hidden = 1;
}

// =========================================================================
// Options — SafepointIRVerifier.cpp
// =========================================================================

def IR_SafepointIRVerifierPrintOnly : Option<
    "safepoint-ir-verifier-print-only", "",
    "bool"> {
  let Default = "false";
  let Registry = IROptsReg;
}

// =========================================================================
// Options — LegacyPassManager.cpp
// =========================================================================

def IR_PassDebugging : Option<
    "debug-pass", "Print legacy PassManager debugging information",
    "PassDebugLevel"> {
  let Registry = IROptsReg;
  let Default = "Disabled";
  let Hidden = 1;
}

// =========================================================================
// Options — Instructions.cpp
// =========================================================================

def IR_DisableI2pP2iOpt : Option<
    "disable-i2p-p2i-opt",
    "Disables inttoptr/ptrtoint roundtrip optimization",
    "bool"> {
  let Registry = IROptsReg;
}

// =========================================================================
// Options — Function.cpp
// =========================================================================

def IR_NonGlobalValueMaxNameSize : Option<
    "non-global-value-max-name-size",
    "Maximum size for the name of non-global values.",
    "int"> {
  let Registry = IROptsReg;
  let Default = "1024";
  let Hidden = 1;
}

// =========================================================================
// Options — Dominators.cpp
// =========================================================================

def IR_VerifyDomInfo : Option<
    "verify-dom-info", "Verify dominator info (time consuming)",
    "bool"> {
  let Registry = IROptsReg;
  let Hidden = 1;
}

// =========================================================================
// Options — AutoUpgrade.cpp
// =========================================================================

def IR_DisableAutoUpgradeDebugInfo : Option<
    "disable-auto-upgrade-debug-info", "Disable autoupgrade of debug info",
    "bool"> {
  let Default = "false";
  let Registry = IROptsReg;
}
