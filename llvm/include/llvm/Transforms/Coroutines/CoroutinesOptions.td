//===- CoroutinesOptions.td - Coroutines options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def CoroutinesOptions : OptionsStruct<"llvm"> {}

defm CORO_ElideBranchRatio : ValueField<
    "coro-elide-branch-ratio", "float", "0.55f",
    "Minimum BranchProbability to consider a elide a coroutine.",
    /*hidden=*/1>;

defm CORO_ElideInfoOutputFile : ValueField<
    "coro-elide-info-output-file", "std::string", "",
    "File to record the coroutines got elided",
    /*hidden=*/1, /*metaVar=*/"filename">;
