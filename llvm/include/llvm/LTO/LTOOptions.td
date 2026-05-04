//===- LTOOptions.td - CLV2 option defs for LTO ----------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def LTOOptsReg : Registry<"llvm::lto_opts"> {
  // Getter bodies are compiled once, in LTOOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

// --- LTOCodeGenerator.cpp ---

def LTO_DiscardValueNames : Option<
    "lto-discard-value-names",
    "Strip names from Value during LTO (other than GlobalValue).",
    "bool"> {
  let Registry = LTOOptsReg;
  let Hidden = 1;
}

def LTO_PassRemarksWithHotness : Option<
    "lto-pass-remarks-with-hotness",
    "With PGO, include profile count in optimization remarks",
    "bool"> {
  let Registry = LTOOptsReg;
  let Hidden = 1;
}

def LTO_PassRemarksHotnessThreshold : Option<
    "lto-pass-remarks-hotness-threshold",
    "Minimum profile count required for an "
    "optimization remark to be output."
    " Use 'auto' to apply the threshold from profile summary.",
    "unsigned"> {
  let Registry = LTOOptsReg;
  let ValueDesc = "uint or 'auto'";
  let Hidden = 1;
}

def LTO_PassRemarksOutput : Option<
    "lto-pass-remarks-output",
    "Output filename for pass remarks",
    "std::string"> {
  let Registry = LTOOptsReg;
  let ValueDesc = "filename";
  let Hidden = 1;
}

def LTO_PassRemarksFilter : Option<
    "lto-pass-remarks-filter",
    "Only record optimization remarks from passes whose "
    "names match the given regular expression",
    "std::string"> {
  let Registry = LTOOptsReg;
  let ValueDesc = "regex";
  let Hidden = 1;
}

def LTO_PassRemarksFormat : Option<
    "lto-pass-remarks-format",
    "The format used for serializing remarks (default: YAML)",
    "std::string"> {
  let Registry = LTOOptsReg;
  let Default = "\"yaml\"";
  let ValueDesc = "format";
  let Hidden = 1;
}

def LTO_StatsFile : Option<
    "lto-stats-file",
    "Save statistics to the specified file",
    "std::string"> {
  let Registry = LTOOptsReg;
  let Hidden = 1;
}

def LTO_AIXSystemAssembler : Option<
    "lto-aix-system-assembler",
    "Path to a system assembler, picked up on AIX only",
    "std::string"> {
  let Registry = LTOOptsReg;
  let ValueDesc = "path";
  let Hidden = 1;
}

def LTO_CSProfileGenerate : Option<
    "cs-profile-generate",
    "Perform context sensitive PGO instrumentation",
    "bool"> {
  let Registry = LTOOptsReg;
  let Hidden = 1;
}

def LTO_CSProfilePath : Option<
    "cs-profile-path",
    "Context sensitive profile file path",
    "std::string"> {
  let Registry = LTOOptsReg;
  let Hidden = 1;
}

// --- LTO.cpp ---

def LTO_DumpThinCGSCCs : Option<
    "dump-thin-cg-sccs",
    "Dump the SCCs in the ThinLTO index's callgraph",
    "bool"> {
  let Registry = LTOOptsReg;
  let Hidden = 1;
}

def LTO_EnableInternalization : Option<
    "enable-lto-internalization",
    "Enable global value internalization in LTO",
    "bool"> {
  let Registry = LTOOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def LTO_KeepSymbolCopies : Option<
    "lto-keep-symbol-copies",
    "Keep copies of symbols in LTO indexing",
    "bool"> {
  let Registry = LTOOptsReg;
  let Hidden = 1;
}

// --- ThinLTOCodeGenerator.cpp ---

def LTO_ThreadCount : Option<
    "threads", "",
    "int"> {
  let Registry = LTOOptsReg;
  let Hidden = 1;
}

// --- LTOBackend.cpp ---

def LTOBitcodeEmbeddingDoNotEmbed : EnumValue<"none",
    "Do not embed"> {
  let CppName = "DoNotEmbed";
  let Value = 0;
}

def LTOBitcodeEmbeddingEmbedOptimized : EnumValue<"optimized",
    "Embed after all optimization passes"> {
  let CppName = "EmbedOptimized";
  let Value = 1;
}

def LTOBitcodeEmbeddingEmbedPostMergePreOptimized : EnumValue<
    "post-merge-pre-opt",
    "Embed post merge, but before optimizations"> {
  let CppName = "EmbedPostMergePreOptimized";
  let Value = 2;
}

def LTOBitcodeEmbeddingEnum : EnumType<"LTOBitcodeEmbedding",
    [LTOBitcodeEmbeddingDoNotEmbed, LTOBitcodeEmbeddingEmbedOptimized,
     LTOBitcodeEmbeddingEmbedPostMergePreOptimized]>;

def LTO_EmbedBitcode : Option<
    "lto-embed-bitcode",
    "Embed LLVM bitcode in object files produced by LTO",
    "LTOBitcodeEmbedding"> {
  let Registry = LTOOptsReg;
  let Default = "none";
}

def LTO_ThinLTOAssumeMerged : Option<
    "thinlto-assume-merged",
    "Assume the input has already undergone ThinLTO function "
    "importing and the other pre-optimization pipeline changes.",
    "bool"> {
  let Registry = LTOOptsReg;
}

def LTO_FilterSaveModules : ListOption<
    "filter-save-modules",
    "Only save bitcode for module whose name without "
    "path matches this for -save-temps options",
    "std::string"> {
  let Registry = LTOOptsReg;
  let ValueDesc = "module names";
  let Hidden = 1;
  let CommaSeparated = 1;
}
