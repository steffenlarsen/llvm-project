//===- MCOptions.td - CLV2 option defs for MC ---------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Support/CLV2Options.td"

def MCOptsReg : Registry<"llvm::mc_opts"> {
  // Getter bodies are compiled once, in MCOptionsGetters.cpp.
  let OutOfLineGetters = 1;
}

def MCScheduleOptionsCategory : OptionCategory<"Machine scheduling model options">;

// =========================================================================
// External enums (defined in MCTargetOptions.h)
// =========================================================================

def EmitDwarfUnwindTypeAlways : EnumValue<"always",
    "Always emit EH frame entries"> {
  let CppName = "Always";
}

def EmitDwarfUnwindTypeNoCompactUnwind : EnumValue<"no-compact-unwind",
    "Only emit EH frame entries when compact unwind is not available"> {
  let CppName = "NoCompactUnwind";
}

def EmitDwarfUnwindTypeDwarfOnly : EnumValue<"dwarf-only",
    "Force compact unwind to reference DWARF"> {
  let CppName = "DwarfOnly";
}

def EmitDwarfUnwindTypeDefault : EnumValue<"default",
    "Use target platform default"> {
  let CppName = "Default";
}

def EmitDwarfUnwindTypeEnum : EnumType<"EmitDwarfUnwindType",
    [EmitDwarfUnwindTypeAlways, EmitDwarfUnwindTypeNoCompactUnwind,
     EmitDwarfUnwindTypeDwarfOnly, EmitDwarfUnwindTypeDefault]> {
  let External = 1;
}

def RelocSectionSymTypeAll : EnumValue<"all",
    "Use section symbols for all eligible local symbols"> {
  let CppName = "All";
}

def RelocSectionSymTypeInternal : EnumValue<"internal",
    "Only use section symbols for internal local symbols"> {
  let CppName = "Internal";
}

def RelocSectionSymTypeNone : EnumValue<"none",
    "Never use section symbols"> {
  let CppName = "None";
}

def RelocSectionSymTypeEnum : EnumType<"RelocSectionSymType",
    [RelocSectionSymTypeAll, RelocSectionSymTypeInternal,
     RelocSectionSymTypeNone]> {
  let External = 1;
}

// =========================================================================
// Inline enum: DefaultOnOff
// =========================================================================

def DefaultOnOffDefault : EnumValue<"Default", "Default for platform"> {
  let CppName = "Default";
}

def DefaultOnOffEnable : EnumValue<"Enable", "Enabled"> {
  let CppName = "Enable";
}

def DefaultOnOffDisable : EnumValue<"Disable", "Disabled"> {
  let CppName = "Disable";
}

def DefaultOnOffEnum : EnumType<"DefaultOnOff",
    [DefaultOnOffDefault, DefaultOnOffEnable, DefaultOnOffDisable]>;

// =========================================================================
// Options
// =========================================================================

def MC_RelaxAll : Option<
    "mc-relax-all",
    "When used with filetype=obj, relax all fixups in the emitted object file",
    "bool"> {
  let Registry = MCOptsReg;
}

def MC_IncrementalLinkerCompatible : Option<
    "incremental-linker-compatible",
    "When used with filetype=obj, emit an object file which can be used with "
    "an incremental linker",
    "bool"> {
  let Registry = MCOptsReg;
}

def MC_FDPIC : Option<"fdpic", "Use the FDPIC ABI",
    "bool"> {
  let Registry = MCOptsReg;
}

def MC_DwarfVersion : Option<
    "dwarf-version", "Dwarf version",
    "int"> {
  let Registry = MCOptsReg;
  let Default = "0";
}

def MC_Dwarf64 : Option<
    "dwarf64", "Generate debugging info in the 64-bit DWARF format",
    "bool"> {
  let Registry = MCOptsReg;
}

def MC_EmitDwarfUnwind : Option<
    "emit-dwarf-unwind",
    "Whether to emit DWARF EH frame entries.",
    "EmitDwarfUnwindType"> {
  let Registry = MCOptsReg;
  let Default = "default";
}

def MC_EmitCompactUnwindNonCanonical : Option<
    "emit-compact-unwind-non-canonical",
    "Whether to try to emit Compact Unwind for non canonical entries.",
    "bool"> {
  let Registry = MCOptsReg;
}

def MC_EmitSFrameUnwind : Option<
    "gsframe",
    "Whether to emit .sframe unwind sections.",
    "bool"> {
  let Registry = MCOptsReg;
}

def MC_ShowMCInst : Option<
    "asm-show-inst",
    "Emit internal instruction representation to assembly file",
    "bool"> {
  let Registry = MCOptsReg;
}

def MC_FatalWarnings : Option<
    "fatal-warnings", "Treat warnings as errors",
    "bool"> {
  let Registry = MCOptsReg;
}

def MC_NoWarn : Option<
    "no-warn", "Suppress all warnings",
    "bool"> {
  let Registry = MCOptsReg;
}

