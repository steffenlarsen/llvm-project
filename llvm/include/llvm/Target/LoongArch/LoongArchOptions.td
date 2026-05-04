//===- LoongArchOptions.td - CLV2 option defs for LoongArch -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def LoongArchOptsReg : Registry<"llvm::la_opts"> {
  // Getter bodies are compiled once, in LoongArchOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

// --- LoongArchSubtarget.cpp ---

def LA_UseAA : Option<
    "loongarch-use-aa",
    "Enable the use of AA during codegen.",
    "bool"> {
  let Registry = LoongArchOptsReg;
  let Default = "1";
}

// --- LoongArchAsmPrinter.cpp ---

def LA_AnnotateTableJump : Option<
    "loongarch-annotate-tablejump",
    "Annotate table jump instruction to correlate it with the jump table.",
    "bool"> {
  let Registry = LoongArchOptsReg;
  let Hidden = 1;
}

// --- LoongArchTargetMachine.cpp ---

def LA_EnableDeadDefs : Option<
    "loongarch-enable-dead-defs",
    "Enable the pass that removes dead"
    " definitons and replaces stores to"
    " them with stores to r0",
    "bool"> {
  let Registry = LoongArchOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def LA_EnableLoopDataPrefetch : Option<
    "loongarch-enable-loop-data-prefetch",
    "Enable the loop data prefetch pass",
    "bool"> {
  let Registry = LoongArchOptsReg;
  let Hidden = 1;
}

def LA_EnableMergeOffset : Option<
    "loongarch-enable-merge-offset",
    "Enable the merge base offset pass",
    "bool"> {
  let Registry = LoongArchOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def LA_EnableSinkFold : Option<
    "loongarch-enable-sink-fold",
    "Enable sinking and folding of instruction copies",
    "bool"> {
  let Registry = LoongArchOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- LoongArchInstrInfo.cpp ---

def LA_DisableRelocSched : Option<
    "loongarch-disable-reloc-sched",
    "Disable scheduling of instructions with target flags",
    "bool"> {
  let Default = "false";
  let Registry = LoongArchOptsReg;
  let Hidden = 1;
}

// --- LoongArchOptWInstrs.cpp ---

def LA_DisableSExtWRemoval : Option<
    "loongarch-disable-sextw-removal",
    "Disable removal of sign-extend insn",
    "bool"> {
  let Default = "false";
  let Registry = LoongArchOptsReg;
  let Hidden = 1;
}

def LA_DisableCvtToDSuffix : Option<
    "loongarch-disable-cvt-to-d-suffix",
    "Disable convert to D suffix",
    "bool"> {
  let Default = "false";
  let Registry = LoongArchOptsReg;
  let Hidden = 1;
}

// --- MCTargetDesc/LoongArchInstPrinter.cpp ---

def LA_NoAliases : Option<
    "loongarch-no-aliases",
    "Disable the emission of assembler pseudo instructions",
    "bool"> {
  let Registry = LoongArchOptsReg;
  let Hidden = 1;
}

def LA_NumericReg : Option<
    "loongarch-numeric-reg",
    "Print numeric register names rather than the ABI "
    "names (such as $r0 instead of $zero)",
    "bool"> {
  let Registry = LoongArchOptsReg;
  let Hidden = 1;
}

// --- LoongArchISelLowering.cpp ---

def LAMatFPImmNoMaterialize : EnumValue<"0", "Use constant pool"> {
  let CppName = "NoMaterialize";
  let Value = 0;
}

def LAMatFPImmMaterialize2Ins : EnumValue<"2",
    "Materialize FP immediate within 2 instructions"> {
  let CppName = "Materialize2Ins";
  let Value = 2;
}

def LAMatFPImmMaterialize3Ins : EnumValue<"3",
    "Materialize FP immediate within 3 instructions"> {
  let CppName = "Materialize3Ins";
  let Value = 3;
}

def LAMatFPImmMaterialize4Ins : EnumValue<"4",
    "Materialize FP immediate within 4 instructions"> {
  let CppName = "Materialize4Ins";
  let Value = 4;
}

def LAMatFPImmMaterialize5Ins : EnumValue<"5",
    "Materialize FP immediate within 5 instructions"> {
  let CppName = "Materialize5Ins";
  let Value = 5;
}

def LAMatFPImmMaterialize6Ins : EnumValue<"6",
    "Materialize FP immediate within 6 instructions "
    "(behaves same as 5 on loongarch64)"> {
  let CppName = "Materialize6Ins";
  let Value = 6;
}

def LAMaterializeFPImm : EnumType<"LAMaterializeFPImm",
    [LAMatFPImmNoMaterialize, LAMatFPImmMaterialize2Ins,
     LAMatFPImmMaterialize3Ins, LAMatFPImmMaterialize4Ins,
     LAMatFPImmMaterialize5Ins, LAMatFPImmMaterialize6Ins]>;

def LA_MaterializeFPImmInsNum : Option<
    "loongarch-materialize-float-imm",
    "Maximum number of instructions used (including code sequence "
    "to generate the value and moving the value to FPR) when "
    "materializing floating-point immediates (default = 3)",
    "LAMaterializeFPImm"> {
  let Registry = LoongArchOptsReg;
  let Default = "3";
  let Hidden = 1;
}

def LA_ZeroDivCheck : Option<
    "loongarch-check-zero-division",
    "Trap on integer division by zero.",
    "bool"> {
  let Default = "false";
  let Registry = LoongArchOptsReg;
  let Hidden = 1;
}
