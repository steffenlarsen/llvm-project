//===- SystemZOptions.td - SystemZ target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def SystemZOptions : OptionsStruct<"llvm"> {}

defm SZ_EnableMachineCombinerPass : BoolField<
    "systemz-machine-combiner", "true",
    "Enable the machine combiner pass",
    /*hidden=*/1>;

defm SZ_GenericSched : BoolField<
    "generic-sched", "false",
    "Run the generic pre-ra scheduler instead of the SystemZ scheduler.",
    /*hidden=*/1>;

defm SZ_ProcResCostLim : ValueField<
    "procres-cost-lim", "int", "8",
    "The OOO window for processor resources during scheduling.",
    /*hidden=*/1>;

defm SZ_UseSubRegLiveness : BoolField<
    "systemz-subreg-liveness", "false",
    "Enable subregister liveness tracking for SystemZ (experimental)",
    /*hidden=*/1>;

// std::optional (not a plain bool) so callers can distinguish "not passed"
// (falls back to TargetOptions::VerifyArgABICompliance) from an explicit
// "-argext-abi-check=false" (forces the check off).
defm SZ_EnableIntArgExtCheck : OptionalValueField<
    "argext-abi-check", "bool", "true",
    "Verify that narrow int args are properly extended per the SystemZ ABI.">;
