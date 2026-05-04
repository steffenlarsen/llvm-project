//===- LanaiOptions.td - Lanai target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def LanaiOptsReg : Registry<"llvm::lanai_opts"> {
  // Getter bodies are compiled once, in LanaiOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

def LANAI_SSThreshold : Option<
    "lanai-ssection-threshold",
    "Small data and bss section threshold size (default=0)",
    "unsigned"> {
  let Registry = LanaiOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def LANAI_NopDelaySlotFiller : Option<
    "lanai-nop-delay-filler",
    "Fill Lanai delay slots with NOPs.",
    "bool"> {
  let Registry = LanaiOptsReg;
  let Default = "0";
  let Hidden = 1;
}

def LANAI_LowerConstantMulThreshold : Option<
    "lanai-constant-mul-threshold",
    "Maximum number of instruction to generate when lowering constant "
    "multiplication instead of calling library function [default=14]",
    "int"> {
  let Registry = LanaiOptsReg;
  let Default = "14";
  let Hidden = 1;
}

def LANAI_DisableMemAluCombiner : Option<
    "disable-lanai-mem-alu-combiner",
    "Do not combine ALU and memory operators",
    "bool"> {
  let Registry = LanaiOptsReg;
  let Default = "0";
  let Hidden = 1;
}
