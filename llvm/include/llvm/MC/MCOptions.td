//===- MCOptions.td - MC options -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

// Named MCLibraryOptions rather than the more obvious "MCOptions": two tools
// (llvm-mca.cpp, llvm-mc.cpp) already have a local variable literally named
// MCOptions holding an MCTargetOptions instance, and reusing that name here
// would make parseLibraryOptionsChain<..., MCOptions, ...> fragile / shadow
// that variable's name in the same scope. Mirrors the InstCombineCLOptions
// precedent (renamed away from InstCombineOptions to dodge a pre-existing
// name in InstCombine.h).
def MCLibraryOptions : OptionsStruct<"llvm"> {}

// =========================================================================
// External enums (defined in MCTargetOptions.h)
// =========================================================================

def EmitDwarfUnwindTypeEnum : OptionEnum<"llvm::EmitDwarfUnwindType", [
    EnumMember<"Always", "always">,
    EnumMember<"NoCompactUnwind", "no-compact-unwind">,
    EnumMember<"DwarfOnly", "dwarf-only">,
    EnumMember<"Default", "default">]> {
  let External = 1;
  let Ident = "EmitDwarfUnwindType";
}

def RelocSectionSymTypeEnum : OptionEnum<"llvm::RelocSectionSymType", [
    EnumMember<"All", "all">,
    EnumMember<"Internal", "internal">,
    EnumMember<"None", "none">]> {
  let External = 1;
  let Ident = "RelocSectionSymType";
}

// =========================================================================
// Inline enum: DefaultOnOff
// =========================================================================

// Spellings are capitalized (unlike this library's other enums) to match a
// pre-existing lit test (DebugInfo/Generic/extended-loc-directive.ll) that
// passes -dwarf-extended-loc=Enable / =Disable.
def DefaultOnOffEnum : OptionEnum<"DefaultOnOff", [
    EnumMember<"Default", "Default">,
    EnumMember<"Enable", "Enable">,
    EnumMember<"Disable", "Disable">]>;

// =========================================================================
// Options
// =========================================================================

// getRelaxAll() resolves to a plain bool (default false), but
// getExplicitRelaxAll() must distinguish "never specified" from an explicit
// value -- hence std::optional<bool> via OptionalValueField rather than a
// plain BoolField.
defm MC_RelaxAll : OptionalValueField<
    "mc-relax-all", "bool", "true",
    "When used with filetype=obj, relax all fixups in the emitted object "
    "file">;

defm MC_IncrementalLinkerCompatible : BoolField<
    "incremental-linker-compatible", "false",
    "When used with filetype=obj, emit an object file which can be used "
    "with an incremental linker">;

defm MC_FDPIC : BoolField<"fdpic", "false", "Use the FDPIC ABI">;

defm MC_DwarfVersion : ValueField<
    "dwarf-version", "int", "0", "Dwarf version">;

defm MC_Dwarf64 : BoolField<
    "dwarf64", "false",
    "Generate debugging info in the 64-bit DWARF format">;

defm MC_EmitDwarfUnwind : EnumField<
    "emit-dwarf-unwind", EmitDwarfUnwindTypeEnum,
    "llvm::EmitDwarfUnwindType::Default",
    "Whether to emit DWARF EH frame entries.">;

defm MC_EmitCompactUnwindNonCanonical : BoolField<
    "emit-compact-unwind-non-canonical", "false",
    "Whether to try to emit Compact Unwind for non canonical entries.">;

defm MC_EmitSFrameUnwind : BoolField<
    "gsframe", "false", "Whether to emit .sframe unwind sections.">;

defm MC_ShowMCInst : BoolField<
    "asm-show-inst", "false",
    "Emit internal instruction representation to assembly file">;

defm MC_FatalWarnings : BoolField<
    "fatal-warnings", "false", "Treat warnings as errors">;

defm MC_NoWarn : BoolField<
    "no-warn", "false", "Suppress all warnings">;

def MC_NoWarnW : Flag<["-", "--"], "W">, Alias<MC_NoWarn>,
    HelpText<"Alias for --no-warn">;

defm MC_NoDeprecatedWarn : BoolField<
    "no-deprecated-warn", "false", "Suppress all deprecated warnings">;

