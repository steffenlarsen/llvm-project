//===- Config.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLD_MACHO_CONFIG_H
#define LLD_MACHO_CONFIG_H

#include "lld/Common/BPSectionOrdererBase.h"
#include "lld/Common/CommonLinkerContext.h"
#include "llvm/ADT/CachedHashString.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/CachePruning.h"
#include "llvm/Support/GlobPattern.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/Support/VersionTuple.h"
#include "llvm/TextAPI/Architecture.h"
#include "llvm/TextAPI/Platform.h"
#include "llvm/TextAPI/Target.h"

#include <vector>

namespace llvm {
enum class CodeGenOptLevel;
class TarWriter;
} // namespace llvm

namespace lld {
namespace macho {
class ArchiveFile;
class BindingSection;
class CStringSection;
class ChainedFixupsSection;
class ConcatInputSection;
class ConcatOutputSection;
class DeduplicatedCStringSection;
class DependencyTracker;
class DylibFile;
class ExportSection;
class GotSection;
class InitOffsetsSection;
class InputFile;
class InputSection;
class LazyBindingSection;
class LazyPointerSection;
class MachHeaderSection;
class ObjCImageInfoSection;
class ObjCMethListSection;
class ObjCStubsSection;
class OutputSection;
class OutputSegment;
class PriorityBuilder;
class RebaseSection;
class SerialBackgroundWorkQueue;
class StubHelperSection;
class StubsSection;
class Symbol;
class SymbolTable;
class SyntheticSection;
class TargetInfo;
class UnwindInfoSection;
class WeakBindingSection;
class WordLiteralSection;
struct Ctx;
struct ThunkInfo;
struct ThunkKey;
struct ThunkMapKeyInfo;

using NamePair = std::pair<llvm::StringRef, llvm::StringRef>;
using SectionRenameMap = llvm::DenseMap<NamePair, NamePair>;
using SegmentRenameMap = llvm::DenseMap<llvm::StringRef, llvm::StringRef>;
using ThunkMap = llvm::DenseMap<ThunkKey, ThunkInfo, ThunkMapKeyInfo>;

struct PlatformInfo {
  llvm::MachO::Target target;
  llvm::VersionTuple sdk;
};

inline uint32_t encodeVersion(const llvm::VersionTuple &version) {
  return ((version.getMajor() << 020) |
          (version.getMinor().value_or(0) << 010) |
          version.getSubminor().value_or(0));
}

enum class NamespaceKind {
  twolevel,
  flat,
};

enum class UndefinedSymbolTreatment {
  unknown,
  error,
  warning,
  suppress,
  dynamic_lookup,
};

enum class ICFLevel {
  unknown,
  none,
  safe,
  safe_thunks,
  all,
};

enum class ObjCStubsMode {
  fast,
  small,
};

struct SectionAlign {
  llvm::StringRef segName;
  llvm::StringRef sectName;
  uint32_t align;
};

struct SegmentProtection {
  llvm::StringRef name;
  uint32_t maxProt;
  uint32_t initProt;
};

class SymbolPatterns {
public:
  // GlobPattern can also match literals,
  // but we prefer the O(1) lookup of DenseSet.
  llvm::SetVector<llvm::CachedHashStringRef> literals;
  std::vector<llvm::GlobPattern> globs;

  bool empty() const { return literals.empty() && globs.empty(); }
  void clear();
  void insert(Ctx &ctx, llvm::StringRef symbolName);
  bool matchLiteral(llvm::StringRef symbolName) const;
  bool matchGlob(llvm::StringRef symbolName) const;
  bool match(llvm::StringRef symbolName) const;
};

enum class SymtabPresence {
  All,
  None,
  SelectivelyIncluded,
  SelectivelyExcluded,
};

struct Configuration {
  Symbol *entry = nullptr;
  bool hasReexports = false;
  bool allLoad = false;
  bool applicationExtension = false;
  bool archMultiple = false;
  bool exportDynamic = false;
  bool forceLoadObjC = false;
  bool forceLoadSwift = false; // Only applies to LC_LINKER_OPTIONs.
  bool staticLink = false;
  bool implicitDylibs = false;
  bool isPic = false;
  bool headerPadMaxInstallNames = false;
  bool markDeadStrippableDylib = false;
  bool printDylibSearch = false;
  bool printEachFile = false;
  bool printWhyLoad = false;
  bool searchDylibsFirst = false;
  bool saveTemps = false;
  bool adhocCodesign = false;
  bool emitFunctionStarts = false;
  bool emitDataInCodeInfo = false;
  bool emitEncryptionInfo = false;
  bool emitInitOffsets = false;
  bool emitChainedFixups = false;
  bool emitRelativeMethodLists = false;
  bool thinLTOEmitImportsFiles;
  bool thinLTOEmitIndexFiles;
  bool thinLTOIndexOnly;
  bool timeTraceEnabled = false;
  bool dataConst = false;
  bool dedupStrings = true;
  bool dedupSymbolStrings = true;
  bool deadStripDuplicates = false;
  bool omitDebugInfo = false;
  bool stripSwiftForceLoad = false;
  bool warnDylibInstallName = false;
  bool ignoreOptimizationHints = false;
  bool forceExactCpuSubtypeMatch = false;
  uint32_t headerPad;
  uint32_t dylibCompatibilityVersion = 0;
  uint32_t dylibCurrentVersion = 0;
  uint32_t timeTraceGranularity = 500;
  unsigned optimize;
  std::string progName;

