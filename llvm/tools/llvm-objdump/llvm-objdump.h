//===--- llvm-objdump.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TOOLS_LLVM_OBJDUMP_LLVM_OBJDUMP_H
#define LLVM_TOOLS_LLVM_OBJDUMP_LLVM_OBJDUMP_H

#include "llvm/ADT/StringSet.h"
#include "llvm/DebugInfo/DIContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Object/Archive.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/FormattedStream.h"
#include <functional>
#include <memory>

namespace llvm {
class StringRef;
class Twine;

namespace opt {
class Arg;
} // namespace opt

namespace object {
class RelocationRef;
struct VersionEntry;

class COFFObjectFile;
class ELFObjectFileBase;
class MachOObjectFile;
class WasmObjectFile;
class XCOFFObjectFile;
class DXContainer;
} // namespace object

namespace objdump {

enum DebugFormat { DFASCII, DFDisabled, DFInvalid, DFLimitsOnly, DFUnicode };

enum class ColorOutput {
  Auto,
  Enable,
  Disable,
  Invalid,
};

struct ObjdumpOptions {
  bool ArchiveHeaders = false;
  int DbgIndent = 52;
  DebugFormat DbgVariables = DFDisabled;
  DebugFormat DbgInlinedFunctions = DFDisabled;
  bool Demangle = false;
  bool Disassemble = false;
  bool DisassembleAll = false;
  std::vector<std::string> DisassemblerOptions;
  ColorOutput DisassemblyColor = ColorOutput::Auto;
  DIDumpType DwarfDumpType = DIDT_Null;
  std::vector<std::string> FilterSections;
  bool LeadingAddr = false;
  bool LoadRelocs = false;
  bool LoadTypes = false;
  std::vector<std::string> MAttrs;
  std::string MCPU;
  std::string Prefix;
  uint32_t PrefixStrip = 0;
  bool PrintImmHex = false;
  bool PrintLines = false;
  bool PrintSource = false;
  bool PrivateHeaders = false;
  bool Relocations = false;
  bool SectionHeaders = false;
  bool SectionContents = false;
  bool ShowRawInsn = false;
  bool SymbolDescription = false;
  bool TracebackTable = false;
  bool SymbolTable = false;
  std::string TripleName;
  bool UnwindInfo = false;
};

extern ObjdumpOptions Opts;

extern std::vector<std::string> SourceDirs;
extern std::vector<std::pair<std::string, std::string>> SubstitutePaths;
extern bool UnwindShowWODPool;

extern StringSet<> FoundSectionSet;

class Dumper {
  const object::ObjectFile &O;
  StringSet<> Warnings;

protected:
  llvm::raw_ostream &OS;
  std::function<Error(const Twine &Msg)> WarningHandler;

public:
  Dumper(const object::ObjectFile &O);
  virtual ~Dumper() = default;

  void reportUniqueWarning(Error Err);
  void reportUniqueWarning(const Twine &Msg);

  virtual void printPrivateHeaders();
  virtual void printDynamicRelocations() {}
  void printSymbolTable(StringRef ArchiveName,
                        StringRef ArchitectureName = StringRef(),
                        bool DumpDynamic = false);
  void printSymbol(const object::SymbolRef &Symbol,
                   ArrayRef<object::VersionEntry> SymbolVersions,
                   StringRef FileName, StringRef ArchiveName,
                   StringRef ArchitectureName, bool DumpDynamic);
  void printRelocations();
};

std::unique_ptr<Dumper> createCOFFDumper(const object::COFFObjectFile &Obj);
std::unique_ptr<Dumper> createELFDumper(const object::ELFObjectFileBase &Obj);
std::unique_ptr<Dumper> createMachODumper(const object::MachOObjectFile &Obj);
std::unique_ptr<Dumper> createWasmDumper(const object::WasmObjectFile &Obj);
std::unique_ptr<Dumper> createXCOFFDumper(const object::XCOFFObjectFile &Obj);
std::unique_ptr<Dumper>
createDXContainerDumper(const object::DXContainerObjectFile &Obj);

// Various helper functions.

/// Creates a SectionFilter with a standard predicate that conditionally skips
/// sections when the --section objdump flag is provided.
///
/// Idx is an optional output parameter that keeps track of which section index
/// this is. This may be different than the actual section number, as some
/// sections may be filtered (e.g. symbol tables).
object::SectionFilter ToolSectionFilter(const llvm::object::ObjectFile &O,
                                        uint64_t *Idx = nullptr);

bool isRelocAddressLess(object::RelocationRef A, object::RelocationRef B);
void printSectionHeaders(object::ObjectFile &O);
void printSectionContents(const object::ObjectFile *O);
[[noreturn]] void reportError(StringRef File, const Twine &Message);
[[noreturn]] void reportError(Error E, StringRef FileName,
                              StringRef ArchiveName = "",
                              StringRef ArchitectureName = "");
void reportWarning(const Twine &Message, StringRef File);

template <typename T, typename... Ts>
T unwrapOrError(Expected<T> EO, Ts &&...Args) {
  if (EO)
    return std::move(*EO);
  reportError(EO.takeError(), std::forward<Ts>(Args)...);
}

void invalidArgValue(const opt::Arg *A);

std::string getFileNameForError(const object::Archive::Child &C,
                                unsigned Index);
SymbolInfoTy createSymbolInfo(const object::ObjectFile &Obj,
                              const object::SymbolRef &Symbol,
                              bool IsMappingSymbol = false);
unsigned getInstStartColumn(const MCSubtargetInfo &STI);
void printRawData(llvm::ArrayRef<uint8_t> Bytes, uint64_t Address,
                  llvm::formatted_raw_ostream &OS,
                  llvm::MCSubtargetInfo const &STI);

} // namespace objdump
} // end namespace llvm

#endif