defm MC_NoTypeCheck : BoolField<
    "no-type-check", "false", "Suppress type errors (Wasm)">;

defm MC_SaveTempLabels : BoolField<
    "save-temp-labels", "false", "Don't discard temporary labels">;

defm MC_Crel : BoolField<
    "crel", "false", "Use CREL relocation format for ELF">;

defm MC_ImplicitMapSyms : BoolField<
    "implicit-mapsyms", "false",
    "Allow mapping symbol at section beginning to be implicit, lowering "
    "number of mapping symbols at the expense of some portability. "
    "Recommended for projects that can build all their object files using "
    "this option">;

defm MC_X86RelaxRelocations : BoolField<
    "x86-relax-relocations", "true",
    "Emit GOTPCRELX/REX_GOTPCRELX/CODE_4_GOTPCRELX instead of GOTPCREL on "
    "x86-64 ELF">;

defm MC_X86Sse2Avx : BoolField<
    "x86-sse2avx", "false",
    "Specify that the assembler should encode SSE instructions with VEX "
    "prefix">;

defm MC_RelocSectionSym : EnumField<
    "reloc-section-sym", RelocSectionSymTypeEnum,
    "llvm::RelocSectionSymType::All",
    "Control section symbol conversion for relocations">;

defm MC_LargeEHEncoding : BoolField<
    "large-eh-encoding", "false",
    "Force 8-byte (sdata8) pointer encodings for ELF "
    "exception-handling sections to avoid relocation overflows in "
    "large binaries (x86_64: FDE/personality/LSDA/TType; "
    "AArch64/PPC64: FDE only, the rest are already sdata8)">;

defm MC_DisableIntegratedAS : BoolField<
    "no-integrated-as", "false", "Disable integrated assembler">;

defm MC_ABIName : ValueField<
    "target-abi", "std::string", "\"\"",
    "The name of the ABI to be targeted from the backend.">;

defm MC_AsSecureLogFile : ValueField<
    "as-secure-log-file", "std::string", "\"\"",
    "As secure log file name",
    /*hidden=*/1>;

// --- MCAsmInfo.cpp options ---

defm MC_DwarfExtendedLoc : EnumField<
    "dwarf-extended-loc", DefaultOnOffEnum, "DefaultOnOff::Default",
    "Disable emission of the extended flags in .loc directives.",
    /*hidden=*/1>;

// getUseLEB128Directives() must distinguish "never specified" from an
// explicit value (see MCAsmInfo.cpp / MCAsmInfoXCOFF.cpp), hence
// std::optional<bool> via OptionalValueField rather than a plain BoolField.
defm MC_UseLEB128Directives : OptionalValueField<
    "use-leb128-directives", "bool", "true",
    "Disable the usage of LEB128 directives, and generate .byte instead.",
    /*hidden=*/1>;

// --- MCLFI.cpp options ---

defm MC_LFIEnableRewriter : BoolField<
    "lfi-enable-rewriter", "true", "Enable rewriting for LFI.",
    /*hidden=*/1>;

// --- MCParser/MCAsmParser.cpp options ---

defm MC_AsmMacroMaxNestingDepth : ValueField<
    "asm-macro-max-nesting-depth", "unsigned", "100",
    "The maximum nesting depth allowed for assembly macros.",
    /*hidden=*/1>;

// --- MCDXContainerWriter.cpp options ---

defm MC_DXEmbedDebug : BoolField<
    "dx-embed-debug", "false", "Embed PDB in shader container">;

defm MC_DXStripDebug : BoolField<
    "dx-strip-debug", "false",
    "Strip debug information from shader bytecode">;

defm MC_DXSlimDebug : BoolField<
    "dx-slim-debug", "false",
    "Generate slim PDB without ILDB part">;

// --- MCSchedule.cpp options ---

defm MC_RSScaleFactor : ValueField<
    "sched-model-reservation-station-scale-factor", "float", "1.0",
    "Scale the buffer size of all reservation stations by a positive "
    "factor. Buffer sizes of -1/0/1 (unlimited/unbuffered/in-order) "
    "are preserved. Likewise, if the scaled result is <= 1, the "
    "original size is kept. Computed sizes "
    "are truncated towards zero.",
    /*hidden=*/1>;
