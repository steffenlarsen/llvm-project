//===- SparcOptions.td - Sparc target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def SparcOptsReg : Registry<"llvm::sparc_opts"> {
  // Getter bodies are compiled once, in SparcOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

def SPARC_BranchRelaxation : Option<
    "sparc-enable-branch-relax",
    "Relax out of range conditional branches",
    "bool"> {
  let Registry = SparcOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SPARC_DisableLeafProc : Option<
    "disable-sparc-leaf-proc",
    "Disable Sparc leaf procedure optimization.",
    "bool"> {
  let Registry = SparcOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def SPARC_DisableDelaySlotFiller : Option<
    "disable-sparc-delay-filler",
    "Disable the Sparc delay slot filler.",
    "bool"> {
  let Registry = SparcOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def SPARC_ReserveAppRegisters : Option<
    "sparc-reserve-app-registers",
    "Reserve application registers (%g2-%g4)",
    "bool"> {
  let Registry = SparcOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def SPARC_BPccDisplacementBits : Option<
    "sparc-bpcc-offset-bits",
    "Restrict range of BPcc/FBPfcc instructions (DEBUG)",
    "unsigned"> {
  let Registry = SparcOptsReg;
  let Default = "19";
  let Hidden = 1;
}

def SPARC_BPrDisplacementBits : Option<
    "sparc-bpr-offset-bits",
    "Restrict range of BPr instructions (DEBUG)",
    "unsigned"> {
  let Registry = SparcOptsReg;
  let Default = "16";
  let Hidden = 1;
}
