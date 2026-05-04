//===- M68kOptions.td - M68k target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def M68kOptsReg : Registry<"llvm::m68k_opts"> {
  // Getter bodies are compiled once, in M68kOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

def M68K_SSThreshold : Option<
    "m68k-ssection-threshold",
    "Small data and bss section threshold size (default=8)",
    "unsigned"> {
  let Registry = M68kOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def M68K_EnableBasePointer : Option<
    "m68k-use-base-pointer",
    "Enable use of a base pointer for complex stack frames",
    "bool"> {
  let Registry = M68kOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def M68K_RegisterPrefixOptional : Option<
    "m68k-register-prefix-optional",
    "Enable specifying registers without the % prefix",
    "bool"> {
  let Registry = M68kOptsReg;
  let Default = "0";
  let Hidden = 1;
}
