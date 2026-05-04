//===- MipsOptions.td - CLV2 option defs for Mips ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def MipsOptsReg : Registry<"llvm::mips_opts"> {
  // Getter bodies are compiled once, in MipsOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

// --- MCTargetDesc/MipsABIInfo.cpp ---

def MIPS_EmitJalrReloc : Option<
    "mips-jalr-reloc",
    "MIPS: Emit R_{MICRO}MIPS_JALR relocation with jalr",
    "bool"> {
  let Registry = MipsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def MIPS_NoZeroDivCheck : Option<
    "mno-check-zero-division",
    "MIPS: Don't trap on integer division by zero.",
    "bool"> {
  let Registry = MipsOptsReg;
  let Hidden = 1;
}

// --- MCTargetDesc/MipsTargetStreamer.cpp ---

def MIPS_RoundSectionSizes : Option<
    "mips-round-section-sizes",
    "Round section sizes up to the section alignment",
    "bool"> {
  let Registry = MipsOptsReg;
  let Hidden = 1;
}

// --- Mips16ISelLowering.cpp ---

def MIPS_DontExpandCondPseudos16 : Option<
    "mips16-dont-expand-cond-pseudo",
    "Don't expand conditional move related pseudos for Mips 16",
    "bool"> {
  let Default = "false";
  let Registry = MipsOptsReg;
  let Hidden = 1;
}

// --- MipsBranchExpansion.cpp ---

def MIPS_SkipLongBranch : Option<
    "skip-mips-long-branch",
    "MIPS: Skip branch expansion pass.",
    "bool"> {
  let Default = "false";
  let Registry = MipsOptsReg;
  let Hidden = 1;
}

def MIPS_ForceLongBranch : Option<
    "force-mips-long-branch",
    "MIPS: Expand all branches to long format.",
    "bool"> {
  let Default = "false";
  let Registry = MipsOptsReg;
  let Hidden = 1;
}

// --- MipsConstantIslandPass.cpp ---

def MIPS_AlignConstantIslands : Option<
    "mips-align-constant-islands",
    "Align constant islands in code",
    "bool"> {
  let Registry = MipsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def MIPS_ConstantIslandsSmallOffset : Option<
    "mips-constant-islands-small-offset",
    "Make small offsets be this amount for testing purposes",
    "int"> {
  let Default = "0";
  let Registry = MipsOptsReg;
  let Hidden = 1;
}

def MIPS_NoLoadRelaxation : Option<
    "mips-constant-islands-no-load-relaxation",
    "Don't relax loads to long loads - for testing purposes",
    "bool"> {
  let Default = "false";
  let Registry = MipsOptsReg;
  let Hidden = 1;
}

// --- MipsDelaySlotFiller.cpp ---

def MIPS_DisableDelaySlotFiller : Option<
    "disable-mips-delay-filler",
    "Fill all delay slots with NOPs.",
    "bool"> {
  let Default = "false";
  let Registry = MipsOptsReg;
  let Hidden = 1;
}

def MIPS_DisableForwardSearch : Option<
    "disable-mips-df-forward-search",
    "Disallow MIPS delay filler to search forward.",
    "bool"> {
  let Registry = MipsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def MIPS_DisableSuccBBSearch : Option<
    "disable-mips-df-succbb-search",
    "Disallow MIPS delay filler to search successor basic blocks.",
    "bool"> {
  let Registry = MipsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def MIPS_DisableBackwardSearch : Option<
    "disable-mips-df-backward-search",
    "Disallow MIPS delay filler to search backward.",
    "bool"> {
  let Default = "false";
  let Registry = MipsOptsReg;
  let Hidden = 1;
}

// --- MipsISelLowering.cpp ---

def MIPS_UseTailCalls : Option<
    "mips-tail-calls",
    "MIPS: permit tail calls.",
    "bool"> {
  let Default = "false";
  let Registry = MipsOptsReg;
  let Hidden = 1;
}

// --- MipsMachineFunction.cpp ---

def MIPS_FixGlobalBaseReg : Option<
    "mips-fix-global-base-reg",
    "Always use $gp as the global base register.",
    "bool"> {
  let Registry = MipsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- MipsOptimizePICCall.cpp ---

def MIPS_LoadTargetFromGOT : Option<
    "mips-load-target-from-got",
    "Load target address from GOT",
    "bool"> {
  let Registry = MipsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def MIPS_EraseGPOpnd : Option<
    "mips-erase-gp-opnd",
    "Erase GP Operand",
    "bool"> {
  let Registry = MipsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

// --- MipsOs16.cpp ---

def MIPS_Mips32FunctionMask : Option<
    "mips32-function-mask",
    "Force function to be mips32",
    "std::string"> {
  let Default = "\"\"";
  let Registry = MipsOptsReg;
  let Hidden = 1;
}

// --- MipsSEISelLowering.cpp ---

def MIPS_NoDPLoadStore : Option<
    "mno-ldc1-sdc1",
    "Expand double precision loads and stores to their single precision "
    "counterparts",
    "bool"> {
  let Registry = MipsOptsReg;
}

// --- MipsSubtarget.cpp ---

def MipsCompactBranchNever : EnumValue<"never",
    "Do not use compact branches if possible."> {
  let CppName = "Never";
}

def MipsCompactBranchOptimal : EnumValue<"optimal",
    "Use compact branches where appropriate (default)."> {
  let CppName = "Optimal";
}

def MipsCompactBranchAlways : EnumValue<"always",
    "Always use compact branches if possible."> {
  let CppName = "Always";
}

def MipsCompactBranch : EnumType<"MipsCompactBranch",
    [MipsCompactBranchNever, MipsCompactBranchOptimal,
     MipsCompactBranchAlways]>;

def MIPS_CompactBranchPolicy : Option<
    "mips-compact-branches", "MIPS Specific: Compact branch policy.",
    "MipsCompactBranch"> {
  let Registry = MipsOptsReg;
  let Default = "optimal";
}

def MIPS_Mixed16_32 : Option<
    "mips-mixed-16-32",
    "Allow for a mixture of Mips16 and Mips32 code in a single output file",
    "bool"> {
  let Registry = MipsOptsReg;
  let Hidden = 1;
}

def MIPS_Os16 : Option<
    "mips-os16",
    "Compile all functions that don't use floating point as Mips 16",
    "bool"> {
  let Registry = MipsOptsReg;
  let Hidden = 1;
}

def MIPS_Mips16HardFloat : Option<
    "mips16-hard-float",
    "Enable mips16 hard float.",
    "bool"> {
  let Registry = MipsOptsReg;
}

def MIPS_Mips16ConstantIslands : Option<
    "mips16-constant-islands",
    "Enable mips16 constant islands.",
    "bool"> {
  let Registry = MipsOptsReg;
  let Default = "1";
}

def MIPS_GPOpt : Option<
    "mgpopt",
    "Enable gp-relative addressing of mips small data items",
    "bool"> {
  let Registry = MipsOptsReg;
  let Hidden = 1;
}

// --- MipsTargetObjectFile.cpp ---

def MIPS_SSThreshold : Option<
    "mips-ssection-threshold",
    "Small data and bss section threshold size (default=8)",
    "unsigned"> {
  let Registry = MipsOptsReg;
  let Default = "8";
  let Hidden = 1;
}

def MIPS_LocalSData : Option<
    "mlocal-sdata",
    "MIPS: Use gp_rel for object-local data.",
    "bool"> {
  let Registry = MipsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def MIPS_ExternSData : Option<
    "mextern-sdata",
    "MIPS: Use gp_rel for data that is not defined by the current object.",
    "bool"> {
  let Registry = MipsOptsReg;
  let Default = "1";
  let Hidden = 1;
}

def MIPS_EmbeddedData : Option<
    "membedded-data",
    "MIPS: Try to allocate variables in the following"
    " sections if possible: .rodata, .sdata, .data .",
    "bool"> {
  let Registry = MipsOptsReg;
  let Hidden = 1;
}

// --- MipsTargetMachine.cpp ---

def MIPS_EnableMulMulFix : Option<
    "mfix4300",
    "Enable the VR4300 mulmul bug fix.",
    "bool"> {
  let Registry = MipsOptsReg;
  let Hidden = 1;
}
