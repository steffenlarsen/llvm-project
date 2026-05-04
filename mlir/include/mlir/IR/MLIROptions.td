//===- MLIROptions.td - CLV2 option defs for MLIR core ---*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

// Core MLIR options (context, asm printing, timing) — for tools that
// don't use the pass manager (e.g. mlir-translate).

// Full MLIR options (core + pass infrastructure + debug config).
def MLIROptsReg : Registry<"mlir::mlir_opts"> {
  // Getter bodies are compiled once, in MLIROptionsGetters.cpp.
  let OutOfLineGetters = 1;
  let ContextTypes = [];
}

// ============================================================================
// MLIRContext.cpp options
// ============================================================================

def MLIR_DisableThreading : Option<
    "mlir-disable-threading",
    "Disable multi-threading within MLIR, overrides any further call to "
    "MLIRContext::enableMultiThreading()",
    "bool"> {
  let Registry = MLIROptsReg;
}

def MLIR_PrintOpOnDiagnostic : Option<
    "mlir-print-op-on-diagnostic",
    "When a diagnostic is emitted on an operation, also print the operation "
    "as an attached note",
    "bool"> {
  let Registry = MLIROptsReg;
  let Default = "1";
}

def MLIR_PrintStackTraceOnDiagnostic : Option<
    "mlir-print-stacktrace-on-diagnostic",
    "When a diagnostic is emitted, also print the stack trace as an "
    "attached note",
    "bool"> {
  let Registry = MLIROptsReg;
}

// ============================================================================
// AsmPrinter.cpp options (OpPrintingFlags)
// ============================================================================

def MLIR_PrintElementsAttrsWithHexIfLarger : Option<
    "mlir-print-elementsattrs-with-hex-if-larger",
    "Print DenseElementsAttrs with a hex string that have more elements "
    "than the given upper limit (use -1 to disable)",
    "int64_t"> {
  let Registry = MLIROptsReg;
}

def MLIR_ElideElementsAttrsIfLarger : Option<
    "mlir-elide-elementsattrs-if-larger",
    "Elide ElementsAttrs with \"...\" that have more elements than the "
    "given upper limit",
    "unsigned"> {
  let Registry = MLIROptsReg;
}

def MLIR_ElideResourceStringsIfLarger : Option<
    "mlir-elide-resource-strings-if-larger",
    "Elide printing value of resources if string is too long in chars.",
    "unsigned"> {
  let Registry = MLIROptsReg;
}

def MLIR_PrintDebugInfo : Option<
    "mlir-print-debuginfo", "Print debug info in MLIR output",
    "bool"> {
  let Registry = MLIROptsReg;
}

def MLIR_PrettyDebugInfo : Option<
    "mlir-pretty-debuginfo", "Print pretty debug info in MLIR output",
    "bool"> {
  let Registry = MLIROptsReg;
}

def MLIR_PrintOpGeneric : Option<
    "mlir-print-op-generic", "Print the generic op form",
    "bool"> {
  let Registry = MLIROptsReg;
}

def MLIR_PrintAssumeVerified : Option<
    "mlir-print-assume-verified",
    "Skip op verification when using custom printers",
    "bool"> {
  let Registry = MLIROptsReg;
}

def MLIR_PrintLocalScope : Option<
    "mlir-print-local-scope",
    "Print with local scope and inline information (eliding aliases for "
    "attributes, types, and locations)",
    "bool"> {
  let Registry = MLIROptsReg;
}

def MLIR_PrintSkipRegions : Option<
    "mlir-print-skip-regions", "Skip regions when printing ops.",
    "bool"> {
  let Registry = MLIROptsReg;
}

def MLIR_PrintValueUsers : Option<
    "mlir-print-value-users",
    "Print users of operation results and block arguments as a comment",
    "bool"> {
  let Registry = MLIROptsReg;
}

def MLIR_PrintUniqueSSAIDs : Option<
    "mlir-print-unique-ssa-ids",
    "Print unique SSA ID numbers for values, block arguments and naming "
    "conflicts across all regions",
    "bool"> {
  let Registry = MLIROptsReg;
}

def MLIR_UseNameLocAsPrefix : Option<
    "mlir-use-nameloc-as-prefix",
    "Print SSA IDs using NameLocs as prefixes",
    "bool"> {
  let Registry = MLIROptsReg;
}

// ============================================================================
// PassManagerOptions.cpp options
// ============================================================================

def MLIR_PassCrashReproducer : Option<
    "mlir-pass-pipeline-crash-reproducer",
    "Generate a .mlir reproducer file at the given output path if the pass "
    "manager crashes or fails",
    "std::string"> {
  let Registry = MLIROptsReg;
}

def MLIR_PassLocalReproducer : Option<
    "mlir-pass-pipeline-local-reproducer",
    "When generating a crash reproducer, attempt to generated a reproducer "
    "with the smallest pipeline.",
    "bool"> {
  let Registry = MLIROptsReg;
}

def MLIR_PrintIRBeforeAll : Option<
    "mlir-print-ir-before-all", "Print IR before each pass",
    "bool"> {
  let Registry = MLIROptsReg;
}

