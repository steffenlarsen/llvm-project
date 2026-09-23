//===- AggressiveInstCombineOptions.td - Options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def AggressiveInstCombineOptions : OptionsStruct<"llvm"> {}

defm AIC_MaxInstrsToScan : ValueField<
    "aggressive-instcombine-max-scan-instrs", "unsigned", "64",
    "Max number of instructions to scan for aggressive instcombine.",
    /*hidden=*/1>;

defm AIC_StrNCmpInlineThreshold : ValueField<
    "strncmp-inline-threshold", "unsigned", "3",
    "The maximum length of a constant string for a builtin string cmp "
    "call eligible for inlining. The default value is 3.",
    /*hidden=*/1>;

defm AIC_MemChrInlineThreshold : ValueField<
    "memchr-inline-threshold", "unsigned", "3",
    "The maximum length of a constant string to inline a memchr call.",
    /*hidden=*/1>;
