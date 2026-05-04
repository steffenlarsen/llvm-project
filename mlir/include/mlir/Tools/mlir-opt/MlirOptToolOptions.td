//===- MlirOptToolOptions.td - CLV2 option defs for mlir-opt ---*- tablegen -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def MlirOptToolOptsReg : Registry<"llvm::mlir_opt_tool_opts"> {
  // Getter bodies are compiled once, in MlirOptToolOptionsGetters.cpp.
  let OutOfLineGetters = 1;
  let ContextTypes = [];
}

// --- General tool options ---

def MLIROPT_AllowUnregisteredDialect : Option<
    "allow-unregistered-dialect",
    "Allow operation with no registered dialects",
    "bool"> {
  let Registry = MlirOptToolOptsReg;
}

def MLIROPT_DumpPassPipeline : Option<
    "dump-pass-pipeline", "Print the pipeline that will be run",
    "bool"> {
  let Registry = MlirOptToolOptsReg;
}

def MLIROPT_EmitBytecode : Option<
    "emit-bytecode", "Emit bytecode when generating output",
    "bool"> {
  let Registry = MlirOptToolOptsReg;
}

def MLIROPT_ElideResourceData : Option<
    "elide-resource-data-from-bytecode",
    "Elide resources when generating bytecode",
    "bool"> {
  let Registry = MlirOptToolOptsReg;
}

def MLIROPT_EmitBytecodeProducer : Option<
    "emit-bytecode-producer",
    "Use specified producer when generating bytecode output",
    "std::string"> {
  let Registry = MlirOptToolOptsReg;
}

def MLIROPT_EmitBytecodeVersion : Option<
    "emit-bytecode-version",
    "Use specified bytecode when generating output",
    "int64_t"> {
  let Registry = MlirOptToolOptsReg;
}

def MLIROPT_IrdlFile : Option<
    "irdl-file", "IRDL file to register before processing the input",
    "std::string"> {
  let Registry = MlirOptToolOptsReg;
  let ValueDesc = "filename";
}

// --- Diagnostic verbosity level enum ---

def MLIROpt_VerbosityLevelErrorsOnly : EnumValue<"errors",
    "Errors only"> {
  let CppName = "ErrorsOnly";
  let Value = 0;
}

def MLIROpt_VerbosityLevelErrorsAndWarnings : EnumValue<"warnings",
    "Errors and warnings"> {
  let CppName = "ErrorsAndWarnings";
}

def MLIROpt_VerbosityLevelErrorsWarningsAndRemarks : EnumValue<"remarks",
    "Errors, warnings and remarks"> {
  let CppName = "ErrorsWarningsAndRemarks";
}

def MLIROpt_VerbosityLevelEnum : EnumType<"MLIROpt_VerbosityLevel",
    [MLIROpt_VerbosityLevelErrorsOnly,
     MLIROpt_VerbosityLevelErrorsAndWarnings,
     MLIROpt_VerbosityLevelErrorsWarningsAndRemarks]>;

def MLIROPT_DiagnosticVerbosityLevel : Option<
    "mlir-diagnostic-verbosity-level",
    "Choose level of diagnostic information",
    "MLIROpt_VerbosityLevel"> {
  let Registry = MlirOptToolOptsReg;
  let Default = "remarks";
}

def MLIROPT_DisableDiagnosticNotes : Option<
    "mlir-disable-diagnostic-notes", "Disable diagnostic notes.",
    "bool"> {
  let Registry = MlirOptToolOptsReg;
}

def MLIROPT_NoImplicitModule : Option<
    "no-implicit-module",
    "Disable implicit addition of a top-level module op during parsing",
    "bool"> {
  let Registry = MlirOptToolOptsReg;
}

def MLIROPT_ListPasses : Option<
    "list-passes", "Print the list of registered passes and exit",
    "bool"> {
  let Registry = MlirOptToolOptsReg;
}

def MLIROPT_RunReproducer : Option<
    "run-reproducer", "Run the pipeline stored in the reproducer",
    "bool"> {
  let Registry = MlirOptToolOptsReg;
}

def MLIROPT_ShowDialects : Option<
    "show-dialects", "Print the list of registered dialects and exit",
    "bool"> {
  let Registry = MlirOptToolOptsReg;
}

def MLIROPT_SplitInputFile : Option<
    "split-input-file",
    "Split the input file into chunks using the given or default marker "
    "and process each chunk independently",
    "std::string"> {
  let Registry = MlirOptToolOptsReg;
  let ValueOptional = 1;
}

def MLIROPT_OutputSplitMarker : Option<
    "output-split-marker", "Split marker to use for merging the output",
    "std::string"> {
  let Registry = MlirOptToolOptsReg;
  let Default = "\"// -----\"";
}

// --- Verify diagnostics enum ---

def MLIROpt_VerifyDiagLevelAll : EnumValue<"all",
    "Check all diagnostics (expected, unexpected, near-misses)"> {
  let CppName = "All";
}