def MLIR_PrintIRAfterAll : Option<
    "mlir-print-ir-after-all", "Print IR after each pass",
    "bool"> {
  let Registry = MLIROptsReg;
}

def MLIR_PrintIRAfterChange : Option<
    "mlir-print-ir-after-change",
    "When printing the IR after a pass, only print if the IR changed",
    "bool"> {
  let Registry = MLIROptsReg;
}

def MLIR_PrintIRAfterFailure : Option<
    "mlir-print-ir-after-failure",
    "When printing the IR after a pass, only print if the pass failed",
    "bool"> {
  let Registry = MLIROptsReg;
}

def MLIR_PrintIRModuleScope : Option<
    "mlir-print-ir-module-scope",
    "When printing IR for print-ir-[before|after]{-all} always print the "
    "top-level operation",
    "bool"> {
  let Registry = MLIROptsReg;
}

def MLIR_PrintIRTreeDir : Option<
    "mlir-print-ir-tree-dir",
    "When printing the IR before/after a pass, print file tree rooted at "
    "this directory. Use in conjunction with mlir-print-ir-* flags",
    "std::string"> {
  let Registry = MLIROptsReg;
}

def MLIR_PassStatistics : Option<
    "mlir-pass-statistics", "Display the statistics of each pass",
    "bool"> {
  let Registry = MLIROptsReg;
}

def MLIRPassStatisticsDisplayList : EnumValue<"list",
    "Display as a list"> {
  let CppName = "List";
}

def MLIRPassStatisticsDisplayPipeline : EnumValue<"pipeline",
    "Display as a pipeline"> {
  let CppName = "Pipeline";
}

def MLIRPassStatisticsDisplayEnum : EnumType<"MLIRPassStatisticsDisplay",
    [MLIRPassStatisticsDisplayList, MLIRPassStatisticsDisplayPipeline]>;

def MLIR_PassStatisticsDisplay : Option<
    "mlir-pass-statistics-display", "Display method for pass statistics",
    "MLIRPassStatisticsDisplay"> {
  let Registry = MLIROptsReg;
  let Default = "pipeline";
}

// ============================================================================
// Timing.cpp options (DefaultTimingManager)
// ============================================================================

def MLIR_Timing : Option<
    "mlir-timing", "Display execution times",
    "bool"> {
  let Registry = MLIROptsReg;
}

def MLIRTimingDisplayList : EnumValue<"list",
    "display the results in a list sorted by total time"> {
  let CppName = "List";
}

def MLIRTimingDisplayTree : EnumValue<"tree",
    "display the results ina with a nested tree view"> {
  let CppName = "Tree";
}

def MLIRTimingDisplayEnum : EnumType<"MLIRTimingDisplay",
    [MLIRTimingDisplayList, MLIRTimingDisplayTree]>;

def MLIR_TimingDisplay : Option<
    "mlir-timing-display", "Display method for timing data",
    "MLIRTimingDisplay"> {
  let Registry = MLIROptsReg;
  let Default = "tree";
}

def MLIROutputFormatText : EnumValue<"text",
    "display the results in text format"> {
  let CppName = "Text";
}

def MLIROutputFormatJson : EnumValue<"json",
    "display the results in JSON format"> {
  let CppName = "Json";
}

def MLIROutputFormatEnum : EnumType<"MLIROutputFormat",
    [MLIROutputFormatText, MLIROutputFormatJson]>;

def MLIR_OutputFormat : Option<
    "mlir-output-format", "Output format for timing data",
    "MLIROutputFormat"> {
  let Registry = MLIROptsReg;
  let Default = "text";
}

// ============================================================================
// CLOptionsSetup.cpp options (DebugConfig)
// ============================================================================

def MLIR_LogActionsTo : Option<
    "log-actions-to",
    "Log action execution to a file, or stderr if '-' is passed",
    "std::string"> {
  let Registry = MLIROptsReg;
}

def MLIR_ProfileActionsTo : Option<
    "profile-actions-to",
    "Profile action execution to a file, or stderr if '-' is passed",
    "std::string"> {
  let Registry = MLIROptsReg;
}

def MLIR_LogActionsFilter : ListOption<
    "log-mlir-actions-filter",
    "Comma separated list of locations to filter actions from logging",
    "std::string"> {
  let Registry = MLIROptsReg;
  let CommaSeparated = 1;
}

def MLIR_EnableDebuggerHook : Option<
    "mlir-enable-debugger-hook",
    "Enable Debugger hook for debugging MLIR Actions",
    "bool"> {
  let Registry = MLIROptsReg;
}

// ============================================================================
// DebugCounter.cpp options
// ============================================================================

def MLIR_DebugCounter : ListOption<
    "mlir-debug-counter",
    "Comma separated list of debug counter skip and count arguments",
    "std::string"> {
  let Registry = MLIROptsReg;
  let CommaSeparated = 1;
}

def MLIR_PrintDebugCounter : Option<
    "mlir-print-debug-counter",
    "Print out debug counter information after all counters have been "
    "accumulated",
    "bool"> {
  let Registry = MLIROptsReg;
}
