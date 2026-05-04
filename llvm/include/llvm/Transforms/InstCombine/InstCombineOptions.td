//===- InstCombineOptions.td - CLV2 option defs for InstCombine -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def InstCombineOptsReg : Registry<"llvm::ic_opts"> {
  // Getter bodies are compiled once, in InstCombineOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

// --- InstCombineNegator.cpp ---

def IC_NegatorEnabled : Option<
    "instcombine-negator-enabled",
    "Should we attempt to sink negations?",
    "bool"> {
  let Registry = InstCombineOptsReg;
  let Default = "1";
}

def IC_NegatorMaxDepth : Option<
    "instcombine-negator-max-depth",
    "What is the maximal lookup depth when trying to "
    "check for viability of negation sinking.",
    "unsigned"> {
  let Registry = InstCombineOptsReg;
  let Default = "4294967295";
}

// --- InstCombineLoadStoreAlloca.cpp ---

def IC_MaxCopiedFromConstantUsers : Option<
    "instcombine-max-copied-from-constant-users",
    "Maximum users to visit in copy from constant transform",
    "unsigned"> {
  let Registry = InstCombineOptsReg;
  let Default = "300";
  let Hidden = 1;
}

// --- InstCombinePHI.cpp ---

def IC_MaxNumPhis : Option<
    "instcombine-max-num-phis",
    "Maximum number phis to handle in intptr/ptrint folding",
    "unsigned"> {
  let Registry = InstCombineOptsReg;
  let Default = "512";
}

// --- InstCombineSimplifyDemanded.cpp ---

def IC_VerifyKnownBits : Option<
    "instcombine-verify-known-bits",
    "Verify that computeKnownBits() and "
    "SimplifyDemandedBits() are consistent",
    "bool"> {
  let Registry = InstCombineOptsReg;
  let Hidden = 1;
}

def IC_SimplifyDemandedVectorEltsDepthLimit : Option<
    "instcombine-simplify-vector-elts-depth",
    "Depth limit when simplifying vector instructions and their operands",
    "unsigned"> {
  let Registry = InstCombineOptsReg;
  let Default = "10";
  let Hidden = 1;
}

// --- InstCombineCalls.cpp ---

def IC_GuardWideningWindow : Option<
    "instcombine-guard-widening-window",
    "How wide an instruction window to bypass looking for "
    "another guard",
    "unsigned"> {
  let Registry = InstCombineOptsReg;
  let Default = "3";
}

// --- InstructionCombining.cpp ---

def IC_EnableCodeSinking : Option<
    "instcombine-code-sinking",
    "Enable code sinking",
    "bool"> {
  let Registry = InstCombineOptsReg;
  let Default = "1";
}

def IC_MaxSinkNumUsers : Option<
    "instcombine-max-sink-users",
    "Maximum number of undroppable users for instruction sinking",
    "unsigned"> {
  let Registry = InstCombineOptsReg;
  let Default = "32";
}

def IC_MaxArraySize : Option<
    "instcombine-maxarray-size",
    "Maximum array size considered when doing a combine",
    "unsigned"> {
  let Registry = InstCombineOptsReg;
  let Default = "1024";
}

def IC_ShouldLowerDbgDeclare : Option<
    "instcombine-lower-dbg-declare", "",
    "unsigned"> {
  let Registry = InstCombineOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def IC_MaxAllocSiteRemovableUsers : Option<
    "instcombine-max-allocsite-removable-users",
    "Maximum number of users to visit in alloc-site "
    "removability analysis",
    "unsigned"> {
  let Registry = InstCombineOptsReg;
  let Default = "2048";
  let Hidden = 1;
}
