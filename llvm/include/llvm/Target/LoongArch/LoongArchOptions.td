//===- LoongArchOptions.td - LoongArch target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def LoongArchOptions : OptionsStruct<"llvm"> {}

// --- LoongArchSubtarget.cpp ---

defm LA_UseAA : BoolField<
    "loongarch-use-aa", "1",
    "Enable the use of AA during codegen.">;

// --- LoongArchAsmPrinter.cpp ---

defm LA_AnnotateTableJump : BoolField<
    "loongarch-annotate-tablejump", "false",
    "Annotate table jump instruction to correlate it with the jump table.",
    /*hidden=*/1>;

// --- LoongArchTargetMachine.cpp ---

defm LA_EnableDeadDefs : BoolField<
    "loongarch-enable-dead-defs", "1",
    "Enable the pass that removes dead"
    " definitons and replaces stores to"
    " them with stores to r0",
    /*hidden=*/1>;

defm LA_EnableLoopDataPrefetch : BoolField<
    "loongarch-enable-loop-data-prefetch", "false",
    "Enable the loop data prefetch pass",
    /*hidden=*/1>;

defm LA_EnableMergeOffset : BoolField<
    "loongarch-enable-merge-offset", "1",
    "Enable the merge base offset pass",
    /*hidden=*/1>;

defm LA_EnableSinkFold : BoolField<
    "loongarch-enable-sink-fold", "1",
    "Enable sinking and folding of instruction copies",
    /*hidden=*/1>;

// --- LoongArchInstrInfo.cpp ---

defm LA_DisableRelocSched : BoolField<
    "loongarch-disable-reloc-sched", "false",
    "Disable scheduling of instructions with target flags",
    /*hidden=*/1>;

// --- LoongArchOptWInstrs.cpp ---

defm LA_DisableSExtWRemoval : BoolField<
    "loongarch-disable-sextw-removal", "false",
    "Disable removal of sign-extend insn",
    /*hidden=*/1>;

defm LA_DisableCvtToDSuffix : BoolField<
    "loongarch-disable-cvt-to-d-suffix", "false",
    "Disable convert to D suffix",
    /*hidden=*/1>;

// --- MCTargetDesc/LoongArchInstPrinter.cpp ---

defm LA_NoAliases : BoolField<
    "loongarch-no-aliases", "false",
    "Disable the emission of assembler pseudo instructions",
    /*hidden=*/1>;

defm LA_NumericReg : BoolField<
    "loongarch-numeric-reg", "false",
    "Print numeric register names rather than the ABI "
    "names (such as $r0 instead of $zero)",
    /*hidden=*/1>;

// --- LoongArchISelLowering.cpp ---

// The spellings below are the numeric instruction counts accepted on the
// command line. There is deliberately no "1": the original clv2 schema never
// defined a value for it either (a pre-existing gap, not a regression here).
def LAMaterializeFPImm : OptionEnum<"LAMaterializeFPImm", [
    EnumMember<"NoMaterialize", "0">,
    EnumMember<"Materialize2Ins", "2">,
    EnumMember<"Materialize3Ins", "3">,
    EnumMember<"Materialize4Ins", "4">,
    EnumMember<"Materialize5Ins", "5">,
    EnumMember<"Materialize6Ins", "6">]>;

defm LA_MaterializeFPImmInsNum : EnumField<
    "loongarch-materialize-float-imm", LAMaterializeFPImm,
    "LAMaterializeFPImm::Materialize3Ins",
    "Maximum number of instructions used (including code sequence "
    "to generate the value and moving the value to FPR) when "
    "materializing floating-point immediates (default = 3)",
    /*hidden=*/1>;

defm LA_ZeroDivCheck : BoolField<
    "loongarch-check-zero-division", "false",
    "Trap on integer division by zero.",
    /*hidden=*/1>;

// --- LoongArchMemoryBarrierOpt.cpp ---

defm LA_RequireNoPathBypass : BoolField<
    "loongarch-require-no-path-bypass", "1",
    "Optimize only when no paths bypass either memory barrier",
    /*hidden=*/1>;

defm LA_MergeAMOWithMB : BoolField<
    "loongarch-merge-amo-with-dbar", "1",
    "Merge AMOs with DBARs into AMO_DB during optimization",
    /*hidden=*/1>;

defm LA_DisableInlineAsmBarrierOpt : BoolField<
    "loongarch-disable-inline-asm-barrier-opt", "false",
    "Disable optimization of memory barriers in InlineAsm",
    /*hidden=*/1>;

defm LA_ReplaceEliminatedDbarToNop : BoolField<
    "loongarch-replace-eliminated-dbar-to-nop", "false",
    "Replace eliminated DBARs with NOPs to preserve code layout",
    /*hidden=*/1>;
