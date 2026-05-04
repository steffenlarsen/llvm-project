//===- SystemZOptions.td - SystemZ target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def SystemZOptsReg : Registry<"llvm::sz_opts"> {
  // Getter bodies are compiled once, in SystemZOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

def SZ_EnableMachineCombinerPass : Option<
    "systemz-machine-combiner",
    "Enable the machine combiner pass",
    "bool"> {
  let Registry = SystemZOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def SZ_GenericSched : Option<
    "generic-sched",
    "Run the generic pre-ra scheduler instead of the SystemZ scheduler.",
    "bool"> {
  let Registry = SystemZOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def SZ_ProcResCostLim : Option<
    "procres-cost-lim",
    "The OOO window for processor resources during scheduling.",
    "int"> {
  let Registry = SystemZOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def SZ_UseSubRegLiveness : Option<
    "systemz-subreg-liveness",
    "Enable subregister liveness tracking for SystemZ (experimental)",
    "bool"> {
  let Registry = SystemZOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def SZ_EnableIntArgExtCheck : Option<
    "argext-abi-check",
    "Verify that narrow int args are properly extended per the SystemZ ABI.",
    "bool"> {
  let Registry = SystemZOptsReg;
  let Default = "0";
}
