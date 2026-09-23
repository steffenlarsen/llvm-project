//===- CGDataOptions.td - Options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def CGDataOptions : OptionsStruct<"llvm"> {}

// --- CodeGenData.cpp ---

defm CGD_CodeGenDataGenerate : BoolField<
    "codegen-data-generate", "false",
    "Emit CodeGen Data into custom sections",
    /*hidden=*/1>;

defm CGD_CodeGenDataUsePath : ValueField<
    "codegen-data-use-path", "std::string", "",
    "File path to where .cgdata file is read",
    /*hidden=*/1>;

defm CGD_CodeGenDataThinLTOTwoRounds : BoolField<
    "codegen-data-thinlto-two-rounds", "false",
    "Enable two-round ThinLTO code generation",
    /*hidden=*/1>;

// --- CodeGenDataReader.cpp ---

defm CGD_IndexedCodeGenDataReadFunctionMapNames : BoolField<
    "indexed-codegen-data-read-function-map-names", "true",
    "Read function map names in indexed CodeGenData",
    /*hidden=*/1>;

// --- StableFunctionMap.cpp ---

defm CGD_GlobalMergingMinMerges : ValueField<
    "global-merging-min-merges", "unsigned", "2",
    "Minimum number of similar functions with the same hash required for "
    "merging.",
    /*hidden=*/1>;

defm CGD_GlobalMergingMinInstrs : ValueField<
    "global-merging-min-instrs", "unsigned", "1",
    "The minimum instruction count required when merging functions.",
    /*hidden=*/1>;

defm CGD_GlobalMergingMaxParams : ValueField<
    "global-merging-max-params", "unsigned", "4294967295",
    "The maximum number of parameters allowed when merging functions.",
    /*hidden=*/1>;

defm CGD_GlobalMergingSkipNoParams : BoolField<
    "global-merging-skip-no-params", "true",
    "Skip merging functions with no parameters.",
    /*hidden=*/1>;

defm CGD_GlobalMergingInstOverhead : ValueField<
    "global-merging-inst-overhead", "double", "1.2",
    "The overhead cost associated with each instruction when lowering to "
    "machine instruction.",
    /*hidden=*/1>;

defm CGD_GlobalMergingParamOverhead : ValueField<
    "global-merging-param-overhead", "double", "2.0",
    "The overhead cost associated with each parameter when merging "
    "functions.",
    /*hidden=*/1>;

defm CGD_GlobalMergingCallOverhead : ValueField<
    "global-merging-call-overhead", "double", "1.0",
    "The overhead cost associated with each function call when merging "
    "functions.",
    /*hidden=*/1>;

defm CGD_GlobalMergingExtraThreshold : ValueField<
    "global-merging-extra-threshold", "double", "0.0",
    "An additional cost threshold that must be exceeded for merging to be "
    "considered beneficial.",
    /*hidden=*/1>;
