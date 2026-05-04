//===- MLIRTranslateOptions.td - CLV2 option defs for MLIR translation ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def MLIRTranslateOptsReg : Registry<"mlir::mlir_translate_opts"> {
  // Getter bodies are compiled once, in MLIRTranslateOptionsGetters.cpp.
  let OutOfLineGetters = 1;
  let ContextTypes = [];
}

// --- Translation.cpp ---

def MLIRT_NoImplicitModule : Option<
    "no-implicit-module",
    "Disable the parsing of an implicit top-level module op",
    "bool"> {
  let Registry = MLIRTranslateOptsReg;
}

// --- Target/Cpp/TranslateRegistration.cpp ---

def MLIRT_DeclareVariablesAtTop : Option<
    "declare-variables-at-top",
    "Declare variables at top when emitting C/C++",
    "bool"> {
  let Registry = MLIRTranslateOptsReg;
}

def MLIRT_FileId : Option<
    "file-id", "Emit emitc.file ops with matching id",
    "std::string"> {
  let Registry = MLIRTranslateOptsReg;
}

// --- Target/LLVMIR/ConvertFromLLVMIR.cpp ---

def MLIRT_EmitExpensiveWarnings : Option<
    "emit-expensive-warnings",
    "Emit expensive warnings during LLVM IR import "
    "(discouraged: testing only!)",
    "bool"> {
  let Registry = MLIRTranslateOptsReg;
}

def MLIRT_ConvertDebugRecToIntrinsics : Option<
    "convert-debug-rec-to-intrinsics",
    "Change the input LLVM module to use old debug intrinsics instead of "
    "records via convertFromNewDbgValues, this happens before importing "
    "the debug information(discouraged: to be removed soon!)",
    "bool"> {
  let Registry = MLIRTranslateOptsReg;
}

def MLIRT_DropDICompositeTypeElements : Option<
    "drop-di-composite-type-elements",
    "Avoid translating the elements of DICompositeTypes during the LLVM IR "
    "import (discouraged: testing only!)",
    "bool"> {
  let Registry = MLIRTranslateOptsReg;
}

def MLIRT_PreferUnregisteredIntrinsics : Option<
    "prefer-unregistered-intrinsics",
    "Prefer translating all intrinsics into llvm.call_intrinsic instead of "
    "using dialect supported intrinsics",
    "bool"> {
  let Registry = MLIRTranslateOptsReg;
}

def MLIRT_ImportStructsAsLiterals : Option<
    "import-structs-as-literals",
    "Controls if structs should be imported as literal structs, i.e., "
    "nameless structs.",
    "bool"> {
  let Registry = MLIRTranslateOptsReg;
}

// --- Target/SPIRV/TranslateRegistration.cpp ---

def MLIRT_SPIRVStructurizeControlFlow : Option<
    "spirv-structurize-control-flow",
    "Enable control flow structurization into `spirv.mlir.selection` and "
    "`spirv.mlir.loop`. This may need to be disabled to support "
    "deserialization of early exits (see #138688)",
    "bool"> {
  let Registry = MLIRTranslateOptsReg;
  let Default = "1";
}

def MLIRT_SPIRVSaveValidationFilesPrefix : Option<
    "spirv-save-validation-files-with-prefix",
    "When non-empty string is passed each serialized SPIR-V module is saved "
    "to an additional file that starts with the given prefix. This is used "
    "to generate separate binaries for validation, where "
    "`--split-input-file` normally combines all outputs into one. The one "
    "combined output (`-o`) is still written. Created files need to be "
    "removed manually once processed.",
    "std::string"> {
  let Registry = MLIRTranslateOptsReg;
}

// --- Target/SMTLIB/ExportSMTLIB.cpp ---

def MLIRT_SMTLIBInlineSingleUseValues : Option<
    "smtlibexport-inline-single-use-values",
    "Inline expressions that are used only once rather than generating a "
    "let-binding",
    "bool"> {
  let Registry = MLIRTranslateOptsReg;
}
