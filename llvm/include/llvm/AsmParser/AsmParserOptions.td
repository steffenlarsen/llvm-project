//===- AsmParserOptions.td - AsmParser options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def AsmParserOptions : OptionsStruct<"llvm"> {}

defm ASM_AllowIncompleteIR : BoolField<
    "allow-incomplete-ir", "false",
    "Allow incomplete IR on a best effort basis (references to unknown "
    "metadata will be dropped)", /*hidden=*/1>;
