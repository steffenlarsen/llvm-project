//===- MSP430Options.td - MSP430 target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def MSP430Options : OptionsStruct<"llvm"> {}

def MSP430HWMultEnum : OptionEnum<"MSP430HWMult", [
    EnumMember<"NoHWMult", "none">,
    EnumMember<"HWMult16", "16bit">,
    EnumMember<"HWMult32", "32bit">,
    EnumMember<"HWMultF5", "f5series">]>;

defm MSP430_HWMultMode : EnumField<
    "mhwmult", MSP430HWMultEnum, "MSP430HWMult::NoHWMult",
    "Hardware multiplier use mode for MSP430",
    /*hidden=*/1>;

defm MSP430_NoLegalImmediate : BoolField<
    "msp430-no-legal-immediate", "false",
    "Enable non legal immediates (for testing purposes only)",
    /*hidden=*/1>;

defm MSP430_BranchSelectEnabled : BoolField<
    "msp430-branch-select", "true",
    "Expand out of range branches",
    /*hidden=*/1>;