def MC_NoWarnW : Alias<"W", "no-warn", "Alias for --no-warn"> {
  let Registry = MCOptsReg;
}

def MC_NoDeprecatedWarn : Option<
    "no-deprecated-warn", "Suppress all deprecated warnings",
    "bool"> {
  let Registry = MCOptsReg;
}

def MC_NoTypeCheck : Option<
    "no-type-check", "Suppress type errors (Wasm)",
    "bool"> {
  let Registry = MCOptsReg;
}

def MC_SaveTempLabels : Option<
    "save-temp-labels", "Don't discard temporary labels",
    "bool"> {
  let Registry = MCOptsReg;
}

def MC_Crel : Option<
    "crel", "Use CREL relocation format for ELF",
    "bool"> {
  let Registry = MCOptsReg;
}

def MC_ImplicitMapSyms : Option<
    "implicit-mapsyms",
    "Allow mapping symbol at section beginning to be implicit, lowering "
    "number of mapping symbols at the expense of some portability. "
    "Recommended for projects that can build all their object files using "
    "this option",
    "bool"> {
  let Registry = MCOptsReg;
}

def MC_X86RelaxRelocations : Option<
    "x86-relax-relocations",
    "Emit GOTPCRELX/REX_GOTPCRELX/CODE_4_GOTPCRELX instead of GOTPCREL on "
    "x86-64 ELF",
    "bool"> {
  let Registry = MCOptsReg;
  let Default = "1";
}

def MC_X86Sse2Avx : Option<
    "x86-sse2avx",
    "Specify that the assembler should encode SSE instructions with VEX prefix",
    "bool"> {
  let Registry = MCOptsReg;
}

def MC_RelocSectionSym : Option<
    "reloc-section-sym",
    "Control section symbol conversion for relocations",
    "RelocSectionSymType"> {
  let Registry = MCOptsReg;
  let Default = "all";
}

def MC_LargeEHEncoding : Option<
    "large-eh-encoding",
    "Force 8-byte (sdata8) pointer encodings for ELF "
    "exception-handling sections to avoid relocation overflows in "
    "large binaries (x86_64: FDE/personality/LSDA/TType; "
    "AArch64/PPC64: FDE only, the rest are already sdata8)",
    "bool"> {
  let Registry = MCOptsReg;
}

def MC_ABIName : Option<
    "target-abi",
    "The name of the ABI to be targeted from the backend.",
    "std::string"> {
  let Registry = MCOptsReg;
}

def MC_AsSecureLogFile : Option<
    "as-secure-log-file", "As secure log file name",
    "std::string"> {
  let Registry = MCOptsReg;
  let Hidden = 1;
}

// --- MCAsmInfo.cpp options ---

def MC_DwarfExtendedLoc : Option<
    "dwarf-extended-loc",
    "Disable emission of the extended flags in .loc directives.",
    "DefaultOnOff"> {
  let Registry = MCOptsReg;
  let Default = "Default";
  let Hidden = 1;
}

def MC_UseLEB128Directives : Option<
    "use-leb128-directives",
    "Disable the usage of LEB128 directives, and generate .byte instead.",
    "bool"> {
  let Registry = MCOptsReg;
  let Hidden = 1;
}

// --- MCLFI.cpp options ---

def MC_LFIEnableRewriter : Option<
    "lfi-enable-rewriter", "Enable rewriting for LFI.",
    "bool"> {
  let Registry = MCOptsReg;
  let Hidden = 1;
}

// --- MCParser/MCAsmParser.cpp options ---

def MC_AsmMacroMaxNestingDepth : Option<
    "asm-macro-max-nesting-depth",
    "The maximum nesting depth allowed for assembly macros.",
    "unsigned"> {
  let Registry = MCOptsReg;
  let Default = "20";
  let Hidden = 1;
}

// --- MCDXContainerWriter.cpp options ---

def MC_DXEmbedDebug : Option<
    "dx-embed-debug", "Embed PDB in shader container",
    "bool"> {
  let Registry = MCOptsReg;
}

def MC_DXStripDebug : Option<
    "dx-strip-debug", "Strip debug information from shader bytecode",
    "bool"> {
  let Registry = MCOptsReg;
}

def MC_DXSlimDebug : Option<
    "dx-slim-debug", "Generate slim PDB without ILDB part",
    "bool"> {
  let Registry = MCOptsReg;
}

// --- MCSchedule.cpp options ---

def MC_RSScaleFactor : Option<
    "sched-model-reservation-station-scale-factor",
    "Scale the buffer size of all reservation stations by a positive "
    "factor. Buffer sizes of -1/0/1 (unlimited/unbuffered/in-order) "
    "are preserved. Likewise, if the scaled result is <= 1, the "
    "original size is kept. Computed sizes "
    "are truncated towards zero.",
    "float"> {
  let Registry = MCOptsReg;
  let Default = "1.0";
  let Hidden = 1;
  let Category = MCScheduleOptionsCategory;
}
