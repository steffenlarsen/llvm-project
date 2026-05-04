//===- llvm-pdbutil.cpp - Dump debug info from a PDB file -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Dumps debug information present in PDB files.
//
//===----------------------------------------------------------------------===//

#include "llvm-pdbutil.h"

#include "BytesOutputStyle.h"
#include "DumpOutputStyle.h"
#include "ExplainOutputStyle.h"
#include "OutputStyle.h"
#include "PrettyClassDefinitionDumper.h"
#include "PrettyCompilandDumper.h"
#include "PrettyEnumDumper.h"
#include "PrettyExternalSymbolDumper.h"
#include "PrettyFunctionDumper.h"
#include "PrettyTypeDumper.h"
#include "PrettyTypedefDumper.h"
#include "PrettyVariableDumper.h"
#include "YAMLOutputStyle.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Config/config.h"
#include "llvm/DebugInfo/CodeView/AppendingTypeTableBuilder.h"
#include "llvm/DebugInfo/CodeView/DebugChecksumsSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugInlineeLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/DebugLinesSubsection.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/CodeView/MergingTypeTableBuilder.h"
#include "llvm/DebugInfo/CodeView/StringsAndChecksums.h"
#include "llvm/DebugInfo/CodeView/SymbolDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeStreamMerger.h"
#include "llvm/DebugInfo/MSF/MSFBuilder.h"
#include "llvm/DebugInfo/MSF/MappedBlockStream.h"
#include "llvm/DebugInfo/PDB/ConcreteSymbolEnumerator.h"
#include "llvm/DebugInfo/PDB/IPDBEnumChildren.h"
#include "llvm/DebugInfo/PDB/IPDBInjectedSource.h"
#include "llvm/DebugInfo/PDB/IPDBLineNumber.h"
#include "llvm/DebugInfo/PDB/IPDBRawSymbol.h"
#include "llvm/DebugInfo/PDB/IPDBSession.h"
#include "llvm/DebugInfo/PDB/Native/DbiModuleDescriptorBuilder.h"
#include "llvm/DebugInfo/PDB/Native/DbiStreamBuilder.h"
#include "llvm/DebugInfo/PDB/Native/GSIStreamBuilder.h"
#include "llvm/DebugInfo/PDB/Native/InfoStream.h"
#include "llvm/DebugInfo/PDB/Native/InfoStreamBuilder.h"
#include "llvm/DebugInfo/PDB/Native/InputFile.h"
#include "llvm/DebugInfo/PDB/Native/NativeSession.h"
#include "llvm/DebugInfo/PDB/Native/PDBFile.h"
#include "llvm/DebugInfo/PDB/Native/PDBFileBuilder.h"
#include "llvm/DebugInfo/PDB/Native/PDBStringTableBuilder.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/DebugInfo/PDB/Native/RawError.h"
#include "llvm/DebugInfo/PDB/Native/TpiHashing.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/DebugInfo/PDB/Native/TpiStreamBuilder.h"
#include "llvm/DebugInfo/PDB/PDB.h"
#include "llvm/DebugInfo/PDB/PDBSymbolCompiland.h"
#include "llvm/DebugInfo/PDB/PDBSymbolData.h"
#include "llvm/DebugInfo/PDB/PDBSymbolExe.h"
#include "llvm/DebugInfo/PDB/PDBSymbolFunc.h"
#include "llvm/DebugInfo/PDB/PDBSymbolPublicSymbol.h"
#include "llvm/DebugInfo/PDB/PDBSymbolThunk.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeBuiltin.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeEnum.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionArg.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeFunctionSig.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeTypedef.h"
#include "llvm/DebugInfo/PDB/PDBSymbolTypeUDT.h"
#include "llvm/ObjectYAML/yaml2obj.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/BoolOrDefault.h"
#include "llvm/Support/COM.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::codeview;
using namespace llvm::msf;
using namespace llvm::pdb;

// =====================================================================
// clv2 option descriptors
// =====================================================================

// --- diadump subcommand options ---
inline constexpr clv2::ListOptionInfo<std::string> DD_InputFilenamesOpt{
    "", "<input PDB files>", clv2::Positional{}};
inline constexpr clv2::OptionInfo<bool> DD_NativeOpt{
    "native", "Use native PDB reader instead of DIA"};
inline constexpr clv2::OptionInfo<bool> DD_HierarchyOpt{
    "hierarchy", "Show lexical and class parents"};
inline constexpr clv2::OptionInfo<bool> DD_NoIdsOpt{
    "no-ids", "Don't show any SymIndexId fields (overrides -hierarchy)"};
inline constexpr clv2::OptionInfo<bool> DD_RecurseOpt{
    "recurse", "When dumping a SymIndexId, dump the full details"};
inline constexpr clv2::OptionInfo<bool> DD_EnumsOpt{"enums", "Dump enum types"};
inline constexpr clv2::OptionInfo<bool> DD_PointersOpt{"pointers",
                                                       "Dump enum types"};
inline constexpr clv2::OptionInfo<bool> DD_UDTsOpt{"udts", "Dump udt types"};
inline constexpr clv2::OptionInfo<bool> DD_CompilandsOpt{
    "compilands", "Dump compiland information"};
inline constexpr clv2::OptionInfo<bool> DD_FuncsigsOpt{
    "funcsigs", "Dump function signature information"};
inline constexpr clv2::OptionInfo<bool> DD_ArraysOpt{"arrays",
                                                     "Dump array types"};
inline constexpr clv2::OptionInfo<bool> DD_VTShapesOpt{
    "vtshapes", "Dump virtual table shapes"};
inline constexpr clv2::OptionInfo<bool> DD_TypedefsOpt{"typedefs",
                                                       "Dump typedefs"};

// --- pretty subcommand options ---
inline constexpr clv2::ListOptionInfo<std::string> PR_InputFilenamesOpt{
    "", "<input PDB files>", clv2::Positional{}};
inline constexpr clv2::OptionInfo<bool> PR_InjectedSourcesOpt{
    "injected-sources", "Display injected sources"};
inline constexpr clv2::OptionInfo<bool> PR_ShowInjectedSourceContentOpt{
    "injected-source-content",
    "When displaying an injected source, display the file content"};
inline constexpr clv2::ListOptionInfo<std::string> PR_WithNameOpt{
    "with-name", "Display any symbol or type with the specified exact name"};
inline constexpr clv2::OptionInfo<bool> PR_CompilandsOpt{"compilands",
                                                         "Display compilands"};
inline constexpr clv2::OptionInfo<bool> PR_SymbolsOpt{
    "module-syms", "Display symbols for each compiland"};
inline constexpr clv2::OptionInfo<bool> PR_GlobalsOpt{"globals",
                                                      "Dump global symbols"};
inline constexpr clv2::OptionInfo<bool> PR_ExternalsOpt{
    "externals", "Dump external symbols"};

inline constexpr clv2::ListOptionInfo<std::string> PR_SymTypesOpt{
    "sym-types", "Type of symbols to dump (default all)", clv2::CommaSeparated};

inline constexpr clv2::OptionInfo<bool> PR_TypesOpt{
    "types", "Display all types (implies -classes, -enums, -typedefs)"};
inline constexpr clv2::OptionInfo<bool> PR_ClassesOpt{"classes",
                                                      "Display class types"};
inline constexpr clv2::OptionInfo<bool> PR_EnumsOpt{"enums",
                                                    "Display enum types"};
inline constexpr clv2::OptionInfo<bool> PR_TypedefsOpt{"typedefs",
                                                       "Display typedef types"};
inline constexpr clv2::OptionInfo<bool> PR_FuncsigsOpt{
    "funcsigs", "Display function signatures"};
inline constexpr clv2::OptionInfo<bool> PR_PointersOpt{"pointers",
                                                       "Display pointer types"};
inline constexpr clv2::OptionInfo<bool> PR_ArraysOpt{"arrays",
                                                     "Display arrays"};
inline constexpr clv2::OptionInfo<bool> PR_VTShapesOpt{
    "vtshapes", "Display vftable shapes"};

using opts::pretty::SymbolSortMode;
inline constexpr clv2::EnumVal<SymbolSortMode> PR_SymbolOrderVals[] = {
    {"none", SymbolSortMode::None, "Undefined / no particular sort order"},
    {"name", SymbolSortMode::Name, "Sort symbols by name"},
    {"size", SymbolSortMode::Size, "Sort symbols by size"},
};
inline constexpr auto PR_SymbolOrderOpt = clv2::makeEnumOption<SymbolSortMode>(
    "symbol-order", "symbol sort order", PR_SymbolOrderVals,
    clv2::Init{SymbolSortMode::None});

using opts::pretty::ClassSortMode;
inline constexpr clv2::EnumVal<ClassSortMode> PR_ClassOrderVals[] = {
    {"none", ClassSortMode::None, "Undefined / no particular sort order"},
    {"name", ClassSortMode::Name, "Sort classes by name"},
    {"size", ClassSortMode::Size, "Sort classes by size"},
    {"padding", ClassSortMode::Padding, "Sort classes by amount of padding"},
    {"padding-pct", ClassSortMode::PaddingPct,
     "Sort classes by percentage of space consumed by padding"},
    {"padding-imm", ClassSortMode::PaddingImmediate,
     "Sort classes by amount of immediate padding"},
    {"padding-pct-imm", ClassSortMode::PaddingPctImmediate,
     "Sort classes by percentage of space consumed by immediate padding"},
};
inline constexpr auto PR_ClassOrderOpt = clv2::makeEnumOption<ClassSortMode>(
    "class-order", "Class sort order", PR_ClassOrderVals,
    clv2::Init{ClassSortMode::None});

using opts::pretty::ClassDefinitionFormat;
inline constexpr clv2::EnumVal<ClassDefinitionFormat> PR_ClassFormatVals[] = {
    {"all", ClassDefinitionFormat::All,
     "Display all class members including data, constants, typedefs, etc"},
    {"layout", ClassDefinitionFormat::Layout,
     "Only display members that contribute to class size."},
    {"none", ClassDefinitionFormat::None, "Don't display class definitions"},
};
inline constexpr auto PR_ClassFormatOpt =
    clv2::makeEnumOption<ClassDefinitionFormat>(
        "class-definitions", "Class definition format", PR_ClassFormatVals,
        clv2::Init{ClassDefinitionFormat::All});

inline constexpr clv2::OptionInfo<uint32_t> PR_ClassRecursionDepthOpt{
    "class-recurse-depth", "Class recursion depth (0=no limit)",
    clv2::Init{0u}};
inline constexpr clv2::OptionInfo<bool> PR_LinesOpt{"lines", "Line tables"};
inline constexpr clv2::OptionInfo<bool> PR_AllOpt{
    "all", "Implies all other options in 'Symbol Types' category"};
inline constexpr clv2::OptionInfo<uint64_t> PR_LoadAddressOpt{
    "load-address", "Assume the module is loaded at the specified address"};
inline constexpr clv2::OptionInfo<bool> PR_NativeOpt{
    "native", "Use native PDB reader instead of DIA"};
inline constexpr clv2::OptionInfo<std::string> PR_ColorOutputOpt{
    "color-output", "Override use of color (default = isatty)",
    clv2::ValueOptional};
inline constexpr clv2::ListOptionInfo<std::string> PR_ExcludeTypesOpt{
    "exclude-types", "Exclude types by regular expression"};
inline constexpr clv2::ListOptionInfo<std::string> PR_ExcludeSymbolsOpt{
    "exclude-symbols", "Exclude symbols by regular expression"};
inline constexpr clv2::ListOptionInfo<std::string> PR_ExcludeCompilandsOpt{
    "exclude-compilands", "Exclude compilands by regular expression"};
inline constexpr clv2::ListOptionInfo<std::string> PR_IncludeTypesOpt{
    "include-types", "Include only types which match a regular expression"};
inline constexpr clv2::ListOptionInfo<std::string> PR_IncludeSymbolsOpt{
    "include-symbols", "Include only symbols which match a regular expression"};
inline constexpr clv2::ListOptionInfo<std::string> PR_IncludeCompilandsOpt{
    "include-compilands",
    "Include only compilands those which match a regular expression"};
inline constexpr clv2::OptionInfo<uint32_t> PR_SizeThresholdOpt{
    "min-type-size",
    "Displays only those types which are greater than or equal to the "
    "specified size.",
    clv2::Init{0u}};
inline constexpr clv2::OptionInfo<uint32_t> PR_PaddingThresholdOpt{
    "min-class-padding",
    "Displays only those classes which have at least the specified amount "
    "of padding.",
    clv2::Init{0u}};
inline constexpr clv2::OptionInfo<uint32_t> PR_ImmediatePaddingThresholdOpt{
    "min-class-padding-imm",
    "Displays only those classes which have at least the specified amount "
    "of immediate padding, ignoring padding internal to bases and aggregates.",
    clv2::Init{0u}};
