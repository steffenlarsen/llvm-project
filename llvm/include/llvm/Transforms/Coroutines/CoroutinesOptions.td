//===- CoroutinesOptions.td - Coroutines options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def CoroutinesOptsReg : Registry<"llvm::coro_opts"> {
  // Getter bodies are compiled once, in CoroutinesOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

def CORO_ElideBranchRatio : Option<
    "coro-elide-branch-ratio",
    "Minimum BranchProbability to consider a elide a coroutine.",
    "float"> {
  let Registry = CoroutinesOptsReg;
  let Default = "0.55f";
  let Hidden = 1;
}

def CORO_ElideInfoOutputFile : Option<
    "coro-elide-info-output-file",
    "File to record the coroutines got elided",
    "std::string"> {
  let Registry = CoroutinesOptsReg;
  let Hidden = 1;
  let ValueDesc = "filename";
}
