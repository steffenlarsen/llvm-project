//===- llvm-pdbutil.h ----------------------------------------- *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVMPDBDUMP_LLVMPDBDUMP_H
#define LLVM_TOOLS_LLVMPDBDUMP_LLVMPDBDUMP_H

#include "llvm/ADT/PointerUnion.h"
#include "llvm/DebugInfo/PDB/Native/LinePrinter.h"
#include "llvm/Support/BoolOrDefault.h"
#include "llvm/Support/raw_ostream.h"

#include <memory>
#include <optional>
#include <stdint.h>

namespace llvm {
namespace object {
class COFFObjectFile;
}
namespace pdb {
class PDBSymbolData;
class PDBSymbolFunc;
class PDBFile;
uint32_t getTypeLength(const PDBSymbolData &Symbol);
}
typedef llvm::PointerUnion<object::COFFObjectFile *, pdb::PDBFile *>
    PdbOrCoffObj;
}

namespace opts {

enum class DumpLevel { None, Basic, Verbose };

enum class ModuleSubsection {
  Unknown,
  Lines,
  FileChecksums,
  InlineeLines,
  CrossScopeImports,
  CrossScopeExports,
  StringTable,
  Symbols,
  FrameData,
  CoffSymbolRVAs,
  All
};

extern FilterOptions Filters;

namespace pretty {

enum class ClassDefinitionFormat { None, Layout, All };
enum class ClassSortMode {
  None,
  Name,
  Size,
  Padding,
  PaddingPct,
  PaddingImmediate,
  PaddingPctImmediate
};

enum class SymbolSortMode { None, Name, Size };

enum class SymLevel { Functions, Data, Thunks, All };

bool shouldDumpSymLevel(SymLevel Level);
bool compareFunctionSymbols(
    const std::unique_ptr<llvm::pdb::PDBSymbolFunc> &F1,
    const std::unique_ptr<llvm::pdb::PDBSymbolFunc> &F2);
bool compareDataSymbols(const std::unique_ptr<llvm::pdb::PDBSymbolData> &F1,
                        const std::unique_ptr<llvm::pdb::PDBSymbolData> &F2);

extern std::vector<std::string> WithName;

extern bool InjectedSources;
extern bool ShowInjectedSourceContent;
extern bool Compilands;
extern bool Symbols;
extern bool Globals;
extern bool Externals;
extern bool Classes;
extern bool Enums;
extern bool Funcsigs;
extern bool Arrays;
extern bool Typedefs;
extern bool Pointers;
extern bool VTShapes;
extern bool All;
extern bool Lines;
extern bool ExcludeCompilerGenerated;
extern bool ExcludeSystemLibraries;
extern bool Native;
extern uint64_t LoadAddress;
extern llvm::cl::boolOrDefault ColorOutput;

extern bool NoEnumDefs;
extern std::vector<std::string> ExcludeTypes;
extern std::vector<std::string> ExcludeSymbols;
extern std::vector<std::string> ExcludeCompilands;
extern std::vector<std::string> IncludeTypes;
extern std::vector<std::string> IncludeSymbols;
extern std::vector<std::string> IncludeCompilands;
extern SymbolSortMode SymbolOrder;
extern ClassSortMode ClassOrder;
extern uint32_t SizeThreshold;
extern uint32_t PaddingThreshold;
extern uint32_t ImmediatePaddingThreshold;
extern ClassDefinitionFormat ClassFormat;
extern uint32_t ClassRecursionDepth;
}

namespace bytes {
struct NumberRange {
  uint64_t Min;
  std::optional<uint64_t> Max;
};

extern std::optional<NumberRange> DumpBlockRange;
extern std::optional<NumberRange> DumpByteRange;
extern std::vector<std::string> DumpStreamData;
extern bool NameMap;
extern bool Fpm;

extern bool SectionContributions;
extern bool SectionMap;
extern bool ModuleInfos;
extern bool FileInfo;
extern bool TypeServerMap;
extern bool ECData;

extern std::vector<uint32_t> TypeIndex;
extern std::vector<uint32_t> IdIndex;

extern std::optional<uint32_t> ModuleIndex;
extern bool ModuleSyms;
extern bool ModuleC11;
extern bool ModuleC13;
extern bool SplitChunks;
} // namespace bytes

namespace dump {

extern bool DumpSummary;
extern bool DumpFpm;
extern bool DumpStreams;
extern bool DumpSymbolStats;
extern bool DumpTypeStats;
extern bool DumpIDStats;
extern bool DumpUdtStats;
extern bool DumpStreamBlocks;

extern bool DumpLines;
extern bool DumpInlineeLines;
extern bool DumpXmi;
extern bool DumpXme;
extern bool DumpNamedStreams;
extern bool DumpStringTable;
extern bool DumpStringTableDetails;
extern bool DumpTypes;
extern bool DumpTypeData;
extern bool DumpTypeExtras;
extern std::vector<uint32_t> DumpTypeIndex;
extern bool DumpTypeDependents;
extern bool DumpTypeRefStats;
extern bool DumpSectionHeaders;

extern bool DumpIds;
extern bool DumpIdData;
extern bool DumpIdExtras;
extern std::vector<uint32_t> DumpIdIndex;
extern std::optional<uint32_t> DumpModi;
extern bool JustMyCode;
extern bool DontResolveForwardRefs;
extern bool DumpSymbols;
extern bool DumpSymRecordBytes;
extern bool DumpGSIRecords;
extern bool DumpGlobals;
extern std::vector<std::string> DumpGlobalNames;
extern bool DumpGlobalExtras;
extern bool DumpPublics;
extern bool DumpPublicExtras;
extern bool DumpSectionContribs;
extern bool DumpSectionMap;
extern bool DumpModules;
extern bool DumpModuleFiles;
extern bool DumpDXContainer;
extern bool DumpFpo;
extern bool RawAll;
extern uint32_t DumpSymbolOffset;
extern bool DumpParents;
extern uint32_t DumpParentDepth;
extern bool DumpChildren;
extern uint32_t DumpChildrenDepth;
}

namespace pdb2yaml {
extern bool All;
extern bool NoFileHeaders;
extern bool Minimal;
extern bool StreamMetadata;
extern bool StreamDirectory;
extern bool StringTable;
extern bool PdbStream;
extern bool DbiStream;
extern bool TpiStream;
extern bool IpiStream;
extern bool PublicsStream;
extern std::vector<std::string> InputFilename;
extern bool DumpModules;
extern bool DumpModuleFiles;
extern std::vector<ModuleSubsection> DumpModuleSubsections;
extern bool DumpModuleSyms;
extern bool DumpSectionContribs;
extern bool DumpSectionHeaders;
extern bool DXContainerStream;
} // namespace pdb2yaml

namespace explain {
enum class InputFileType { PDBFile, PDBStream, DBIStream, Names, ModuleStream };

extern std::vector<std::string> InputFilename;
extern std::vector<uint64_t> Offsets;
extern InputFileType InputType;
} // namespace explain

namespace exportstream {
extern std::string OutputFile;
extern std::string Stream;
extern bool ForceName;
} // namespace exportstream
}

#endif