inline constexpr clv2::OptionInfo<bool> PR_ExcludeCompilerGeneratedOpt{
    "no-compiler-generated", "Don't show compiler generated types and symbols"};
inline constexpr clv2::OptionInfo<bool> PR_ExcludeSystemLibrariesOpt{
    "no-system-libs", "Don't show symbols from system libraries"};
inline constexpr clv2::OptionInfo<bool> PR_NoEnumDefsOpt{
    "no-enum-definitions", "Don't display full enum definitions"};

// --- bytes subcommand categories ---
inline constexpr clv2::OptionCategory BY_MsfBytesCat{"MSF File Options"};
inline constexpr clv2::OptionCategory BY_DbiBytesCat{"Dbi Stream Options"};
inline constexpr clv2::OptionCategory BY_PdbBytesCat{"PDB Stream Options"};
inline constexpr clv2::OptionCategory BY_TypesCat{"Symbol Type Options"};
inline constexpr clv2::OptionCategory BY_ModuleCat{"Module Options"};

// --- bytes subcommand options ---
inline constexpr clv2::ListOptionInfo<std::string> BY_InputFilenamesOpt{
    "", "<input PDB files>", clv2::Positional{}};
inline constexpr clv2::OptionInfo<std::string> BY_DumpBlockRangeOpt{
    "block-range", "Dump binary data from specified range of blocks.",
    clv2::value_desc("start[-end]"), clv2::cat(BY_MsfBytesCat)};
inline constexpr clv2::OptionInfo<std::string> BY_DumpByteRangeOpt{
    "byte-range", "Dump binary data from specified range of bytes",
    clv2::value_desc("start[-end]"), clv2::cat(BY_MsfBytesCat)};
inline constexpr clv2::ListOptionInfo<std::string> BY_DumpStreamDataOpt{
    "stream-data",
    "Dump binary data from specified streams. Format is SN[:Start][@Size]",
    clv2::CommaSeparated, clv2::cat(BY_MsfBytesCat)};
inline constexpr clv2::OptionInfo<bool> BY_NameMapOpt{
    "name-map", "Dump bytes of PDB Name Map", clv2::cat(BY_PdbBytesCat)};
inline constexpr clv2::OptionInfo<bool> BY_FpmOpt{"fpm", "Dump free page map",
                                                  clv2::cat(BY_MsfBytesCat)};
inline constexpr clv2::OptionInfo<bool> BY_SectionContributionsOpt{
    "sc", "Dump section contributions", clv2::cat(BY_DbiBytesCat)};
inline constexpr clv2::OptionInfo<bool> BY_SectionMapOpt{
    "sm", "Dump section map", clv2::cat(BY_DbiBytesCat)};
inline constexpr clv2::OptionInfo<bool> BY_ModuleInfosOpt{
    "modi", "Dump module info", clv2::cat(BY_DbiBytesCat)};
inline constexpr clv2::OptionInfo<bool> BY_FileInfoOpt{
    "files", "Dump source file info", clv2::cat(BY_DbiBytesCat)};
inline constexpr clv2::OptionInfo<bool> BY_TypeServerMapOpt{
    "type-server", "Dump type server map", clv2::cat(BY_DbiBytesCat)};
inline constexpr clv2::OptionInfo<bool> BY_ECDataOpt{
    "ec", "Dump edit and continue map", clv2::cat(BY_DbiBytesCat)};
inline constexpr clv2::ListOptionInfo<uint32_t> BY_TypeIndexOpt{
    "type", "Dump the type record with the given type index",
    clv2::CommaSeparated, clv2::cat(BY_TypesCat)};
inline constexpr clv2::ListOptionInfo<uint32_t> BY_IdIndexOpt{
    "id", "Dump the id record with the given type index", clv2::CommaSeparated,
    clv2::cat(BY_TypesCat)};
inline constexpr clv2::OptionInfo<uint32_t> BY_ModuleIndexOpt{
    "mod",
    "Limit options in the Modules category to the specified module index",
    clv2::cat(BY_ModuleCat)};
inline constexpr clv2::OptionInfo<bool> BY_ModuleSymsOpt{
    "syms", "Dump symbol record substream", clv2::cat(BY_ModuleCat)};
inline constexpr clv2::OptionInfo<bool> BY_ModuleC11Opt{
    "c11-chunks", "Dump C11 CodeView debug chunks", clv2::Hidden,
    clv2::cat(BY_ModuleCat)};
inline constexpr clv2::OptionInfo<bool> BY_ModuleC13Opt{
    "chunks", "Dump C13 CodeView debug chunk subsection",
    clv2::cat(BY_ModuleCat)};
inline constexpr clv2::OptionInfo<bool> BY_SplitChunksOpt{
    "split-chunks",
    "When dumping debug chunks, show a different section for each chunk",
    clv2::cat(BY_ModuleCat)};

// --- dump subcommand options ---
inline constexpr clv2::ListOptionInfo<std::string> DU_InputFilenamesOpt{
    "", "<input PDB files>", clv2::Positional{}};
inline constexpr clv2::OptionInfo<bool> DU_SummaryOpt{"summary",
                                                      "dump file summary"};
inline constexpr clv2::OptionInfo<bool> DU_StreamsOpt{
    "streams", "dump summary of the PDB streams"};
inline constexpr clv2::OptionInfo<bool> DU_StreamBlocksOpt{
    "stream-blocks", "Add block information to the output of -streams"};
inline constexpr clv2::OptionInfo<bool> DU_SymbolStatsOpt{
    "sym-stats",
    "Dump a detailed breakdown of symbol usage/size for each module"};
inline constexpr clv2::OptionInfo<bool> DU_TypeStatsOpt{
    "type-stats", "Dump a detailed breakdown of type usage/size"};
inline constexpr clv2::OptionInfo<bool> DU_IDStatsOpt{
    "id-stats", "Dump a detailed breakdown of IPI types usage/size"};
inline constexpr clv2::OptionInfo<bool> DU_UdtStatsOpt{
    "udt-stats", "Dump a detailed breakdown of S_UDT record usage / stats"};
inline constexpr clv2::OptionInfo<bool> DU_TypesOpt{
    "types", "dump CodeView type records from TPI stream"};
inline constexpr clv2::OptionInfo<bool> DU_TypeDataOpt{
    "type-data", "dump CodeView type record raw bytes from TPI stream"};
inline constexpr clv2::OptionInfo<bool> DU_TypeRefStatsOpt{
    "type-ref-stats",
    "dump statistics on the number and size of types transitively referenced "
    "by symbol records"};
inline constexpr clv2::OptionInfo<bool> DU_TypeExtrasOpt{
    "type-extras", "dump type hashes and index offsets"};
inline constexpr clv2::OptionInfo<bool> DU_DontResolveForwardRefsOpt{
    "dont-resolve-forward-refs",
    "When dumping type records, don't try to resolve forward references"};
inline constexpr clv2::ListOptionInfo<uint32_t> DU_TypeIndexOpt{
    "type-index", "only dump types with the specified hexadecimal type index",
    clv2::CommaSeparated};
inline constexpr clv2::OptionInfo<bool> DU_IdsOpt{
    "ids", "dump CodeView type records from IPI stream"};
inline constexpr clv2::OptionInfo<bool> DU_IdDataOpt{
    "id-data", "dump CodeView type record raw bytes from IPI stream"};
inline constexpr clv2::OptionInfo<bool> DU_IdExtrasOpt{
    "id-extras", "dump id hashes and index offsets"};
inline constexpr clv2::ListOptionInfo<uint32_t> DU_IdIndexOpt{
    "id-index", "only dump ids with the specified hexadecimal type index",
    clv2::CommaSeparated};
inline constexpr clv2::OptionInfo<bool> DU_TypeDependentsOpt{
    "dependents",
    "With -type-index/-id-index, dumps the entire dependency graph"};
inline constexpr clv2::OptionInfo<bool> DU_GlobalsOpt{
    "globals", "dump Globals symbol records"};
inline constexpr clv2::OptionInfo<bool> DU_GlobalExtrasOpt{
    "global-extras", "dump Globals hashes"};
inline constexpr clv2::ListOptionInfo<std::string> DU_GlobalNamesOpt{
    "global-name",
    "With -globals, only dump globals whose name matches the given value"};
inline constexpr clv2::OptionInfo<bool> DU_PublicsOpt{
    "publics", "dump Publics stream data"};
inline constexpr clv2::OptionInfo<bool> DU_PublicExtrasOpt{
    "public-extras", "dump Publics hashes and address maps"};
inline constexpr clv2::OptionInfo<bool> DU_GSIRecordsOpt{
    "gsi-records", "dump public / global common record stream"};
inline constexpr clv2::OptionInfo<bool> DU_SymbolsOpt{"symbols",
                                                      "dump module symbols"};
inline constexpr clv2::OptionInfo<bool> DU_SymRecordBytesOpt{
    "sym-data", "dump CodeView symbol record raw bytes"};
inline constexpr clv2::OptionInfo<bool> DU_FpoOpt{"fpo", "dump FPO records"};
inline constexpr clv2::OptionInfo<uint32_t> DU_SymbolOffsetOpt{
    "symbol-offset",
    "only dump symbol record with the specified symbol offset"};
inline constexpr clv2::OptionInfo<bool> DU_ParentsOpt{
    "show-parents", "dump the symbols record's all parents."};
inline constexpr clv2::OptionInfo<uint32_t> DU_ParentDepthOpt{
    "parent-recurse-depth",
    "only recurse to a depth of N when displaying parents of a symbol record.",
    clv2::Init{~0u}};
inline constexpr clv2::OptionInfo<bool> DU_ChildrenOpt{
    "show-children", "dump the symbols record's all children."};
inline constexpr clv2::OptionInfo<uint32_t> DU_ChildrenDepthOpt{
    "children-recurse-depth",
    "only recurse to a depth of N when displaying children of a symbol record.",
    clv2::Init{~0u}};
inline constexpr clv2::OptionInfo<bool> DU_ModulesOpt{
    "modules", "dump compiland information"};
inline constexpr clv2::OptionInfo<bool> DU_ModuleFilesOpt{
    "files", "Dump the source files that contribute to each module's."};
inline constexpr clv2::OptionInfo<bool> DU_LinesOpt{
    "l", "dump source file/line information (DEBUG_S_LINES subsection)"};
inline constexpr clv2::OptionInfo<bool> DU_InlineeLinesOpt{
    "il", "dump inlinee line information (DEBUG_S_INLINEELINES subsection)"};
inline constexpr clv2::OptionInfo<bool> DU_XmiOpt{
    "xmi", "dump cross module imports (DEBUG_S_CROSSSCOPEIMPORTS subsection)"};
inline constexpr clv2::OptionInfo<bool> DU_XmeOpt{
    "xme", "dump cross module exports (DEBUG_S_CROSSSCOPEEXPORTS subsection)"};
inline constexpr clv2::OptionInfo<uint32_t> DU_ModiOpt{
    "modi", "For all options that iterate over modules, limit to the "
            "specified module"};
inline constexpr clv2::OptionInfo<bool> DU_JustMyCodeOpt{
    "jmc", "For all options that iterate over modules, ignore modules from "
           "system libraries"};
inline constexpr clv2::OptionInfo<bool> DU_NamedStreamsOpt{
    "named-streams", "dump PDB named stream table"};
inline constexpr clv2::OptionInfo<bool> DU_StringTableOpt{
    "string-table", "dump PDB String Table"};
inline constexpr clv2::OptionInfo<bool> DU_StringTableDetailsOpt{
    "string-table-details", "dump PDB String Table Details"};
inline constexpr clv2::OptionInfo<bool> DU_SectionContribsOpt{
    "section-contribs", "dump section contributions"};
inline constexpr clv2::OptionInfo<bool> DU_SectionMapOpt{"section-map",
                                                         "dump section map"};
inline constexpr clv2::OptionInfo<bool> DU_SectionHeadersOpt{
    "section-headers", "Dump image section headers"};
inline constexpr clv2::OptionInfo<bool> DU_DXContainerOpt{"dxcontainer",
                                                          "dump DXContainer"};
inline constexpr clv2::OptionInfo<bool> DU_RawAllOpt{
    "all", "Implies most other options."};

// --- yaml2pdb subcommand options ---
inline constexpr clv2::OptionInfo<std::string> Y2P_OutputFileOpt{
    "pdb", "the name of the PDB file to write"};
inline constexpr clv2::OptionInfo<std::string> Y2P_InputFilenameOpt{
    "", "<input YAML file>", clv2::Positional{}, clv2::Required};
inline constexpr clv2::OptionInfo<unsigned> Y2P_DocNumOpt{
    "docnum", "Read specified document from input (default = 1)",
    clv2::Init{1u}};

// --- pdb2yaml subcommand options ---
inline constexpr clv2::ListOptionInfo<std::string> P2Y_InputFilenameOpt{
    "", "<input PDB file>", clv2::Positional{}, clv2::Required};