  // For `clang -arch arm64 -arch x86_64`, clang will:
  // 1. invoke the linker twice, to write one temporary output per arch
  // 2. invoke `lipo` to merge the two outputs into a single file
  // `outputFile` is the name of the temporary file the linker writes to.
  // `finalOutput `is the name of the file lipo writes to after the link.
  llvm::StringRef outputFile;
  llvm::StringRef finalOutput;

  llvm::StringRef installName;
  llvm::StringRef clientName;
  llvm::StringRef mapFile;
  llvm::StringRef ltoNewPmPasses;
  llvm::StringRef ltoObjPath;
  llvm::StringRef thinLTOJobs;
  llvm::StringRef umbrella;
  uint32_t ltoo = 2;
  llvm::CodeGenOptLevel ltoCgo;
  llvm::CachePruningPolicy thinLTOCachePolicy;
  llvm::StringRef thinLTOCacheDir;
  llvm::StringRef thinLTOIndexOnlyArg;
  std::pair<llvm::StringRef, llvm::StringRef> thinLTOObjectSuffixReplace;
  llvm::StringRef thinLTOPrefixReplaceOld;
  llvm::StringRef thinLTOPrefixReplaceNew;
  llvm::StringRef thinLTOPrefixReplaceNativeObject;
  bool deadStripDylibs = false;
  bool demangle = false;
  bool deadStrip = false;
  bool interposable = false;
  bool errorForArchMismatch = false;
  bool ignoreAutoLink = false;
  int readWorkers = 0;
  // ld64 allows invalid auto link options as long as the link succeeds. LLD
  // does not, but there are cases in the wild where the invalid linker options
  // exist. This allows users to ignore the specific invalid options in the case
  // they can't easily fix them.
  llvm::StringSet<> ignoreAutoLinkOptions;
  bool strictAutoLink = false;
  PlatformInfo platformInfo;
  std::optional<PlatformInfo> secondaryPlatformInfo;
  NamespaceKind namespaceKind = NamespaceKind::twolevel;
  UndefinedSymbolTreatment undefinedSymbolTreatment =
      UndefinedSymbolTreatment::error;
  ICFLevel icfLevel = ICFLevel::none;
  bool keepICFStabs = false;
  ObjCStubsMode objcStubsMode = ObjCStubsMode::fast;
  llvm::MachO::HeaderFileType outputType;
  std::vector<llvm::StringRef> systemLibraryRoots;
  std::vector<llvm::StringRef> librarySearchPaths;
  std::vector<llvm::StringRef> frameworkSearchPaths;
  bool warnDuplicateRpath = true;
  llvm::SmallVector<llvm::StringRef, 0> runtimePaths;
  llvm::SmallVector<llvm::StringRef, 0> allowableClients;
  std::vector<std::string> astPaths;
  std::vector<Symbol *> explicitUndefineds;
  llvm::StringSet<> explicitDynamicLookups;
  // There are typically few custom sectionAlignments or segmentProtections,
  // so use a vector instead of a map.
  std::vector<SectionAlign> sectionAlignments;
  std::vector<SegmentProtection> segmentProtections;
  bool ltoDebugPassManager = false;
  bool emitLLVM = false;
  llvm::StringRef codegenDataGeneratePath;
  bool csProfileGenerate = false;
  llvm::StringRef csProfilePath;
  bool pgoWarnMismatch;
  bool warnThinArchiveMissingMembers;
  bool warnMissingSubsectionsViaSymbols = false;
  bool disableVerify;
  bool separateCstringLiteralSections;
  bool tailMergeStrings;
  unsigned slopScale = 256;

  bool callGraphProfileSort = false;
  llvm::StringRef printSymbolOrder;

  llvm::StringRef irpgoProfilePath;
  bool bpStartupFunctionSort = false;
  bool bpCompressionSortStartupFunctions = false;
  bool bpFunctionOrderForCompression = false;
  bool bpDataOrderForCompression = false;
  llvm::SmallVector<BPCompressionSortSpec> bpCompressionSortSpecs;
  bool bpVerboseSectionOrderer = false;

  SectionRenameMap sectionRenameMap;
  SegmentRenameMap segmentRenameMap;

  bool hasExplicitExports = false;
  SymbolPatterns exportedSymbols;
  SymbolPatterns unexportedSymbols;
  SymbolPatterns whyLive;

  std::vector<std::pair<llvm::StringRef, llvm::StringRef>> aliasedSymbols;

  SymtabPresence localSymbolsPresence = SymtabPresence::All;
  SymbolPatterns localSymbolPatterns;
  llvm::SmallVector<llvm::StringRef, 0> mllvmOpts;
  llvm::SmallVector<llvm::StringRef, 0> passPlugins;

