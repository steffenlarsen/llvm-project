//===- M68kOptions.td - M68k target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def M68kOptions : OptionsStruct<"llvm"> {}

defm M68K_SSThreshold : ValueField<
    "m68k-ssection-threshold", "unsigned", "8",
    "Small data and bss section threshold size (default=8)",
    /*hidden=*/1>;

defm M68K_EnableBasePointer : BoolField<
    "m68k-use-base-pointer", "true",
    "Enable use of a base pointer for complex stack frames",
    /*hidden=*/1>;

defm M68K_RegisterPrefixOptional : BoolField<
    "m68k-register-prefix-optional", "false",
    "Enable specifying registers without the % prefix",
    /*hidden=*/1>;