inline constexpr clv2::OptionInfo<bool> P2Y_AllOpt{
    "all", "Dump everything we know how to dump."};
inline constexpr clv2::OptionInfo<bool> P2Y_NoFileHeadersOpt{
    "no-file-headers", "Do not dump MSF file headers"};
inline constexpr clv2::OptionInfo<bool> P2Y_MinimalOpt{
    "minimal", "Don't write fields with default values"};
inline constexpr clv2::OptionInfo<bool> P2Y_StreamMetadataOpt{
    "stream-metadata", "Dump the number of streams and each stream's size"};
inline constexpr clv2::OptionInfo<bool> P2Y_StreamDirectoryOpt{
    "stream-directory",
    "Dump each stream's block map (implies -stream-metadata)"};
inline constexpr clv2::OptionInfo<bool> P2Y_PdbStreamOpt{
    "pdb-stream", "Dump the PDB Stream (Stream 1)"};
inline constexpr clv2::OptionInfo<bool> P2Y_StringTableOpt{
    "string-table", "Dump the PDB String Table"};
inline constexpr clv2::OptionInfo<bool> P2Y_DbiStreamOpt{
    "dbi-stream", "Dump the DBI Stream Headers (Stream 2)"};
inline constexpr clv2::OptionInfo<bool> P2Y_TpiStreamOpt{
    "tpi-stream", "Dump the TPI Stream (Stream 3)"};
inline constexpr clv2::OptionInfo<bool> P2Y_IpiStreamOpt{
    "ipi-stream", "Dump the IPI Stream (Stream 5)"};
inline constexpr clv2::OptionInfo<bool> P2Y_PublicsStreamOpt{
    "publics-stream", "Dump the Publics Stream"};
inline constexpr clv2::OptionInfo<bool> P2Y_DumpModulesOpt{
    "modules", "dump compiland information"};
inline constexpr clv2::OptionInfo<bool> P2Y_DumpModuleFilesOpt{
    "module-files", "dump file information"};
inline constexpr clv2::ListOptionInfo<std::string> P2Y_DumpModuleSubsectionsOpt{
    "subsections", "dump subsections from each module's debug stream",
    clv2::CommaSeparated};
inline constexpr clv2::OptionInfo<bool> P2Y_DumpModuleSymsOpt{
    "module-syms", "dump module symbols"};
inline constexpr clv2::OptionInfo<bool> P2Y_DumpSectionHeadersOpt{
    "section-headers", "Dump section headers."};
inline constexpr clv2::OptionInfo<bool> P2Y_DumpSectionContribsOpt{
    "section-contribs", "dump section contributions"};
inline constexpr clv2::OptionInfo<bool> P2Y_DXContainerStreamOpt{
    "dxcontainer", "Dump the DXContainer Stream"};

// --- merge subcommand options ---
inline constexpr clv2::ListOptionInfo<std::string> MG_InputFilenamesOpt{
    "", "<input PDB files>", clv2::Positional{}};
inline constexpr clv2::OptionInfo<std::string> MG_PdbOutputFileOpt{
    "pdb", "the name of the PDB file to write"};

// --- explain subcommand options ---
inline constexpr clv2::ListOptionInfo<std::string> EX_InputFilenameOpt{
    "", "<input PDB file>", clv2::Positional{}, clv2::Required};
inline constexpr clv2::ListOptionInfo<uint64_t> EX_OffsetsOpt{
    "offset", "The file offset to explain"};

using opts::explain::InputFileType;
inline constexpr clv2::EnumVal<InputFileType> EX_InputTypeVals[] = {
    {"pdb-file", InputFileType::PDBFile, "Treat input as a PDB file (default)"},
    {"pdb-stream", InputFileType::PDBStream,
     "Treat input as raw contents of PDB stream"},
    {"dbi-stream", InputFileType::DBIStream,
     "Treat input as raw contents of DBI stream"},
    {"names-stream", InputFileType::Names,
     "Treat input as raw contents of /names named stream"},
    {"mod-stream", InputFileType::ModuleStream,
     "Treat input as raw contents of a module stream"},
};
inline constexpr auto EX_InputTypeOpt = clv2::makeEnumOption<InputFileType>(
    "input-type", "Specify how to interpret the input file", EX_InputTypeVals,
    clv2::Init{InputFileType::PDBFile});

// --- export subcommand options ---
inline constexpr clv2::ListOptionInfo<std::string> ES_InputFilenameOpt{
    "", "<input PDB file>", clv2::Positional{}, clv2::Required};
inline constexpr clv2::OptionInfo<std::string> ES_OutputFileOpt{
    "out", "The file to write the stream to", clv2::Required};
inline constexpr clv2::OptionInfo<std::string> ES_StreamOpt{
    "stream", "The index or name of the stream whose contents to export"};
inline constexpr clv2::OptionInfo<bool> ES_ForceNameOpt{
    "name", "Force the interpretation of -stream as a string, even if it is "
            "a valid integer"};
inline constexpr clv2::OptionInfo<bool> ES_DXContainerOpt{
    "dxcontainer", "Export DirectX Container, if present"};

// =====================================================================
// SubCommandInfo entries
// =====================================================================

inline constexpr clv2::SubCommandInfo<
    &DD_InputFilenamesOpt, &DD_NativeOpt, &DD_HierarchyOpt, &DD_NoIdsOpt,
    &DD_RecurseOpt, &DD_EnumsOpt, &DD_PointersOpt, &DD_UDTsOpt,
    &DD_CompilandsOpt, &DD_FuncsigsOpt, &DD_ArraysOpt, &DD_VTShapesOpt,
    &DD_TypedefsOpt>
    DiaDumpCmd{"diadump", "Dump debug information using a DIA-like API"};

inline constexpr clv2::SubCommandInfo<
    &PR_InputFilenamesOpt, &PR_InjectedSourcesOpt,
    &PR_ShowInjectedSourceContentOpt, &PR_WithNameOpt, &PR_CompilandsOpt,
    &PR_SymbolsOpt, &PR_GlobalsOpt, &PR_ExternalsOpt, &PR_SymTypesOpt,
    &PR_TypesOpt, &PR_ClassesOpt, &PR_EnumsOpt, &PR_TypedefsOpt,
    &PR_FuncsigsOpt, &PR_PointersOpt, &PR_ArraysOpt, &PR_VTShapesOpt,
    &PR_SymbolOrderOpt, &PR_ClassOrderOpt, &PR_ClassFormatOpt,
    &PR_ClassRecursionDepthOpt, &PR_LinesOpt, &PR_AllOpt, &PR_LoadAddressOpt,
    &PR_NativeOpt, &PR_ColorOutputOpt, &PR_ExcludeTypesOpt,
    &PR_ExcludeSymbolsOpt, &PR_ExcludeCompilandsOpt, &PR_IncludeTypesOpt,
    &PR_IncludeSymbolsOpt, &PR_IncludeCompilandsOpt, &PR_SizeThresholdOpt,
    &PR_PaddingThresholdOpt, &PR_ImmediatePaddingThresholdOpt,
    &PR_ExcludeCompilerGeneratedOpt, &PR_ExcludeSystemLibrariesOpt,
    &PR_NoEnumDefsOpt>
    PrettyCmd{"pretty", "Dump semantic information about types and symbols"};

inline constexpr clv2::SubCommandInfo<
    &BY_InputFilenamesOpt, &BY_DumpBlockRangeOpt, &BY_DumpByteRangeOpt,
    &BY_DumpStreamDataOpt, &BY_NameMapOpt, &BY_FpmOpt,
    &BY_SectionContributionsOpt, &BY_SectionMapOpt, &BY_ModuleInfosOpt,
    &BY_FileInfoOpt, &BY_TypeServerMapOpt, &BY_ECDataOpt, &BY_TypeIndexOpt,
    &BY_IdIndexOpt, &BY_ModuleIndexOpt, &BY_ModuleSymsOpt, &BY_ModuleC11Opt,
    &BY_ModuleC13Opt, &BY_SplitChunksOpt>
    BytesCmd{"bytes", "Dump raw bytes from the PDB file"};

inline constexpr clv2::SubCommandInfo<
    &DU_InputFilenamesOpt, &DU_SummaryOpt, &DU_StreamsOpt, &DU_StreamBlocksOpt,
    &DU_SymbolStatsOpt, &DU_TypeStatsOpt, &DU_IDStatsOpt, &DU_UdtStatsOpt,
    &DU_TypesOpt, &DU_TypeDataOpt, &DU_TypeRefStatsOpt, &DU_TypeExtrasOpt,
    &DU_DontResolveForwardRefsOpt, &DU_TypeIndexOpt, &DU_IdsOpt, &DU_IdDataOpt,
    &DU_IdExtrasOpt, &DU_IdIndexOpt, &DU_TypeDependentsOpt, &DU_GlobalsOpt,
    &DU_GlobalExtrasOpt, &DU_GlobalNamesOpt, &DU_PublicsOpt,
    &DU_PublicExtrasOpt, &DU_GSIRecordsOpt, &DU_SymbolsOpt,
    &DU_SymRecordBytesOpt, &DU_FpoOpt, &DU_SymbolOffsetOpt, &DU_ParentsOpt,
    &DU_ParentDepthOpt, &DU_ChildrenOpt, &DU_ChildrenDepthOpt, &DU_ModulesOpt,
    &DU_ModuleFilesOpt, &DU_LinesOpt, &DU_InlineeLinesOpt, &DU_XmiOpt,
    &DU_XmeOpt, &DU_ModiOpt, &DU_JustMyCodeOpt, &DU_NamedStreamsOpt,
    &DU_StringTableOpt, &DU_StringTableDetailsOpt, &DU_SectionContribsOpt,
    &DU_SectionMapOpt, &DU_SectionHeadersOpt, &DU_DXContainerOpt, &DU_RawAllOpt>
    DumpCmd{"dump", "Dump MSF and CodeView debug info"};

inline constexpr clv2::SubCommandInfo<&Y2P_OutputFileOpt, &Y2P_InputFilenameOpt,
                                      &Y2P_DocNumOpt>
    YamlToPdbCmd{"yaml2pdb", "Generate a PDB file from a YAML description"};

inline constexpr clv2::SubCommandInfo<
    &P2Y_InputFilenameOpt, &P2Y_AllOpt, &P2Y_NoFileHeadersOpt, &P2Y_MinimalOpt,
    &P2Y_StreamMetadataOpt, &P2Y_StreamDirectoryOpt, &P2Y_PdbStreamOpt,
    &P2Y_StringTableOpt, &P2Y_DbiStreamOpt, &P2Y_TpiStreamOpt,
    &P2Y_IpiStreamOpt, &P2Y_PublicsStreamOpt, &P2Y_DumpModulesOpt,
    &P2Y_DumpModuleFilesOpt, &P2Y_DumpModuleSubsectionsOpt,
    &P2Y_DumpModuleSymsOpt, &P2Y_DumpSectionHeadersOpt,
    &P2Y_DumpSectionContribsOpt, &P2Y_DXContainerStreamOpt>
    PdbToYamlCmd{"pdb2yaml",
                 "Generate a detailed YAML description of a PDB File"};

inline constexpr clv2::SubCommandInfo<&MG_InputFilenamesOpt,
                                      &MG_PdbOutputFileOpt>
    MergeCmd{"merge", "Merge multiple PDBs into a single PDB"};

inline constexpr clv2::SubCommandInfo<&EX_InputFilenameOpt, &EX_OffsetsOpt,
                                      &EX_InputTypeOpt>
    ExplainCmd{"explain", "Explain the meaning of a file offset"};

inline constexpr clv2::SubCommandInfo<&ES_InputFilenameOpt, &ES_OutputFileOpt,
                                      &ES_StreamOpt, &ES_ForceNameOpt,
                                      &ES_DXContainerOpt>
    ExportCmd{"export", "Write binary data from a stream to a file"};

// =====================================================================
// Registries
// =====================================================================

static constexpr clv2::OptionsRegistry<&DiaDumpCmd, &PrettyCmd, &BytesCmd,
                                       &DumpCmd, &YamlToPdbCmd, &PdbToYamlCmd,
                                       &MergeCmd, &ExplainCmd, &ExportCmd>
    PDBUtilToolReg;

// =====================================================================
// File-scope parsed variables
// =====================================================================

