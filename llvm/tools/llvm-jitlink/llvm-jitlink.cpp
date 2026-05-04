//===- llvm-jitlink.cpp -- Command line interface/tester for llvm-jitlink -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This utility provides a simple command line interface to the llvm jitlink
// library, which makes relocatable object files executable in memory. Its
// primary function is as a testing utility for the jitlink library.
//
//===----------------------------------------------------------------------===//

#include "llvm-jitlink.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Config/llvm-config.h" // for LLVM_ON_UNIX, LLVM_ENABLE_THREADS
#include "llvm/ExecutionEngine/Orc/AbsoluteSymbols.h"
#include "llvm/ExecutionEngine/Orc/BacktraceTools.h"
#include "llvm/ExecutionEngine/Orc/COFFAutoImportGenerator.h"
#include "llvm/ExecutionEngine/Orc/COFFPlatform.h"
#include "llvm/ExecutionEngine/Orc/Debugging/DebugInfoSupport.h"
#include "llvm/ExecutionEngine/Orc/Debugging/DebuggerSupportPlugin.h"
#include "llvm/ExecutionEngine/Orc/Debugging/ELFDebugObjectPlugin.h"
#include "llvm/ExecutionEngine/Orc/Debugging/PerfSupportPlugin.h"
#include "llvm/ExecutionEngine/Orc/Debugging/VTuneSupportPlugin.h"
#include "llvm/ExecutionEngine/Orc/EHFrameRegistrationPlugin.h"
#include "llvm/ExecutionEngine/Orc/ELFNixPlatform.h"
#include "llvm/ExecutionEngine/Orc/EPCDynamicLibrarySearchGenerator.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/IndirectionUtils.h"
#include "llvm/ExecutionEngine/Orc/JITLinkRedirectableSymbolManager.h"
#include "llvm/ExecutionEngine/Orc/JITLinkReentryTrampolines.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/LoadLinkableFile.h"
#include "llvm/ExecutionEngine/Orc/MachO.h"
#include "llvm/ExecutionEngine/Orc/MachOPlatform.h"
#include "llvm/ExecutionEngine/Orc/MapperJITLinkMemoryManager.h"
#include "llvm/ExecutionEngine/Orc/ObjectFileInterface.h"
#include "llvm/ExecutionEngine/Orc/SectCreate.h"
#include "llvm/ExecutionEngine/Orc/SelfExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/Shared/OrcRTBridge.h"
#include "llvm/ExecutionEngine/Orc/SimpleMemoryMapSPS.h"
#include "llvm/ExecutionEngine/Orc/SimpleRemoteMemoryMapper.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/JITLoaderGDB.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/JITLoaderPerf.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/JITLoaderVTune.h"
#include "llvm/ExecutionEngine/Orc/TargetProcess/RegisterEHFrames.h"
#include "llvm/ExecutionEngine/Orc/UnwindInfoRegistrationPlugin.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCInstrAnalysis.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCTargetOptions.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/COFF.h"
#include "llvm/Object/MachO.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/TapiUniversal.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SupportOptionsOptInfos.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Timer.h"
#include <chrono>
#include <cstring>
#include <deque>
#include <string>

#ifdef LLVM_ON_UNIX
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif // LLVM_ON_UNIX

#define DEBUG_TYPE "llvm_jitlink"

using namespace llvm;
using namespace llvm::jitlink;
using namespace llvm::orc;

// =====================================================================
// clv2 OptionInfo descriptors
// =====================================================================

inline constexpr clv2::OptionCategory JITLinkCategory{"JITLink Options"};

inline constexpr clv2::ListOptionInfo<std::string> InputFilesOpt{
    "", "input files", clv2::Positional{}, clv2::cat(JITLinkCategory)};

inline constexpr clv2::ListOptionInfo<bool> LazyLinkOpt{
    "lazy", "Link the following file lazily", clv2::cat(JITLinkCategory)};

enum class SpeculateKind { None, Simple };

inline constexpr clv2::EnumVal<SpeculateKind> SpeculateKindVals[] = {
    {"none", SpeculateKind::None, "No speculation"},
    {"simple", SpeculateKind::Simple, "Simple speculation"},
};
inline constexpr auto SpeculateOpt = clv2::makeEnumOption<SpeculateKind>(
    "speculate", "Choose speculation scheme", SpeculateKindVals,
    clv2::Init{SpeculateKind::None}, clv2::cat(JITLinkCategory));

