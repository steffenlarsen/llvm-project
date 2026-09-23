//===- LanaiOptions.td - Lanai target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def LanaiOptions : OptionsStruct<"llvm"> {}

defm LANAI_SSThreshold : ValueField<
    "lanai-ssection-threshold", "unsigned", "0",
    "Small data and bss section threshold size (default=0)",
    /*hidden=*/1>;

defm LANAI_NopDelaySlotFiller : BoolField<
    "lanai-nop-delay-filler", "false",
    "Fill Lanai delay slots with NOPs.",
    /*hidden=*/1>;

defm LANAI_LowerConstantMulThreshold : ValueField<
    "lanai-constant-mul-threshold", "int", "14",
    "Maximum number of instruction to generate when lowering constant "
    "multiplication instead of calling library function [default=14]",
    /*hidden=*/1>;

defm LANAI_DisableMemAluCombiner : BoolField<
    "disable-lanai-mem-alu-combiner", "false",
    "Do not combine ALU and memory operators",
    /*hidden=*/1>;