namespace opts {

namespace diadump {
static std::vector<std::string> InputFilenames;
static bool Native = false;
static bool ShowClassHierarchy = false;
static bool NoSymIndexIds = false;
static bool Recurse = false;
static bool Enums = false;
static bool Pointers = false;
static bool UDTs = false;
static bool Compilands = false;
static bool Funcsigs = false;
static bool Arrays = false;
static bool VTShapes = false;
static bool Typedefs = false;
} // namespace diadump

FilterOptions Filters;

namespace pretty {
static std::vector<std::string> InputFilenames;
bool InjectedSources = false;
bool ShowInjectedSourceContent = false;
std::vector<std::string> WithName;
bool Compilands = false;
bool Symbols = false;
bool Globals = false;
bool Externals = false;
static std::vector<SymLevel> SymTypes;
bool Classes = false;
bool Enums = false;
bool Typedefs = false;
bool Funcsigs = false;
bool Pointers = false;
bool Arrays = false;
bool VTShapes = false;
static bool Types = false;
SymbolSortMode SymbolOrder = SymbolSortMode::None;
ClassSortMode ClassOrder = ClassSortMode::None;
ClassDefinitionFormat ClassFormat = ClassDefinitionFormat::All;
uint32_t ClassRecursionDepth = 0;
bool Lines = false;
bool All = false;
uint64_t LoadAddress = 0;
bool Native = false;
cl::boolOrDefault ColorOutput = cl::boolOrDefault::BOU_UNSET;
std::vector<std::string> ExcludeTypes;
std::vector<std::string> ExcludeSymbols;
std::vector<std::string> ExcludeCompilands;
std::vector<std::string> IncludeTypes;
std::vector<std::string> IncludeSymbols;
std::vector<std::string> IncludeCompilands;
uint32_t SizeThreshold = 0;
uint32_t PaddingThreshold = 0;
uint32_t ImmediatePaddingThreshold = 0;
bool ExcludeCompilerGenerated = false;
bool ExcludeSystemLibraries = false;
bool NoEnumDefs = false;
} // namespace pretty

namespace bytes {
std::optional<NumberRange> DumpBlockRange;
std::optional<NumberRange> DumpByteRange;
static std::string DumpBlockRangeOpt;
static std::string DumpByteRangeOpt;
std::vector<std::string> DumpStreamData;
bool NameMap = false;
bool Fpm = false;
bool SectionContributions = false;
bool SectionMap = false;
bool ModuleInfos = false;
bool FileInfo = false;
bool TypeServerMap = false;
bool ECData = false;
std::vector<uint32_t> TypeIndex;
std::vector<uint32_t> IdIndex;
std::optional<uint32_t> ModuleIndex;
bool ModuleSyms = false;
bool ModuleC11 = false;
bool ModuleC13 = false;
bool SplitChunks = false;
static std::vector<std::string> InputFilenames;
} // namespace bytes

namespace dump {
bool DumpSummary = false;
bool DumpFpm = false;
bool DumpStreams = false;
bool DumpSymbolStats = false;
bool DumpTypeStats = false;
bool DumpIDStats = false;
bool DumpUdtStats = false;
bool DumpStreamBlocks = false;
bool DumpTypes = false;
bool DumpTypeData = false;
bool DumpTypeRefStats = false;
bool DumpTypeExtras = false;
bool DontResolveForwardRefs = false;
std::vector<uint32_t> DumpTypeIndex;
bool DumpIds = false;
bool DumpIdData = false;
bool DumpIdExtras = false;
std::vector<uint32_t> DumpIdIndex;
bool DumpTypeDependents = false;
bool DumpGlobals = false;
bool DumpGlobalExtras = false;
std::vector<std::string> DumpGlobalNames;
bool DumpPublics = false;
bool DumpPublicExtras = false;
bool DumpGSIRecords = false;
bool DumpSymbols = false;
bool DumpSymRecordBytes = false;
bool DumpDXContainer = false;
bool DumpFpo = false;
uint32_t DumpSymbolOffset = 0;
bool DumpParents = false;
uint32_t DumpParentDepth = ~0u;
bool DumpChildren = false;
uint32_t DumpChildrenDepth = ~0u;
bool DumpModules = false;
bool DumpModuleFiles = false;
bool DumpLines = false;
bool DumpInlineeLines = false;
bool DumpXmi = false;
bool DumpXme = false;
std::optional<uint32_t> DumpModi;
bool JustMyCode = false;
bool DumpNamedStreams = false;
bool DumpStringTable = false;
bool DumpStringTableDetails = false;
bool DumpSectionContribs = false;
bool DumpSectionMap = false;
bool DumpSectionHeaders = false;
bool RawAll = false;
static std::vector<std::string> InputFilenames;
} // namespace dump

namespace yaml2pdb {
static std::string YamlPdbOutputFile;
static std::string InputFilename;
static unsigned DocNum = 1;
} // namespace yaml2pdb

namespace pdb2yaml {
bool All = false;
bool NoFileHeaders = false;
bool Minimal = false;
bool StreamMetadata = false;
bool StreamDirectory = false;
bool PdbStream = false;
bool StringTable = false;
bool DbiStream = false;
bool TpiStream = false;
bool IpiStream = false;
bool PublicsStream = false;
bool DumpModules = false;
bool DumpModuleFiles = false;
std::vector<ModuleSubsection> DumpModuleSubsections;
bool DumpModuleSyms = false;
bool DumpSectionContribs = false;
bool DumpSectionHeaders = false;
bool DXContainerStream = false;
std::vector<std::string> InputFilename;
} // namespace pdb2yaml

namespace merge {
static std::vector<std::string> InputFilenames;
static std::string PdbOutputFile;
} // namespace merge

namespace explain {
std::vector<std::string> InputFilename;
std::vector<uint64_t> Offsets;
InputFileType InputType = InputFileType::PDBFile;
} // namespace explain

namespace exportstream {
static std::vector<std::string> InputFilename;
std::string OutputFile;
std::string Stream;
bool ForceName = false;
bool DXContainer = false;
} // namespace exportstream
} // namespace opts

static ExitOnError ExitOnErr;

static void yamlToPdb(StringRef Path, unsigned DocNum) {
  BumpPtrAllocator Allocator;
  ErrorOr<std::unique_ptr<MemoryBuffer>> ErrorOrBuffer =
      MemoryBuffer::getFileOrSTDIN(Path, /*IsText=*/false,
                                   /*RequiresNullTerminator=*/false);

  if (ErrorOrBuffer.getError()) {
    ExitOnErr(createFileError(Path, errorCodeToError(ErrorOrBuffer.getError())));
  }

  if (DocNum == 0)
    ExitOnErr(createStringError(
        "document numbers are 1-based, there is no 0th document"));

  std::unique_ptr<MemoryBuffer> &Buffer = ErrorOrBuffer.get();

  llvm::yaml::Input In(Buffer->getBuffer());
  for (unsigned CurrentDoc = 1; CurrentDoc < DocNum; ++CurrentDoc) {
    if (!In.nextDocument())
      ExitOnErr(createFileError(
          Path,
          createStringError("cannot find the " + Twine(DocNum) +
                            getOrdinalSuffix(DocNum).data() + " document")));
  }
  pdb::yaml::PdbObject YamlObj(Allocator);
  In >> YamlObj;

  PDBFileBuilder Builder(Allocator);

  uint32_t BlockSize = 4096;
  if (YamlObj.Headers)
    BlockSize = YamlObj.Headers->SuperBlock.BlockSize;
  ExitOnErr(Builder.initialize(BlockSize));
  // Add each of the reserved streams.  We ignore stream metadata in the
  // yaml, because we will reconstruct our own view of the streams.  For
  // example, the YAML may say that there were 20 streams in the original
  // PDB, but maybe we only dump a subset of those 20 streams, so we will
  // have fewer, and the ones we do have may end up with different indices
  // than the ones in the original PDB.  So we just start with a clean slate.
  for (uint32_t I = 0; I < kSpecialStreamCount; ++I)
    ExitOnErr(Builder.getMsfBuilder().addStream(0));

  auto &Dxc = YamlObj.DXContainerStream;
  if (Dxc) {
    // If there is a DXContainer, add add it as a stream #5.
    ExitOnErr(Builder.getMsfBuilder().addStream(0));
  }

  pdb::yaml::PdbInfoStream DefaultInfoStream;
  const auto &Info = YamlObj.PdbStream.value_or(DefaultInfoStream);

  auto &InfoBuilder = Builder.getInfoBuilder();
  InfoBuilder.setAge(Info.Age);
  InfoBuilder.setGuid(Info.Guid);
  InfoBuilder.setSignature(Info.Signature);
  InfoBuilder.setVersion(Info.Version);
  for (auto F : Info.Features)
    InfoBuilder.addFeature(F);

  if (Dxc) {
    auto &Data = Builder.getDXContainerData();
    llvm::raw_svector_ostream DataStream(*Data);
    std::string ErrorMsg;
    llvm::yaml::yaml2dxcontainer(
        Dxc->DXC, DataStream, [&ErrorMsg](const Twine &Msg) {
          ErrorMsg = (Twine("DXContainer error: ") + Msg).str();
        });
    if (!ErrorMsg.empty())
      ExitOnErr(createStringError(ErrorMsg));

    codeview::GUID IgnoredOutGuid;
    ExitOnErr(
        Builder.commit(opts::yaml2pdb::YamlPdbOutputFile, &IgnoredOutGuid));
    // Leave all other streams empty if there is a DXContainer.
    return;
  }

  StringsAndChecksums Strings;
  Strings.setStrings(std::make_shared<DebugStringTableSubsection>());

  if (YamlObj.StringTable) {
    for (auto S : *YamlObj.StringTable)
      Strings.strings()->insert(S);
  }

  pdb::yaml::PdbDbiStream DefaultDbiStream;
  pdb::yaml::PdbTpiStream DefaultTpiStream;
  pdb::yaml::PdbTpiStream DefaultIpiStream;

  const auto &Dbi = YamlObj.DbiStream.value_or(DefaultDbiStream);
  auto &DbiBuilder = Builder.getDbiBuilder();
  DbiBuilder.setAge(Dbi.Age);
  DbiBuilder.setBuildNumber(Dbi.BuildNumber);
  DbiBuilder.setFlags(Dbi.Flags);
  DbiBuilder.setMachineType(Dbi.MachineType);
  DbiBuilder.setPdbDllRbld(Dbi.PdbDllRbld);
  DbiBuilder.setPdbDllVersion(Dbi.PdbDllVersion);
  DbiBuilder.setVersionHeader(Dbi.VerHeader);
  for (const auto &MI : Dbi.ModInfos) {
    auto &ModiBuilder = ExitOnErr(DbiBuilder.addModuleInfo(MI.Mod));
    ModiBuilder.setObjFileName(MI.Obj);

    for (auto S : MI.SourceFiles)
      ExitOnErr(DbiBuilder.addModuleSourceFile(ModiBuilder, S));
    if (MI.Modi) {
      const auto &ModiStream = *MI.Modi;
      for (const auto &Symbol : ModiStream.Symbols) {
        ModiBuilder.addSymbol(
            Symbol.toCodeViewSymbol(Allocator, CodeViewContainer::Pdb));
      }
    }

    // Each module has its own checksum subsection, so scan for it every time.
    Strings.setChecksums(nullptr);
    CodeViewYAML::initializeStringsAndChecksums(MI.Subsections, Strings);

    auto CodeViewSubsections = ExitOnErr(CodeViewYAML::toCodeViewSubsectionList(
        Allocator, MI.Subsections, Strings));
    for (auto &SS : CodeViewSubsections) {
      ModiBuilder.addDebugSubsection(SS);
    }
  }

  if (Dbi.SectionContribs) {
    // DbiStreamBuilder only supports writing Ver60 section contribs.
    if (Dbi.SectionContribs->Version !=
        PdbRaw_DbiSecContribVer::DbiSecContribVer60)
      ExitOnErr(createStringError(
          "Only DBI section contrib Version Ver60 is supported"));

    for (const auto &Contrib : Dbi.SectionContribs->Items) {
      SectionContrib SC;
      SC.ISect = Contrib.ISect;
      SC.Padding[0] = 0;
      SC.Padding[1] = 0;
      SC.Off = Contrib.Off;
      SC.Size = Contrib.Size;
      SC.Characteristics = Contrib.Characteristics;
      SC.Imod = Contrib.Imod;
      SC.Padding2[0] = 0;
      SC.Padding2[1] = 0;
      SC.DataCrc = Contrib.DataCrc;
      SC.RelocCrc = Contrib.RelocCrc;
      DbiBuilder.addSectionContrib(SC);
    }
  }

  std::vector<object::coff_section> Sections;
  if (!Dbi.SectionHeaders.empty()) {
    for (const auto &Hdr : Dbi.SectionHeaders)
      Sections.emplace_back(Hdr.toCoffSection());

    DbiBuilder.createSectionMap(Sections);
    ExitOnErr(DbiBuilder.addDbgStream(
        pdb::DbgHeaderType::SectionHdr,
        // FIXME: Downcasting to an ArrayRef<uint8_t> should use a helper
        // function in LLVM
        ArrayRef<uint8_t>{(const uint8_t *)Sections.data(),
                          Sections.size() * sizeof(object::coff_section)}));
  }

  auto &TpiBuilder = Builder.getTpiBuilder();
  const auto &Tpi = YamlObj.TpiStream.value_or(DefaultTpiStream);
  TpiBuilder.setVersionHeader(Tpi.Version);
  AppendingTypeTableBuilder TS(Allocator);
  for (const auto &R : Tpi.Records) {
    CVType Type = R.toCodeViewRecord(TS);
    uint32_t Hash = ExitOnErr(llvm::pdb::hashTypeRecord(Type));
    TpiBuilder.addTypeRecord(Type.RecordData, Hash);
  }

  const auto &Ipi = YamlObj.IpiStream.value_or(DefaultIpiStream);
  auto &IpiBuilder = Builder.getIpiBuilder();
  IpiBuilder.setVersionHeader(Ipi.Version);
  for (const auto &R : Ipi.Records) {
    CVType Type = R.toCodeViewRecord(TS);
    uint32_t Hash = ExitOnErr(llvm::pdb::hashTypeRecord(Type));
    IpiBuilder.addTypeRecord(Type.RecordData, Hash);
  }

  if (YamlObj.PublicsStream) {
    auto &GsiBuilder = Builder.getGsiBuilder();
    std::vector<BulkPublic> BulkPublics;
    for (const auto &P : YamlObj.PublicsStream->PubSyms) {
      CVSymbol CV = P.toCodeViewSymbol(Allocator, CodeViewContainer::Pdb);
      auto PS = cantFail(SymbolDeserializer::deserializeAs<PublicSym32>(CV));

      BulkPublic BP;
      BP.Name = PS.Name.data();
      BP.NameLen = PS.Name.size();
      BP.setFlags(PS.Flags);
      BP.Offset = PS.Offset;
      BP.Segment = PS.Segment;
      BulkPublics.emplace_back(BP);
    }
    GsiBuilder.addPublicSymbols(std::move(BulkPublics));
  }

  Builder.getStringTableBuilder().setStrings(*Strings.strings());

  codeview::GUID IgnoredOutGuid;
  ExitOnErr(Builder.commit(opts::yaml2pdb::YamlPdbOutputFile, &IgnoredOutGuid));
}

