//===- ClangCodeGenOptions.td - CLV2 option defs -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def ClangCodeGenOptsReg : Registry<"clang_codegen_opts"> {
  // Getter bodies are compiled once, in ClangCodeGenOptionsGetters.cpp.
  let OutOfLineGetters = 1;
  let ContextTypes = [];
}

// --- Enum-as-int for pgo-cold-func-opt ---
// This uses makeEnumOption<int> with hardcoded int values.

def PgoColdFuncOptDefault : EnumValue<"default", "Default (no attribute)"> {
  let CppName = "0";
  let Value = 0;
}
def PgoColdFuncOptOptsize : EnumValue<"optsize",
    "Mark cold functions with optsize."> {
  let CppName = "1";
  let Value = 1;
}
def PgoColdFuncOptMinsize : EnumValue<"minsize",
    "Mark cold functions with minsize."> {
  let CppName = "2";
  let Value = 2;
}
def PgoColdFuncOptOptnone : EnumValue<"optnone",
    "Mark cold functions with optnone."> {
  let CppName = "3";
  let Value = 3;
}
def PgoColdFuncOptEnum : EnumType<"int",
    [PgoColdFuncOptDefault, PgoColdFuncOptOptsize,
     PgoColdFuncOptMinsize, PgoColdFuncOptOptnone]> {
  let External = 1;
}

// --- BackendUtil.cpp ---

def CLANGCG_SanitizerEarlyOptEp : Option<
    "sanitizer-early-opt-ep",
    "Insert sanitizers on OptimizerEarlyEP.",
    "bool"> {
  let Registry = ClangCodeGenOptsReg;
  let Default = "false";
}

def CLANGCG_PgoColdFuncOpt : Option<
    "pgo-cold-func-opt",
    "Function attribute to apply to cold functions as determined by PGO",
    "int"> {
  let Registry = ClangCodeGenOptsReg;
  let Default = "default";
  let Hidden = 1;
}

// --- CGExpr.cpp ---

def CLANGCG_UbsanGuardChecks : Option<
    "ubsan-guard-checks",
    "Guard UBSAN checks with `llvm.allow.ubsan.check()`.",
    "bool"> {
  let Registry = ClangCodeGenOptsReg;
  let Default = "false";
}

// --- CodeGenModule.cpp ---

def CLANGCG_LimitedCoverageExperimental : Option<
    "limited-coverage-experimental",
    "Emit limited coverage mapping information (experimental)",
    "bool"> {
  let Registry = ClangCodeGenOptsReg;
  let Default = "false";
  let Hidden = 1;
}

// --- CodeGenPGO.cpp ---

def CLANGCG_EnableValueProfiling : Option<
    "enable-value-profiling",
    "Enable value profiling",
    "bool"> {
  let Registry = ClangCodeGenOptsReg;
  let Default = "false";
  let Hidden = 1;
}

// --- CoverageMappingGen.cpp ---

def CLANGCG_EnableSingleByteCoverage : Option<
    "enable-single-byte-coverage",
    "Enable single byte coverage",
    "bool"> {
  let Registry = ClangCodeGenOptsReg;
  let Default = "false";
  let Hidden = 1;
}

def CLANGCG_EmptylineCommentCoverage : Option<
    "emptyline-comment-coverage",
    "Emit emptylines and comment lines as skipped regions (only "
    "disable it on test)",
    "bool"> {
  let Registry = ClangCodeGenOptsReg;
  let Default = "true";
  let Hidden = 1;
}

def CLANGCG_SystemHeadersCoverage : Option<
    "system-headers-coverage",
    "Enable collecting coverage from system headers",
    "bool"> {
  let Registry = ClangCodeGenOptsReg;
  let Default = "false";
  let Hidden = 1;
}

// --- Tooling/AllTUsExecution.cpp ---

def CLANGCG_Filter : Option<
    "filter",
    "Only process files that match this filter. "
    "This flag only applies to all-TUs.",
    "std::string"> {
  let Registry = ClangCodeGenOptsReg;
}

def CLANGCG_ExecuteConcurrency : Option<
    "execute-concurrency",
    "The number of threads used to process all files in "
    "parallel. Set to 0 for hardware concurrency. "
    "This flag only applies to all-TUs.",
    "unsigned"> {
  let Registry = ClangCodeGenOptsReg;
  let Default = "0";
}

// --- Tooling/Execution.cpp ---

def CLANGCG_Executor : Option<
    "executor",
    "The name of the executor to use.",
    "std::string"> {
  let Registry = ClangCodeGenOptsReg;
}

// --- Analysis/FlowSensitive/DataflowAnalysisContext.cpp ---

def CLANGCG_DataflowLog : Option<
    "dataflow-log",
    "Emit log of dataflow analysis. With no arg, writes textual "
    "log to stderr. With an arg, writes HTML logs under the "
    "specified directory (one per analyzed function).",
    "std::string"> {
  let Registry = ClangCodeGenOptsReg;
  let ValueOptional = 1;
  let Hidden = 1;
}
