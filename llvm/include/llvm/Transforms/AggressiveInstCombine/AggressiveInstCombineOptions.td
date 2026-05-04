//===- AggressiveInstCombineOptions.td - Options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def AggressiveInstCombineOptsReg : Registry<"llvm::aic_opts"> {
  // Getter bodies are compiled once, in AggressiveInstCombineOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

def AIC_MaxInstrsToScan : Option<
    "aggressive-instcombine-max-scan-instrs",
    "Max number of instructions to scan for aggressive instcombine.",
    "unsigned"> {
  let Registry = AggressiveInstCombineOptsReg;
  let Default = "64";
  let Hidden = 1;
}

def AIC_StrNCmpInlineThreshold : Option<
    "strncmp-inline-threshold",
    "The maximum length of a constant string for a builtin string cmp "
    "call eligible for inlining. The default value is 3.",
    "unsigned"> {
  let Registry = AggressiveInstCombineOptsReg;
  let Default = "3";
  let Hidden = 1;
}

def AIC_MemChrInlineThreshold : Option<
    "memchr-inline-threshold",
    "The maximum length of a constant string to inline a memchr call.",
    "unsigned"> {
  let Registry = AggressiveInstCombineOptsReg;
  let Default = "3";
  let Hidden = 1;
}