  bool zeroModTime = true;
  bool generateUuid = true;

  llvm::StringRef osoPrefix;

  std::vector<llvm::StringRef> dyldEnvs;

  llvm::MachO::Architecture arch() const { return platformInfo.target.Arch; }

  llvm::MachO::PlatformType platform() const {
    return platformInfo.target.Platform;
  }
};

struct ArchiveFileInfo {
  ArchiveFile *file;
  bool isCommandLineLoad;
};

// Linker generated sections which can be used as inputs and are not specific
// to an input file.
struct InStruct {
  const uint8_t *bufferStart = nullptr;
  MachHeaderSection *header = nullptr;
  /// The list of cstring sections. Note that this includes \p cStringSection
  /// and \p objcMethnameSection already.
  llvm::SmallVector<CStringSection *> cStringSections;
  CStringSection *cStringSection = nullptr;
  DeduplicatedCStringSection *objcMethnameSection = nullptr;
  WordLiteralSection *wordLiteralSection = nullptr;
  RebaseSection *rebase = nullptr;
  BindingSection *binding = nullptr;
  WeakBindingSection *weakBinding = nullptr;
  LazyBindingSection *lazyBinding = nullptr;
  ExportSection *exports = nullptr;
  GotSection *got = nullptr;
  LazyPointerSection *lazyPointers = nullptr;
  StubsSection *stubs = nullptr;
  StubHelperSection *stubHelper = nullptr;
  ObjCStubsSection *objcStubs = nullptr;
  UnwindInfoSection *unwindInfo = nullptr;
  ObjCImageInfoSection *objCImageInfo = nullptr;
  ConcatInputSection *imageLoaderCache = nullptr;
  InitOffsetsSection *initOffsets = nullptr;
  ObjCMethListSection *objcMethList = nullptr;
  ChainedFixupsSection *chainedFixups = nullptr;

  // Maps a cstring section name to its index in cStringSections. Use
  // getOrCreateCStringSection() to look up or create a section.
  llvm::StringMap<unsigned> cStringSectionMap;
};

// The context of a MachO link. It holds all of the link's state; nothing in
// the MachO port is global, so links may run concurrently.
struct Ctx : CommonLinkerContext {
  Ctx();
  ~Ctx();

  // Value-initialized, since some fields of Configuration have no initializer.
  Configuration arg = Configuration();
  std::unique_ptr<TargetInfo> target;
  std::unique_ptr<SymbolTable> symtab;
  InStruct in;
  std::vector<SyntheticSection *> syntheticSections;

  // If --reproduce option is given, all input files are written to this tar
  // archive.
  std::unique_ptr<llvm::TarWriter> tar;
  std::unique_ptr<DependencyTracker> depTracker;
  std::unique_ptr<PriorityBuilder> priorityBuilder;

  llvm::SetVector<InputFile *> inputFiles;
  llvm::DenseMap<llvm::CachedHashStringRef, llvm::MemoryBufferRef> cachedReads;
  llvm::SmallVector<llvm::StringRef> unprocessedLCLinkerOptions;
  // Provides InputFile::id, which sorts input files deterministically.
  int nextInputFileId = 0;

  std::vector<ConcatInputSection *> inputSections;
  // This is used as a counter for specifying input order for input sections.
  int inputSectionsOrder = 0;

  std::vector<OutputSegment *> outputSegments;
  llvm::DenseMap<llvm::StringRef, OutputSegment *> nameToOutputSegment;
  // Output sections are added to output segments in iteration order of
  // ConcatOutputSection, so must have deterministic iteration order.
  llvm::MapVector<NamePair, ConcatOutputSection *> concatOutputSections;
  OutputSection *firstTLVDataSection = nullptr;
  std::unique_ptr<ThunkMap> thunkMap;

  // Driver state.
  llvm::DenseMap<llvm::CachedHashStringRef, llvm::StringRef> resolvedLibraries;
  llvm::DenseMap<llvm::CachedHashStringRef, llvm::StringRef> resolvedFrameworks;
  llvm::DenseMap<llvm::StringRef, ArchiveFileInfo> loadedArchives;
  llvm::DenseSet<llvm::StringRef> loadedObjectFrameworks;
  std::vector<llvm::StringRef> missingAutolinkWarnings;
  llvm::DenseMap<llvm::CachedHashStringRef, DylibFile *> loadedDylibs;

  llvm::DenseMap<llvm::CachedHashStringRef, ConcatInputSection *>
      methnameToSelref;

#if LLVM_ENABLE_THREADS
  // Declared last so that it is destroyed first: its destructor waits for the
  // background thread, which may still be reading the link's input files.
  std::unique_ptr<SerialBackgroundWorkQueue> pageInQueue;
#endif
};

// To evaluate the second argument lazily, we use C macro.
#define CHECK(E, S) lld::check2(ctx.e, (E), [&] { return toString(S); })
} // namespace macho
} // namespace lld

#endif
