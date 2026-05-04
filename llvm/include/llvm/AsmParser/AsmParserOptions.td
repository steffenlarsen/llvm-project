//===- AsmParserOptions.td - AsmParser options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def AsmParserOptsReg : Registry<"llvm::asm_opts"> {
  // Getter bodies are compiled once, in AsmParserOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

def ASM_AllowIncompleteIR : Option<
    "allow-incomplete-ir",
    "Allow incomplete IR on a best effort basis (references to unknown "
    "metadata will be dropped)",
    "bool"> {
  let Registry = AsmParserOptsReg;
  let Default = "0";
  let Hidden = 1;
}
