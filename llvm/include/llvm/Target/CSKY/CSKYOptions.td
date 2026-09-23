//===- CSKYOptions.td - CSKY target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def CSKYOptions : OptionsStruct<"llvm"> {}

defm CSKY_EnableCompressedInst : BoolField<
    "enable-csky-asm-compressed-inst", "false",
    "Enable C-SKY asm compressed instruction",
    /*hidden=*/1>;

defm CSKY_NoAliases : BoolField<
    "csky-no-aliases", "false",
    "Disable the emission of assembler pseudo instructions",
    /*hidden=*/1>;

defm CSKY_ArchRegNames : BoolField<
    "csky-arch-reg-names", "false",
    "Print architectural register names rather than the "
    "ABI names (such as r14 instead of sp)",
    /*hidden=*/1>;

defm CSKY_AddBuildAttributes : BoolField<
    "csky-add-build-attributes", "true", "">;
