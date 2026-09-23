//===- LTOOptions.td - LTO options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def LTOOptions : OptionsStruct<"llvm"> {}

// --- LTOCodeGenerator.cpp ---

// Consumers (LTOCodeGenerator.cpp's getLTODiscardValueNames(),
// ThinLTOCodeGenerator.cpp's getLTODiscardValueNames()) fall back to
// NDEBUG ? true : false when this was never specified on the command line --
// a build-config-dependent default that can't be expressed as a single
// literal ValueField default -- hence std::optional<bool> via
// OptionalValueField rather than a plain BoolField.
defm LTO_DiscardValueNames : OptionalValueField<
    "lto-discard-value-names", "bool", "true",
    "Strip names from Value during LTO (other than GlobalValue).",
    /*hidden=*/1>;

defm LTO_PassRemarksWithHotness : BoolField<
    "lto-pass-remarks-with-hotness", "false",
    "With PGO, include profile count in optimization remarks",
    /*hidden=*/1>;

// Also needs optional semantics (consumers distinguish "never specified"
// from "0"), but the original clv2 Option had no bare-flag spelling, so
// this deliberately uses ValueField -- with an std::optional<unsigned>
// FieldType, which parseFieldValue's generic std::optional<T> overload
// (LibraryOptions.h) already supports regardless of which multiclass
// produced the Field<> -- rather than OptionalValueField, which would add
// an unwanted new bare "-lto-pass-remarks-hotness-threshold" spelling.
// NOTE: despite the stale-looking ValueDesc text below, this option's own
// C++ type is plain unsigned; it does not itself parse the literal string
// "auto". That string-based translation happens in unrelated, separate,
// tool-local options (e.g. opt/llc/llvm-lto2's own RemarksHotnessThreshold).
defm LTO_PassRemarksHotnessThreshold : ValueField<
    "lto-pass-remarks-hotness-threshold", "std::optional<unsigned>",
    "std::nullopt",
    "Minimum profile count required for an optimization remark to be "
    "output. Use 'auto' to apply the threshold from profile summary.",
    /*hidden=*/1, /*metaVar=*/"uint or 'auto'">;

defm LTO_PassRemarksOutput : ValueField<
    "lto-pass-remarks-output", "std::string", "\"\"",
    "Output filename for pass remarks",
    /*hidden=*/1, /*metaVar=*/"filename">;

defm LTO_PassRemarksFilter : ValueField<
    "lto-pass-remarks-filter", "std::string", "\"\"",
    "Only record optimization remarks from passes whose names match the "
    "given regular expression",
    /*hidden=*/1, /*metaVar=*/"regex">;

defm LTO_PassRemarksFormat : ValueField<
    "lto-pass-remarks-format", "std::string", "\"yaml\"",
    "The format used for serializing remarks (default: YAML)",
    /*hidden=*/1, /*metaVar=*/"format">;

defm LTO_StatsFile : ValueField<
    "lto-stats-file", "std::string", "\"\"",
    "Save statistics to the specified file",
    /*hidden=*/1>;

defm LTO_AIXSystemAssembler : ValueField<
    "lto-aix-system-assembler", "std::string", "\"\"",
    "Path to a system assembler, picked up on AIX only",
    /*hidden=*/1, /*metaVar=*/"path">;

defm LTO_CSProfileGenerate : BoolField<
    "cs-profile-generate", "false",
    "Perform context sensitive PGO instrumentation",
    /*hidden=*/1>;

defm LTO_CSProfilePath : ValueField<
    "cs-profile-path", "std::string", "\"\"",
    "Context sensitive profile file path",
    /*hidden=*/1>;

// --- LTO.cpp ---

defm LTO_DumpThinCGSCCs : BoolField<
    "dump-thin-cg-sccs", "false",
    "Dump the SCCs in the ThinLTO index's callgraph",
    /*hidden=*/1>;

defm LTO_EnableInternalization : BoolField<
    "enable-lto-internalization", "true",
    "Enable global value internalization in LTO",
    /*hidden=*/1>;

defm LTO_KeepSymbolCopies : BoolField<
    "lto-keep-symbol-copies", "false",
    "Keep copies of symbols in LTO indexing",
    /*hidden=*/1>;

// --- ThinLTOCodeGenerator.cpp ---

defm LTO_ThreadCount : ValueField<
    "threads", "int", "0", "",
    /*hidden=*/1>;

// --- LTOBackend.cpp ---

def LTOBitcodeEmbeddingEnum : OptionEnum<"LTOBitcodeEmbedding", [
    EnumMember<"DoNotEmbed", "none">,
    EnumMember<"EmbedOptimized", "optimized">,
    EnumMember<"EmbedPostMergePreOptimized", "post-merge-pre-opt">]>;

defm LTO_EmbedBitcode : EnumField<
    "lto-embed-bitcode", LTOBitcodeEmbeddingEnum,
    "LTOBitcodeEmbedding::DoNotEmbed",
    "Embed LLVM bitcode in object files produced by LTO">;

defm LTO_ThinLTOAssumeMerged : BoolField<
    "thinlto-assume-merged", "false",
    "Assume the input has already undergone ThinLTO function "
    "importing and the other pre-optimization pipeline changes.">;

defm LTO_FilterSaveModules : ListField<
    "filter-save-modules", "std::string",
    "Only save bitcode for module whose name without "
    "path matches this for -save-temps options",
    /*commaJoined=*/1, /*appendEquals=*/1, /*hidden=*/1,
    /*metaVar=*/"module names">;
