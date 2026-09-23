//===- SparcOptions.td - Sparc target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def SparcOptions : OptionsStruct<"llvm"> {}

defm SPARC_BranchRelaxation : BoolField<
    "sparc-enable-branch-relax", "true",
    "Relax out of range conditional branches",
    /*hidden=*/1>;

defm SPARC_DisableLeafProc : BoolField<
    "disable-sparc-leaf-proc", "false",
    "Disable Sparc leaf procedure optimization.",
    /*hidden=*/1>;

defm SPARC_DisableDelaySlotFiller : BoolField<
    "disable-sparc-delay-filler", "false",
    "Disable the Sparc delay slot filler.",
    /*hidden=*/1>;

defm SPARC_ReserveAppRegisters : BoolField<
    "sparc-reserve-app-registers", "false",
    "Reserve application registers (%g2-%g4)",
    /*hidden=*/1>;

defm SPARC_BPccDisplacementBits : ValueField<
    "sparc-bpcc-offset-bits", "unsigned", "19u",
    "Restrict range of BPcc/FBPfcc instructions (DEBUG)",
    /*hidden=*/1>;

defm SPARC_BPrDisplacementBits : ValueField<
    "sparc-bpr-offset-bits", "unsigned", "16u",
    "Restrict range of BPr instructions (DEBUG)",
    /*hidden=*/1>;
