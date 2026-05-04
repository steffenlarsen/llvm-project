//===- MSP430Options.td - MSP430 target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def MSP430OptsReg : Registry<"llvm::msp430_opts"> {
  // Getter bodies are compiled once, in MSP430OptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

//===----------------------------------------------------------------------===//
// MSP430HWMult enum
//===----------------------------------------------------------------------===//

def MSP430HWMultNoHWMult : EnumValue<"none", "Do not use hardware multiplier"> {
  let CppName = "NoHWMult";
}
def MSP430HWMultHWMult16 : EnumValue<"16bit", "Use 16-bit hardware multiplier"> {
  let CppName = "HWMult16";
}
def MSP430HWMultHWMult32 : EnumValue<"32bit", "Use 32-bit hardware multiplier"> {
  let CppName = "HWMult32";
}
def MSP430HWMultHWMultF5 : EnumValue<"f5series", "Use F5 series hardware multiplier"> {
  let CppName = "HWMultF5";
}

def MSP430HWMultEnum : EnumType<"MSP430HWMult",
    [MSP430HWMultNoHWMult, MSP430HWMultHWMult16,
     MSP430HWMultHWMult32, MSP430HWMultHWMultF5]>;

//===----------------------------------------------------------------------===//
// Options
//===----------------------------------------------------------------------===//

def MSP430_HWMultMode : Option<
    "mhwmult",
    "Hardware multiplier use mode for MSP430",
    "MSP430HWMult"> {
  let Registry = MSP430OptsReg;
  let Default = "none";
  let Hidden = 1;
}

def MSP430_NoLegalImmediate : Option<
    "msp430-no-legal-immediate",
    "Enable non legal immediates (for testing purposes only)",
    "bool"> {
  let Registry = MSP430OptsReg;
  let Default = "0";
  let Hidden = 1;
}

def MSP430_BranchSelectEnabled : Option<
    "msp430-branch-select",
    "Expand out of range branches",
    "bool"> {
  let Registry = MSP430OptsReg;
  let Default = "1";
  let Hidden = 1;
}