inline constexpr clv2::OptionInfo<std::string> SpeculateOrderOpt{
    "speculate-order",
    "A CSV file containing (JITDylib, Function) pairs to"
    "speculatively look up",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<std::string> RecordLazyExecsOpt{
    "record-lazy-execs",
    "Write lazy-function executions to a CSV file as (JITDylib, "
    "function) pairs",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<size_t> MaterializationThreadsOpt{
    "num-threads", "Number of materialization threads to use",
    clv2::Init{std::numeric_limits<size_t>::max()}, clv2::cat(JITLinkCategory)};

inline constexpr clv2::ListOptionInfo<std::string> LibrarySearchPathsOpt{
    "L", "Add dir to the list of library search paths", clv2::PrefixFormat,
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::ListOptionInfo<std::string> LibrariesOpt{
    "l", "Link against library X in the library search paths",
    clv2::PrefixFormat, clv2::cat(JITLinkCategory)};

inline constexpr clv2::ListOptionInfo<std::string> LibrariesHiddenOpt{
    "hidden-l",
    "Link against library X in the library search "
    "paths with hidden visibility",
    clv2::PrefixFormat, clv2::cat(JITLinkCategory)};

inline constexpr clv2::ListOptionInfo<std::string> LoadHiddenOpt{
    "load_hidden", "Link against library X with hidden visibility",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<std::string> WriteSymbolTableToOpt{
    "write-symtab",
    "Write the symbol table for the JIT'd program to the specified file",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<std::string> SymbolicateWithOpt{
    "symbolicate-with",
    "Given a path to a symbol table file, symbolicate the given backtrace(s)",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::ListOptionInfo<std::string> LibrariesWeakOpt{
    "weak-l",
    "Emulate weak link against library X. Must resolve "
    "to a TextAPI file, and all symbols in the "
    "interface will resolve to null.",
    clv2::PrefixFormat, clv2::cat(JITLinkCategory)};

inline constexpr clv2::ListOptionInfo<std::string> WeakLibrariesOpt{
    "weak_library",
    "Emulate weak link against library X. X must point to a "
    "TextAPI file, and all symbols in the interface will resolve to null",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::ListOptionInfo<std::string> LibrariesAutoOpt{
    "auto-l",
    "Link against library X in the library search paths "
    "(auto-generate corresponding import library)",
    clv2::PrefixFormat, clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<bool> SearchSystemLibraryOpt{
    "search-sys-lib", "Add system library paths to library search paths",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<bool> NoExecOpt{
    "noexec", "Do not execute loaded code", clv2::cat(JITLinkCategory)};

inline constexpr clv2::ListOptionInfo<std::string> CheckFilesOpt{
    "check", "File containing verifier checks", clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<std::string> CheckNameOpt{
    "check-name", "Name of checks to match against",
    clv2::Init{"jitlink-check"}, clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<std::string> EntryPointNameOpt{
    "entry", "Symbol to call as main entry point", clv2::cat(JITLinkCategory)};

inline constexpr clv2::ListOptionInfo<std::string> JITDylibsOpt{
    "jd",
    "Specifies the JITDylib to be used for any subsequent "
    "input file, -L<seacrh-path>, and -l<library> arguments",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::ListOptionInfo<std::string> DylibsOpt{
    "preload",
    "Pre-load dynamic libraries (e.g. language runtimes "
    "required by the ORC runtime)",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<bool> DebuggerSupportOpt{
    "debugger-support", "Enable debugger suppport (default = !-noexec)",
    clv2::Init{true}, clv2::Hidden, clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<bool> PerfSupportOpt{
    "perf-support", "Enable perf profiling support", clv2::Hidden,
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<bool> VTuneSupportOpt{
    "vtune-support", "Enable vtune profiling support", clv2::Hidden,
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<bool> NoProcessSymbolsOpt{
    "no-process-syms", "Do not resolve to llvm-jitlink process symbols",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::ListOptionInfo<std::string> AbsoluteDefsOpt{
    "abs", "Inject absolute symbol definitions (syntax: <name>=<addr>)",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::ListOptionInfo<std::string> AliasesOpt{
    "alias", "Inject symbol aliases (syntax: <alias-name>=<aliasee>)",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::ListOptionInfo<std::string> SectCreateOpt{
    "sectcreate",
    "given <sectname>,<filename>[@<sym>=<offset>,...]  "
    "add the content of <filename> to <sectname>",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<bool> ShowLinkedFilesOpt{
    "show-linked-files", "List each file/graph name if/when it is linked",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<bool> ShowInitialExecutionSessionStateOpt{
    "show-init-es", "Print ExecutionSession state before resolving entry point",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<bool> ShowEntryExecutionSessionStateOpt{
    "show-entry-es", "Print ExecutionSession state after resolving entry point",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<bool> ShowAddrsOpt{
    "show-addrs", "Print registered symbol, section, got and stub addresses",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<std::string> ShowLinkGraphsOpt{
    "show-graphs",
    "Takes a posix regex and prints the link graphs of all files "
    "matching that regex after fixups have been applied",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<bool> ShowTimesOpt{
    "show-times", "Show times for llvm-jitlink phases",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<std::string> SlabAllocateSizeStringOpt{
    "slab-allocate",
    "Allocate from a slab of the given size "
    "(allowable suffixes: Kb, Mb, Gb. default = Kb)",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<uint64_t> SlabAddressOpt{
    "slab-address",
    "Set slab target address (requires -slab-allocate and -noexec)",
    clv2::Init{~0ULL}, clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<uint64_t> SlabPageSizeOpt{
    "slab-page-size",
    "Set page size for slab (requires -slab-allocate and -noexec)",
    clv2::Init{uint64_t{0}}, clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<bool> ShowRelocatedSectionContentsOpt{
    "show-relocated-section-contents",
    "show section contents after fixups have been applied",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<bool> PhonyExternalsOpt{
    "phony-externals", "resolve all otherwise unresolved externals to null",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<std::string> OutOfProcessExecutorOpt{
    "oop-executor", "Launch an out-of-process executor to run code",
    clv2::ValueOptional, clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<std::string> OutOfProcessExecutorConnectOpt{
    "oop-executor-connect", "Connect to an out-of-process executor via TCP",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<std::string> OrcRuntimeOpt{
    "orc-runtime", "Use ORC runtime from given path",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<bool> AddSelfRelocationsOpt{
    "add-self-relocations",
    "Add relocations to function pointers to the current function",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<bool> ShowErrFailedToMaterializeOpt{
    "show-err-failed-to-materialize", "Show FailedToMaterialize errors",
    clv2::cat(JITLinkCategory)};

enum class MemMgr { Default, Generic, SimpleRemote, Shared };

inline constexpr clv2::EnumVal<MemMgr> MemMgrVals[] = {
    {"default", MemMgr::Default, "Use setup default (InProcess or EPCGeneric)"},
    {"generic", MemMgr::Generic, "Generic remote memory manager"},
    {"simple-remote", MemMgr::SimpleRemote,
     "Mapper memory manager with simple-remote backend"},
    {"shared", MemMgr::Shared,
     "Mapper memory manager with shared-memory manager"},
};
inline constexpr auto UseMemMgrOpt = clv2::makeEnumOption<MemMgr>(
    "use-memmgr", "Choose memory manager", MemMgrVals,
    clv2::Init{MemMgr::Generic}, clv2::cat(JITLinkCategory));

namespace llvm {
struct JITLinkArgs {
  std::vector<std::string> InputFiles;
  std::vector<unsigned> InputFilePositions;
  std::vector<bool> LazyLink;
  std::vector<unsigned> LazyLinkPositions;
  SpeculateKind Speculate = SpeculateKind::None;
  std::string SpeculateOrder;
  std::string RecordLazyExecs;
  size_t MaterializationThreads = std::numeric_limits<size_t>::max();
  unsigned MaterializationThreadsOccurrences = 0;
  std::vector<std::string> LibrarySearchPaths;
  std::vector<unsigned> LibrarySearchPathPositions;
  std::vector<std::string> Libraries;
  std::vector<unsigned> LibraryPositions;
  std::vector<std::string> LibrariesHidden;
  std::vector<unsigned> LibrariesHiddenPositions;
  std::vector<std::string> LoadHidden;
  std::vector<unsigned> LoadHiddenPositions;
  std::string WriteSymbolTableTo;
  std::string SymbolicateWith;
  std::vector<std::string> LibrariesWeak;
  std::vector<unsigned> LibrariesWeakPositions;
  std::vector<std::string> LibrariesAuto;
  std::vector<unsigned> LibrariesAutoPositions;
  std::vector<std::string> WeakLibraries;
  std::vector<unsigned> WeakLibraryPositions;
  bool SearchSystemLibrary = false;
  bool NoExec = false;
  std::vector<std::string> CheckFiles;
  std::string CheckName;
  std::string EntryPointName;
  std::vector<std::string> JITDylibs;
  std::vector<unsigned> JITDylibPositions;
  std::vector<std::string> Dylibs;
  bool DebuggerSupport = true;
  unsigned DebuggerSupportOccurrences = 0;
  bool PerfSupport = false;
  bool VTuneSupport = false;
  bool NoProcessSymbols = false;
  std::vector<std::string> AbsoluteDefs;
  std::vector<unsigned> AbsoluteDefPositions;
  std::vector<std::string> Aliases;
  std::vector<unsigned> AliasPositions;
  std::vector<std::string> SectCreate;
  std::vector<unsigned> SectCreatePositions;
  bool ShowLinkedFiles = false;
  bool ShowInitialExecutionSessionState = false;
  bool ShowEntryExecutionSessionState = false;
  bool ShowAddrs = false;
  std::string ShowLinkGraphs;
  bool ShowTimes = false;
  std::string SlabAllocateSizeString;
  uint64_t SlabAddress = ~0ULL;
  uint64_t SlabPageSize = 0;
  bool ShowRelocatedSectionContents = false;
  bool PhonyExternals = false;
  std::string OutOfProcessExecutor;
  unsigned OutOfProcessExecutorOccurrences = 0;
  std::string OutOfProcessExecutorConnect;
  unsigned OutOfProcessExecutorConnectOccurrences = 0;
  std::string OrcRuntime;
  bool AddSelfRelocations = false;
  bool ShowErrFailedToMaterialize = false;
  MemMgr UseMemMgr = MemMgr::Generic;
  std::string OverrideTriple;
  bool AllLoad = false;
  bool ForceLoadObjC = false;
  std::string WaitingOnGraphCapture;
  std::string WaitingOnGraphReplay;
  bool ShowPrePruneTotalBlockSize = false;
  bool ShowPostFixupTotalBlockSize = false;
  std::vector<std::string> InputArgv;
  std::vector<std::string> TestHarnesses;
};
} // namespace llvm

inline constexpr clv2::OptionInfo<std::string> OverrideTripleOpt{
    "triple", "Override target triple detection", clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<bool> AllLoadOpt{
    "all_load", "Load all members of static archives",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<bool> ForceLoadObjCOpt{
    "ObjC",
    "Load all members of static archives that implement "
    "Objective-C classes or categories, or Swift structs, "
    "classes or extensions",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<std::string> WaitingOnGraphCaptureOpt{
    "waiting-on-graph-capture",
    "Record WaitingOnGraph operations to the given file",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<std::string> WaitingOnGraphReplayOpt{
    "waiting-on-graph-replay",
    "Replay WaitingOnGraph operations from the given file",
    clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<bool> ShowPrePruneTotalBlockSizeOpt{
    "pre-prune-total-block-size",
    "Total size of all blocks (including zero-fill) in all "
    "graphs (pre-pruning)",
    clv2::Hidden, clv2::cat(JITLinkCategory)};

inline constexpr clv2::OptionInfo<bool> ShowPostFixupTotalBlockSizeOpt{
    "post-fixup-total-block-size",
    "Total size of all blocks (including zero-fill) in all "
    "graphs (post-fixup)",
    clv2::Hidden, clv2::cat(JITLinkCategory)};

inline constexpr clv2::ListOptionInfo<std::string> InputArgvOpt{
    "args", "<program arguments>...", clv2::Positional{},
    clv2::PositionalEatsArgs, clv2::cat(JITLinkCategory)};

inline constexpr clv2::ListOptionInfo<std::string> TestHarnessesOpt{
    "harness", "Test harness files", clv2::Positional{},
    clv2::PositionalEatsArgs, clv2::cat(JITLinkCategory)};

// =====================================================================
// OptionsRegistry (must follow OptionInfo descriptors)
// =====================================================================

static constexpr clv2::OptionsRegistry<
    &InputFilesOpt, &LazyLinkOpt, &SpeculateOpt, &SpeculateOrderOpt,
    &RecordLazyExecsOpt, &MaterializationThreadsOpt, &LibrarySearchPathsOpt,
    &LibrariesOpt, &LibrariesHiddenOpt, &LoadHiddenOpt, &WriteSymbolTableToOpt,
    &SymbolicateWithOpt, &LibrariesWeakOpt, &WeakLibrariesOpt,
    &LibrariesAutoOpt, &SearchSystemLibraryOpt, &NoExecOpt, &CheckFilesOpt,
    &CheckNameOpt, &EntryPointNameOpt, &JITDylibsOpt, &DylibsOpt,
    &DebuggerSupportOpt, &PerfSupportOpt, &VTuneSupportOpt,
    &NoProcessSymbolsOpt, &AbsoluteDefsOpt, &AliasesOpt, &SectCreateOpt,
    &ShowLinkedFilesOpt, &ShowInitialExecutionSessionStateOpt,
    &ShowEntryExecutionSessionStateOpt, &ShowAddrsOpt, &ShowLinkGraphsOpt,
    &ShowTimesOpt, &SlabAllocateSizeStringOpt, &SlabAddressOpt,
    &SlabPageSizeOpt, &ShowRelocatedSectionContentsOpt, &PhonyExternalsOpt,
    &OutOfProcessExecutorOpt, &OutOfProcessExecutorConnectOpt, &OrcRuntimeOpt,
    &AddSelfRelocationsOpt, &ShowErrFailedToMaterializeOpt, &UseMemMgrOpt,
    &OverrideTripleOpt, &AllLoadOpt, &ForceLoadObjCOpt,
    &WaitingOnGraphCaptureOpt, &WaitingOnGraphReplayOpt,
    &ShowPrePruneTotalBlockSizeOpt, &ShowPostFixupTotalBlockSizeOpt,
    &InputArgvOpt, &TestHarnessesOpt>
    JITLinkToolReg;

static ExitOnError ExitOnErr;

static LLVM_ATTRIBUTE_USED void linkComponents() {
  errs() << "Linking in runtime functions\n"
         << (void *)&llvm_orc_registerEHFrameSectionAllocAction << '\n'
         << (void *)&llvm_orc_deregisterEHFrameSectionAllocAction << '\n'
         << (void *)&llvm_orc_registerJITLoaderGDBAllocAction << '\n'
         << (void *)&llvm_orc_registerJITLoaderPerfStart << '\n'
         << (void *)&llvm_orc_registerJITLoaderPerfEnd << '\n'
         << (void *)&llvm_orc_registerJITLoaderPerfImpl << '\n'
         << (void *)&llvm_orc_registerVTuneImpl << '\n'
         << (void *)&llvm_orc_unregisterVTuneImpl << '\n'
         << (void *)&llvm_orc_test_registerVTuneImpl << '\n';
}

static bool UseTestResultOverride = false;
static int64_t TestResultOverride = 0;

extern "C" LLVM_ATTRIBUTE_USED void
llvm_jitlink_setTestResultOverride(int64_t Value) {
  TestResultOverride = Value;
  UseTestResultOverride = true;
}

static Error addSelfRelocations(LinkGraph &G);

namespace {

template <typename ErrT>

class ConditionalPrintErr {
public:
  ConditionalPrintErr(bool C) : C(C) {}
  void operator()(ErrT &EI) {
    if (C) {
      errs() << "llvm-jitlink error: ";
      EI.log(errs());
      errs() << "\n";
    }
  }

private:
  bool C;
};

Expected<std::unique_ptr<MemoryBuffer>> getFile(const Twine &FileName) {
  if (auto F = MemoryBuffer::getFile(FileName))
    return std::move(*F);
  else
    return createFileError(FileName, F.getError());
}

void reportLLVMJITLinkError(Error Err, bool ShowFailedToMaterialize) {
  handleAllErrors(
      std::move(Err),
      ConditionalPrintErr<orc::FailedToMaterialize>(ShowFailedToMaterialize),
      ConditionalPrintErr<ErrorInfoBase>(true));
}

} // end anonymous namespace

namespace llvm {

static raw_ostream &
operator<<(raw_ostream &OS, const Session::MemoryRegionInfo &MRI) {
  OS << "target addr = " << format("0x%016" PRIx64, MRI.getTargetAddress());

  if (MRI.isZeroFill())
    OS << ", zero-fill: " << MRI.getZeroFillLength() << " bytes";
  else
    OS << ", content: " << (const void *)MRI.getContent().data() << " -- "
       << (const void *)(MRI.getContent().data() + MRI.getContent().size())
       << " (" << MRI.getContent().size() << " bytes)";

  return OS;
}

static raw_ostream &
operator<<(raw_ostream &OS, const Session::SymbolInfoMap &SIM) {
  OS << "Symbols:\n";
  for (auto &SKV : SIM)
    OS << "  \"" << SKV.first << "\" " << SKV.second << "\n";
  return OS;
}

static raw_ostream &
operator<<(raw_ostream &OS, const Session::FileInfo &FI) {
  for (auto &SIKV : FI.SectionInfos)
    OS << "  Section \"" << SIKV.first() << "\": " << SIKV.second << "\n";
  for (auto &GOTKV : FI.GOTEntryInfos)
    OS << "  GOT \"" << GOTKV.first() << "\": " << GOTKV.second << "\n";
  for (auto &StubKVs : FI.StubInfos) {
    OS << "  Stubs \"" << StubKVs.first() << "\":";
    for (auto MemRegion : StubKVs.second)
      OS << " " << MemRegion;
    OS << "\n";
  }
  return OS;
}

static raw_ostream &
operator<<(raw_ostream &OS, const Session::FileInfoMap &FIM) {
  for (auto &FIKV : FIM)
    OS << "File \"" << FIKV.first() << "\":\n" << FIKV.second;
  return OS;
}

bool lazyLinkingRequested(const JITLinkArgs &Args) {
  for (auto LL : Args.LazyLink)
    if (LL)
      return true;
  return false;
}

static Error applyLibraryLinkModifiers(Session &S, LinkGraph &G) {
  // If there are hidden archives and this graph is an archive
  // member then apply hidden modifier.
  if (!S.HiddenArchives.empty()) {
    StringRef ObjName(G.getName());
    if (ObjName.ends_with(')')) {
      auto LibName = ObjName.split('[').first;
      if (S.HiddenArchives.count(LibName)) {
        for (auto *Sym : G.defined_symbols())
          Sym->setScope(std::max(Sym->getScope(), Scope::Hidden));
      }
    }
  }

  return Error::success();
}

static Error applyHarnessPromotions(Session &S, LinkGraph &G) {
  std::lock_guard<std::mutex> Lock(S.M);

  // If this graph is part of the test harness there's nothing to do.
  if (S.HarnessFiles.empty() || S.HarnessFiles.count(G.getName()))
    return Error::success();

  LLVM_DEBUG(dbgs() << "Applying promotions to graph " << G.getName() << "\n");

  // If this graph is part of the test then promote any symbols referenced by
  // the harness to default scope, remove all symbols that clash with harness
  // definitions.
  std::vector<Symbol *> DefinitionsToRemove;
  for (auto *Sym : G.defined_symbols()) {

    if (!Sym->hasName())
      continue;

    if (Sym->getLinkage() == Linkage::Weak) {
      auto It = S.CanonicalWeakDefs.find(*Sym->getName());
      if (It == S.CanonicalWeakDefs.end() || It->second != G.getName()) {
        LLVM_DEBUG({
          dbgs() << "  Externalizing weak symbol " << Sym->getName() << "\n";
        });
        DefinitionsToRemove.push_back(Sym);
      } else {
        LLVM_DEBUG({
          dbgs() << "  Making weak symbol " << Sym->getName() << " strong\n";
        });
        if (S.HarnessExternals.count(*Sym->getName()))
          Sym->setScope(Scope::Default);
        else
          Sym->setScope(Scope::Hidden);
        Sym->setLinkage(Linkage::Strong);
      }
    } else if (S.HarnessExternals.count(*Sym->getName())) {
      LLVM_DEBUG(dbgs() << "  Promoting " << Sym->getName() << "\n");
      Sym->setScope(Scope::Default);
      Sym->setLive(true);
      continue;
    } else if (S.HarnessDefinitions.count(*Sym->getName())) {
      LLVM_DEBUG(dbgs() << "  Externalizing " << Sym->getName() << "\n");
      DefinitionsToRemove.push_back(Sym);
    }
  }

  for (auto *Sym : DefinitionsToRemove)
    G.makeExternal(*Sym);

  return Error::success();
}

static void dumpSectionContents(raw_ostream &OS, Session &S, LinkGraph &G) {
  std::lock_guard<std::mutex> Lock(S.M);

  outs() << "Relocated section contents for " << G.getName() << ":\n";

  constexpr orc::ExecutorAddrDiff DumpWidth = 16;
  static_assert(isPowerOf2_64(DumpWidth), "DumpWidth must be a power of two");

  // Put sections in address order.
  std::vector<Section *> Sections;
  for (auto &S : G.sections())
    Sections.push_back(&S);

  llvm::sort(Sections, [](const Section *LHS, const Section *RHS) {
    if (LHS->symbols().empty() && RHS->symbols().empty())
      return false;
    if (LHS->symbols().empty())
      return false;
    if (RHS->symbols().empty())
      return true;
    SectionRange LHSRange(*LHS);
    SectionRange RHSRange(*RHS);
    return LHSRange.getStart() < RHSRange.getStart();
  });

  for (auto *S : Sections) {
    OS << S->getName() << " content:";
    if (S->symbols().empty()) {
      OS << "\n  section empty\n";
      continue;
    }

    // Sort symbols into order, then render.
    std::vector<Symbol *> Syms(S->symbols().begin(), S->symbols().end());
    llvm::sort(Syms, [](const Symbol *LHS, const Symbol *RHS) {
      return LHS->getAddress() < RHS->getAddress();
    });

    orc::ExecutorAddr NextAddr(Syms.front()->getAddress().getValue() &
                               ~(DumpWidth - 1));
    for (auto *Sym : Syms) {
      bool IsZeroFill = Sym->getBlock().isZeroFill();
      auto SymStart = Sym->getAddress();
      auto SymSize = Sym->getSize();
      auto SymEnd = SymStart + SymSize;
      const uint8_t *SymData = IsZeroFill ? nullptr
                                          : reinterpret_cast<const uint8_t *>(
                                                Sym->getSymbolContent().data());

      // Pad any space before the symbol starts.
      while (NextAddr != SymStart) {
        if (NextAddr % DumpWidth == 0)
          OS << formatv("\n{0:x16}:", NextAddr);
        OS << "   ";
        ++NextAddr;
      }

      // Render the symbol content.
      while (NextAddr != SymEnd) {
        if (NextAddr % DumpWidth == 0)
          OS << formatv("\n{0:x16}:", NextAddr);
        if (IsZeroFill)
          OS << " 00";
        else
          OS << formatv(" {0:x-2}", SymData[NextAddr - SymStart]);
        ++NextAddr;
      }
    }
    OS << "\n";
  }
}

// A memory mapper with a fake offset applied only used for -noexec testing
class InProcessDeltaMapper final : public InProcessMemoryMapper {
public:
  InProcessDeltaMapper(size_t PageSize, uint64_t TargetAddr)
      : InProcessMemoryMapper(PageSize), TargetMapAddr(TargetAddr),
        DeltaAddr(0) {}

  static Expected<std::unique_ptr<InProcessDeltaMapper>>
  Create(uint64_t SlabPageSz, uint64_t SlabAddr) {
    size_t PageSize = SlabPageSz;
    if (!PageSize) {
      if (auto PageSizeOrErr = sys::Process::getPageSize())
        PageSize = *PageSizeOrErr;
      else
        return PageSizeOrErr.takeError();
    }

    if (PageSize == 0)
      return make_error<StringError>("Page size is zero",
                                     inconvertibleErrorCode());

    return std::make_unique<InProcessDeltaMapper>(PageSize, SlabAddr);
  }

  void reserve(size_t NumBytes, OnReservedFunction OnReserved) override {
    InProcessMemoryMapper::reserve(
        NumBytes, [this, OnReserved = std::move(OnReserved)](
                      Expected<ExecutorAddrRange> Result) mutable {
          if (!Result)
            return OnReserved(Result.takeError());

          assert(DeltaAddr == 0 && "Overwriting previous offset");
          if (TargetMapAddr != ~0ULL)
            DeltaAddr = TargetMapAddr - Result->Start.getValue();
          auto OffsetRange = ExecutorAddrRange(Result->Start + DeltaAddr,
                                               Result->End + DeltaAddr);

          OnReserved(OffsetRange);
        });
  }

  char *prepare(jitlink::LinkGraph &G, ExecutorAddr Addr,
                size_t ContentSize) override {
    return InProcessMemoryMapper::prepare(G, Addr - DeltaAddr, ContentSize);
  }

  void initialize(AllocInfo &AI, OnInitializedFunction OnInitialized) override {
    // Slide mapping based on delta, make all segments read-writable, and
    // discard allocation actions.
    auto FixedAI = std::move(AI);
    FixedAI.MappingBase -= DeltaAddr;
    for (auto &Seg : FixedAI.Segments)
      Seg.AG = {MemProt::Read | MemProt::Write, Seg.AG.getMemLifetime()};
    FixedAI.Actions.clear();
    InProcessMemoryMapper::initialize(
        FixedAI, [this, OnInitialized = std::move(OnInitialized)](
                     Expected<ExecutorAddr> Result) mutable {
          if (!Result)
            return OnInitialized(Result.takeError());

          OnInitialized(ExecutorAddr(Result->getValue() + DeltaAddr));
        });
  }

  void deinitialize(ArrayRef<ExecutorAddr> Allocations,
                    OnDeinitializedFunction OnDeInitialized) override {
    std::vector<ExecutorAddr> Addrs(Allocations.size());
    for (const auto Base : Allocations) {
      Addrs.push_back(Base - DeltaAddr);
    }

    InProcessMemoryMapper::deinitialize(Addrs, std::move(OnDeInitialized));
  }

  void release(ArrayRef<ExecutorAddr> Reservations,
               OnReleasedFunction OnRelease) override {
    std::vector<ExecutorAddr> Addrs(Reservations.size());
    for (const auto Base : Reservations) {
      Addrs.push_back(Base - DeltaAddr);
    }
    InProcessMemoryMapper::release(Addrs, std::move(OnRelease));
  }

private:
  uint64_t TargetMapAddr;
  uint64_t DeltaAddr;
};

Expected<uint64_t> getSlabAllocSize(StringRef SizeString) {
  SizeString = SizeString.trim();

  uint64_t Units = 1024;

  if (SizeString.ends_with_insensitive("kb"))
    SizeString = SizeString.drop_back(2).rtrim();
  else if (SizeString.ends_with_insensitive("mb")) {
    Units = 1024 * 1024;
    SizeString = SizeString.drop_back(2).rtrim();
  } else if (SizeString.ends_with_insensitive("gb")) {
    Units = 1024 * 1024 * 1024;
    SizeString = SizeString.drop_back(2).rtrim();
  }

  uint64_t SlabSize = 0;
  if (SizeString.getAsInteger(10, SlabSize))
    return make_error<StringError>("Invalid numeric format for slab size",
                                   inconvertibleErrorCode());

  return SlabSize * Units;
}

static std::unique_ptr<JITLinkMemoryManager>
createInProcessMemoryManager(const JITLinkArgs &Args) {
  uint64_t SlabSize;
#ifdef _WIN32
  SlabSize = 1024 * 1024;
#else
  SlabSize = 1024 * 1024 * 1024;
#endif

  if (!Args.SlabAllocateSizeString.empty())
    SlabSize = ExitOnErr(getSlabAllocSize(Args.SlabAllocateSizeString));

  // If this is a -no-exec case and we're tweaking the slab address or size then
  // use the delta mapper.
  if (Args.NoExec && (Args.SlabAddress || Args.SlabPageSize))
    return ExitOnErr(
        MapperJITLinkMemoryManager::CreateWithMapper<InProcessDeltaMapper>(
            SlabSize, Args.SlabPageSize, Args.SlabAddress));

  // Otherwise use the standard in-process mapper.
  return ExitOnErr(
      MapperJITLinkMemoryManager::CreateWithMapper<InProcessMemoryMapper>(
          SlabSize));
}

Expected<std::unique_ptr<jitlink::JITLinkMemoryManager>>
createSimpleRemoteMemoryManager(ExecutorProcessControl &EPC) {
  auto &ES = EPC.getExecutionSession();
  auto B = sps::createSimpleMemoryMapBindings(ES);
  if (!B)
    return B.takeError();
#ifdef _WIN32
  size_t SlabSize = 1024 * 1024;
#else
  size_t SlabSize = 1024 * 1024 * 1024;
#endif
  return MapperJITLinkMemoryManager::CreateWithMapper<SimpleRemoteMemoryMapper>(
      SlabSize, ES, std::move(*B));
}

Expected<std::unique_ptr<jitlink::JITLinkMemoryManager>>
createSharedMemoryManager(ExecutorProcessControl &EPC,
                          const JITLinkArgs &Args) {
  SharedMemoryMapper::SymbolAddrs SAs;
  if (auto Err = EPC.getBootstrapSymbols(
          {{SAs.Instance, rt::ExecutorSharedMemoryMapperServiceInstanceName},
           {SAs.Reserve,
            rt::ExecutorSharedMemoryMapperServiceReserveWrapperName},
           {SAs.Initialize,
            rt::ExecutorSharedMemoryMapperServiceInitializeWrapperName},
           {SAs.Deinitialize,
            rt::ExecutorSharedMemoryMapperServiceDeinitializeWrapperName},
           {SAs.Release,
            rt::ExecutorSharedMemoryMapperServiceReleaseWrapperName}}))
    return std::move(Err);

#ifdef _WIN32
  size_t SlabSize = 1024 * 1024;
#else
  size_t SlabSize = 1024 * 1024 * 1024;
#endif

  if (!Args.SlabAllocateSizeString.empty())
    SlabSize = ExitOnErr(getSlabAllocSize(Args.SlabAllocateSizeString));

  return MapperJITLinkMemoryManager::CreateWithMapper<SharedMemoryMapper>(
      SlabSize, EPC, SAs);
}

static Expected<std::unique_ptr<jitlink::JITLinkMemoryManager>>
createMemoryManager(ExecutorProcessControl &EPC, const JITLinkArgs &Args) {
  if (Args.OutOfProcessExecutorOccurrences ||
      Args.OutOfProcessExecutorConnectOccurrences) {

    switch (Args.UseMemMgr) {
    case MemMgr::Default:
    case MemMgr::Generic:
      return EPC.createDefaultMemoryManager();
    case MemMgr::SimpleRemote:
      return createSimpleRemoteMemoryManager(EPC);
    case MemMgr::Shared:
      return createSharedMemoryManager(EPC, Args);
    }
  }

  return createInProcessMemoryManager(Args);
}

static Expected<MaterializationUnit::Interface>
getTestObjectFileInterface(Session &S, MemoryBufferRef O) {

  // Get the standard interface for this object, but ignore the symbols field.
  // We'll handle that manually to include promotion.
  auto I = getObjectFileInterface(S.ES, O);
  if (!I)
    return I.takeError();
  I->SymbolFlags.clear();

  // If creating an object file was going to fail it would have happened above,
  // so we can 'cantFail' this.
  auto Obj = cantFail(object::ObjectFile::createObjectFile(O));

  // The init symbol must be included in the SymbolFlags map if present.
  if (I->InitSymbol)
    I->SymbolFlags[I->InitSymbol] =
        JITSymbolFlags::MaterializationSideEffectsOnly;

  for (auto &Sym : Obj->symbols()) {
    Expected<uint32_t> SymFlagsOrErr = Sym.getFlags();
    if (!SymFlagsOrErr)
      // TODO: Test this error.
      return SymFlagsOrErr.takeError();

    // Skip symbols not defined in this object file.
    if ((*SymFlagsOrErr & object::BasicSymbolRef::SF_Undefined))
      continue;

    auto Name = Sym.getName();
    if (!Name)
      return Name.takeError();

    // Skip symbols that have type SF_File.
    if (auto SymType = Sym.getType()) {
      if (*SymType == object::SymbolRef::ST_File)
        continue;
    } else
      return SymType.takeError();

    auto SymFlags = JITSymbolFlags::fromObjectSymbol(Sym);
    if (!SymFlags)
      return SymFlags.takeError();

    if (SymFlags->isWeak()) {
      // If this is a weak symbol that's not defined in the harness then we
      // need to either mark it as strong (if this is the first definition
      // that we've seen) or discard it.
      if (S.HarnessDefinitions.count(*Name) || S.CanonicalWeakDefs.count(*Name))
        continue;
      S.CanonicalWeakDefs[*Name] = O.getBufferIdentifier();
      *SymFlags &= ~JITSymbolFlags::Weak;
      if (!S.HarnessExternals.count(*Name))
        *SymFlags &= ~JITSymbolFlags::Exported;
    } else if (S.HarnessExternals.count(*Name)) {
      *SymFlags |= JITSymbolFlags::Exported;
    } else if (S.HarnessDefinitions.count(*Name) ||
               !(*SymFlagsOrErr & object::BasicSymbolRef::SF_Global))
      continue;

    I->SymbolFlags[S.ES.intern(*Name)] = std::move(*SymFlags);
  }

  return I;
}

static Error loadProcessSymbols(Session &S) {
  S.ProcessSymsJD = &S.ES.createBareJITDylib("Process");
  auto FilterMainEntryPoint =
      [EPName = S.ES.intern(S.Args.EntryPointName)](SymbolStringPtr Name) {
        return Name != EPName;
      };
  S.ProcessSymsJD->addGenerator(
      ExitOnErr(orc::EPCDynamicLibrarySearchGenerator::GetForTargetProcess(
          S.ES, *S.DylibMgr, std::move(FilterMainEntryPoint))));

  return Error::success();
}

static Error loadDylibs(Session &S) {
  LLVM_DEBUG(dbgs() << "Loading dylibs...\n");
  for (const auto &Dylib : S.Args.Dylibs) {
    LLVM_DEBUG(dbgs() << "  " << Dylib << "\n");
    auto DL = S.getOrLoadDynamicLibrary(Dylib);
    if (!DL)
      return DL.takeError();
  }

  return Error::success();
}

static Expected<std::unique_ptr<ExecutorProcessControl>>
launchExecutor(const JITLinkArgs &JLArgs) {
#ifndef LLVM_ON_UNIX
  // FIXME: Add support for Windows.
  return make_error<StringError>("-" + StringRef("oop-executor") +
                                     " not supported on non-unix platforms",
                                 inconvertibleErrorCode());
#elif !LLVM_ENABLE_THREADS
  // Out of process mode using SimpleRemoteEPC depends on threads.
  return make_error<StringError>(
      "-" + StringRef("oop-executor") +
          " requires threads, but LLVM was built with "
          "LLVM_ENABLE_THREADS=Off",
      inconvertibleErrorCode());
#else

  constexpr int ReadEnd = 0;
  constexpr int WriteEnd = 1;

  // Pipe FDs.
  int ToExecutor[2];
  int FromExecutor[2];

  pid_t ChildPID;

  // Create pipes to/from the executor..
  if (pipe(ToExecutor) != 0 || pipe(FromExecutor) != 0)
    return make_error<StringError>("Unable to create pipe for executor",
                                   inconvertibleErrorCode());

  ChildPID = fork();

  if (ChildPID == 0) {
    // In the child...

    // Close the parent ends of the pipes
    close(ToExecutor[WriteEnd]);
    close(FromExecutor[ReadEnd]);

    // Execute the child process.
    std::unique_ptr<char[]> ExecutorPath, FDSpecifier;
    {
      ExecutorPath =
          std::make_unique<char[]>(JLArgs.OutOfProcessExecutor.size() + 1);
      strcpy(ExecutorPath.get(), JLArgs.OutOfProcessExecutor.data());

      std::string FDSpecifierStr("filedescs=");
      FDSpecifierStr += utostr(ToExecutor[ReadEnd]);
      FDSpecifierStr += ',';
      FDSpecifierStr += utostr(FromExecutor[WriteEnd]);
      FDSpecifier = std::make_unique<char[]>(FDSpecifierStr.size() + 1);
      strcpy(FDSpecifier.get(), FDSpecifierStr.c_str());
    }

    char *const Args[] = {ExecutorPath.get(), FDSpecifier.get(), nullptr};
    int RC = execvp(ExecutorPath.get(), Args);
    if (RC != 0) {
      errs() << "unable to launch out-of-process executor \""
             << ExecutorPath.get() << "\"\n";
      exit(1);
    }
  }
  // else we're the parent...

  // Close the child ends of the pipes
  close(ToExecutor[ReadEnd]);
  close(FromExecutor[WriteEnd]);

  return SimpleRemoteEPC::Create<FDSimpleRemoteEPCTransport>(
      std::make_unique<DynamicThreadPoolTaskDispatcher>(
          JLArgs.MaterializationThreads),
      FromExecutor[ReadEnd], ToExecutor[WriteEnd]);
#endif
}

#if LLVM_ON_UNIX && LLVM_ENABLE_THREADS
static Error createTCPSocketError(StringRef OOPConnect, Twine Details) {
  return make_error<StringError>(
      formatv("Failed to connect TCP socket '{0}': {1}", OOPConnect, Details),
      inconvertibleErrorCode());
}

static Expected<int> connectTCPSocket(StringRef OOPConnect, std::string Host,
                                      std::string PortStr) {
  addrinfo *AI;
  addrinfo Hints{};
  Hints.ai_family = AF_INET;
  Hints.ai_socktype = SOCK_STREAM;
  Hints.ai_flags = AI_NUMERICSERV;

  if (int EC = getaddrinfo(Host.c_str(), PortStr.c_str(), &Hints, &AI))
    return createTCPSocketError(OOPConnect, "Address resolution failed (" +
                                                StringRef(gai_strerror(EC)) +
                                                ")");

  // Cycle through the returned addrinfo structures and connect to the first
  // reachable endpoint.
  int SockFD;
  addrinfo *Server;
  for (Server = AI; Server != nullptr; Server = Server->ai_next) {
    // socket might fail, e.g. if the address family is not supported. Skip to
    // the next addrinfo structure in such a case.
    if ((SockFD = socket(AI->ai_family, AI->ai_socktype, AI->ai_protocol)) < 0)
      continue;

    // If connect returns null, we exit the loop with a working socket.
    if (connect(SockFD, Server->ai_addr, Server->ai_addrlen) == 0)
      break;

    close(SockFD);
  }
  freeaddrinfo(AI);

  // If we reached the end of the loop without connecting to a valid endpoint,
  // dump the last error that was logged in socket() or connect().
  if (Server == nullptr)
    return createTCPSocketError(OOPConnect, std::strerror(errno));

  return SockFD;
}
#endif

static Expected<std::unique_ptr<ExecutorProcessControl>>
connectToExecutor(const JITLinkArgs &JLArgs) {
#ifndef LLVM_ON_UNIX
  // FIXME: Add TCP support for Windows.
  return make_error<StringError>("-" + StringRef("oop-executor-connect") +
                                     " not supported on non-unix platforms",
                                 inconvertibleErrorCode());
#elif !LLVM_ENABLE_THREADS
  // Out of process mode using SimpleRemoteEPC depends on threads.
  return make_error<StringError>(
      "-" + StringRef("oop-executor-connect") +
          " requires threads, but LLVM was built with "
          "LLVM_ENABLE_THREADS=Off",
      inconvertibleErrorCode());
#else

  StringRef Host, PortStr;
  std::tie(Host, PortStr) =
      StringRef(JLArgs.OutOfProcessExecutorConnect).split(':');
  if (Host.empty())
    return createTCPSocketError(JLArgs.OutOfProcessExecutorConnect,
                                "Host name for -" +
                                    StringRef("oop-executor-connect") +
                                    " can not be empty");
  if (PortStr.empty())
    return createTCPSocketError(JLArgs.OutOfProcessExecutorConnect,
                                "Port number in -" +
                                    StringRef("oop-executor-connect") +
                                    " can not be empty");
  int Port = 0;
  if (PortStr.getAsInteger(10, Port))
    return createTCPSocketError(JLArgs.OutOfProcessExecutorConnect,
                                "Port number '" + PortStr +
                                    "' is not a valid integer");

  Expected<int> SockFD = connectTCPSocket(JLArgs.OutOfProcessExecutorConnect,
                                          Host.str(), PortStr.str());
  if (!SockFD)
    return SockFD.takeError();

  return SimpleRemoteEPC::Create<FDSimpleRemoteEPCTransport>(
      std::make_unique<DynamicThreadPoolTaskDispatcher>(std::nullopt), *SockFD,
      *SockFD);
#endif
}

class PhonyExternalsGenerator : public DefinitionGenerator {
public:
  Error tryToGenerate(LookupState &LS, LookupKind K, JITDylib &JD,
                      JITDylibLookupFlags JDLookupFlags,
                      const SymbolLookupSet &LookupSet) override {
    SymbolMap PhonySymbols;
    for (auto &KV : LookupSet)
      PhonySymbols[KV.first] = {ExecutorAddr(), JITSymbolFlags::Exported};
    return JD.define(absoluteSymbols(std::move(PhonySymbols)));
  }
};

Expected<std::unique_ptr<Session::LazyLinkingSupport>>
createLazyLinkingSupport(Session &S) {
  auto MemAccess = S.ES.getExecutorProcessControl().createDefaultMemoryAccess();
  if (!MemAccess)
    return MemAccess.takeError();

  auto RSMgr =
      JITLinkRedirectableSymbolManager::Create(*S.ObjLayer, **MemAccess);
  if (!RSMgr)
    return RSMgr.takeError();

  std::shared_ptr<SimpleLazyReexportsSpeculator> Speculator;
  switch (S.Args.Speculate) {
  case SpeculateKind::None:
    break;
  case SpeculateKind::Simple:
    SimpleLazyReexportsSpeculator::RecordExecutionFunction RecordExecs;

    if (!S.Args.RecordLazyExecs.empty())
      RecordExecs = [&S](const LazyReexportsManager::CallThroughInfo &CTI) {
        S.LazyFnExecOrder.push_back({CTI.JD->getName(), CTI.BodyName});
      };

    Speculator =
        SimpleLazyReexportsSpeculator::Create(S.ES, std::move(RecordExecs));
    break;
  }

  auto LRMgr = createJITLinkLazyReexportsManager(
      *S.ObjLayer, **RSMgr, *S.PlatformJD, Speculator.get());
  if (!LRMgr)
    return LRMgr.takeError();

  return std::make_unique<Session::LazyLinkingSupport>(
      std::move(*MemAccess), std::move(*RSMgr), std::move(Speculator),
      std::move(*LRMgr), *S.ObjLayer);
}

static Error writeLazyExecOrder(Session &S) {
  if (S.Args.RecordLazyExecs.empty())
    return Error::success();

  std::error_code EC;
  raw_fd_ostream ExecOrderOut(S.Args.RecordLazyExecs, EC);
  if (EC)
    return createFileError(S.Args.RecordLazyExecs, EC);

  for (auto &[JDName, FunctionName] : S.LazyFnExecOrder)
    ExecOrderOut << JDName << ", " << FunctionName << "\n";

  return Error::success();
}

Expected<std::unique_ptr<Session>> Session::Create(Triple TT,
                                                   SubtargetFeatures Features,
                                                   const JITLinkArgs &Args) {

  std::unique_ptr<ExecutorProcessControl> EPC;
  if (Args.OutOfProcessExecutorOccurrences) {
    /// If -oop-executor is passed then launch the executor.
    if (auto REPC = launchExecutor(Args))
      EPC = std::move(*REPC);
    else
      return REPC.takeError();
  } else if (Args.OutOfProcessExecutorConnectOccurrences) {
    /// If -oop-executor-connect is passed then connect to the executor.
    if (auto REPC = connectToExecutor(Args))
      EPC = std::move(*REPC);
    else
      return REPC.takeError();
  } else {
    /// Otherwise use SelfExecutorProcessControl to target the current process.
    auto PageSize = sys::Process::getPageSize();
    if (!PageSize)
      return PageSize.takeError();
    std::unique_ptr<TaskDispatcher> Dispatcher;
    if (Args.MaterializationThreads == 0)
      Dispatcher = std::make_unique<InPlaceTaskDispatcher>();
    else {
#if LLVM_ENABLE_THREADS
      Dispatcher = std::make_unique<DynamicThreadPoolTaskDispatcher>(
          Args.MaterializationThreads);
#else
      llvm_unreachable("MaterializationThreads should be 0");
#endif
    }

    EPC = std::make_unique<SelfExecutorProcessControl>(
        std::make_shared<SymbolStringPool>(), std::move(Dispatcher),
        std::move(TT), *PageSize);
  }

  Error Err = Error::success();
  std::unique_ptr<Session> S(new Session(std::move(EPC), Args, Err));
  if (Err)
    return std::move(Err);
  S->Features = std::move(Features);

  if (lazyLinkingRequested(Args)) {
    if (auto LazyLinking = createLazyLinkingSupport(*S))
      S->LazyLinking = std::move(*LazyLinking);
    else
      return LazyLinking.takeError();
  }

  return std::move(S);
}

Session::~Session() {
  if (auto Err = writeLazyExecOrder(*this))
    ES.reportError(std::move(Err));

  if (auto Err = ES.endSession())
    ES.reportError(std::move(Err));
}

Session::Session(std::unique_ptr<ExecutorProcessControl> EPC,
                 const JITLinkArgs &Args, Error &Err)
    : ES(std::move(EPC)), Args(Args) {

  /// Local ObjectLinkingLayer::Plugin class to forward modifyPassConfig to the
  /// Session.
  class JITLinkSessionPlugin : public ObjectLinkingLayer::Plugin {
  public:
    JITLinkSessionPlugin(Session &S) : S(S) {}
    void modifyPassConfig(MaterializationResponsibility &MR, LinkGraph &G,
                          PassConfiguration &PassConfig) override {
      S.modifyPassConfig(G, PassConfig);
    }

    Error notifyFailed(MaterializationResponsibility &MR) override {
      return Error::success();
    }
    Error notifyRemovingResources(JITDylib &JD, ResourceKey K) override {
      return Error::success();
    }
    void notifyTransferringResources(JITDylib &JD, ResourceKey DstKey,
                                     ResourceKey SrcKey) override {}

  private:
    Session &S;
  };

  ErrorAsOutParameter _(&Err);

  if (auto MM = createMemoryManager(ES.getExecutorProcessControl(), Args)) {
    MemoryMgr = std::move(*MM);
    ObjLayer = std::make_unique<orc::ObjectLinkingLayer>(ES, *MemoryMgr);
  } else {
    Err = MM.takeError();
    return;
  }

  if (auto DM = ES.getExecutorProcessControl().createDefaultDylibMgr())
    DylibMgr = std::move(*DM);
  else {
    Err = DM.takeError();
    return;
  }

  ES.setErrorReporter([this](Error Err) {
    reportLLVMJITLinkError(std::move(Err),
                           this->Args.ShowErrFailedToMaterialize);
  });

  // Attach WaitingOnGraph recorder if requested.
  if (!Args.WaitingOnGraphCapture.empty()) {
    if (auto GRecorderOrErr =
            WaitingOnGraphOpRecorder::Create(Args.WaitingOnGraphCapture)) {
      GOpRecorder = std::move(*GRecorderOrErr);
      ES.setWaitingOnGraphOpRecorder(*GOpRecorder);
    } else {
      Err = GRecorderOrErr.takeError();
      return;
    }
  }

  if (!Args.NoProcessSymbols)
    ExitOnErr(loadProcessSymbols(*this));

  ExitOnErr(loadDylibs(*this));

  auto &TT = ES.getTargetTriple();

  if (!Args.WriteSymbolTableTo.empty()) {
    if (auto STDump = SymbolTableDumpPlugin::Create(Args.WriteSymbolTableTo))
      ObjLayer->addPlugin(std::move(*STDump));
    else {
      Err = STDump.takeError();
      return;
    }
  }

  if (Args.DebuggerSupport && TT.isOSBinFormatMachO()) {
    if (!ProcessSymsJD) {
      Err = make_error<StringError>("MachO debugging requires process symbols",
                                    inconvertibleErrorCode());
      return;
    }
    ObjLayer->addPlugin(ExitOnErr(GDBJITDebugInfoRegistrationPlugin::Create(
        this->ES, this->ES.getBootstrapJITDylib())));
  }

  if (Args.PerfSupport && TT.isOSBinFormatELF()) {
    if (!ProcessSymsJD) {
      Err = make_error<StringError>("MachO debugging requires process symbols",
                                    inconvertibleErrorCode());
      return;
    }
    ObjLayer->addPlugin(ExitOnErr(DebugInfoPreservationPlugin::Create()));
    ObjLayer->addPlugin(ExitOnErr(PerfSupportPlugin::Create(
        this->ES.getExecutorProcessControl(), *ProcessSymsJD, true, true)));
  }

  if (Args.VTuneSupport && TT.isOSBinFormatELF()) {
    ObjLayer->addPlugin(ExitOnErr(DebugInfoPreservationPlugin::Create()));
    ObjLayer->addPlugin(ExitOnErr(
        VTuneSupportPlugin::Create(this->ES.getExecutorProcessControl(),
                                   *ProcessSymsJD, /*EmitDebugInfo=*/true,
                                   /*TestMode=*/true)));
  }

  // Set up the platform.
  if (!Args.OrcRuntime.empty()) {
    assert(ProcessSymsJD && "ProcessSymsJD should have been set");
    PlatformJD = &ES.createBareJITDylib("Platform");
    PlatformJD->addToLinkOrder(*ProcessSymsJD);

    if (TT.isOSBinFormatMachO()) {
      if (auto P = MachOPlatform::Create(*ObjLayer, *PlatformJD,
                                         Args.OrcRuntime.c_str()))
        ES.setPlatform(std::move(*P));
      else {
        Err = P.takeError();
        return;
      }
    } else if (TT.isOSBinFormatELF()) {
      if (auto P = ELFNixPlatform::Create(*ObjLayer, *PlatformJD,
                                          Args.OrcRuntime.c_str()))
        ES.setPlatform(std::move(*P));
      else {
        Err = P.takeError();
        return;
      }
    } else if (TT.isOSBinFormatCOFF()) {
      auto LoadDynLibrary = [&, this](JITDylib &JD,
                                      StringRef DLLName) -> Error {
        if (!DLLName.ends_with_insensitive(".dll"))
          return make_error<StringError>("DLLName not ending with .dll",
                                         inconvertibleErrorCode());
        return loadAndLinkDynamicLibrary(JD, DLLName);
      };

      if (auto P = COFFPlatform::Create(*ObjLayer, *PlatformJD,
                                        Args.OrcRuntime.c_str(),
                                        std::move(LoadDynLibrary)))
        ES.setPlatform(std::move(*P));
      else {
        Err = P.takeError();
        return;
      }
    } else {
      Err = make_error<StringError>(
          "-orc-runtime specified, but format " +
              Triple::getObjectFormatTypeName(TT.getObjectFormat()) +
              " not supported",
          inconvertibleErrorCode());
      return;
    }
  } else if (TT.isOSBinFormatMachO()) {
    if (!Args.NoExec) {
      std::optional<bool> ForceEHFrames;
      if ((Err = ES.getBootstrapMapValue<bool, bool>("darwin-use-ehframes-only",
                                                     ForceEHFrames)))
        return;
      bool UseEHFrames = ForceEHFrames.value_or(false);
      if (!UseEHFrames)
        ObjLayer->addPlugin(
            ExitOnErr(UnwindInfoRegistrationPlugin::Create(ES)));
      else
        ObjLayer->addPlugin(ExitOnErr(EHFrameRegistrationPlugin::Create(ES)));
    }
  } else if (TT.isOSBinFormatELF()) {
    if (!Args.NoExec)
      ObjLayer->addPlugin(ExitOnErr(EHFrameRegistrationPlugin::Create(ES)));
    if (Args.DebuggerSupport) {
      Error TargetSymErr = Error::success();
      auto Plugin =
          std::make_unique<ELFDebugObjectPlugin>(ES, true, TargetSymErr);
      if (!TargetSymErr)
        ObjLayer->addPlugin(std::move(Plugin));
      else
        logAllUnhandledErrors(std::move(TargetSymErr), errs(),
                              "Debugger support not available: ");
    }
  }

  if (auto MainJDOrErr = ES.createJITDylib("main"))
    MainJD = &*MainJDOrErr;
  else {
    Err = MainJDOrErr.takeError();
    return;
  }

  if (Args.NoProcessSymbols) {
    // This symbol is used in testcases, but we're not reflecting process
    // symbols so we'll need to make it available some other way.
    auto &TestResultJD = ES.createBareJITDylib("<TestResultJD>");
    ExitOnErr(TestResultJD.define(absoluteSymbols(
        {{ES.intern("llvm_jitlink_setTestResultOverride"),
          {ExecutorAddr::fromPtr(llvm_jitlink_setTestResultOverride),
           JITSymbolFlags::Exported}}})));
    MainJD->addToLinkOrder(TestResultJD);
  }

  ObjLayer->addPlugin(std::make_unique<JITLinkSessionPlugin>(*this));

  // Process any harness files.
  for (auto &HarnessFile : Args.TestHarnesses) {
    HarnessFiles.insert(HarnessFile);

    auto ObjBuffer =
        ExitOnErr(loadLinkableFile(HarnessFile, ES.getTargetTriple(),
                                   LoadArchives::Never))
            .first;

    auto ObjInterface =
        ExitOnErr(getObjectFileInterface(ES, ObjBuffer->getMemBufferRef()));

    for (auto &KV : ObjInterface.SymbolFlags)
      HarnessDefinitions.insert(*KV.first);

    auto Obj = ExitOnErr(
        object::ObjectFile::createObjectFile(ObjBuffer->getMemBufferRef()));

    for (auto &Sym : Obj->symbols()) {
      uint32_t SymFlags = ExitOnErr(Sym.getFlags());
      auto Name = ExitOnErr(Sym.getName());

      if (Name.empty())
        continue;

      if (SymFlags & object::BasicSymbolRef::SF_Undefined)
        HarnessExternals.insert(Name);
    }
  }

  // If a name is defined by some harness file then it's a definition, not an
  // external.
  for (auto &DefName : HarnessDefinitions)
    HarnessExternals.erase(DefName.getKey());

  if (!Args.ShowLinkGraphs.empty())
    ShowGraphsRegex = Regex(Args.ShowLinkGraphs);
}

void Session::dumpSessionInfo(raw_ostream &OS) {
  OS << "Registered addresses:\n" << SymbolInfos << FileInfos;
}

void Session::modifyPassConfig(LinkGraph &G, PassConfiguration &PassConfig) {

  if (Args.ShowLinkedFiles)
    outs() << "Linking " << G.getName() << "\n";

  if (!Args.CheckFiles.empty() || Args.ShowAddrs)
    PassConfig.PostFixupPasses.push_back([this](LinkGraph &G) {
      if (ES.getTargetTriple().getObjectFormat() == Triple::ELF)
        return registerELFGraphInfo(*this, G);

      if (ES.getTargetTriple().getObjectFormat() == Triple::MachO)
        return registerMachOGraphInfo(*this, G);

      if (ES.getTargetTriple().getObjectFormat() == Triple::COFF)
        return registerCOFFGraphInfo(*this, G);

      return make_error<StringError>("Unsupported object format for GOT/stub "
                                     "registration",
                                     inconvertibleErrorCode());
    });

  if (ShowGraphsRegex)
    PassConfig.PostFixupPasses.push_back([this](LinkGraph &G) -> Error {
      std::lock_guard<std::mutex> Lock(M);
      // Print graph if ShowLinkGraphs is specified-but-empty, or if
      // it contains the given graph.
      if (ShowGraphsRegex->match(G.getName())) {
        outs() << "Link graph \"" << G.getName() << "\" post-fixup:\n";
        G.dump(outs());
      }
      return Error::success();
    });

  PassConfig.PrePrunePasses.push_back([this](LinkGraph &G) {
    std::lock_guard<std::mutex> Lock(M);
    ++ActiveLinks;
    return Error::success();
  });
  PassConfig.PrePrunePasses.push_back(
      [this](LinkGraph &G) { return applyLibraryLinkModifiers(*this, G); });
  PassConfig.PrePrunePasses.push_back(
      [this](LinkGraph &G) { return applyHarnessPromotions(*this, G); });

  if (Args.ShowRelocatedSectionContents)
    PassConfig.PostFixupPasses.push_back([this](LinkGraph &G) -> Error {
      dumpSectionContents(outs(), *this, G);
      return Error::success();
    });

  if (Args.AddSelfRelocations)
    PassConfig.PostPrunePasses.push_back(addSelfRelocations);

  PassConfig.PostFixupPasses.push_back([this](LinkGraph &G) {
    std::lock_guard<std::mutex> Lock(M);
    if (--ActiveLinks == 0)
      ActiveLinksCV.notify_all();
    return Error::success();
  });
}

Expected<JITDylib *> Session::getOrLoadDynamicLibrary(StringRef LibPath) {
  auto It = DynLibJDs.find(LibPath);
  if (It != DynLibJDs.end())
    return It->second;
  auto G =
      EPCDynamicLibrarySearchGenerator::Load(ES, *DylibMgr, LibPath.data());
  if (!G)
    return G.takeError();
  auto JD = &ES.createBareJITDylib(LibPath.str());

  JD->addGenerator(std::move(*G));
  DynLibJDs.emplace(LibPath.str(), JD);
  LLVM_DEBUG({
    dbgs() << "Loaded dynamic library " << LibPath.data() << " for " << LibPath
           << "\n";
  });
  return JD;
}

Error Session::loadAndLinkDynamicLibrary(JITDylib &JD, StringRef LibPath) {
  auto DL = getOrLoadDynamicLibrary(LibPath);
  if (!DL)
    return DL.takeError();
  JD.addToLinkOrder(**DL);
  LLVM_DEBUG({
    dbgs() << "Linking dynamic library " << LibPath << " to " << JD.getName()
           << "\n";
  });
  return Error::success();
}

Expected<JITDylib *> Session::getOrLoadAutoImportDLL(StringRef LibPath) {
  auto It = AutoImportJDs.find(LibPath);
  if (It != AutoImportJDs.end())
    return It->second;
  auto G = orc::COFFAutoImportGenerator::Load(ES, *ObjLayer, *DylibMgr,
                                              LibPath.data());
  if (!G)
    return G.takeError();
  auto JD = &ES.createBareJITDylib(LibPath.str());

  JD->addGenerator(std::move(*G));
  AutoImportJDs.emplace(LibPath.str(), JD);
  LLVM_DEBUG({
    dbgs() << "Loaded auto-import dynamic library " << LibPath.data() << " for "
           << LibPath << "\n";
  });
  return JD;
}

Error Session::loadAndLinkAutoImportDLL(JITDylib &JD, StringRef LibPath) {
  auto DL = getOrLoadAutoImportDLL(LibPath);
  if (!DL)
    return DL.takeError();
  JD.addToLinkOrder(**DL);
  LLVM_DEBUG({
    dbgs() << "Linking auto-import dynamic library " << LibPath << " to "
           << JD.getName() << "\n";
  });
  return Error::success();
}

Error Session::FileInfo::registerGOTEntry(
    LinkGraph &G, Symbol &Sym, GetSymbolTargetFunction GetSymbolTarget) {
  if (Sym.isSymbolZeroFill())
    return make_error<StringError>("Unexpected zero-fill symbol in section " +
                                       Sym.getBlock().getSection().getName(),
                                   inconvertibleErrorCode());
  auto TS = GetSymbolTarget(G, Sym.getBlock());
  if (!TS)
    return TS.takeError();
  GOTEntryInfos[*TS->getName()] = {Sym.getSymbolContent(),
                                   Sym.getAddress().getValue(),
                                   Sym.getTargetFlags()};
  return Error::success();
}

Error Session::FileInfo::registerStubEntry(
    LinkGraph &G, Symbol &Sym, GetSymbolTargetFunction GetSymbolTarget) {
  if (Sym.isSymbolZeroFill())
    return make_error<StringError>("Unexpected zero-fill symbol in section " +
                                       Sym.getBlock().getSection().getName(),
                                   inconvertibleErrorCode());
  auto TS = GetSymbolTarget(G, Sym.getBlock());
  if (!TS)
    return TS.takeError();

  SmallVectorImpl<MemoryRegionInfo> &Entry = StubInfos[*TS->getName()];
  Entry.insert(Entry.begin(),
               {Sym.getSymbolContent(), Sym.getAddress().getValue(),
                Sym.getTargetFlags()});
  return Error::success();
}

Error Session::FileInfo::registerMultiStubEntry(
    LinkGraph &G, Symbol &Sym, GetSymbolTargetFunction GetSymbolTarget) {
  if (Sym.isSymbolZeroFill())
    return make_error<StringError>("Unexpected zero-fill symbol in section " +
                                       Sym.getBlock().getSection().getName(),
                                   inconvertibleErrorCode());

  auto Target = GetSymbolTarget(G, Sym.getBlock());
  if (!Target)
    return Target.takeError();

  SmallVectorImpl<MemoryRegionInfo> &Entry = StubInfos[*Target->getName()];
  Entry.emplace_back(Sym.getSymbolContent(), Sym.getAddress().getValue(),
                     Sym.getTargetFlags());

  // Let's keep stubs ordered by ascending address.
  std::sort(Entry.begin(), Entry.end(),
            [](const MemoryRegionInfo &L, const MemoryRegionInfo &R) {
              return L.getTargetAddress() < R.getTargetAddress();
            });

  return Error::success();
}

Expected<Session::FileInfo &> Session::findFileInfo(StringRef FileName) {
  auto FileInfoItr = FileInfos.find(FileName);
  if (FileInfoItr == FileInfos.end())
    return make_error<StringError>("file \"" + FileName + "\" not recognized",
                                   inconvertibleErrorCode());
  return FileInfoItr->second;
}

Expected<Session::MemoryRegionInfo &>
Session::findSectionInfo(StringRef FileName, StringRef SectionName) {
  auto FI = findFileInfo(FileName);
  if (!FI)
    return FI.takeError();
  auto SecInfoItr = FI->SectionInfos.find(SectionName);
  if (SecInfoItr == FI->SectionInfos.end())
    return make_error<StringError>("no section \"" + SectionName +
                                       "\" registered for file \"" + FileName +
                                       "\"",
                                   inconvertibleErrorCode());
  return SecInfoItr->second;
}

class MemoryMatcher {
public:
  MemoryMatcher(ArrayRef<char> Content)
      : Pos(Content.data()), End(Pos + Content.size()) {}

  template <typename MaskType> bool matchMask(MaskType Mask) {
    if (Mask == (Mask & *reinterpret_cast<const MaskType *>(Pos))) {
      Pos += sizeof(MaskType);
      return true;
    }
    return false;
  }

  template <typename ValueType> bool matchEqual(ValueType Value) {
    if (Value == *reinterpret_cast<const ValueType *>(Pos)) {
      Pos += sizeof(ValueType);
      return true;
    }
    return false;
  }

  bool done() const { return Pos == End; }

private:
  const char *Pos;
  const char *End;
};

static StringRef detectStubKind(const Session::MemoryRegionInfo &Stub) {
  using namespace support::endian;
  auto Armv7MovWTle = byte_swap<uint32_t>(0xe300c000, endianness::little);
  auto Armv7BxR12le = byte_swap<uint32_t>(0xe12fff1c, endianness::little);
  auto Thumbv7MovWTle = byte_swap<uint32_t>(0x0c00f240, endianness::little);
  auto Thumbv7BxR12le = byte_swap<uint16_t>(0x4760, endianness::little);

  MemoryMatcher M(Stub.getContent());
  if (M.matchMask(Thumbv7MovWTle)) {
    if (M.matchMask(Thumbv7MovWTle))
      if (M.matchEqual(Thumbv7BxR12le))
        if (M.done())
          return "thumbv7_abs_le";
  } else if (M.matchMask(Armv7MovWTle)) {
    if (M.matchMask(Armv7MovWTle))
      if (M.matchEqual(Armv7BxR12le))
        if (M.done())
          return "armv7_abs_le";
  }
  return "";
}

Expected<Session::MemoryRegionInfo &>
Session::findStubInfo(StringRef FileName, StringRef TargetName,
                      StringRef KindNameFilter) {
  auto FI = findFileInfo(FileName);
  if (!FI)
    return FI.takeError();
  auto StubInfoItr = FI->StubInfos.find(TargetName);
  if (StubInfoItr == FI->StubInfos.end())
    return make_error<StringError>("no stub for \"" + TargetName +
                                       "\" registered for file \"" + FileName +
                                       "\"",
                                   inconvertibleErrorCode());
  auto &StubsForTarget = StubInfoItr->second;
  assert(!StubsForTarget.empty() && "At least 1 stub in each entry");
  if (KindNameFilter.empty() && StubsForTarget.size() == 1)
    return StubsForTarget[0]; // Regular single-stub match

  std::string KindsStr;
  SmallVector<MemoryRegionInfo *, 1> Matches;
  Regex KindNameMatcher(KindNameFilter.empty() ? ".*" : KindNameFilter);
  for (MemoryRegionInfo &Stub : StubsForTarget) {
    StringRef Kind = detectStubKind(Stub);
    if (KindNameMatcher.match(Kind))
      Matches.push_back(&Stub);
    KindsStr += "\"" + (Kind.empty() ? "<unknown>" : Kind.str()) + "\", ";
  }
  if (Matches.empty())
    return make_error<StringError>(
        "\"" + TargetName + "\" has " + Twine(StubsForTarget.size()) +
            " stubs in file \"" + FileName +
            "\", but none of them matches the stub-kind filter \"" +
            KindNameFilter + "\" (all encountered kinds are " +
            StringRef(KindsStr.data(), KindsStr.size() - 2) + ").",
        inconvertibleErrorCode());
  if (Matches.size() > 1)
    return make_error<StringError>(
        "\"" + TargetName + "\" has " + Twine(Matches.size()) +
            " candidate stubs in file \"" + FileName +
            "\". Please refine stub-kind filter \"" + KindNameFilter +
            "\" for disambiguation (encountered kinds are " +
            StringRef(KindsStr.data(), KindsStr.size() - 2) + ").",
        inconvertibleErrorCode());

  return *Matches[0];
}

Expected<Session::MemoryRegionInfo &>
Session::findGOTEntryInfo(StringRef FileName, StringRef TargetName) {
  auto FI = findFileInfo(FileName);
  if (!FI)
    return FI.takeError();
  auto GOTInfoItr = FI->GOTEntryInfos.find(TargetName);
  if (GOTInfoItr == FI->GOTEntryInfos.end())
    return make_error<StringError>("no GOT entry for \"" + TargetName +
                                       "\" registered for file \"" + FileName +
                                       "\"",
                                   inconvertibleErrorCode());
  return GOTInfoItr->second;
}

bool Session::isSymbolRegistered(const orc::SymbolStringPtr &SymbolName) {
  return SymbolInfos.count(SymbolName);
}

Expected<Session::MemoryRegionInfo &>
Session::findSymbolInfo(const orc::SymbolStringPtr &SymbolName,
                        Twine ErrorMsgStem) {
  auto SymInfoItr = SymbolInfos.find(SymbolName);
  if (SymInfoItr == SymbolInfos.end())
    return make_error<StringError>(ErrorMsgStem + ": symbol " + *SymbolName +
                                       " not found",
                                   inconvertibleErrorCode());
  return SymInfoItr->second;
}

} // end namespace llvm

static std::pair<Triple, SubtargetFeatures>
getFirstFileTripleAndFeatures(const JITLinkArgs &Args) {

  // If we're running in symbolicate mode then just use the process triple.
  if (!Args.SymbolicateWith.empty())
    return std::make_pair(Triple(sys::getProcessTriple()), SubtargetFeatures());

  // Otherwise we need to inspect the input files.
  assert(!Args.InputFiles.empty() && "InputFiles can not be empty");

  if (!Args.OverrideTriple.empty()) {
    LLVM_DEBUG({
      dbgs() << "Triple from -triple override: " << Args.OverrideTriple << "\n";
    });
    return std::make_pair(Triple(Args.OverrideTriple), SubtargetFeatures());
  }

  for (const auto &InputFile : Args.InputFiles) {
    auto ObjBuffer = ExitOnErr(getFile(InputFile));
    file_magic Magic = identify_magic(ObjBuffer->getBuffer());
    switch (Magic) {
    case file_magic::coff_object:
    case file_magic::elf_relocatable:
    case file_magic::macho_object: {
      auto Obj = ExitOnErr(
          object::ObjectFile::createObjectFile(ObjBuffer->getMemBufferRef()));
      Triple TT;
      if (auto *MachOObj = dyn_cast<object::MachOObjectFile>(Obj.get()))
        TT = MachOObj->getArchTriple();
      else
        TT = Obj->makeTriple();
      if (Magic == file_magic::coff_object) {
        // TODO: Move this to makeTriple() if possible.
        TT.setObjectFormat(Triple::COFF);
        TT.setOS(Triple::OSType::Win32);
      }
      SubtargetFeatures Features;
      if (auto ObjFeatures = Obj->getFeatures())
        Features = std::move(*ObjFeatures);

      LLVM_DEBUG({
        dbgs() << "Triple from " << InputFile << ": " << TT.str() << "\n";
      });
      return std::make_pair(TT, Features);
    }
    default:
      break;
    }
  }

  // If no plain object file inputs exist to pin down the triple then detect
  // the host triple and default to that.
  auto JTMB = ExitOnErr(JITTargetMachineBuilder::detectHost());
  LLVM_DEBUG({
    dbgs() << "Triple from host-detection: " << JTMB.getTargetTriple().str()
           << "\n";
  });
  return std::make_pair(JTMB.getTargetTriple(), JTMB.getFeatures());
}

static Error sanitizeArguments(JITLinkArgs &Args, const Triple &TT,
                               const char *ArgV0) {

  if (Args.InputFiles.empty())
    return make_error<StringError>(
        "Not enough positional command line arguments specified! (see "
        "llvm-jitlink --help)",
        inconvertibleErrorCode());

  // If we're in replay mode we should never get here.
  assert(Args.WaitingOnGraphReplay.empty());

  // -noexec and --args should not be used together.
  if (Args.NoExec && !Args.InputArgv.empty())
    errs() << "Warning: --args passed to -noexec run will be ignored.\n";

  // Set the entry point name if not specified.
  if (Args.EntryPointName.empty())
    Args.EntryPointName =
        TT.getObjectFormat() == Triple::MachO ? "_main" : "main";

  // Disable debugger support by default in noexec tests.
  if (Args.DebuggerSupportOccurrences == 0 && Args.NoExec)
    Args.DebuggerSupport = false;

  if (!Args.OrcRuntime.empty() && Args.NoProcessSymbols)
    return make_error<StringError>("-orc-runtime requires process symbols",
                                   inconvertibleErrorCode());

  // If -slab-allocate is passed, check that we're not trying to use it in
  // -oop-executor or -oop-executor-connect mode.
  //
  // FIXME: Remove once we enable remote slab allocation.
  if (Args.SlabAllocateSizeString != "") {
    if (Args.OutOfProcessExecutorOccurrences ||
        Args.OutOfProcessExecutorConnectOccurrences)
      return make_error<StringError>(
          "-slab-allocate cannot be used with -oop-executor or "
          "-oop-executor-connect",
          inconvertibleErrorCode());
  }

  // If -slab-address is passed, require -slab-allocate and -noexec
  if (Args.SlabAddress != ~0ULL) {
    if (Args.SlabAllocateSizeString == "" || !Args.NoExec)
      return make_error<StringError>(
          "-slab-address requires -slab-allocate and -noexec",
          inconvertibleErrorCode());

    if (Args.SlabPageSize == 0)
      errs() << "Warning: -slab-address used without -slab-page-size.\n";
  }

  if (Args.SlabPageSize != 0) {
    // -slab-page-size requires slab alloc.
    if (Args.SlabAllocateSizeString == "")
      return make_error<StringError>("-slab-page-size requires -slab-allocate",
                                     inconvertibleErrorCode());

    // Check -slab-page-size / -noexec interactions.
    if (!Args.NoExec) {
      if (auto RealPageSize = sys::Process::getPageSize()) {
        if (Args.SlabPageSize % *RealPageSize)
          return make_error<StringError>(
              "-slab-page-size must be a multiple of real page size for exec "
              "tests (did you mean to use -noexec ?)\n",
              inconvertibleErrorCode());
      } else {
        errs() << "Could not retrieve process page size:\n";
        logAllUnhandledErrors(RealPageSize.takeError(), errs(), "");
        errs() << "Executing with slab page size = "
               << formatv("{0:x}", Args.SlabPageSize) << ".\n"
               << "Tool may crash if " << formatv("{0:x}", Args.SlabPageSize)
               << " is not a multiple of the real process page size.\n"
               << "(did you mean to use -noexec ?)";
      }
    }
  }

#if LLVM_ENABLE_THREADS
  if (Args.MaterializationThreads == std::numeric_limits<size_t>::max()) {
    if (auto HC = std::thread::hardware_concurrency())
      Args.MaterializationThreads = HC;
    else {
      errs() << "Warning: std::thread::hardware_concurrency() returned 0, "
                "defaulting to -num-threads=1.\n";
      Args.MaterializationThreads = 1;
    }
  }
#else
  if (Args.MaterializationThreadsOccurrences &&
      Args.MaterializationThreads != 0) {
    errs() << "Warning: -num-threads was set, but LLVM was built with threads "
              "disabled. Resetting to -num-threads=0\n";
  }
  Args.MaterializationThreads = 0;
#endif

  if (!!Args.OutOfProcessExecutorOccurrences ||
      !!Args.OutOfProcessExecutorConnectOccurrences) {
    if (Args.NoExec)
      return make_error<StringError>("-noexec cannot be used with " +
                                         StringRef("oop-executor") + " or " +
                                         StringRef("oop-executor-connect"),
                                     inconvertibleErrorCode());

    if (Args.MaterializationThreads == 0)
      return make_error<StringError>("-threads=0 cannot be used with " +
                                         StringRef("oop-executor") + " or " +
                                         StringRef("oop-executor-connect"),
                                     inconvertibleErrorCode());
  }

#ifndef NDEBUG
  if (DebugFlag && Args.MaterializationThreads != 0)
    errs() << "Warning: debugging output is not thread safe. "
              "Use -num-threads=0 to stabilize output.\n";
#endif // NDEBUG

  // Only one of -oop-executor and -oop-executor-connect can be used.
  if (!!Args.OutOfProcessExecutorOccurrences &&
      !!Args.OutOfProcessExecutorConnectOccurrences)
    return make_error<StringError>(
        "Only one of -" + StringRef("oop-executor") + " and -" +
            StringRef("oop-executor-connect") + " can be specified",
        inconvertibleErrorCode());

  // If -oop-executor was used but no value was specified then use a sensible
  // default.
  if (!!Args.OutOfProcessExecutorOccurrences &&
      Args.OutOfProcessExecutor.empty()) {
    SmallString<256> OOPExecutorPath(sys::fs::getMainExecutable(
        ArgV0, reinterpret_cast<void *>(&sanitizeArguments)));
    sys::path::remove_filename(OOPExecutorPath);
    sys::path::append(OOPExecutorPath, "llvm-jitlink-executor");
    Args.OutOfProcessExecutor = OOPExecutorPath.str().str();
  }

  // If lazy linking is requested then check compatibility with other options.
  if (lazyLinkingRequested(Args)) {
    if (Args.OrcRuntime.empty())
      return make_error<StringError>("Lazy linking requries the ORC runtime",
                                     inconvertibleErrorCode());

    if (!Args.TestHarnesses.empty())
      return make_error<StringError>(
          "Lazy linking cannot be used with -harness mode",
          inconvertibleErrorCode());
  } else if (Args.Speculate != SpeculateKind::None) {
    errs() << "Warning: -speculate ignored as there are no -lazy inputs\n";
    Args.Speculate = SpeculateKind::None;
  }

  if (Args.Speculate == SpeculateKind::None) {
    if (!Args.SpeculateOrder.empty()) {
      errs() << "Warning: -speculate-order ignored because speculation is "
                "disabled\n";
      Args.SpeculateOrder = "";
    }

    if (!Args.RecordLazyExecs.empty()) {
      errs() << "Warning: -record-lazy-execs ignored because speculation is "
                "disabled\n";
      Args.RecordLazyExecs = "";
    }
  }

  if (!Args.SymbolicateWith.empty()) {
    if (!Args.WriteSymbolTableTo.empty())
      errs() << "write-symtab specified with "
             << "symbolicate-with, ignoring.";
    if (Args.InputFiles.empty())
      Args.InputFiles.push_back("-");
  }

  return Error::success();
}

static void addPhonyExternalsGenerator(Session &S) {
  S.MainJD->addGenerator(std::make_unique<PhonyExternalsGenerator>());
}

static Error createJITDylibs(Session &S,
                             std::map<unsigned, JITDylib *> &IdxToJD) {
  // First, set up JITDylibs.
  LLVM_DEBUG(dbgs() << "Creating JITDylibs...\n");
  {
    // Create a "main" JITLinkDylib.
    IdxToJD[0] = S.MainJD;
    S.JDSearchOrder.push_back({S.MainJD, JITDylibLookupFlags::MatchAllSymbols});
    LLVM_DEBUG(dbgs() << "  0: " << S.MainJD->getName() << "\n");

    // Add any extra JITDylibs from the command line.
    for (auto JDItr = S.Args.JITDylibs.begin(), JDEnd = S.Args.JITDylibs.end();
         JDItr != JDEnd; ++JDItr) {
      auto JD = S.ES.createJITDylib(*JDItr);
      if (!JD)
        return JD.takeError();
      unsigned JDIdx =
          S.Args.JITDylibPositions[JDItr - S.Args.JITDylibs.begin()];
      IdxToJD[JDIdx] = &*JD;
      S.JDSearchOrder.push_back({&*JD, JITDylibLookupFlags::MatchAllSymbols});
      LLVM_DEBUG(dbgs() << "  " << JDIdx << ": " << JD->getName() << "\n");
    }
  }

  if (S.PlatformJD)
    S.JDSearchOrder.push_back(
        {S.PlatformJD, JITDylibLookupFlags::MatchExportedSymbolsOnly});
  if (S.ProcessSymsJD)
    S.JDSearchOrder.push_back(
        {S.ProcessSymsJD, JITDylibLookupFlags::MatchExportedSymbolsOnly});

  LLVM_DEBUG({
    dbgs() << "Dylib search order is [ ";
    for (auto &KV : S.JDSearchOrder)
      dbgs() << KV.first->getName() << " ";
    dbgs() << "]\n";
  });

  return Error::success();
}

static Error addAbsoluteSymbols(Session &S,
                                const std::map<unsigned, JITDylib *> &IdxToJD) {
  // Define absolute symbols.
  LLVM_DEBUG(dbgs() << "Defining absolute symbols...\n");
  for (auto AbsDefItr = S.Args.AbsoluteDefs.begin(),
            AbsDefEnd = S.Args.AbsoluteDefs.end();
       AbsDefItr != AbsDefEnd; ++AbsDefItr) {
    unsigned AbsDefArgIdx =
        S.Args.AbsoluteDefPositions[AbsDefItr - S.Args.AbsoluteDefs.begin()];
    auto &JD = *std::prev(IdxToJD.lower_bound(AbsDefArgIdx))->second;

    StringRef AbsDefStmt = *AbsDefItr;
    size_t EqIdx = AbsDefStmt.find_first_of('=');
    if (EqIdx == StringRef::npos)
      return make_error<StringError>("Invalid absolute define \"" + AbsDefStmt +
                                     "\". Syntax: <name>=<addr>",
                                     inconvertibleErrorCode());
    StringRef Name = AbsDefStmt.substr(0, EqIdx).trim();
    StringRef AddrStr = AbsDefStmt.substr(EqIdx + 1).trim();

    uint64_t Addr;
    if (AddrStr.getAsInteger(0, Addr))
      return make_error<StringError>("Invalid address expression \"" + AddrStr +
                                         "\" in absolute symbol definition \"" +
                                         AbsDefStmt + "\"",
                                     inconvertibleErrorCode());
    ExecutorSymbolDef AbsDef(ExecutorAddr(Addr), JITSymbolFlags::Exported);
    auto InternedName = S.ES.intern(Name);
    if (auto Err = JD.define(absoluteSymbols({{InternedName, AbsDef}})))
      return Err;

    // Register the absolute symbol with the session symbol infos.
    S.SymbolInfos[std::move(InternedName)] =
      {ArrayRef<char>(), Addr, AbsDef.getFlags().getTargetFlags()};
  }

  return Error::success();
}

static Error addAliases(Session &S,
                        const std::map<unsigned, JITDylib *> &IdxToJD) {
  // Define absolute symbols.
  LLVM_DEBUG(dbgs() << "Defining aliases...\n");

  DenseMap<std::pair<JITDylib *, JITDylib *>, SymbolAliasMap> Reexports;
  for (auto AliasItr = S.Args.Aliases.begin(), AliasEnd = S.Args.Aliases.end();
       AliasItr != AliasEnd; ++AliasItr) {

    auto BadExpr = [&]() {
      return make_error<StringError>(
          "Invalid alias definition \"" + *AliasItr +
              "\". Syntax: [<dst-jd>:]<alias>=[<src-jd>:]<aliasee>",
          inconvertibleErrorCode());
    };

    auto GetJD = [&](StringRef JDName) -> Expected<JITDylib *> {
      if (JDName.empty()) {
        unsigned AliasArgIdx =
            S.Args.AliasPositions[AliasItr - S.Args.Aliases.begin()];
        return std::prev(IdxToJD.lower_bound(AliasArgIdx))->second;
      }

      auto *JD = S.ES.getJITDylibByName(JDName);
      if (!JD)
        return make_error<StringError>(StringRef("In alias definition \"") +
                                           *AliasItr + "\" no dylib named " +
                                           JDName,
                                       inconvertibleErrorCode());

      return JD;
    };

    {
      // First split on '=' to get alias and aliasee.
      StringRef AliasStmt = *AliasItr;
      auto [AliasExpr, AliaseeExpr] = AliasStmt.split('=');
      if (AliaseeExpr.empty())
        return BadExpr();

      auto [AliasJDName, Alias] = AliasExpr.split(':');
      if (Alias.empty())
        std::swap(AliasJDName, Alias);

      auto AliasJD = GetJD(AliasJDName);
      if (!AliasJD)
        return AliasJD.takeError();

      auto [AliaseeJDName, Aliasee] = AliaseeExpr.split(':');
      if (Aliasee.empty())
        std::swap(AliaseeJDName, Aliasee);

      if (AliaseeJDName.empty() && !AliasJDName.empty())
        AliaseeJDName = AliasJDName;
      auto AliaseeJD = GetJD(AliaseeJDName);
      if (!AliaseeJD)
        return AliaseeJD.takeError();

      Reexports[{*AliasJD, *AliaseeJD}][S.ES.intern(Alias)] = {
          S.ES.intern(Aliasee), JITSymbolFlags::Exported};
    }
  }

  for (auto &[JDs, AliasMap] : Reexports) {
    auto [DstJD, SrcJD] = JDs;
    if (auto Err = DstJD->define(reexports(*SrcJD, std::move(AliasMap))))
      return Err;
  }

  return Error::success();
}

static Error addSectCreates(Session &S,
                            const std::map<unsigned, JITDylib *> &IdxToJD) {
  for (auto SCItr = S.Args.SectCreate.begin(), SCEnd = S.Args.SectCreate.end();
       SCItr != SCEnd; ++SCItr) {

    unsigned SCArgIdx =
        S.Args.SectCreatePositions[SCItr - S.Args.SectCreate.begin()];
    auto &JD = *std::prev(IdxToJD.lower_bound(SCArgIdx))->second;

    StringRef SCArg(*SCItr);

    auto [SectAndFileName, ExtraSymbolsString] = SCArg.rsplit('@');
    auto [SectName, FileName] = SectAndFileName.rsplit(',');
    if (SectName.empty())
      return make_error<StringError>("In -sectcreate=" + SCArg +
                                         ", filename component cannot be empty",
                                     inconvertibleErrorCode());
    if (FileName.empty())
      return make_error<StringError>("In -sectcreate=" + SCArg +
                                         ", filename component cannot be empty",
                                     inconvertibleErrorCode());

    auto Content = getFile(FileName);
    if (!Content)
      return Content.takeError();

    SectCreateMaterializationUnit::ExtraSymbolsMap ExtraSymbols;
    while (!ExtraSymbolsString.empty()) {
      StringRef NextSymPair;
      std::tie(NextSymPair, ExtraSymbolsString) = ExtraSymbolsString.split(',');

      auto [Sym, OffsetString] = NextSymPair.split('=');
      size_t Offset;

      if (OffsetString.getAsInteger(0, Offset))
        return make_error<StringError>("In -sectcreate=" + SCArg + ", " +
                                           OffsetString +
                                           " is not a valid integer",
                                       inconvertibleErrorCode());

      ExtraSymbols[S.ES.intern(Sym)] = {JITSymbolFlags::Exported, Offset};
    }

    if (auto Err = JD.define(std::make_unique<SectCreateMaterializationUnit>(
            *S.ObjLayer, SectName.str(), MemProt::Read, 16, std::move(*Content),
            std::move(ExtraSymbols))))
      return Err;
  }

  return Error::success();
}

static Error addTestHarnesses(Session &S) {
  LLVM_DEBUG(dbgs() << "Adding test harness objects...\n");
  for (const auto &HarnessFile : S.Args.TestHarnesses) {
    LLVM_DEBUG(dbgs() << "  " << HarnessFile << "\n");
    auto Linkable = loadLinkableFile(HarnessFile, S.ES.getTargetTriple(),
                                     LoadArchives::Never);
    if (!Linkable)
      return Linkable.takeError();
    if (auto Err = S.ObjLayer->add(*S.MainJD, std::move(Linkable->first)))
      return Err;
  }
  return Error::success();
}

static Error addObjects(Session &S,
                        const std::map<unsigned, JITDylib *> &IdxToJD,
                        const DenseSet<unsigned> &LazyLinkIdxs) {

  // Load each object into the corresponding JITDylib..
  LLVM_DEBUG(dbgs() << "Adding objects...\n");
  for (auto InputFileItr = S.Args.InputFiles.begin(),
            InputFileEnd = S.Args.InputFiles.end();
       InputFileItr != InputFileEnd; ++InputFileItr) {
    unsigned InputFileArgIdx =
        S.Args.InputFilePositions[InputFileItr - S.Args.InputFiles.begin()];
    const std::string &InputFile = *InputFileItr;
    if (StringRef(InputFile).ends_with(".a") ||
        StringRef(InputFile).ends_with(".lib"))
      continue;
    auto &JD = *std::prev(IdxToJD.lower_bound(InputFileArgIdx))->second;
    bool AddLazy = LazyLinkIdxs.count(InputFileArgIdx);
    LLVM_DEBUG(dbgs() << "  " << InputFileArgIdx << ": \"" << InputFile << "\" "
                      << (AddLazy ? " (lazy-linked)" : "") << " to "
                      << JD.getName() << "\n";);
    auto ObjBuffer = loadLinkableFile(InputFile, S.ES.getTargetTriple(),
                                      LoadArchives::Never);
    if (!ObjBuffer)
      return ObjBuffer.takeError();

    if (S.HarnessFiles.empty()) {
      if (auto Err =
              S.getLinkLayer(AddLazy).add(JD, std::move(ObjBuffer->first)))
        return Err;
    } else {
      // We're in -harness mode. Use a custom interface for this
      // test object.
      auto ObjInterface =
          getTestObjectFileInterface(S, ObjBuffer->first->getMemBufferRef());
      if (!ObjInterface)
        return ObjInterface.takeError();

      if (auto Err = S.ObjLayer->add(JD, std::move(ObjBuffer->first),
                                     std::move(*ObjInterface)))
        return Err;
    }
  }

  return Error::success();
}

static Expected<MaterializationUnit::Interface>
getObjectFileInterfaceHidden(ExecutionSession &ES, MemoryBufferRef ObjBuffer) {
  auto I = getObjectFileInterface(ES, ObjBuffer);
  if (I) {
    for (auto &KV : I->SymbolFlags)
      KV.second &= ~JITSymbolFlags::Exported;
  }
  return I;
}

static SmallVector<StringRef, 5> getSearchPathsFromEnvVar(Session &S) {
  // FIXME: Handle EPC environment.
  SmallVector<StringRef, 5> PathVec;
  auto TT = S.ES.getTargetTriple();
  if (TT.isOSBinFormatCOFF())
    StringRef(getenv("PATH")).split(PathVec, ";");
  else if (TT.isOSBinFormatELF())
    StringRef(getenv("LD_LIBRARY_PATH")).split(PathVec, ":");

  return PathVec;
}

static Expected<std::unique_ptr<DefinitionGenerator>>
LoadLibraryWeak(Session &S, StringRef Path) {
  auto Symbols = getDylibInterface(S.ES, Path);
  if (!Symbols)
    return Symbols.takeError();

  return std::make_unique<EPCDynamicLibrarySearchGenerator>(
      S.ES, *S.DylibMgr,
      [Symbols = std::move(*Symbols)](const SymbolStringPtr &Sym) {
        return Symbols.count(Sym);
      });
}

static Error addLibraries(Session &S,
                          const std::map<unsigned, JITDylib *> &IdxToJD,
                          const DenseSet<unsigned> &LazyLinkIdxs) {

  // 1. Collect search paths for each JITDylib.
  DenseMap<const JITDylib *, SmallVector<StringRef, 2>> JDSearchPaths;

  for (auto LSPItr = S.Args.LibrarySearchPaths.begin(),
            LSPEnd = S.Args.LibrarySearchPaths.end();
       LSPItr != LSPEnd; ++LSPItr) {
    unsigned LibrarySearchPathIdx =
        S.Args.LibrarySearchPathPositions[LSPItr -
                                          S.Args.LibrarySearchPaths.begin()];
    auto &JD = *std::prev(IdxToJD.lower_bound(LibrarySearchPathIdx))->second;

    StringRef LibrarySearchPath = *LSPItr;
    if (sys::fs::get_file_type(LibrarySearchPath) !=
        sys::fs::file_type::directory_file)
      return make_error<StringError>("While linking " + JD.getName() + ", -L" +
                                         LibrarySearchPath +
                                         " does not point to a directory",
                                     inconvertibleErrorCode());

    JDSearchPaths[&JD].push_back(*LSPItr);
  }

  LLVM_DEBUG({
    if (!JDSearchPaths.empty())
      dbgs() << "Search paths:\n";
    for (auto &KV : JDSearchPaths) {
      dbgs() << "  " << KV.first->getName() << ": [";
      for (auto &LibSearchPath : KV.second)
        dbgs() << " \"" << LibSearchPath << "\"";
      dbgs() << " ]\n";
    }
  });

  // 2. Collect library loads
  struct LibraryLoad {
    std::string LibName;
    bool IsPath = false;
    unsigned Position;
    ArrayRef<StringRef> CandidateExtensions;
    enum { Standard, Hidden, Weak, Auto } Modifier;
  };

  // Queue to load library as in the order as it appears in the argument list.
  std::deque<LibraryLoad> LibraryLoadQueue;

  // Add archive files from the inputs to LibraryLoads.
  for (auto InputFileItr = S.Args.InputFiles.begin(),
            InputFileEnd = S.Args.InputFiles.end();
       InputFileItr != InputFileEnd; ++InputFileItr) {
    StringRef InputFile = *InputFileItr;
    if (!InputFile.ends_with(".a") && !InputFile.ends_with(".lib"))
      continue;
    LibraryLoad LL;
    LL.LibName = InputFile.str();
    LL.IsPath = true;
    LL.Position =
        S.Args.InputFilePositions[InputFileItr - S.Args.InputFiles.begin()];
    LL.CandidateExtensions = {};
    LL.Modifier = LibraryLoad::Standard;
    LibraryLoadQueue.push_back(std::move(LL));
  }

  // Add -load_hidden arguments to LibraryLoads.
  for (auto LibItr = S.Args.LoadHidden.begin(),
            LibEnd = S.Args.LoadHidden.end();
       LibItr != LibEnd; ++LibItr) {
    LibraryLoad LL;
    LL.LibName = *LibItr;
    LL.IsPath = true;
    LL.Position =
        S.Args.LoadHiddenPositions[LibItr - S.Args.LoadHidden.begin()];
    LL.CandidateExtensions = {};
    LL.Modifier = LibraryLoad::Hidden;
    LibraryLoadQueue.push_back(std::move(LL));
  }

  // Add -weak_library arguments to LibraryLoads.
  for (auto LibItr = S.Args.WeakLibraries.begin(),
            LibEnd = S.Args.WeakLibraries.end();
       LibItr != LibEnd; ++LibItr) {
    LibraryLoad LL;
    LL.LibName = *LibItr;
    LL.IsPath = true;
    LL.Position =
        S.Args.WeakLibraryPositions[LibItr - S.Args.WeakLibraries.begin()];
    LL.CandidateExtensions = {};
    LL.Modifier = LibraryLoad::Weak;
    LibraryLoadQueue.push_back(std::move(LL));
  }

  StringRef StandardExtensions[] = {".so", ".dylib", ".dll", ".a", ".lib"};
  StringRef DynLibExtensionsOnly[] = {".so", ".dylib", ".dll"};
  StringRef ArchiveExtensionsOnly[] = {".a", ".lib"};
  StringRef WeakLinkExtensionsOnly[] = {".dylib", ".tbd"};

  // Add -lx arguments to LibraryLoads.
  for (auto LibItr = S.Args.Libraries.begin(), LibEnd = S.Args.Libraries.end();
       LibItr != LibEnd; ++LibItr) {
    LibraryLoad LL;
    LL.LibName = *LibItr;
    LL.Position = S.Args.LibraryPositions[LibItr - S.Args.Libraries.begin()];
    LL.CandidateExtensions = StandardExtensions;
    LL.Modifier = LibraryLoad::Standard;
    LibraryLoadQueue.push_back(std::move(LL));
  }

  // Add -hidden-lx arguments to LibraryLoads.
  for (auto LibHiddenItr = S.Args.LibrariesHidden.begin(),
            LibHiddenEnd = S.Args.LibrariesHidden.end();
       LibHiddenItr != LibHiddenEnd; ++LibHiddenItr) {
    LibraryLoad LL;
    LL.LibName = *LibHiddenItr;
    LL.Position =
        S.Args.LibrariesHiddenPositions[LibHiddenItr -
                                        S.Args.LibrariesHidden.begin()];
    LL.CandidateExtensions = ArchiveExtensionsOnly;
    LL.Modifier = LibraryLoad::Hidden;
    LibraryLoadQueue.push_back(std::move(LL));
  }

  // Add -weak-lx arguments to LibraryLoads.
  for (auto LibWeakItr = S.Args.LibrariesWeak.begin(),
            LibWeakEnd = S.Args.LibrariesWeak.end();
       LibWeakItr != LibWeakEnd; ++LibWeakItr) {
    LibraryLoad LL;
    LL.LibName = *LibWeakItr;
    LL.Position =
        S.Args
            .LibrariesWeakPositions[LibWeakItr - S.Args.LibrariesWeak.begin()];
    LL.CandidateExtensions = WeakLinkExtensionsOnly;
    LL.Modifier = LibraryLoad::Weak;
    LibraryLoadQueue.push_back(std::move(LL));
  }

  // Add -auto-lx arguments to LibraryLoads.
  for (auto LibAutoItr = S.Args.LibrariesAuto.begin(),
            LibAutoEnd = S.Args.LibrariesAuto.end();
       LibAutoItr != LibAutoEnd; ++LibAutoItr) {
    LibraryLoad LL;
    LL.LibName = *LibAutoItr;
    LL.Position =
        S.Args
            .LibrariesAutoPositions[LibAutoItr - S.Args.LibrariesAuto.begin()];
    LL.CandidateExtensions = DynLibExtensionsOnly;
    LL.Modifier = LibraryLoad::Auto;
    LibraryLoadQueue.push_back(std::move(LL));
  }

  // Sort library loads by position in the argument list.
  llvm::sort(LibraryLoadQueue,
             [](const LibraryLoad &LHS, const LibraryLoad &RHS) {
               return LHS.Position < RHS.Position;
             });

  // 3. Process library loads.
  auto AddArchive = [&](JITDylib &JD, const char *Path, const LibraryLoad &LL)
      -> Expected<std::unique_ptr<StaticLibraryDefinitionGenerator>> {
    StaticLibraryDefinitionGenerator::GetObjectFileInterface
        GetObjFileInterface;
    switch (LL.Modifier) {
    case LibraryLoad::Standard:
      GetObjFileInterface = getObjectFileInterface;
      break;
    case LibraryLoad::Hidden:
      GetObjFileInterface = getObjectFileInterfaceHidden;
      S.HiddenArchives.insert(Path);
      break;
    case LibraryLoad::Weak:
    case LibraryLoad::Auto:
      llvm_unreachable("Unsupported");
      break;
    }

    auto &LinkLayer = S.getLinkLayer(LazyLinkIdxs.count(LL.Position));

    std::set<std::string> ImportedDynamicLibraries;
    StaticLibraryDefinitionGenerator::VisitMembersFunction VisitMembers;

    // COFF gets special handling due to import libraries.
    if (S.ES.getTargetTriple().isOSBinFormatCOFF()) {
      if (S.Args.AllLoad) {
        VisitMembers =
            [ImportScanner = COFFImportFileScanner(ImportedDynamicLibraries),
             LoadAll =
                 StaticLibraryDefinitionGenerator::loadAllObjectFileMembers(
                     LinkLayer, JD)](object::Archive &A,
                                     MemoryBufferRef MemberBuf,
                                     size_t Index) mutable -> Expected<bool> {
          if (!ImportScanner(A, MemberBuf, Index))
            return false;
          return LoadAll(A, MemberBuf, Index);
        };
      } else
        VisitMembers = COFFImportFileScanner(ImportedDynamicLibraries);
    } else if (S.Args.AllLoad)
      VisitMembers = StaticLibraryDefinitionGenerator::loadAllObjectFileMembers(
          LinkLayer, JD);
    else if (S.ES.getTargetTriple().isOSBinFormatMachO() &&
             S.Args.ForceLoadObjC)
      VisitMembers = ForceLoadMachOArchiveMembers(LinkLayer, JD, true);

    auto G = StaticLibraryDefinitionGenerator::Load(
        LinkLayer, Path, std::move(VisitMembers),
        std::move(GetObjFileInterface));
    if (!G)
      return G.takeError();

    // Push additional dynamic libraries to search.
    // Note that this mechanism only happens in COFF.
    for (auto FileName : ImportedDynamicLibraries) {
      LibraryLoad NewLL;
      auto FileNameRef = StringRef(FileName);
      if (!FileNameRef.ends_with_insensitive(".dll"))
        return make_error<StringError>(
            "COFF Imported library not ending with dll extension?",
            inconvertibleErrorCode());
      NewLL.LibName = FileNameRef.drop_back(strlen(".dll")).str();
      NewLL.Position = LL.Position;
      NewLL.CandidateExtensions = DynLibExtensionsOnly;
      NewLL.Modifier = LibraryLoad::Standard;
      LibraryLoadQueue.push_front(std::move(NewLL));
    }
    return G;
  };

  SmallVector<StringRef, 5> SystemSearchPaths;
  if (S.Args.SearchSystemLibrary)
    SystemSearchPaths = getSearchPathsFromEnvVar(S);
  while (!LibraryLoadQueue.empty()) {
    bool LibFound = false;
    auto LL = LibraryLoadQueue.front();
    LibraryLoadQueue.pop_front();
    auto &JD = *std::prev(IdxToJD.lower_bound(LL.Position))->second;

    // If this is the name of a JITDylib then link against that.
    if (auto *LJD = S.ES.getJITDylibByName(LL.LibName)) {
      if (LL.Modifier == LibraryLoad::Weak)
        return make_error<StringError>(
            "Can't use -weak-lx or -weak_library to load JITDylib " +
                LL.LibName,
            inconvertibleErrorCode());
      if (LL.Modifier == LibraryLoad::Auto)
        return make_error<StringError>("Can't use -auto-lx to load JITDylib " +
                                           LL.LibName,
                                       inconvertibleErrorCode());
      JD.addToLinkOrder(*LJD);
      continue;
    }

    if (LL.IsPath) {
      // Must be -weak_library.
      if (LL.Modifier == LibraryLoad::Weak) {
        if (auto G = LoadLibraryWeak(S, LL.LibName)) {
          JD.addGenerator(std::move(*G));
          continue;
        } else
          return G.takeError();
      }

      // Otherwise handle archive.
      auto G = AddArchive(JD, LL.LibName.c_str(), LL);
      if (!G)
        return createFileError(LL.LibName, G.takeError());
      JD.addGenerator(std::move(*G));
      LLVM_DEBUG({
        dbgs() << "Adding generator for static library " << LL.LibName << " to "
               << JD.getName() << "\n";
      });
      continue;
    }

    // Otherwise look through the search paths.
    auto CurJDSearchPaths = JDSearchPaths[&JD];
    for (StringRef SearchPath :
         concat<StringRef>(CurJDSearchPaths, SystemSearchPaths)) {
      for (auto LibExt : LL.CandidateExtensions) {
        SmallVector<char, 256> LibPath;
        LibPath.reserve(SearchPath.size() + strlen("lib") + LL.LibName.size() +
                        LibExt.size() + 2); // +2 for pathsep, null term.
        llvm::append_range(LibPath, SearchPath);
        if (LibExt != ".lib" && LibExt != ".dll")
          sys::path::append(LibPath, "lib" + LL.LibName + LibExt);
        else
          sys::path::append(LibPath, LL.LibName + LibExt);
        LibPath.push_back('\0');

        // Skip missing or non-regular paths.
        if (sys::fs::get_file_type(LibPath.data()) !=
            sys::fs::file_type::regular_file) {
          continue;
        }

        file_magic Magic;
        if (auto EC = identify_magic(LibPath, Magic)) {
          // If there was an error loading the file then skip it.
          LLVM_DEBUG({
            dbgs() << "Library search found \"" << LibPath
                   << "\", but could not identify file type (" << EC.message()
                   << "). Skipping.\n";
          });
          continue;
        }

        // We identified the magic. Assume that we can load it -- we'll reset
        // in the default case.
        LibFound = true;
        switch (Magic) {
        case file_magic::pecoff_executable:
        case file_magic::elf_shared_object:
        case file_magic::macho_dynamically_linked_shared_lib: {
          if (LL.Modifier == LibraryLoad::Weak) {
            if (auto G = LoadLibraryWeak(S, LibPath.data()))
              JD.addGenerator(std::move(*G));
            else
              return G.takeError();
          } else if (LL.Modifier == LibraryLoad::Auto) {
            if (auto Err = S.loadAndLinkAutoImportDLL(JD, LibPath.data()))
              return Err;
          } else {
            if (auto Err = S.loadAndLinkDynamicLibrary(JD, LibPath.data()))
              return Err;
          }
          break;
        }
        case file_magic::archive:
        case file_magic::macho_universal_binary: {
          auto G = AddArchive(JD, LibPath.data(), LL);
          if (!G)
            return G.takeError();
          JD.addGenerator(std::move(*G));
          LLVM_DEBUG({
            dbgs() << "Adding generator for static library " << LibPath.data()
                   << " to " << JD.getName() << "\n";
          });
          break;
        }
        case file_magic::tapi_file:
          assert(LL.Modifier == LibraryLoad::Weak &&
                 "TextAPI file not being loaded as weak?");
          if (auto G = LoadLibraryWeak(S, LibPath.data()))
            JD.addGenerator(std::move(*G));
          else
            return G.takeError();
          break;
        default:
          // This file isn't a recognized library kind.
          LLVM_DEBUG({
            dbgs() << "Library search found \"" << LibPath
                   << "\", but file type is not supported. Skipping.\n";
          });
          LibFound = false;
          break;
        }
        if (LibFound)
          break;
      }
      if (LibFound)
        break;
    }

    if (!LibFound)
      return make_error<StringError>("While linking " + JD.getName() +
                                         ", could not find library for -l" +
                                         LL.LibName,
                                     inconvertibleErrorCode());
  }

  // Add platform and process symbols if available.
  for (auto &[Idx, JD] : IdxToJD) {
    if (S.PlatformJD)
      JD->addToLinkOrder(*S.PlatformJD);
    if (S.ProcessSymsJD)
      JD->addToLinkOrder(*S.ProcessSymsJD);
  }

  return Error::success();
}

static Error addSpeculationOrder(Session &S) {

  if (S.Args.SpeculateOrder.empty())
    return Error::success();

  assert(S.LazyLinking && "SpeculateOrder set, but lazy linking not enabled");
  assert(S.LazyLinking->Speculator && "SpeculatoOrder set, but no speculator");

  auto SpecOrderBuffer = getFile(S.Args.SpeculateOrder);
  if (!SpecOrderBuffer)
    return SpecOrderBuffer.takeError();

  StringRef LineStream((*SpecOrderBuffer)->getBuffer());
  std::vector<std::pair<std::string, SymbolStringPtr>> SpecOrder;

  size_t LineNumber = 0;
  while (!LineStream.empty()) {
    ++LineNumber;

    auto MakeSpecOrderErr = [&](StringRef Reason) {
      return make_error<StringError>("Error in speculation order file \"" +
                                         S.Args.SpeculateOrder + "\" on line " +
                                         Twine(LineNumber) + ": " + Reason,
                                     inconvertibleErrorCode());
    };

    StringRef CurLine;
    std::tie(CurLine, LineStream) = LineStream.split('\n');
    CurLine = CurLine.trim();
    if (CurLine.empty())
      continue;

    auto [JDName, FuncName] = CurLine.split(',');

    if (FuncName.empty())
      return MakeSpecOrderErr("missing ',' separator");

    JDName = JDName.trim();
    if (JDName.empty())
      return MakeSpecOrderErr("no value for column 1 (JIT Dylib name)");

    FuncName = FuncName.trim();
    if (FuncName.empty())
      return MakeSpecOrderErr("no value for column 2 (function name)");

    SpecOrder.push_back({JDName.str(), S.ES.intern(FuncName)});
  }

  S.LazyLinking->Speculator->addSpeculationSuggestions(std::move(SpecOrder));

  return Error::success();
}

static Error addSessionInputs(Session &S) {
  std::map<unsigned, JITDylib *> IdxToJD;
  DenseSet<unsigned> LazyLinkIdxs;

  for (auto LLItr = S.Args.LazyLink.begin(), LLEnd = S.Args.LazyLink.end();
       LLItr != LLEnd; ++LLItr) {
    if (*LLItr)
      LazyLinkIdxs.insert(
          S.Args.LazyLinkPositions[LLItr - S.Args.LazyLink.begin()] + 1);
  }

  if (auto Err = createJITDylibs(S, IdxToJD))
    return Err;

  if (auto Err = addAbsoluteSymbols(S, IdxToJD))
    return Err;

  if (auto Err = addAliases(S, IdxToJD))
    return Err;

  if (auto Err = addSectCreates(S, IdxToJD))
    return Err;

  if (!S.Args.TestHarnesses.empty())
    if (auto Err = addTestHarnesses(S))
      return Err;

  if (auto Err = addObjects(S, IdxToJD, LazyLinkIdxs))
    return Err;

  if (auto Err = addLibraries(S, IdxToJD, LazyLinkIdxs))
    return Err;

  if (auto Err = addSpeculationOrder(S))
    return Err;

  return Error::success();
}

namespace {
struct TargetInfo {
  const Target *TheTarget;
  std::unique_ptr<MCSubtargetInfo> STI;
  std::unique_ptr<MCRegisterInfo> MRI;
  std::unique_ptr<MCAsmInfo> MAI;
  std::unique_ptr<MCContext> Ctx;
  std::unique_ptr<MCDisassembler> Disassembler;
  std::unique_ptr<MCInstrInfo> MII;
  std::unique_ptr<MCInstrAnalysis> MIA;
  std::unique_ptr<MCInstPrinter> InstPrinter;
};
} // anonymous namespace

static TargetInfo
getTargetInfo(const Triple &TT,
              const SubtargetFeatures &TF = SubtargetFeatures()) {
  std::string ErrorStr;
  const Target *TheTarget = TargetRegistry::lookupTarget(TT, ErrorStr);
  if (!TheTarget)
    ExitOnErr(make_error<StringError>("Error accessing target '" + TT.str() +
                                          "': " + ErrorStr,
                                      inconvertibleErrorCode()));

  std::unique_ptr<MCSubtargetInfo> STI(TheTarget->createMCSubtargetInfo(
      TT, "", TF.getString(),
      /*Ctx=*/llvm::clv2::defaultOptionsContext()));
  if (!STI)
    ExitOnErr(
        make_error<StringError>("Unable to create subtarget for " + TT.str(),
                                inconvertibleErrorCode()));

  std::unique_ptr<MCRegisterInfo> MRI(TheTarget->createMCRegInfo(TT));
  if (!MRI)
    ExitOnErr(make_error<StringError>("Unable to create target register info "
                                      "for " +
                                          TT.str(),
                                      inconvertibleErrorCode()));

  MCTargetOptions MCOptions;
  std::unique_ptr<MCAsmInfo> MAI(
      TheTarget->createMCAsmInfo(*MRI, TT, MCOptions));
  if (!MAI)
    ExitOnErr(
        make_error<StringError>("Unable to create target asm info " + TT.str(),
                                inconvertibleErrorCode()));

  auto Ctx = std::make_unique<MCContext>(Triple(TT.str()), *MAI, *MRI, *STI);

  std::unique_ptr<MCDisassembler> Disassembler(
      TheTarget->createMCDisassembler(*STI, *Ctx));
  if (!Disassembler)
    ExitOnErr(
        make_error<StringError>("Unable to create disassembler for " + TT.str(),
                                inconvertibleErrorCode()));

  std::unique_ptr<MCInstrInfo> MII(TheTarget->createMCInstrInfo());
  if (!MII)
    ExitOnErr(make_error<StringError>("Unable to create instruction info for" +
                                          TT.str(),
                                      inconvertibleErrorCode()));

  std::unique_ptr<MCInstrAnalysis> MIA(
      TheTarget->createMCInstrAnalysis(MII.get()));
  if (!MIA)
    ExitOnErr(make_error<StringError>(
        "Unable to create instruction analysis for" + TT.str(),
        inconvertibleErrorCode()));

  std::unique_ptr<MCInstPrinter> InstPrinter(
      TheTarget->createMCInstPrinter(Triple(TT.str()), 0, *MAI, *MII, *MRI));
  if (!InstPrinter)
    ExitOnErr(make_error<StringError>(
        "Unable to create instruction printer for" + TT.str(),
        inconvertibleErrorCode()));
  return {TheTarget,      std::move(STI), std::move(MRI),
          std::move(MAI), std::move(Ctx), std::move(Disassembler),
          std::move(MII), std::move(MIA), std::move(InstPrinter)};
}
static Error runChecks(Session &S, Triple TT, SubtargetFeatures Features) {
  if (S.Args.CheckFiles.empty())
    return Error::success();

  S.waitForFilesLinkedFromEntryPointFile();

  LLVM_DEBUG(dbgs() << "Running checks...\n");

  auto IsSymbolValid = [&S](StringRef Symbol) {
    auto InternedSymbol = S.ES.intern(Symbol);
    return S.isSymbolRegistered(InternedSymbol);
  };

  auto GetSymbolInfo = [&S](StringRef Symbol) {
    auto InternedSymbol = S.ES.intern(Symbol);
    return S.findSymbolInfo(InternedSymbol, "Can not get symbol info");
  };

  auto GetSectionInfo = [&S](StringRef FileName, StringRef SectionName) {
    return S.findSectionInfo(FileName, SectionName);
  };

  auto GetStubInfo = [&S](StringRef FileName, StringRef SectionName,
                          StringRef KindNameFilter) {
    return S.findStubInfo(FileName, SectionName, KindNameFilter);
  };

  auto GetGOTInfo = [&S](StringRef FileName, StringRef SectionName) {
    return S.findGOTEntryInfo(FileName, SectionName);
  };

  RuntimeDyldChecker Checker(
      IsSymbolValid, GetSymbolInfo, GetSectionInfo, GetStubInfo, GetGOTInfo,
      S.ES.getTargetTriple().isLittleEndian() ? llvm::endianness::little
                                              : llvm::endianness::big,
      TT, StringRef(), Features, dbgs());

  std::string CheckLineStart = "# " + S.Args.CheckName + ":";
  for (auto &CheckFile : S.Args.CheckFiles) {
    auto CheckerFileBuf = ExitOnErr(getFile(CheckFile));
    if (!Checker.checkAllRulesInBuffer(CheckLineStart, &*CheckerFileBuf))
      ExitOnErr(make_error<StringError>(
          "Some checks in " + CheckFile + " failed", inconvertibleErrorCode()));
  }

  return Error::success();
}

static Error addSelfRelocations(LinkGraph &G) {
  auto TI = getTargetInfo(G.getTargetTriple());
  for (auto *Sym : G.defined_symbols())
    if (Sym->isCallable())
      if (auto Err = addFunctionPointerRelocationsToCurrentSymbol(
              *Sym, G, *TI.Disassembler, *TI.MIA))
        return Err;
  return Error::success();
}

static Expected<ExecutorSymbolDef> getMainEntryPoint(Session &S) {
  return S.ES.lookup(S.JDSearchOrder, S.ES.intern(S.Args.EntryPointName));
}

static Expected<ExecutorSymbolDef> getOrcRuntimeEntryPoint(Session &S) {
  std::string RuntimeEntryPoint = "__orc_rt_run_program_wrapper";
  if (S.ES.getTargetTriple().getObjectFormat() == Triple::MachO)
    RuntimeEntryPoint = '_' + RuntimeEntryPoint;
  return S.ES.lookup(S.JDSearchOrder, S.ES.intern(RuntimeEntryPoint));
}

static Expected<ExecutorSymbolDef> getEntryPoint(Session &S) {
  ExecutorSymbolDef EntryPoint;

  // Find the entry-point function unconditionally, since we want to force
  // it to be materialized to collect stats.
  if (auto EP = getMainEntryPoint(S))
    EntryPoint = *EP;
  else
    return EP.takeError();
  LLVM_DEBUG({
    dbgs() << "Using entry point \"" << S.Args.EntryPointName
           << "\": " << formatv("{0:x16}", EntryPoint.getAddress()) << "\n";
  });

  // If we're running with the ORC runtime then replace the entry-point
  // with the __orc_rt_run_program symbol.
  if (!S.Args.OrcRuntime.empty()) {
    if (auto EP = getOrcRuntimeEntryPoint(S))
      EntryPoint = *EP;
    else
      return EP.takeError();
    LLVM_DEBUG({
      dbgs() << "(called via __orc_rt_run_program_wrapper at "
             << formatv("{0:x16}", EntryPoint.getAddress()) << ")\n";
    });
  }

  return EntryPoint;
}

static Expected<int> runWithRuntime(Session &S, ExecutorAddr EntryPointAddr) {
  StringRef DemangledEntryPoint = S.Args.EntryPointName;
  if (S.ES.getTargetTriple().getObjectFormat() == Triple::MachO &&
      DemangledEntryPoint.front() == '_')
    DemangledEntryPoint = DemangledEntryPoint.drop_front();
  using llvm::orc::shared::SPSString;
  using SPSRunProgramSig =
      int64_t(SPSString, SPSString, shared::SPSSequence<SPSString>);
  int64_t Result;
  if (auto Err = S.ES.callSPSWrapper<SPSRunProgramSig>(
          EntryPointAddr, Result, S.MainJD->getName(), DemangledEntryPoint,
          S.Args.InputArgv))
    return std::move(Err);
  return Result;
}

static Expected<int> runWithoutRuntime(Session &S,
                                       ExecutorAddr EntryPointAddr) {
  return S.ES.getExecutorProcessControl().runAsMain(EntryPointAddr,
                                                    S.Args.InputArgv);
}

static Error symbolicateBacktraces(const JITLinkArgs &Args) {
  auto Symtab = DumpedSymbolTable::Create(Args.SymbolicateWith);
  if (!Symtab)
    return Symtab.takeError();

  for (auto InputFile : Args.InputFiles) {
    auto BacktraceBuffer = MemoryBuffer::getFileOrSTDIN(InputFile);
    if (!BacktraceBuffer)
      return createFileError(InputFile, BacktraceBuffer.getError());

    outs() << Symtab->symbolicate((*BacktraceBuffer)->getBuffer());
  }

  return Error::success();
}

static Error waitingOnGraphReplay(const JITLinkArgs &Args) {
  // TODO: Warn about ignored options once the parser can enumerate them.

  // Read the replay buffer file.
  auto GraphOpsBuffer = getFile(Args.WaitingOnGraphReplay);
  if (!GraphOpsBuffer)
    return GraphOpsBuffer.takeError();

  using Replay = orc::detail::WaitingOnGraphOpReplay<uintptr_t, uintptr_t>;
  using Graph = typename Replay::Graph;
  using Replayer = typename Replay::Replayer;

  std::vector<typename Replay::Op> RecordedOps;

  // First read the buffer to build the Ops vector. Doing this up-front allows
  // us to avoid polluting the timings below with the cost of parsing.
  Error Err = Error::success();
  for (auto &Op :
       orc::detail::readWaitingOnGraphOpsFromBuffer<uintptr_t, uintptr_t>(
           (*GraphOpsBuffer)->getBuffer(), Err))
    RecordedOps.push_back(std::move(Op));
  if (Err)
    return Err;

  // Now replay the Ops:
  Graph G;
  Replayer R(G);

  outs() << "Replaying WaitingOnGraph operations from "
         << Args.WaitingOnGraphReplay << "...\n";
  auto ReplayStart = std::chrono::high_resolution_clock::now();
  for (auto &Op : RecordedOps)
    R.replay(std::move(Op));
  auto ReplayEnd = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> ReplayDiff = ReplayEnd - ReplayStart;
  outs() << ReplayDiff.count() << "s to replay " << RecordedOps.size()
         << " ops (wall-clock time)\n";
  return Error::success();
}

namespace {
struct JITLinkTimers {
  TimerGroup JITLinkTG{"llvm-jitlink timers", "timers for llvm-jitlink phases"};
  Timer LoadObjectsTimer{"load", "time to load/add object files", JITLinkTG};
  Timer LinkTimer{"link", "time to link object files", JITLinkTG};
  Timer RunTimer{"run", "time to execute jitlink'd code", JITLinkTG};
};
} // namespace

int main(int argc, char *argv[]) {
  InitLLVM X(argc, argv);

  InitializeAllTargetInfos();
  InitializeAllTargetMCs();
  InitializeAllDisassemblers();

  clv2::OptionParser P;
  P.add<&JITLinkToolReg>();
  RegisterCoreLLVMOptions(P);
  P.hideUnrelatedOptions({&JITLinkCategory, &clv2::ColorOptionsCategory});
  auto OptsCtx = P.parse(argc, argv, "llvm jitlink tool");
  auto *Opts = OptsCtx->getViewPtr<&JITLinkToolReg>();

  JITLinkArgs Args;
  Args.InputFiles = Opts->get<&InputFilesOpt>();
  Args.InputFilePositions = Opts->elementPositions<&InputFilesOpt>();
  Args.LazyLink = Opts->get<&LazyLinkOpt>();
  Args.LazyLinkPositions = Opts->elementPositions<&LazyLinkOpt>();
  Args.Speculate = Opts->get<&SpeculateOpt>();
  Args.SpeculateOrder = Opts->get<&SpeculateOrderOpt>();
  Args.RecordLazyExecs = Opts->get<&RecordLazyExecsOpt>();
  Args.MaterializationThreads = Opts->get<&MaterializationThreadsOpt>();
  Args.MaterializationThreadsOccurrences =
      Opts->occurrences<&MaterializationThreadsOpt>();
  Args.LibrarySearchPaths = Opts->get<&LibrarySearchPathsOpt>();
  Args.LibrarySearchPathPositions =
      Opts->elementPositions<&LibrarySearchPathsOpt>();
  Args.Libraries = Opts->get<&LibrariesOpt>();
  Args.LibraryPositions = Opts->elementPositions<&LibrariesOpt>();
  Args.LibrariesHidden = Opts->get<&LibrariesHiddenOpt>();
  Args.LibrariesHiddenPositions = Opts->elementPositions<&LibrariesHiddenOpt>();
  Args.LoadHidden = Opts->get<&LoadHiddenOpt>();
  Args.LoadHiddenPositions = Opts->elementPositions<&LoadHiddenOpt>();
  Args.WriteSymbolTableTo = Opts->get<&WriteSymbolTableToOpt>();
  Args.SymbolicateWith = Opts->get<&SymbolicateWithOpt>();
  Args.LibrariesWeak = Opts->get<&LibrariesWeakOpt>();
  Args.LibrariesWeakPositions = Opts->elementPositions<&LibrariesWeakOpt>();
  Args.LibrariesAuto = Opts->get<&LibrariesAutoOpt>();
  Args.LibrariesAutoPositions = Opts->elementPositions<&LibrariesAutoOpt>();
  Args.WeakLibraries = Opts->get<&WeakLibrariesOpt>();
  Args.WeakLibraryPositions = Opts->elementPositions<&WeakLibrariesOpt>();
  Args.SearchSystemLibrary = Opts->get<&SearchSystemLibraryOpt>();
  Args.NoExec = Opts->get<&NoExecOpt>();
  Args.CheckFiles = Opts->get<&CheckFilesOpt>();
  Args.CheckName = Opts->get<&CheckNameOpt>();
  Args.EntryPointName = Opts->get<&EntryPointNameOpt>();
  Args.JITDylibs = Opts->get<&JITDylibsOpt>();
  Args.JITDylibPositions = Opts->elementPositions<&JITDylibsOpt>();
  Args.Dylibs = Opts->get<&DylibsOpt>();
  Args.DebuggerSupport = Opts->get<&DebuggerSupportOpt>();
  Args.DebuggerSupportOccurrences = Opts->occurrences<&DebuggerSupportOpt>();
  Args.PerfSupport = Opts->get<&PerfSupportOpt>();
  Args.VTuneSupport = Opts->get<&VTuneSupportOpt>();
  Args.NoProcessSymbols = Opts->get<&NoProcessSymbolsOpt>();
  Args.AbsoluteDefs = Opts->get<&AbsoluteDefsOpt>();
  Args.AbsoluteDefPositions = Opts->elementPositions<&AbsoluteDefsOpt>();
  Args.Aliases = Opts->get<&AliasesOpt>();
  Args.AliasPositions = Opts->elementPositions<&AliasesOpt>();
  Args.SectCreate = Opts->get<&SectCreateOpt>();
  Args.SectCreatePositions = Opts->elementPositions<&SectCreateOpt>();
  Args.ShowLinkedFiles = Opts->get<&ShowLinkedFilesOpt>();
  Args.ShowInitialExecutionSessionState =
      Opts->get<&ShowInitialExecutionSessionStateOpt>();
  Args.ShowEntryExecutionSessionState =
      Opts->get<&ShowEntryExecutionSessionStateOpt>();
  Args.ShowAddrs = Opts->get<&ShowAddrsOpt>();
  Args.ShowLinkGraphs = Opts->get<&ShowLinkGraphsOpt>();
  Args.ShowTimes = Opts->get<&ShowTimesOpt>();
  Args.SlabAllocateSizeString = Opts->get<&SlabAllocateSizeStringOpt>();
  Args.SlabAddress = Opts->get<&SlabAddressOpt>();
  Args.SlabPageSize = Opts->get<&SlabPageSizeOpt>();
  Args.ShowRelocatedSectionContents =
      Opts->get<&ShowRelocatedSectionContentsOpt>();
  Args.PhonyExternals = Opts->get<&PhonyExternalsOpt>();
  Args.OutOfProcessExecutor = Opts->get<&OutOfProcessExecutorOpt>();
  Args.OutOfProcessExecutorOccurrences =
      Opts->occurrences<&OutOfProcessExecutorOpt>();
  Args.OutOfProcessExecutorConnect =
      Opts->get<&OutOfProcessExecutorConnectOpt>();
  Args.OutOfProcessExecutorConnectOccurrences =
      Opts->occurrences<&OutOfProcessExecutorConnectOpt>();
  Args.OrcRuntime = Opts->get<&OrcRuntimeOpt>();
  Args.AddSelfRelocations = Opts->get<&AddSelfRelocationsOpt>();
  Args.ShowErrFailedToMaterialize = Opts->get<&ShowErrFailedToMaterializeOpt>();
  Args.UseMemMgr = Opts->get<&UseMemMgrOpt>();
  Args.OverrideTriple = Opts->get<&OverrideTripleOpt>();
  Args.AllLoad = Opts->get<&AllLoadOpt>();
  Args.ForceLoadObjC = Opts->get<&ForceLoadObjCOpt>();
  Args.WaitingOnGraphCapture = Opts->get<&WaitingOnGraphCaptureOpt>();
  Args.WaitingOnGraphReplay = Opts->get<&WaitingOnGraphReplayOpt>();
  Args.ShowPrePruneTotalBlockSize = Opts->get<&ShowPrePruneTotalBlockSizeOpt>();
  Args.ShowPostFixupTotalBlockSize =
      Opts->get<&ShowPostFixupTotalBlockSizeOpt>();
  Args.InputArgv = Opts->get<&InputArgvOpt>();
  Args.TestHarnesses = Opts->get<&TestHarnessesOpt>();
  ExitOnErr.setBanner(std::string(argv[0]) + ": ");

  // Check for WaitingOnGraph replay mode.
  if (!Args.WaitingOnGraphReplay.empty()) {
    ExitOnErr(waitingOnGraphReplay(Args));
    return 0;
  }

  /// If timers are enabled, create a JITLinkTimers instance.
  std::unique_ptr<JITLinkTimers> Timers =
      Args.ShowTimes ? std::make_unique<JITLinkTimers>() : nullptr;

  auto [TT, Features] = getFirstFileTripleAndFeatures(Args);
  ExitOnErr(sanitizeArguments(Args, TT, argv[0]));

  if (!Args.SymbolicateWith.empty()) {
    ExitOnErr(symbolicateBacktraces(Args));
    return 0;
  }

  auto S = ExitOnErr(Session::Create(TT, Features, Args));

  enableStatistics(*S, !Args.OrcRuntime.empty(),
                   Args.ShowPrePruneTotalBlockSize,
                   Args.ShowPostFixupTotalBlockSize);

  {
    TimeRegion TR(Timers ? &Timers->LoadObjectsTimer : nullptr);
    ExitOnErr(addSessionInputs(*S));
  }

  if (Args.PhonyExternals)
    addPhonyExternalsGenerator(*S);

  if (Args.ShowInitialExecutionSessionState)
    S->ES.dump(outs());

  Expected<ExecutorSymbolDef> EntryPoint((ExecutorSymbolDef()));
  {
    ExpectedAsOutParameter<ExecutorSymbolDef> _(&EntryPoint);
    TimeRegion TR(Timers ? &Timers->LinkTimer : nullptr);
    EntryPoint = getEntryPoint(*S);
  }

  // Print any reports regardless of whether we succeeded or failed.
  if (Args.ShowEntryExecutionSessionState)
    S->ES.dump(outs());

  if (Args.ShowAddrs)
    S->dumpSessionInfo(outs());

  if (!EntryPoint) {
    if (Timers)
      Timers->JITLinkTG.printAll(errs());
    reportLLVMJITLinkError(EntryPoint.takeError(),
                           Args.ShowErrFailedToMaterialize);
    ExitOnErr(S->ES.endSession());
    exit(1);
  }

  ExitOnErr(runChecks(*S, std::move(TT), std::move(Features)));

  int Result = 0;
  if (!Args.NoExec) {
    LLVM_DEBUG(dbgs() << "Running \"" << Args.EntryPointName << "\"...\n");
    TimeRegion TR(Timers ? &Timers->RunTimer : nullptr);
    if (!Args.OrcRuntime.empty())
      Result = ExitOnErr(runWithRuntime(*S, EntryPoint->getAddress()));
    else
      Result = ExitOnErr(runWithoutRuntime(*S, EntryPoint->getAddress()));
  }

  // Destroy the session.
  ExitOnErr(S->ES.endSession());
  S.reset();

  if (Timers)
    Timers->JITLinkTG.printAll(errs());

  // If the executing code set a test result override then use that.
  if (UseTestResultOverride)
    Result = TestResultOverride;

  return Result;
}