static PDBFile &loadPDB(StringRef Path, std::unique_ptr<IPDBSession> &Session) {
  ExitOnErr(loadDataForPDB(PDB_ReaderType::Native, Path, Session));

  NativeSession *NS = static_cast<NativeSession *>(Session.get());
  return NS->getPDBFile();
}

static void pdb2Yaml(StringRef Path) {
  std::unique_ptr<IPDBSession> Session;
  auto &File = loadPDB(Path, Session);

  auto O = std::make_unique<YAMLOutputStyle>(File);

  ExitOnErr(O->dump());
}

static void dumpRaw(StringRef Path) {
  InputFile IF = ExitOnErr(InputFile::open(Path));

  auto O = std::make_unique<DumpOutputStyle>(IF);
  ExitOnErr(O->dump());
}

static void dumpBytes(StringRef Path) {
  std::unique_ptr<IPDBSession> Session;
  auto &File = loadPDB(Path, Session);

  auto O = std::make_unique<BytesOutputStyle>(File);

  ExitOnErr(O->dump());
}

bool opts::pretty::shouldDumpSymLevel(SymLevel Search) {
  if (SymTypes.empty())
    return true;
  if (llvm::is_contained(SymTypes, Search))
    return true;
  if (llvm::is_contained(SymTypes, SymLevel::All))
    return true;
  return false;
}

uint32_t llvm::pdb::getTypeLength(const PDBSymbolData &Symbol) {
  auto SymbolType = Symbol.getType();
  const IPDBRawSymbol &RawType = SymbolType->getRawSymbol();

  return RawType.getLength();
}

bool opts::pretty::compareFunctionSymbols(
    const std::unique_ptr<PDBSymbolFunc> &F1,
    const std::unique_ptr<PDBSymbolFunc> &F2) {
  assert(opts::pretty::SymbolOrder != opts::pretty::SymbolSortMode::None);

  if (opts::pretty::SymbolOrder == opts::pretty::SymbolSortMode::Name)
    return F1->getName() < F2->getName();

  // Note that we intentionally sort in descending order on length, since
  // long functions are more interesting than short functions.
  return F1->getLength() > F2->getLength();
}

bool opts::pretty::compareDataSymbols(
    const std::unique_ptr<PDBSymbolData> &F1,
    const std::unique_ptr<PDBSymbolData> &F2) {
  assert(opts::pretty::SymbolOrder != opts::pretty::SymbolSortMode::None);

  if (opts::pretty::SymbolOrder == opts::pretty::SymbolSortMode::Name)
    return F1->getName() < F2->getName();

  // Note that we intentionally sort in descending order on length, since
  // large types are more interesting than short ones.
  return getTypeLength(*F1) > getTypeLength(*F2);
}

static std::string stringOr(std::string Str, std::string IfEmpty) {
  return (Str.empty()) ? IfEmpty : Str;
}

static void dumpInjectedSources(LinePrinter &Printer, IPDBSession &Session) {
  auto Sources = Session.getInjectedSources();
  if (!Sources || !Sources->getChildCount()) {
    Printer.printLine("There are no injected sources.");
    return;
  }

  while (auto IS = Sources->getNext()) {
    Printer.NewLine();
    std::string File = stringOr(IS->getFileName(), "<null>");
    uint64_t Size = IS->getCodeByteSize();
    std::string Obj = stringOr(IS->getObjectFileName(), "<null>");
    std::string VFName = stringOr(IS->getVirtualFileName(), "<null>");
    uint32_t CRC = IS->getCrc32();

    WithColor(Printer, PDB_ColorItem::Path).get() << File;
    Printer << " (";
    WithColor(Printer, PDB_ColorItem::LiteralValue).get() << Size;
    Printer << " bytes): ";
    WithColor(Printer, PDB_ColorItem::Keyword).get() << "obj";
    Printer << "=";
    WithColor(Printer, PDB_ColorItem::Path).get() << Obj;
    Printer << ", ";
    WithColor(Printer, PDB_ColorItem::Keyword).get() << "vname";
    Printer << "=";
    WithColor(Printer, PDB_ColorItem::Path).get() << VFName;
    Printer << ", ";
    WithColor(Printer, PDB_ColorItem::Keyword).get() << "crc";
    Printer << "=";
    WithColor(Printer, PDB_ColorItem::LiteralValue).get() << CRC;
    Printer << ", ";
    WithColor(Printer, PDB_ColorItem::Keyword).get() << "compression";
    Printer << "=";
    dumpPDBSourceCompression(
        WithColor(Printer, PDB_ColorItem::LiteralValue).get(),
        IS->getCompression());

    if (!opts::pretty::ShowInjectedSourceContent)
      continue;

    // Set the indent level to 0 when printing file content.
    int Indent = Printer.getIndentLevel();
    Printer.Unindent(Indent);

    if (IS->getCompression() == PDB_SourceCompression::None)
      Printer.printLine(IS->getCode());
    else
      Printer.formatBinary("Compressed data",
                           arrayRefFromStringRef(IS->getCode()),
                           /*StartOffset=*/0);

    // Re-indent back to the original level.
    Printer.Indent(Indent);
  }
}

template <typename OuterT, typename ChildT>
void diaDumpChildren(PDBSymbol &Outer, PdbSymbolIdField Ids,
                     PdbSymbolIdField Recurse) {
  OuterT *ConcreteOuter = dyn_cast<OuterT>(&Outer);
  if (!ConcreteOuter)
    return;

  auto Children = ConcreteOuter->template findAllChildren<ChildT>();
  while (auto Child = Children->getNext()) {
    outs() << "  {";
    Child->defaultDump(outs(), 4, Ids, Recurse);
    outs() << "\n  }\n";
  }
}

static void dumpDia(StringRef Path) {
  std::unique_ptr<IPDBSession> Session;

  const auto ReaderType =
      opts::diadump::Native ? PDB_ReaderType::Native : PDB_ReaderType::DIA;
  ExitOnErr(loadDataForPDB(ReaderType, Path, Session));

  auto GlobalScope = Session->getGlobalScope();

  std::vector<PDB_SymType> SymTypes;

  if (opts::diadump::Compilands)
    SymTypes.push_back(PDB_SymType::Compiland);
  if (opts::diadump::Enums)
    SymTypes.push_back(PDB_SymType::Enum);
  if (opts::diadump::Pointers)
    SymTypes.push_back(PDB_SymType::PointerType);
  if (opts::diadump::UDTs)
    SymTypes.push_back(PDB_SymType::UDT);
  if (opts::diadump::Funcsigs)
    SymTypes.push_back(PDB_SymType::FunctionSig);
  if (opts::diadump::Arrays)
    SymTypes.push_back(PDB_SymType::ArrayType);
  if (opts::diadump::VTShapes)
    SymTypes.push_back(PDB_SymType::VTableShape);
  if (opts::diadump::Typedefs)
    SymTypes.push_back(PDB_SymType::Typedef);
  PdbSymbolIdField Ids = opts::diadump::NoSymIndexIds ? PdbSymbolIdField::None
                                                      : PdbSymbolIdField::All;

  PdbSymbolIdField Recurse = PdbSymbolIdField::None;
  if (opts::diadump::Recurse)
    Recurse = PdbSymbolIdField::All;
  if (!opts::diadump::ShowClassHierarchy)
    Ids &= ~(PdbSymbolIdField::ClassParent | PdbSymbolIdField::LexicalParent);

  for (PDB_SymType ST : SymTypes) {
    auto Children = GlobalScope->findAllChildren(ST);
    while (auto Child = Children->getNext()) {
      outs() << "{";
      Child->defaultDump(outs(), 2, Ids, Recurse);

      diaDumpChildren<PDBSymbolTypeEnum, PDBSymbolData>(*Child, Ids, Recurse);
      outs() << "\n}\n";
    }
  }
}

