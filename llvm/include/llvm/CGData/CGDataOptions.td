//===- CGDataOptions.td - CLV2 option defs for CGData ------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def CGDataOptsReg : Registry<"llvm::cgd_opts"> {
  // Getter bodies are compiled once, in CGDataOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

// --- CodeGenData.cpp ---

def CGD_CodeGenDataGenerate : Option<
    "codegen-data-generate",
    "Emit CodeGen Data into custom sections",
    "bool"> {
  let Registry = CGDataOptsReg;
  let Hidden = 1;
}

def CGD_CodeGenDataUsePath : Option<
    "codegen-data-use-path",
    "File path to where .cgdata file is read",
    "std::string"> {
  let Registry = CGDataOptsReg;
  let Hidden = 1;
}

def CGD_CodeGenDataThinLTOTwoRounds : Option<
    "codegen-data-thinlto-two-rounds",
    "Enable two-round ThinLTO code generation",
    "bool"> {
  let Registry = CGDataOptsReg;
  let Hidden = 1;
}

// --- CodeGenDataReader.cpp ---

def CGD_IndexedCodeGenDataReadFunctionMapNames : Option<
    "indexed-codegen-data-read-function-map-names",
    "Read function map names in indexed CodeGenData",
    "bool"> {
  let Registry = CGDataOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- StableFunctionMap.cpp ---

def CGD_GlobalMergingMinMerges : Option<
    "global-merging-min-merges",
    "Minimum number of similar functions with the same hash required for "
    "merging.",
    "unsigned"> {
  let Registry = CGDataOptsReg;
  let Default = "2";
  let Hidden = 1;
}

def CGD_GlobalMergingMinInstrs : Option<
    "global-merging-min-instrs",
    "The minimum instruction count required when merging functions.",
    "unsigned"> {
  let Registry = CGDataOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def CGD_GlobalMergingMaxParams : Option<
    "global-merging-max-params",
    "The maximum number of parameters allowed when merging functions.",
    "unsigned"> {
  let Registry = CGDataOptsReg;
  let Default = "4294967295";
  let Hidden = 1;
}

def CGD_GlobalMergingSkipNoParams : Option<
    "global-merging-skip-no-params",
    "Skip merging functions with no parameters.",
    "bool"> {
  let Registry = CGDataOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def CGD_GlobalMergingInstOverhead : Option<
    "global-merging-inst-overhead",
    "The overhead cost associated with each instruction when lowering to "
    "machine instruction.",
    "double"> {
  let Registry = CGDataOptsReg;
  let Default = "1.2";
  let Hidden = 1;
}

def CGD_GlobalMergingParamOverhead : Option<
    "global-merging-param-overhead",
    "The overhead cost associated with each parameter when merging "
    "functions.",
    "double"> {
  let Registry = CGDataOptsReg;
  let Default = "2.0";
  let Hidden = 1;
}

def CGD_GlobalMergingCallOverhead : Option<
    "global-merging-call-overhead",
    "The overhead cost associated with each function call when merging "
    "functions.",
    "double"> {
  let Registry = CGDataOptsReg;
  let Default = "1.0";
  let Hidden = 1;
}

def CGD_GlobalMergingExtraThreshold : Option<
    "global-merging-extra-threshold",
    "An additional cost threshold that must be exceeded for merging to be "
    "considered beneficial.",
    "double"> {
  let Registry = CGDataOptsReg;
  let Default = "0.0";
  let Hidden = 1;
}
