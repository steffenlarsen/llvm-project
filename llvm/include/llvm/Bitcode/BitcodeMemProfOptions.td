//===- BitcodeMemProfOptions.td - Bitcode options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// These 2 options are read from combined/summary-index-only reader and writer
// paths (e.g. llvm::writeIndexToFile()'s callers) that have no Module or
// LLVMContext in scope, so LLVMContext::getOptions<T>() is not reachable at
// those sites. They read BitcodeMemProfOptions::Current directly instead, the
// same no-context fallback used by e.g. CGDataOptions and WebAssemblyOptions.
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def BitcodeMemProfOptions : OptionsStruct<"llvm"> {}

defm BC_CombinedIndexMemProfContext : BoolField<"combined-index-memprof-context",
    "llvm::bitcode::CombinedIndexMemProfContextDefault",
    "Include allocation context in combined memprof summary", 1>;

defm BC_PrintSummaryGUIDs : BoolField<"print-summary-global-ids", "false",
    "Print the global id for each value when reading the module summary", 1>;
