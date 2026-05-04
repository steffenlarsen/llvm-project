//===- CSKYOptions.td - CSKY target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def CSKYOptsReg : Registry<"llvm::csky_opts"> {
  // Getter bodies are compiled once, in CSKYOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

def CSKY_EnableCompressedInst : Option<
    "enable-csky-asm-compressed-inst",
    "Enable C-SKY asm compressed instruction",
    "bool"> {
  let Registry = CSKYOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def CSKY_NoAliases : Option<
    "csky-no-aliases",
    "Disable the emission of assembler pseudo instructions",
    "bool"> {
  let Registry = CSKYOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def CSKY_ArchRegNames : Option<
    "csky-arch-reg-names",
    "Print architectural register names rather than the "
    "ABI names (such as r14 instead of sp)",
    "bool"> {
  let Registry = CSKYOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def CSKY_AddBuildAttributes : Option<
    "csky-add-build-attributes", "",
    "bool"> {
  let Registry = CSKYOptsReg;
  let Default = "true";
}
