//===- MipsOptions.td - Mips target options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def MipsOptions : OptionsStruct<"llvm"> {}

// --- MCTargetDesc/MipsABIInfo.cpp ---

defm MIPS_EmitJalrReloc : BoolField<
    "mips-jalr-reloc", "1",
    "MIPS: Emit R_{MICRO}MIPS_JALR relocation with jalr",
    /*hidden=*/1>;

defm MIPS_NoZeroDivCheck : BoolField<
    "mno-check-zero-division", "false",
    "MIPS: Don't trap on integer division by zero.",
    /*hidden=*/1>;

// --- MCTargetDesc/MipsTargetStreamer.cpp ---

defm MIPS_RoundSectionSizes : BoolField<
    "mips-round-section-sizes", "false",
    "Round section sizes up to the section alignment",
    /*hidden=*/1>;

// --- Mips16ISelLowering.cpp ---

defm MIPS_DontExpandCondPseudos16 : BoolField<
    "mips16-dont-expand-cond-pseudo", "false",
    "Don't expand conditional move related pseudos for Mips 16",
    /*hidden=*/1>;

// --- MipsBranchExpansion.cpp ---

defm MIPS_SkipLongBranch : BoolField<
    "skip-mips-long-branch", "false",
    "MIPS: Skip branch expansion pass.",
    /*hidden=*/1>;

defm MIPS_ForceLongBranch : BoolField<
    "force-mips-long-branch", "false",
    "MIPS: Expand all branches to long format.",
    /*hidden=*/1>;

// --- MipsConstantIslandPass.cpp ---

defm MIPS_AlignConstantIslands : BoolField<
    "mips-align-constant-islands", "1",
    "Align constant islands in code",
    /*hidden=*/1>;

defm MIPS_ConstantIslandsSmallOffset : ValueField<
    "mips-constant-islands-small-offset", "int", "0",
    "Make small offsets be this amount for testing purposes",
    /*hidden=*/1>;

defm MIPS_NoLoadRelaxation : BoolField<
    "mips-constant-islands-no-load-relaxation", "false",
    "Don't relax loads to long loads - for testing purposes",
    /*hidden=*/1>;

// --- MipsDelaySlotFiller.cpp ---

defm MIPS_DisableDelaySlotFiller : BoolField<
    "disable-mips-delay-filler", "false",
    "Fill all delay slots with NOPs.",
    /*hidden=*/1>;

defm MIPS_DisableForwardSearch : BoolField<
    "disable-mips-df-forward-search", "1",
    "Disallow MIPS delay filler to search forward.",
    /*hidden=*/1>;

defm MIPS_DisableSuccBBSearch : BoolField<
    "disable-mips-df-succbb-search", "1",
    "Disallow MIPS delay filler to search successor basic blocks.",
    /*hidden=*/1>;

defm MIPS_DisableBackwardSearch : BoolField<
    "disable-mips-df-backward-search", "false",
    "Disallow MIPS delay filler to search backward.",
    /*hidden=*/1>;

// --- MipsISelLowering.cpp ---

defm MIPS_UseTailCalls : BoolField<
    "mips-tail-calls", "false",
    "MIPS: permit tail calls.",
    /*hidden=*/1>;

// --- MipsMachineFunction.cpp ---

defm MIPS_FixGlobalBaseReg : BoolField<
    "mips-fix-global-base-reg", "1",
    "Always use $gp as the global base register.",
    /*hidden=*/1>;

// --- MipsOptimizePICCall.cpp ---

defm MIPS_LoadTargetFromGOT : BoolField<
    "mips-load-target-from-got", "1",
    "Load target address from GOT",
    /*hidden=*/1>;

defm MIPS_EraseGPOpnd : BoolField<
    "mips-erase-gp-opnd", "1",
    "Erase GP Operand",
    /*hidden=*/1>;

// --- MipsOs16.cpp ---

defm MIPS_Mips32FunctionMask : ValueField<
    "mips32-function-mask", "std::string", "\"\"",
    "Force function to be mips32",
    /*hidden=*/1>;

// --- MipsSEISelLowering.cpp ---

defm MIPS_NoDPLoadStore : BoolField<
    "mno-ldc1-sdc1", "false",
    "Expand double precision loads and stores to their single precision "
    "counterparts">;

// --- MipsSubtarget.cpp ---

// The spellings below are the string tokens accepted on the command line
// (unlike e.g. LoongArch's numeric-instruction-count enum, these have no
// external C++ counterpart to collide ordinals with).
def MipsCompactBranch : OptionEnum<"MipsCompactBranch", [
    EnumMember<"Never", "never">,
    EnumMember<"Optimal", "optimal">,
    EnumMember<"Always", "always">]>;

defm MIPS_CompactBranchPolicy : EnumField<
    "mips-compact-branches", MipsCompactBranch,
    "MipsCompactBranch::Optimal",
    "MIPS Specific: Compact branch policy.">;

defm MIPS_Mixed16_32 : BoolField<
    "mips-mixed-16-32", "false",
    "Allow for a mixture of Mips16 and Mips32 code in a single output file",
    /*hidden=*/1>;

defm MIPS_Os16 : BoolField<
    "mips-os16", "false",
    "Compile all functions that don't use floating point as Mips 16",
    /*hidden=*/1>;

defm MIPS_Mips16HardFloat : BoolField<
    "mips16-hard-float", "false",
    "Enable mips16 hard float.">;

defm MIPS_Mips16ConstantIslands : BoolField<
    "mips16-constant-islands", "1",
    "Enable mips16 constant islands.">;

defm MIPS_GPOpt : BoolField<
    "mgpopt", "false",
    "Enable gp-relative addressing of mips small data items",
    /*hidden=*/1>;

// --- MipsTargetObjectFile.cpp ---

defm MIPS_SSThreshold : ValueField<
    "mips-ssection-threshold", "unsigned", "8",
    "Small data and bss section threshold size (default=8)",
    /*hidden=*/1>;

defm MIPS_LocalSData : BoolField<
    "mlocal-sdata", "1",
    "MIPS: Use gp_rel for object-local data.",
    /*hidden=*/1>;

defm MIPS_ExternSData : BoolField<
    "mextern-sdata", "1",
    "MIPS: Use gp_rel for data that is not defined by the current object.",
    /*hidden=*/1>;

defm MIPS_EmbeddedData : BoolField<
    "membedded-data", "false",
    "MIPS: Try to allocate variables in the following"
    " sections if possible: .rodata, .sdata, .data .",
    /*hidden=*/1>;

// --- MipsTargetMachine.cpp ---

defm MIPS_EnableMulMulFix : BoolField<
    "mfix4300", "false",
    "Enable the VR4300 mulmul bug fix.",
    /*hidden=*/1>;