static void dumpPretty(StringRef Path) {
  std::unique_ptr<IPDBSession> Session;

  const auto ReaderType =
      opts::pretty::Native ? PDB_ReaderType::Native : PDB_ReaderType::DIA;
  ExitOnErr(loadDataForPDB(ReaderType, Path, Session));

  if (opts::pretty::LoadAddress)
    Session->setLoadAddress(opts::pretty::LoadAddress);

  auto &Stream = outs();
  const bool UseColor =
      opts::pretty::ColorOutput == cl::boolOrDefault::BOU_UNSET
          ? Stream.has_colors()
          : opts::pretty::ColorOutput == cl::boolOrDefault::BOU_TRUE;
  LinePrinter Printer(2, UseColor, Stream, opts::Filters);

  auto GlobalScope(Session->getGlobalScope());
  if (!GlobalScope)
    return;
  std::string FileName(GlobalScope->getSymbolsFileName());

  WithColor(Printer, PDB_ColorItem::None).get() << "Summary for ";
  WithColor(Printer, PDB_ColorItem::Path).get() << FileName;
  Printer.Indent();
  uint64_t FileSize = 0;

  Printer.NewLine();
  WithColor(Printer, PDB_ColorItem::Identifier).get() << "Size";
  if (!sys::fs::file_size(FileName, FileSize)) {
    Printer << ": " << FileSize << " bytes";
  } else {
    Printer << ": (Unable to obtain file size)";
  }

  Printer.NewLine();
  WithColor(Printer, PDB_ColorItem::Identifier).get() << "Guid";
  Printer << ": " << GlobalScope->getGuid();

  Printer.NewLine();
  WithColor(Printer, PDB_ColorItem::Identifier).get() << "Age";
  Printer << ": " << GlobalScope->getAge();

  Printer.NewLine();
  WithColor(Printer, PDB_ColorItem::Identifier).get() << "Attributes";
  Printer << ": ";
  if (GlobalScope->hasCTypes())
    outs() << "HasCTypes ";
  if (GlobalScope->hasPrivateSymbols())
    outs() << "HasPrivateSymbols ";
  Printer.Unindent();

  if (!opts::pretty::WithName.empty()) {
    Printer.NewLine();
    WithColor(Printer, PDB_ColorItem::SectionHeader).get()
        << "---SYMBOLS & TYPES BY NAME---";

    for (StringRef Name : opts::pretty::WithName) {
      auto Symbols = GlobalScope->findChildren(
          PDB_SymType::None, Name, PDB_NameSearchFlags::NS_CaseSensitive);
      if (!Symbols || Symbols->getChildCount() == 0) {
        Printer.formatLine("[not found] - {0}", Name);
        continue;
      }
      Printer.formatLine("[{0} occurrences] - {1}", Symbols->getChildCount(),
                         Name);

      AutoIndent Indent(Printer);
      Printer.NewLine();

      while (auto Symbol = Symbols->getNext()) {
        switch (Symbol->getSymTag()) {
        case PDB_SymType::Typedef: {
          TypedefDumper TD(Printer);
          std::unique_ptr<PDBSymbolTypeTypedef> T =
              llvm::unique_dyn_cast<PDBSymbolTypeTypedef>(std::move(Symbol));
          TD.start(*T);
          break;
        }
        case PDB_SymType::Enum: {
          EnumDumper ED(Printer);
          std::unique_ptr<PDBSymbolTypeEnum> E =
              llvm::unique_dyn_cast<PDBSymbolTypeEnum>(std::move(Symbol));
          ED.start(*E);
          break;
        }
        case PDB_SymType::UDT: {
          ClassDefinitionDumper CD(Printer);
          std::unique_ptr<PDBSymbolTypeUDT> C =
              llvm::unique_dyn_cast<PDBSymbolTypeUDT>(std::move(Symbol));
          CD.start(*C);
          break;
        }
        case PDB_SymType::BaseClass:
        case PDB_SymType::Friend: {
          TypeDumper TD(Printer);
          Symbol->dump(TD);
          break;
        }
        case PDB_SymType::Function: {
          FunctionDumper FD(Printer);
          std::unique_ptr<PDBSymbolFunc> F =
              llvm::unique_dyn_cast<PDBSymbolFunc>(std::move(Symbol));
          FD.start(*F, FunctionDumper::PointerType::None);
          break;
        }
        case PDB_SymType::Data: {
          VariableDumper VD(Printer);
          std::unique_ptr<PDBSymbolData> D =
              llvm::unique_dyn_cast<PDBSymbolData>(std::move(Symbol));
          VD.start(*D);
          break;
        }
        case PDB_SymType::PublicSymbol: {
          ExternalSymbolDumper ED(Printer);
          std::unique_ptr<PDBSymbolPublicSymbol> PS =
              llvm::unique_dyn_cast<PDBSymbolPublicSymbol>(std::move(Symbol));
          ED.dump(*PS);
          break;
        }
        default:
          llvm_unreachable("Unexpected symbol tag!");
        }
      }
    }
    llvm::outs().flush();
  }

  if (opts::pretty::Compilands) {
    Printer.NewLine();
    WithColor(Printer, PDB_ColorItem::SectionHeader).get()
        << "---COMPILANDS---";
    auto Compilands = GlobalScope->findAllChildren<PDBSymbolCompiland>();

    if (Compilands) {
      Printer.Indent();
      CompilandDumper Dumper(Printer);
      CompilandDumpFlags options = CompilandDumper::Flags::None;
      if (opts::pretty::Lines)
        options = options | CompilandDumper::Flags::Lines;
      while (auto Compiland = Compilands->getNext())
        Dumper.start(*Compiland, options);
      Printer.Unindent();
    }
  }

  if (opts::pretty::Classes || opts::pretty::Enums || opts::pretty::Typedefs ||
      opts::pretty::Funcsigs || opts::pretty::Pointers ||
      opts::pretty::Arrays || opts::pretty::VTShapes) {
    Printer.NewLine();
    WithColor(Printer, PDB_ColorItem::SectionHeader).get() << "---TYPES---";
    Printer.Indent();
    TypeDumper Dumper(Printer);
    Dumper.start(*GlobalScope);
    Printer.Unindent();
  }

  if (opts::pretty::Symbols) {
    Printer.NewLine();
    WithColor(Printer, PDB_ColorItem::SectionHeader).get() << "---SYMBOLS---";
    if (auto Compilands = GlobalScope->findAllChildren<PDBSymbolCompiland>()) {
      Printer.Indent();
      CompilandDumper Dumper(Printer);
      while (auto Compiland = Compilands->getNext())
        Dumper.start(*Compiland, true);
      Printer.Unindent();
    }
  }

  if (opts::pretty::Globals) {
    Printer.NewLine();
    WithColor(Printer, PDB_ColorItem::SectionHeader).get() << "---GLOBALS---";
    Printer.Indent();
    if (shouldDumpSymLevel(opts::pretty::SymLevel::Functions)) {
      if (auto Functions = GlobalScope->findAllChildren<PDBSymbolFunc>()) {
        FunctionDumper Dumper(Printer);
        if (opts::pretty::SymbolOrder == opts::pretty::SymbolSortMode::None) {
          while (auto Function = Functions->getNext()) {
            Printer.NewLine();
            Dumper.start(*Function, FunctionDumper::PointerType::None);
          }
        } else {
          std::vector<std::unique_ptr<PDBSymbolFunc>> Funcs;
          while (auto Func = Functions->getNext())
            Funcs.push_back(std::move(Func));
          llvm::sort(Funcs, opts::pretty::compareFunctionSymbols);
          for (const auto &Func : Funcs) {
            Printer.NewLine();
            Dumper.start(*Func, FunctionDumper::PointerType::None);
          }
        }
      }
    }
    if (shouldDumpSymLevel(opts::pretty::SymLevel::Data)) {
      if (auto Vars = GlobalScope->findAllChildren<PDBSymbolData>()) {
        VariableDumper Dumper(Printer);
        if (opts::pretty::SymbolOrder == opts::pretty::SymbolSortMode::None) {
          while (auto Var = Vars->getNext())
            Dumper.start(*Var);
        } else {
          std::vector<std::unique_ptr<PDBSymbolData>> Datas;
          while (auto Var = Vars->getNext())
            Datas.push_back(std::move(Var));
          llvm::sort(Datas, opts::pretty::compareDataSymbols);
          for (const auto &Var : Datas)
            Dumper.start(*Var);
        }
      }
    }
    if (shouldDumpSymLevel(opts::pretty::SymLevel::Thunks)) {
      if (auto Thunks = GlobalScope->findAllChildren<PDBSymbolThunk>()) {
        CompilandDumper Dumper(Printer);
        while (auto Thunk = Thunks->getNext())
          Dumper.dump(*Thunk);
      }
    }
    Printer.Unindent();
  }
  if (opts::pretty::Externals) {
    Printer.NewLine();
    WithColor(Printer, PDB_ColorItem::SectionHeader).get() << "---EXTERNALS---";
    Printer.Indent();
    ExternalSymbolDumper Dumper(Printer);
    Dumper.start(*GlobalScope);
  }
  if (opts::pretty::Lines) {
    Printer.NewLine();
  }
  if (opts::pretty::InjectedSources) {
    Printer.NewLine();
    WithColor(Printer, PDB_ColorItem::SectionHeader).get()
        << "---INJECTED SOURCES---";
    AutoIndent Indent1(Printer);
    dumpInjectedSources(Printer, *Session);
  }

  Printer.NewLine();
  outs().flush();
}

static void mergePdbs() {
  BumpPtrAllocator Allocator;
  MergingTypeTableBuilder MergedTpi(Allocator);
  MergingTypeTableBuilder MergedIpi(Allocator);

  // Create a Tpi and Ipi type table with all types from all input files.
  for (const auto &Path : opts::merge::InputFilenames) {
    std::unique_ptr<IPDBSession> Session;
    auto &File = loadPDB(Path, Session);
    SmallVector<TypeIndex, 128> TypeMap;
    SmallVector<TypeIndex, 128> IdMap;
    if (File.hasPDBTpiStream()) {
      auto &Tpi = ExitOnErr(File.getPDBTpiStream());
      ExitOnErr(
          codeview::mergeTypeRecords(MergedTpi, TypeMap, Tpi.typeArray()));
    }
    if (File.hasPDBIpiStream()) {
      auto &Ipi = ExitOnErr(File.getPDBIpiStream());
      ExitOnErr(codeview::mergeIdRecords(MergedIpi, TypeMap, IdMap,
                                         Ipi.typeArray()));
    }
  }

  // Then write the PDB.
  PDBFileBuilder Builder(Allocator);
  ExitOnErr(Builder.initialize(4096));
  // Add each of the reserved streams.  We might not put any data in them,
  // but at least they have to be present.
  for (uint32_t I = 0; I < kSpecialStreamCount; ++I)
    ExitOnErr(Builder.getMsfBuilder().addStream(0));

  auto &DestTpi = Builder.getTpiBuilder();
  auto &DestIpi = Builder.getIpiBuilder();
  MergedTpi.ForEachRecord([&DestTpi](TypeIndex TI, const CVType &Type) {
    uint32_t Hash = ExitOnErr(llvm::pdb::hashTypeRecord(Type));
    DestTpi.addTypeRecord(Type.RecordData, Hash);
  });
  MergedIpi.ForEachRecord([&DestIpi](TypeIndex TI, const CVType &Type) {
    uint32_t Hash = ExitOnErr(llvm::pdb::hashTypeRecord(Type));
    DestIpi.addTypeRecord(Type.RecordData, Hash);
  });
  Builder.getInfoBuilder().addFeature(PdbRaw_FeatureSig::VC140);

  SmallString<64> OutFile(opts::merge::PdbOutputFile);
  if (OutFile.empty()) {
    OutFile = opts::merge::InputFilenames[0];
    llvm::sys::path::replace_extension(OutFile, "merged.pdb");
  }

  codeview::GUID IgnoredOutGuid;
  ExitOnErr(Builder.commit(OutFile, &IgnoredOutGuid));
}

static void explain() {
  InputFile IF =
      ExitOnErr(InputFile::open(opts::explain::InputFilename.front(), true));

  for (uint64_t Off : opts::explain::Offsets) {
    auto O = std::make_unique<ExplainOutputStyle>(IF, Off);

    ExitOnErr(O->dump());
  }
}

static void exportStream() {
  std::unique_ptr<IPDBSession> Session;
  PDBFile &File = loadPDB(opts::exportstream::InputFilename.front(), Session);

  std::unique_ptr<MappedBlockStream> SourceStream;
  uint32_t Index = 0;
  bool Success = false;
  std::string OutFileName = opts::exportstream::OutputFile;

  if (opts::exportstream::DXContainer) {
    auto Dxc = File.getDXContainerStream();
    if (!Dxc) {
      errs() << "Error: DirectX Container is not present.\n";
      exit(1);
    }
    if (!opts::exportstream::Stream.empty()) {
      outs() << "Note: option --stream was ignored.\n";
    }
    Index = pdb::StreamDXContainer;
    outs() << "Dumping contents of DirectX Container stream (index " << Index
           << ") to file " << OutFileName << ".\n";
  } else {
    if (opts::exportstream::Stream.empty()) {
      errs() << "llvm-pdbutil: either --stream or --dxcontainer must be "
                "specified!\n";
      exit(1);
    }
    if (!opts::exportstream::ForceName) {
      // First try to parse it as an integer, if it fails fall back to treating
      // it as a named stream.
      if (to_integer(opts::exportstream::Stream, Index)) {
        if (Index >= File.getNumStreams()) {
          errs() << "Error: " << Index << " is not a valid stream index.\n";
          exit(1);
        }
        Success = true;
        outs() << "Dumping contents of stream index " << Index << " to file "
               << OutFileName << ".\n";
      }
    }

    if (!Success) {
      InfoStream &IS = cantFail(File.getPDBInfoStream());
      Index = ExitOnErr(IS.getNamedStreamIndex(opts::exportstream::Stream));
      outs() << "Dumping contents of stream '" << opts::exportstream::Stream
             << "' (index " << Index << ") to file " << OutFileName << ".\n";
    }
  }

  SourceStream = File.createIndexedStream(Index);
  auto OutFile = ExitOnErr(
      FileOutputBuffer::create(OutFileName, SourceStream->getLength()));
  FileBufferByteStream DestStream(std::move(OutFile), llvm::endianness::little);
  BinaryStreamWriter Writer(DestStream);
  ExitOnErr(Writer.writeStreamRef(*SourceStream));
  ExitOnErr(DestStream.commit());
}

static bool parseRange(StringRef Str,
                       std::optional<opts::bytes::NumberRange> &Parsed) {
  if (Str.empty())
    return true;

  llvm::Regex R("^([^-]+)(-([^-]+))?$");
  llvm::SmallVector<llvm::StringRef, 2> Matches;
  if (!R.match(Str, &Matches))
    return false;

  Parsed.emplace();
  if (!to_integer(Matches[1], Parsed->Min))
    return false;

  if (!Matches[3].empty()) {
    Parsed->Max.emplace();
    if (!to_integer(Matches[3], *Parsed->Max))
      return false;
  }
  return true;
}

static void simplifyChunkList(std::vector<opts::ModuleSubsection> &Chunks) {
  if (!llvm::is_contained(Chunks, opts::ModuleSubsection::All))
    return;
  Chunks.clear();
  Chunks.push_back(opts::ModuleSubsection::All);
}

static opts::pretty::SymLevel parseSymLevel(StringRef S) {
  return StringSwitch<opts::pretty::SymLevel>(S)
      .Case("thunks", opts::pretty::SymLevel::Thunks)
      .Case("data", opts::pretty::SymLevel::Data)
      .Case("funcs", opts::pretty::SymLevel::Functions)
      .Case("all", opts::pretty::SymLevel::All)
      .Default(opts::pretty::SymLevel::All);
}

