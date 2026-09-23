//===- InstCombineOptions.td - Options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

// Named InstCombineCLOptions (not InstCombineOptions) to avoid colliding
// with the pre-existing llvm::InstCombineOptions pass-parameter struct in
// InstCombine.h (used for pipeline text like
// "instcombine<no-verify-fixpoint>"), which is unrelated to these
// command-line options.
def InstCombineCLOptions : OptionsStruct<"llvm"> {}

// --- InstCombineNegator.cpp ---

defm IC_NegatorEnabled : BoolField<
    "instcombine-negator-enabled", "true",
    "Should we attempt to sink negations?">;

defm IC_NegatorMaxDepth : ValueField<
    "instcombine-negator-max-depth", "unsigned", "4294967295",
    "What is the maximal lookup depth when trying to check for viability of "
    "negation sinking.">;

// --- InstCombineLoadStoreAlloca.cpp ---

defm IC_MaxCopiedFromConstantUsers : ValueField<
    "instcombine-max-copied-from-constant-users", "unsigned", "300",
    "Maximum users to visit in copy from constant transform",
    /*hidden=*/1>;

// --- InstCombinePHI.cpp ---

defm IC_MaxNumPhis : ValueField<
    "instcombine-max-num-phis", "unsigned", "512",
    "Maximum number phis to handle in intptr/ptrint folding">;

// --- InstCombineSimplifyDemanded.cpp ---

defm IC_VerifyKnownBits : BoolField<
    "instcombine-verify-known-bits", "false",
    "Verify that computeKnownBits() and SimplifyDemandedBits() are consistent",
    /*hidden=*/1>;

defm IC_SimplifyDemandedVectorEltsDepthLimit : ValueField<
    "instcombine-simplify-vector-elts-depth", "unsigned", "10",
    "Depth limit when simplifying vector instructions and their operands",
    /*hidden=*/1>;

// --- InstCombineCalls.cpp ---

defm IC_GuardWideningWindow : ValueField<
    "instcombine-guard-widening-window", "unsigned", "3",
    "How wide an instruction window to bypass looking for another guard">;

// --- InstructionCombining.cpp ---

defm IC_EnableCodeSinking : BoolField<
    "instcombine-code-sinking", "true",
    "Enable code sinking">;

defm IC_MaxSinkNumUsers : ValueField<
    "instcombine-max-sink-users", "unsigned", "32",
    "Maximum number of undroppable users for instruction sinking">;

defm IC_MaxArraySize : ValueField<
    "instcombine-maxarray-size", "unsigned", "1024",
    "Maximum array size considered when doing a combine">;

defm IC_ShouldLowerDbgDeclare : ValueField<
    "instcombine-lower-dbg-declare", "unsigned", "1",
    "",
    /*hidden=*/1>;

defm IC_MaxAllocSiteRemovableUsers : ValueField<
    "instcombine-max-allocsite-removable-users", "unsigned", "2048",
    "Maximum number of users to visit in alloc-site removability analysis",
    /*hidden=*/1>;
