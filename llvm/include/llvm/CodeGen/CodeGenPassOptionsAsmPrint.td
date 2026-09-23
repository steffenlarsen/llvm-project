//===- CodeGenPassOptionsAsmPrint.td - AsmPrinter option defs -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "llvm/Option/OptParser.td"

def CodeGenAsmPrintOptions : OptionsStruct<"llvm"> {}

// =========================================================================
// Enum: PGOMapFeaturesEnum (bitmask multi-select, AsmPrinter.cpp)
// =========================================================================

def PGOMapFeaturesEnum : OptionEnum<"PGOMapFeaturesEnum", [
    EnumMember<"None", "none">,
    EnumMember<"FuncEntryCount", "func-entry-count">,
    EnumMember<"BBFreq", "bb-freq">,
    EnumMember<"BrProb", "br-prob">,
    EnumMember<"PropellerCFG", "propeller-cfg">,
    EnumMember<"All", "all">]>;

// =========================================================================
// Enum: DwarfDefaultOnOff (DwarfDebug.cpp)
// =========================================================================

def DwarfDefaultOnOffEnum : OptionEnum<"DwarfDefaultOnOff", [
    EnumMember<"Default", "Default">,
    EnumMember<"Enable", "Enable">,
    EnumMember<"Disable", "Disable">]>;

// =========================================================================
// Options — AsmPrinter/AsmPrinter.cpp
// =========================================================================

defm CGPASS_PgoAnalysisMap : EnumListField<"pgo-analysis-map", PGOMapFeaturesEnum,
    "Enable PGO analysis map", /*commaJoined=*/1, /*appendEquals=*/1,
    /*hidden=*/1>;

defm CGPASS_PgoAnalysisMapEmitBbSectionsCfg : BoolField<
    "pgo-analysis-map-emit-bb-sections-cfg", "false",
    "Enable the post-link cfg information from the basic block sections "
    "profile in the PGO analysis map", /*hidden=*/1>;

defm CGPASS_BasicBlockAddressMapSkipBbEntries : BoolField<
    "basic-block-address-map-skip-bb-entries", "false",
    "Skip emitting basic block entries in the SHT_LLVM_BB_ADDR_MAP section. "
    "It's used to save binary size when BB entries ...", /*hidden=*/1>;

defm CGPASS_EmitJumpTableSizesSection : BoolField<
    "emit-jump-table-sizes-section", "false",
    "Emit a section containing jump table addresses and sizes",
    /*hidden=*/1>;

defm CGPASS_AsmPrintLatency : BoolField<"asm-print-latency", "false",
    "Print instruction latencies as verbose asm comments", /*hidden=*/1>;

defm CGPASS_StackUsageFile : ValueField<"stack-usage-file", "std::string",
    "\"\"", "Output filename for stack usage information", /*hidden=*/1>;

// =========================================================================
// Options — AsmPrinter/DebugHandlerBase.cpp
// =========================================================================

defm CGPASS_TrimVarLocs : BoolField<"trim-var-locs", "true", "",
    /*hidden=*/1>;

// =========================================================================
// Options — AsmPrinter/DwarfCompileUnit.cpp
// =========================================================================

defm CGPASS_EmitFuncDebugLineTableOffsets : BoolField<
    "emit-func-debug-line-table-offsets", "false",
    "Include line table offset in function's debug info and emit end "
    "sequence after each function's line data.", /*hidden=*/1>;

defm CGPASS_AddLinkageNamesToDeclarationCallOrigins : OptionalValueField<
    "add-linkage-names-to-declaration-call-origins", "bool", "true",
    "Add DW_AT_linkage_name to function declaration DIEs referenced by "
    "DW_AT_call_origin attributes. Enabled by default fo...", /*hidden=*/1>;

// =========================================================================
// Options — AsmPrinter/DwarfDebug.cpp
// =========================================================================

defm CGPASS_UseDwarfRangesBaseAddressSpecifier : BoolField<
    "use-dwarf-ranges-base-address-specifier", "false",
    "Use base address specifiers in debug_ranges", /*hidden=*/1>;

defm CGPASS_GenerateArangeSection : BoolField<"generate-arange-section",
    "false", "Generate dwarf aranges", /*hidden=*/1>;

defm CGPASS_GenerateTypeUnits : BoolField<"generate-type-units", "false",
    "Generate DWARF4 type units.", /*hidden=*/1>;

defm CGPASS_SplitDwarfCrossCuReferences : BoolField<
    "split-dwarf-cross-cu-references", "false",
    "Enable cross-cu references in DWO files", /*hidden=*/1>;

defm CGPASS_NoDwarfRangesSection : BoolField<"no-dwarf-ranges-section",
    "false", "Disable emission .debug_ranges section.", /*hidden=*/1>;

defm CGPASS_UseGnuDebugMacro : BoolField<"use-gnu-debug-macro", "false",
    "Emit the GNU .debug_macro format with DWARF <5", /*hidden=*/1>;

defm CGPASS_DwarfUseKeyInstructions : BoolField<"dwarf-use-key-instructions",
    "true", "Set to false to ignore Key Instructions metadata",
    /*hidden=*/1>;

// =========================================================================
// Options — AsmPrinter/PseudoProbePrinter.cpp
// =========================================================================

defm CGPASS_PseudoProbeVerifyGuidExistenceInDesc : BoolField<
    "pseudo-probe-verify-guid-existence-in-desc", "false",
    "Verify whether GUID exists in the .pseudo_probe_desc.", /*hidden=*/1>;

// =========================================================================
// Options — DwarfDebug.cpp (enum option)
// =========================================================================

defm CGPASS_UseUnknownLocations : EnumField<"use-unknown-locations",
    DwarfDefaultOnOffEnum, "DwarfDefaultOnOff::Default",
    "Make an absence of debug location information explicit.",
    /*hidden=*/1>;