def MLIROpt_VerifyDiagLevelDefault : EnumValue<"",
    "Check all diagnostics (default)"> {
  let CppName = "All";
  let AliasOnly = 1;
}

def MLIROpt_VerifyDiagLevelOnlyExpected : EnumValue<"only-expected",
    "Check only expected diagnostics"> {
  let CppName = "OnlyExpected";
}

def MLIROpt_VerifyDiagLevelEnum : EnumType<"MLIROpt_VerifyDiagLevel",
    [MLIROpt_VerifyDiagLevelAll, MLIROpt_VerifyDiagLevelDefault,
     MLIROpt_VerifyDiagLevelOnlyExpected]>;

def MLIROPT_VerifyDiagnostics : Option<
    "verify-diagnostics",
    "Check that emitted diagnostics match expected-* lines on the "
    "corresponding line",
    "MLIROpt_VerifyDiagLevel"> {
  let Registry = MlirOptToolOptsReg;
  let ValueOptional = 1;
}

def MLIROPT_VerifyEach : Option<
    "verify-each", "Run the verifier after each transformation pass",
    "bool"> {
  let Registry = MlirOptToolOptsReg;
  let Default = "1";
}

def MLIROPT_DisableVerifierOnParsing : Option<
    "mlir-very-unsafe-disable-verifier-on-parsing",
    "Disable the verifier on parsing (very unsafe)",
    "bool"> {
  let Registry = MlirOptToolOptsReg;
}

def MLIROPT_VerifyRoundtrip : Option<
    "verify-roundtrip",
    "Round-trip the IR after parsing and ensure it succeeds",
    "bool"> {
  let Registry = MlirOptToolOptsReg;
}

def MLIROPT_GenerateReproducer : Option<
    "mlir-generate-reproducer",
    "Generate an mlir reproducer at the provided filename "
    "(no crash required)",
    "std::string"> {
  let Registry = MlirOptToolOptsReg;
  let ValueDesc = "filename";
}

// --- Remark format enum ---

def MLIROpt_RemarkFormatEmitRemark : EnumValue<"emitRemark",
    "Print as emitRemark to command-line"> {
  let CppName = "EmitRemark";
  let Value = 0;
}

def MLIROpt_RemarkFormatYAML : EnumValue<"yaml",
    "Output remarks in YAML format"> {
  let CppName = "YAML";
}

def MLIROpt_RemarkFormatEnum : EnumType<"MLIROpt_RemarkFormat",
    [MLIROpt_RemarkFormatEmitRemark, MLIROpt_RemarkFormatYAML]>;

def MLIROPT_RemarkFormat : Option<
    "remark-format", "Specify the format for remark output.",
    "MLIROpt_RemarkFormat"> {
  let Registry = MlirOptToolOptsReg;
  let Default = "emitRemark";
}

// --- Remark policy enum ---

def MLIROpt_RemarkPolicyAll : EnumValue<"all",
    "Print all remarks"> {
  let CppName = "All";
  let Value = 0;
}

def MLIROpt_RemarkPolicyFinal : EnumValue<"final",
    "Print final remarks"> {
  let CppName = "Final";
}

def MLIROpt_RemarkPolicyEnum : EnumType<"MLIROpt_RemarkPolicy",
    [MLIROpt_RemarkPolicyAll, MLIROpt_RemarkPolicyFinal]>;

def MLIROPT_RemarkPolicy : Option<
    "remark-policy", "Specify the policy for remark output.",
    "MLIROpt_RemarkPolicy"> {
  let Registry = MlirOptToolOptsReg;
  let Default = "all";
}

// --- Remark filter options ---

def MLIROPT_RemarksOutputFile : Option<
    "remarks-output-file",
    "Output filename for remarks, using the format specified via "
    "-remark-format. e.g. mlir-remarks.yaml or mlir-remarks.bitstream",
    "std::string"> {
  let Registry = MlirOptToolOptsReg;
}

def MLIROPT_RemarksFilter : Option<
    "remarks-filter",
    "Show all remarks: passed, missed, failed, analysis",
    "std::string"> {
  let Registry = MlirOptToolOptsReg;
}

def MLIROPT_RemarksFilterPassed : Option<
    "remarks-filter-passed", "Show passed remarks",
    "std::string"> {
  let Registry = MlirOptToolOptsReg;
}

def MLIROPT_RemarksFilterFailed : Option<
    "remarks-filter-failed", "Show failed remarks",
    "std::string"> {
  let Registry = MlirOptToolOptsReg;
}

def MLIROPT_RemarksFilterMissed : Option<
    "remarks-filter-missed", "Show missed remarks",
    "std::string"> {
  let Registry = MlirOptToolOptsReg;
}

def MLIROPT_RemarksFilterAnalyse : Option<
    "remarks-filter-analyse", "Show analysis remarks",
    "std::string"> {
  let Registry = MlirOptToolOptsReg;
}