static opts::ModuleSubsection parseModuleSubsection(StringRef S) {
  return StringSwitch<opts::ModuleSubsection>(S)
      .Case("lines", opts::ModuleSubsection::Lines)
      .Case("fc", opts::ModuleSubsection::FileChecksums)
      .Case("il", opts::ModuleSubsection::InlineeLines)
      .Case("xmi", opts::ModuleSubsection::CrossScopeImports)
      .Case("xme", opts::ModuleSubsection::CrossScopeExports)
      .Case("strings", opts::ModuleSubsection::StringTable)
      .Case("syms", opts::ModuleSubsection::Symbols)
      .Case("framedata", opts::ModuleSubsection::FrameData)
      .Case("coff-symrva", opts::ModuleSubsection::CoffSymbolRVAs)
      .Case("all", opts::ModuleSubsection::All)
      .Default(opts::ModuleSubsection::Unknown);
}

int main(int Argc, const char **Argv) {
  InitLLVM X(Argc, Argv);
  ExitOnErr.setBanner("llvm-pdbutil: ");

  clv2::OptionParser P;
  P.add<&PDBUtilToolReg>();
  RegisterAllLLVMOptions(P);
  P.hideUnrelatedOptions({&BY_MsfBytesCat, &BY_DbiBytesCat, &BY_PdbBytesCat,
                          &BY_TypesCat, &BY_ModuleCat});
  auto OptsCtx = P.parse(Argc, Argv, "LLVM PDB Dumper\n");
  auto *Opts = OptsCtx->getViewPtr<&PDBUtilToolReg>();

  // --- Extract DiaDump subcommand options ---
  if (Opts->isActive<&DiaDumpCmd>()) {
    auto DD = Opts->getSubOptions<&DiaDumpCmd>();
    opts::diadump::InputFilenames = DD.get<&DD_InputFilenamesOpt>();
    opts::diadump::Native = DD.get<&DD_NativeOpt>();
    opts::diadump::ShowClassHierarchy = DD.get<&DD_HierarchyOpt>();
    opts::diadump::NoSymIndexIds = DD.get<&DD_NoIdsOpt>();
    opts::diadump::Recurse = DD.get<&DD_RecurseOpt>();
    opts::diadump::Enums = DD.get<&DD_EnumsOpt>();
    opts::diadump::Pointers = DD.get<&DD_PointersOpt>();
    opts::diadump::UDTs = DD.get<&DD_UDTsOpt>();
    opts::diadump::Compilands = DD.get<&DD_CompilandsOpt>();
    opts::diadump::Funcsigs = DD.get<&DD_FuncsigsOpt>();
    opts::diadump::Arrays = DD.get<&DD_ArraysOpt>();
    opts::diadump::VTShapes = DD.get<&DD_VTShapesOpt>();
    opts::diadump::Typedefs = DD.get<&DD_TypedefsOpt>();
  }

  // --- Extract Pretty subcommand options ---
  if (Opts->isActive<&PrettyCmd>()) {
    auto PR = Opts->getSubOptions<&PrettyCmd>();
    opts::pretty::InputFilenames = PR.get<&PR_InputFilenamesOpt>();
    opts::pretty::InjectedSources = PR.get<&PR_InjectedSourcesOpt>();
    opts::pretty::ShowInjectedSourceContent =
        PR.get<&PR_ShowInjectedSourceContentOpt>();
    opts::pretty::WithName = PR.get<&PR_WithNameOpt>();
    opts::pretty::Compilands = PR.get<&PR_CompilandsOpt>();
    opts::pretty::Symbols = PR.get<&PR_SymbolsOpt>();
    opts::pretty::Globals = PR.get<&PR_GlobalsOpt>();
    opts::pretty::Externals = PR.get<&PR_ExternalsOpt>();
    for (auto &S : PR.get<&PR_SymTypesOpt>())
      opts::pretty::SymTypes.push_back(parseSymLevel(S));
    opts::pretty::Types = PR.get<&PR_TypesOpt>();
    opts::pretty::Classes = PR.get<&PR_ClassesOpt>();
    opts::pretty::Enums = PR.get<&PR_EnumsOpt>();
    opts::pretty::Typedefs = PR.get<&PR_TypedefsOpt>();
    opts::pretty::Funcsigs = PR.get<&PR_FuncsigsOpt>();
    opts::pretty::Pointers = PR.get<&PR_PointersOpt>();
    opts::pretty::Arrays = PR.get<&PR_ArraysOpt>();
    opts::pretty::VTShapes = PR.get<&PR_VTShapesOpt>();
    opts::pretty::SymbolOrder = PR.get<&PR_SymbolOrderOpt>();
    opts::pretty::ClassOrder = PR.get<&PR_ClassOrderOpt>();
    opts::pretty::ClassFormat = PR.get<&PR_ClassFormatOpt>();
    opts::pretty::ClassRecursionDepth = PR.get<&PR_ClassRecursionDepthOpt>();
    opts::pretty::Lines = PR.get<&PR_LinesOpt>();
    opts::pretty::All = PR.get<&PR_AllOpt>();
    opts::pretty::LoadAddress = PR.get<&PR_LoadAddressOpt>();
    opts::pretty::Native = PR.get<&PR_NativeOpt>();
    {
      auto ColorStr = PR.get<&PR_ColorOutputOpt>();
      if (PR.occurrences<&PR_ColorOutputOpt>() == 0)
        opts::pretty::ColorOutput = cl::boolOrDefault::BOU_UNSET;
      else if (ColorStr.empty() || ColorStr == "true")
        opts::pretty::ColorOutput = cl::boolOrDefault::BOU_TRUE;
      else
        opts::pretty::ColorOutput = cl::boolOrDefault::BOU_FALSE;
    }
    opts::pretty::ExcludeTypes = PR.get<&PR_ExcludeTypesOpt>();
    opts::pretty::ExcludeSymbols = PR.get<&PR_ExcludeSymbolsOpt>();
    opts::pretty::ExcludeCompilands = PR.get<&PR_ExcludeCompilandsOpt>();
    opts::pretty::IncludeTypes = PR.get<&PR_IncludeTypesOpt>();
    opts::pretty::IncludeSymbols = PR.get<&PR_IncludeSymbolsOpt>();
    opts::pretty::IncludeCompilands = PR.get<&PR_IncludeCompilandsOpt>();
    opts::pretty::SizeThreshold = PR.get<&PR_SizeThresholdOpt>();
    opts::pretty::PaddingThreshold = PR.get<&PR_PaddingThresholdOpt>();
    opts::pretty::ImmediatePaddingThreshold =
        PR.get<&PR_ImmediatePaddingThresholdOpt>();
    opts::pretty::ExcludeCompilerGenerated =
        PR.get<&PR_ExcludeCompilerGeneratedOpt>();
    opts::pretty::ExcludeSystemLibraries =
        PR.get<&PR_ExcludeSystemLibrariesOpt>();
    opts::pretty::NoEnumDefs = PR.get<&PR_NoEnumDefsOpt>();
  }

  // --- Extract Bytes subcommand options ---
  if (Opts->isActive<&BytesCmd>()) {
    auto BY = Opts->getSubOptions<&BytesCmd>();
    opts::bytes::InputFilenames = BY.get<&BY_InputFilenamesOpt>();
    opts::bytes::DumpBlockRangeOpt = BY.get<&BY_DumpBlockRangeOpt>();
    opts::bytes::DumpByteRangeOpt = BY.get<&BY_DumpByteRangeOpt>();
    opts::bytes::DumpStreamData = BY.get<&BY_DumpStreamDataOpt>();
    opts::bytes::NameMap = BY.get<&BY_NameMapOpt>();
    opts::bytes::Fpm = BY.get<&BY_FpmOpt>();
    opts::bytes::SectionContributions = BY.get<&BY_SectionContributionsOpt>();
    opts::bytes::SectionMap = BY.get<&BY_SectionMapOpt>();
    opts::bytes::ModuleInfos = BY.get<&BY_ModuleInfosOpt>();
    opts::bytes::FileInfo = BY.get<&BY_FileInfoOpt>();
    opts::bytes::TypeServerMap = BY.get<&BY_TypeServerMapOpt>();
    opts::bytes::ECData = BY.get<&BY_ECDataOpt>();
    opts::bytes::TypeIndex = BY.get<&BY_TypeIndexOpt>();
    opts::bytes::IdIndex = BY.get<&BY_IdIndexOpt>();
    if (BY.occurrences<&BY_ModuleIndexOpt>() > 0)
      opts::bytes::ModuleIndex = BY.get<&BY_ModuleIndexOpt>();
    opts::bytes::ModuleSyms = BY.get<&BY_ModuleSymsOpt>();
    opts::bytes::ModuleC11 = BY.get<&BY_ModuleC11Opt>();
    opts::bytes::ModuleC13 = BY.get<&BY_ModuleC13Opt>();
    opts::bytes::SplitChunks = BY.get<&BY_SplitChunksOpt>();

    if (!parseRange(opts::bytes::DumpBlockRangeOpt,
                    opts::bytes::DumpBlockRange)) {
      errs() << "Argument '" << opts::bytes::DumpBlockRangeOpt
             << "' invalid format.\n";
      errs().flush();
      exit(1);
    }
    if (!parseRange(opts::bytes::DumpByteRangeOpt,
                    opts::bytes::DumpByteRange)) {
      errs() << "Argument '" << opts::bytes::DumpByteRangeOpt
             << "' invalid format.\n";
      errs().flush();
      exit(1);
    }
  }

  // --- Extract Dump subcommand options ---
  if (Opts->isActive<&DumpCmd>()) {
    auto DU = Opts->getSubOptions<&DumpCmd>();
    opts::dump::InputFilenames = DU.get<&DU_InputFilenamesOpt>();
    opts::dump::DumpSummary = DU.get<&DU_SummaryOpt>();
    opts::dump::DumpStreams = DU.get<&DU_StreamsOpt>();
    opts::dump::DumpStreamBlocks = DU.get<&DU_StreamBlocksOpt>();
    opts::dump::DumpSymbolStats = DU.get<&DU_SymbolStatsOpt>();
    opts::dump::DumpTypeStats = DU.get<&DU_TypeStatsOpt>();
    opts::dump::DumpIDStats = DU.get<&DU_IDStatsOpt>();
    opts::dump::DumpUdtStats = DU.get<&DU_UdtStatsOpt>();
    opts::dump::DumpTypes = DU.get<&DU_TypesOpt>();
    opts::dump::DumpTypeData = DU.get<&DU_TypeDataOpt>();
    opts::dump::DumpTypeRefStats = DU.get<&DU_TypeRefStatsOpt>();
    opts::dump::DumpTypeExtras = DU.get<&DU_TypeExtrasOpt>();
    opts::dump::DontResolveForwardRefs =
        DU.get<&DU_DontResolveForwardRefsOpt>();
    opts::dump::DumpTypeIndex = DU.get<&DU_TypeIndexOpt>();
    opts::dump::DumpIds = DU.get<&DU_IdsOpt>();
    opts::dump::DumpIdData = DU.get<&DU_IdDataOpt>();
    opts::dump::DumpIdExtras = DU.get<&DU_IdExtrasOpt>();
    opts::dump::DumpIdIndex = DU.get<&DU_IdIndexOpt>();
    opts::dump::DumpTypeDependents = DU.get<&DU_TypeDependentsOpt>();
    opts::dump::DumpGlobals = DU.get<&DU_GlobalsOpt>();
    opts::dump::DumpGlobalExtras = DU.get<&DU_GlobalExtrasOpt>();
    opts::dump::DumpGlobalNames = DU.get<&DU_GlobalNamesOpt>();
    opts::dump::DumpPublics = DU.get<&DU_PublicsOpt>();
    opts::dump::DumpPublicExtras = DU.get<&DU_PublicExtrasOpt>();
    opts::dump::DumpGSIRecords = DU.get<&DU_GSIRecordsOpt>();
    opts::dump::DumpSymbols = DU.get<&DU_SymbolsOpt>();
    opts::dump::DumpSymRecordBytes = DU.get<&DU_SymRecordBytesOpt>();
    opts::dump::DumpFpo = DU.get<&DU_FpoOpt>();
    opts::dump::DumpSymbolOffset = DU.get<&DU_SymbolOffsetOpt>();
    opts::dump::DumpParents = DU.get<&DU_ParentsOpt>();
    opts::dump::DumpParentDepth = DU.get<&DU_ParentDepthOpt>();
    opts::dump::DumpChildren = DU.get<&DU_ChildrenOpt>();
    opts::dump::DumpChildrenDepth = DU.get<&DU_ChildrenDepthOpt>();
    opts::dump::DumpModules = DU.get<&DU_ModulesOpt>();
    opts::dump::DumpModuleFiles = DU.get<&DU_ModuleFilesOpt>();
    opts::dump::DumpLines = DU.get<&DU_LinesOpt>();
    opts::dump::DumpInlineeLines = DU.get<&DU_InlineeLinesOpt>();
    opts::dump::DumpXmi = DU.get<&DU_XmiOpt>();
    opts::dump::DumpXme = DU.get<&DU_XmeOpt>();
    if (DU.occurrences<&DU_ModiOpt>() > 1) {
      errs() << "argument '-modi' specified more than once.\n";
      return 1;
    }
    if (DU.occurrences<&DU_ModiOpt>() > 0)
      opts::dump::DumpModi = DU.get<&DU_ModiOpt>();
    opts::dump::JustMyCode = DU.get<&DU_JustMyCodeOpt>();
    opts::dump::DumpNamedStreams = DU.get<&DU_NamedStreamsOpt>();
    opts::dump::DumpStringTable = DU.get<&DU_StringTableOpt>();
    opts::dump::DumpStringTableDetails = DU.get<&DU_StringTableDetailsOpt>();
    opts::dump::DumpSectionContribs = DU.get<&DU_SectionContribsOpt>();
    opts::dump::DumpSectionMap = DU.get<&DU_SectionMapOpt>();
    opts::dump::DumpSectionHeaders = DU.get<&DU_SectionHeadersOpt>();
    opts::dump::DumpDXContainer = DU.get<&DU_DXContainerOpt>();
    opts::dump::RawAll = DU.get<&DU_RawAllOpt>();

    if (opts::dump::RawAll) {
      opts::dump::DumpDXContainer = true;
      opts::dump::DumpGlobals = true;
      opts::dump::DumpFpo = true;
      opts::dump::DumpInlineeLines = true;
      opts::dump::DumpIds = true;
      opts::dump::DumpIdExtras = true;
      opts::dump::DumpLines = true;
      opts::dump::DumpModules = true;
      opts::dump::DumpModuleFiles = true;
      opts::dump::DumpPublics = true;
      opts::dump::DumpSectionContribs = true;
      opts::dump::DumpSectionHeaders = true;
      opts::dump::DumpSectionMap = true;
      opts::dump::DumpStreams = true;
      opts::dump::DumpStreamBlocks = true;
      opts::dump::DumpStringTable = true;
      opts::dump::DumpStringTableDetails = true;
      opts::dump::DumpSummary = true;
      opts::dump::DumpSymbols = true;
      opts::dump::DumpSymbolStats = true;
      opts::dump::DumpTypes = true;
      opts::dump::DumpTypeExtras = true;
      opts::dump::DumpUdtStats = true;
      opts::dump::DumpXme = true;
      opts::dump::DumpXmi = true;
    }
  }

  // --- Extract YamlToPdb subcommand options ---
  if (Opts->isActive<&YamlToPdbCmd>()) {
    auto Y2P = Opts->getSubOptions<&YamlToPdbCmd>();
    opts::yaml2pdb::YamlPdbOutputFile = Y2P.get<&Y2P_OutputFileOpt>();
    opts::yaml2pdb::InputFilename = Y2P.get<&Y2P_InputFilenameOpt>();
    opts::yaml2pdb::DocNum = Y2P.get<&Y2P_DocNumOpt>();
  }

  // --- Extract PdbToYaml subcommand options ---
  if (Opts->isActive<&PdbToYamlCmd>()) {
    auto P2Y = Opts->getSubOptions<&PdbToYamlCmd>();
    opts::pdb2yaml::InputFilename = P2Y.get<&P2Y_InputFilenameOpt>();
    opts::pdb2yaml::All = P2Y.get<&P2Y_AllOpt>();
    opts::pdb2yaml::NoFileHeaders = P2Y.get<&P2Y_NoFileHeadersOpt>();
    opts::pdb2yaml::Minimal = P2Y.get<&P2Y_MinimalOpt>();
    opts::pdb2yaml::StreamMetadata = P2Y.get<&P2Y_StreamMetadataOpt>();
    opts::pdb2yaml::StreamDirectory = P2Y.get<&P2Y_StreamDirectoryOpt>();
    opts::pdb2yaml::PdbStream = P2Y.get<&P2Y_PdbStreamOpt>();
    opts::pdb2yaml::StringTable = P2Y.get<&P2Y_StringTableOpt>();
    opts::pdb2yaml::DbiStream = P2Y.get<&P2Y_DbiStreamOpt>();
    opts::pdb2yaml::TpiStream = P2Y.get<&P2Y_TpiStreamOpt>();
    opts::pdb2yaml::IpiStream = P2Y.get<&P2Y_IpiStreamOpt>();
    opts::pdb2yaml::PublicsStream = P2Y.get<&P2Y_PublicsStreamOpt>();
    opts::pdb2yaml::DumpModules = P2Y.get<&P2Y_DumpModulesOpt>();
    opts::pdb2yaml::DumpModuleFiles = P2Y.get<&P2Y_DumpModuleFilesOpt>();
    for (auto &S : P2Y.get<&P2Y_DumpModuleSubsectionsOpt>())
      opts::pdb2yaml::DumpModuleSubsections.push_back(parseModuleSubsection(S));
    opts::pdb2yaml::DumpModuleSyms = P2Y.get<&P2Y_DumpModuleSymsOpt>();
    opts::pdb2yaml::DumpSectionHeaders = P2Y.get<&P2Y_DumpSectionHeadersOpt>();
    opts::pdb2yaml::DumpSectionContribs =
        P2Y.get<&P2Y_DumpSectionContribsOpt>();
    opts::pdb2yaml::DXContainerStream = P2Y.get<&P2Y_DXContainerStreamOpt>();

    if (opts::pdb2yaml::All) {
      opts::pdb2yaml::StreamMetadata = true;
      opts::pdb2yaml::StreamDirectory = true;
      opts::pdb2yaml::PdbStream = true;
      opts::pdb2yaml::StringTable = true;
      opts::pdb2yaml::DbiStream = true;
      opts::pdb2yaml::TpiStream = true;
      opts::pdb2yaml::IpiStream = true;
      opts::pdb2yaml::DXContainerStream = true;
      opts::pdb2yaml::PublicsStream = true;
      opts::pdb2yaml::DumpModules = true;
      opts::pdb2yaml::DumpModuleFiles = true;
      opts::pdb2yaml::DumpModuleSyms = true;
      opts::pdb2yaml::DumpSectionHeaders = true;
      opts::pdb2yaml::DumpSectionContribs = true;
      opts::pdb2yaml::DumpModuleSubsections.push_back(
          opts::ModuleSubsection::All);
    }
    simplifyChunkList(opts::pdb2yaml::DumpModuleSubsections);

    if (opts::pdb2yaml::DumpModuleSyms || opts::pdb2yaml::DumpModuleFiles)
      opts::pdb2yaml::DumpModules = true;

    if (opts::pdb2yaml::DumpModules || opts::pdb2yaml::DumpSectionHeaders ||
        opts::pdb2yaml::DumpSectionContribs)
      opts::pdb2yaml::DbiStream = true;
  }

  // --- Extract Merge subcommand options ---
  if (Opts->isActive<&MergeCmd>()) {
    auto MG = Opts->getSubOptions<&MergeCmd>();
    opts::merge::InputFilenames = MG.get<&MG_InputFilenamesOpt>();
    opts::merge::PdbOutputFile = MG.get<&MG_PdbOutputFileOpt>();
  }

  // --- Extract Explain subcommand options ---
  if (Opts->isActive<&ExplainCmd>()) {
    auto EX = Opts->getSubOptions<&ExplainCmd>();
    opts::explain::InputFilename = EX.get<&EX_InputFilenameOpt>();
    opts::explain::Offsets = EX.get<&EX_OffsetsOpt>();
    opts::explain::InputType = EX.get<&EX_InputTypeOpt>();
  }

  // --- Extract Export subcommand options ---
  if (Opts->isActive<&ExportCmd>()) {
    auto ES = Opts->getSubOptions<&ExportCmd>();
    opts::exportstream::InputFilename = ES.get<&ES_InputFilenameOpt>();
    opts::exportstream::OutputFile = ES.get<&ES_OutputFileOpt>();
    opts::exportstream::Stream = ES.get<&ES_StreamOpt>();
    opts::exportstream::ForceName = ES.get<&ES_ForceNameOpt>();
    opts::exportstream::DXContainer = ES.get<&ES_DXContainerOpt>();
  }

  llvm::sys::InitializeCOMRAII COM(llvm::sys::COMThreadingMode::MultiThreaded);

  // Initialize the filters for LinePrinter.
  auto propagate = [&](auto &Target, auto &Reference) {
    llvm::append_range(Target, Reference);
  };

  propagate(opts::Filters.ExcludeTypes, opts::pretty::ExcludeTypes);
  propagate(opts::Filters.ExcludeSymbols, opts::pretty::ExcludeSymbols);
  propagate(opts::Filters.ExcludeCompilands, opts::pretty::ExcludeCompilands);
  propagate(opts::Filters.IncludeTypes, opts::pretty::IncludeTypes);
  propagate(opts::Filters.IncludeSymbols, opts::pretty::IncludeSymbols);
  propagate(opts::Filters.IncludeCompilands, opts::pretty::IncludeCompilands);
  opts::Filters.PaddingThreshold = opts::pretty::PaddingThreshold;
  opts::Filters.SizeThreshold = opts::pretty::SizeThreshold;
  opts::Filters.JustMyCode = opts::dump::JustMyCode;
  if (opts::dump::DumpModi.has_value()) {
    opts::Filters.DumpModi = *opts::dump::DumpModi;
  }
  if (opts::dump::DumpSymbolOffset) {
    if (!opts::dump::DumpModi.has_value()) {
      errs()
          << "need to specify argument '-modi' when using '-symbol-offset'.\n";
      errs().flush();
      exit(1);
    }
    opts::Filters.SymbolOffset = opts::dump::DumpSymbolOffset;
    if (opts::dump::DumpParents)
      opts::Filters.ParentRecurseDepth = opts::dump::DumpParentDepth;
    if (opts::dump::DumpChildren)
      opts::Filters.ChildrenRecurseDepth = opts::dump::DumpChildrenDepth;
  }

  if (Opts->isActive<&PdbToYamlCmd>()) {
    pdb2Yaml(opts::pdb2yaml::InputFilename.front());
  } else if (Opts->isActive<&YamlToPdbCmd>()) {
    if (opts::yaml2pdb::YamlPdbOutputFile.empty()) {
      SmallString<16> OutputFilename(opts::yaml2pdb::InputFilename);
      sys::path::replace_extension(OutputFilename, ".pdb");
      opts::yaml2pdb::YamlPdbOutputFile = std::string(OutputFilename);
    }
    yamlToPdb(opts::yaml2pdb::InputFilename, opts::yaml2pdb::DocNum);
  } else if (Opts->isActive<&DiaDumpCmd>()) {
    llvm::for_each(opts::diadump::InputFilenames, dumpDia);
  } else if (Opts->isActive<&PrettyCmd>()) {
    if (opts::pretty::Lines)
      opts::pretty::Compilands = true;

    if (opts::pretty::All) {
      opts::pretty::Compilands = true;
      opts::pretty::Symbols = true;
      opts::pretty::Globals = true;
      opts::pretty::Types = true;
      opts::pretty::Externals = true;
      opts::pretty::Lines = true;
    }

    if (opts::pretty::Types) {
      opts::pretty::Classes = true;
      opts::pretty::Typedefs = true;
      opts::pretty::Enums = true;
      opts::pretty::Pointers = true;
      opts::pretty::Funcsigs = true;
    }

    // When adding filters for excluded compilands and types, we need to
    // remember that these are regexes.  So special characters such as * and \
    // need to be escaped in the regex.  In the case of a literal \, this means
    // it needs to be escaped again in the C++.  So matching a single \ in the
    // input requires 4 \es in the C++.
    if (opts::pretty::ExcludeCompilerGenerated) {
      opts::Filters.ExcludeTypes.push_back("__vc_attributes");
      opts::Filters.ExcludeCompilands.push_back("\\* Linker \\*");
    }
    if (opts::pretty::ExcludeSystemLibraries) {
      opts::Filters.ExcludeCompilands.push_back(
          "f:\\\\binaries\\\\Intermediate\\\\vctools\\\\crt_bld");
      opts::Filters.ExcludeCompilands.push_back("f:\\\\dd\\\\vctools\\\\crt");
      opts::Filters.ExcludeCompilands.push_back(
          "d:\\\\th.obj.x86fre\\\\minkernel");
    }
    llvm::for_each(opts::pretty::InputFilenames, dumpPretty);
  } else if (Opts->isActive<&DumpCmd>()) {
    llvm::for_each(opts::dump::InputFilenames, dumpRaw);
  } else if (Opts->isActive<&BytesCmd>()) {
    llvm::for_each(opts::bytes::InputFilenames, dumpBytes);
  } else if (Opts->isActive<&MergeCmd>()) {
    if (opts::merge::InputFilenames.size() < 2) {
      errs() << "merge subcommand requires at least 2 input files.\n";
      exit(1);
    }
    mergePdbs();
  } else if (Opts->isActive<&ExplainCmd>()) {
    explain();
  } else if (Opts->isActive<&ExportCmd>()) {
    exportStream();
  }

  outs().flush();
  return 0;
}
