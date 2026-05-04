//===- BitcodeOptions.td - Bitcode options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def BitcodeOptsReg : Registry<"llvm::bc_opts"> {
  // Getter bodies are compiled once, in BitcodeOptionsGetters.cpp.
  let OutOfLineGetters = 1;
  // Getter bodies are compiled once, in BitcodeOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

def BC_MDIndexThreshold : Option<
    "bitcode-mdindex-threshold",
    "Number of metadatas above which we emit an index to enable lazy-loading",
    "unsigned"> {
  let Registry = BitcodeOptsReg;
  let Default = "25";
  let Hidden = 1;
}

def BC_CombinedIndexMemProfContext : Option<
    "combined-index-memprof-context",
    "Include allocation context in combined memprof summary",
    "bool"> {
  let Registry = BitcodeOptsReg;
  let Default = "llvm::bitcode::CombinedIndexMemProfContextDefault";
  let Hidden = 1;
}

def BC_PreserveBCUseListOrder : Option<
    "preserve-bc-uselistorder",
    "Preserve use-list order when writing LLVM bitcode.",
    "bool"> {
  let Registry = BitcodeOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def BC_ImportFullTypeDefinitions : Option<
    "import-full-type-definitions",
    "Import full type definitions for ThinLTO.",
    "bool"> {
  let Registry = BitcodeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def BC_DisableLazyLoading : Option<
    "disable-ondemand-mds-loading",
    "Force disable the lazy-loading on-demand of metadata when loading bitcode "
    "for importing.",
    "bool"> {
  let Registry = BitcodeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def BC_PrintSummaryGUIDs : Option<
    "print-summary-global-ids",
    "Print the global id for each value when reading the module summary",
    "bool"> {
  let Registry = BitcodeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def BC_ExpandConstantExprs : Option<
    "expand-constant-exprs",
    "Expand constant expressions to instructions for testing purposes",
    "bool"> {
  let Registry = BitcodeOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def BC_FlushThreshold : Option<
    "bitcode-flush-threshold",
    "The threshold (unit M) for flushing LLVM bitcode.",
    "unsigned"> {
  let Registry = BitcodeOptsReg;
  let Default = "512";
  let Hidden = 1;
}
