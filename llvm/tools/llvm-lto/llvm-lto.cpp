//===- llvm-lto: a simple command-line program to link modules with LTO ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program takes in a list of bitcode files, links them, performs link-time
// optimization, and outputs an object file.
//
//===----------------------------------------------------------------------===//

#include "llvm-c/lto.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/LTO/legacy/LTOCodeGenerator.h"
#include "llvm/LTO/legacy/LTOModule.h"
#include "llvm/LTO/legacy/ThinLTOCodeGenerator.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SupportOptionsOptInfos.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/AArch64/AArch64OptionsOptInfos.h"
#include "llvm/Target/AMDGPU/AMDGPUOptionsOptInfos.h"
#include "llvm/Target/ARM/ARMOptionsOptInfos.h"
#include "llvm/Target/BPF/BPFOptionsOptInfos.h"
#include "llvm/Target/Hexagon/HexagonOptionsOptInfos.h"
#include "llvm/Target/Lanai/LanaiOptionsOptInfos.h"
#include "llvm/Target/LoongArch/LoongArchOptionsOptInfos.h"
#include "llvm/Target/MSP430/MSP430OptionsOptInfos.h"
#include "llvm/Target/Mips/MipsOptionsOptInfos.h"
#include "llvm/Target/NVPTX/NVPTXOptionsOptInfos.h"
#include "llvm/Target/PowerPC/PowerPCOptionsOptInfos.h"
#include "llvm/Target/RISCV/RISCVOptionsOptInfos.h"
#include "llvm/Target/SPIRV/SPIRVOptionsOptInfos.h"
#include "llvm/Target/Sparc/SparcOptionsOptInfos.h"
#include "llvm/Target/SystemZ/SystemZOptionsOptInfos.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/Target/WebAssembly/WebAssemblyOptionsOptInfos.h"
#include "llvm/Target/X86/X86OptionsOptInfos.h"
#include "llvm/Target/XCore/XCoreOptionsOptInfos.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

using namespace llvm;

inline constexpr clv2::OptionCategory LTOCategory("LTO Options");

enum ThinLTOModes {
  THINLINK,
  THINDISTRIBUTE,
  THINEMITIMPORTS,
  THINPROMOTE,
  THINIMPORT,
  THININTERNALIZE,
  THINOPT,
  THINCODEGEN,
  THINALL
};

inline constexpr clv2::EnumVal<ThinLTOModes> ThinLTOModeVals[] = {
    {"thinlink", THINLINK,
     "ThinLink: produces the index by linking only the summaries."},
    {"distributedindexes", THINDISTRIBUTE,
     "Produces individual indexes for distributed backends."},
    {"emitimports", THINEMITIMPORTS,
     "Emit imports files for distributed backends."},
    {"promote", THINPROMOTE,
     "Perform pre-import promotion (requires -thinlto-index)."},
    {"import", THINIMPORT,
     "Perform both promotion and "
     "cross-module importing (requires "
     "-thinlto-index)."},
    {"internalize", THININTERNALIZE,
     "Perform internalization driven by -exported-symbol "
     "(requires -thinlto-index)."},
    {"optimize", THINOPT, "Perform ThinLTO optimizations."},
    {"codegen", THINCODEGEN, "CodeGen (expected to match llc)"},
    {"run", THINALL, "Perform ThinLTO end-to-end"},
};

inline constexpr clv2::ListOptionInfo<std::string> InputFilenamesOpt{
    "", "<input bitcode files>", clv2::Positional{}, clv2::OneOrMore,
    clv2::cat(LTOCategory)};
inline constexpr clv2::OptionInfo<std::string> OutputFilenameOpt{
    "o", "Override output filename", clv2::cat(LTOCategory),
    clv2::value_desc("filename")};
inline constexpr clv2::OptionInfo<unsigned> OptLevelOpt{
    "O",
    "Optimization level. [-O0, -O1, -O2, or -O3] "
    "(default = '-O2')",
    clv2::PrefixFormat,
    clv2::value_desc("char"),
    clv2::cat(LTOCategory),
    clv2::Init{2u}};
inline constexpr clv2::OptionInfo<bool> IndexStatsOpt{
    "thinlto-index-stats", "Print statistic for the index in every input files",
    clv2::cat(LTOCategory)};
inline constexpr clv2::OptionInfo<bool> DisableVerifyOpt{
    "disable-verify",
    "Do not run the verifier during the optimization pipeline",
    clv2::cat(LTOCategory)};
inline constexpr clv2::OptionInfo<bool> EnableFreestandingOpt{
    "lto-freestanding",
    "Enable Freestanding (disable builtins / TLI) during LTO",
    clv2::cat(LTOCategory)};
inline constexpr clv2::OptionInfo<bool> UseDiagnosticHandlerOpt{
    "use-diagnostic-handler",
    "Use a diagnostic handler to test the handler interface",
    clv2::cat(LTOCategory)};
inline constexpr clv2::OptionInfo<bool> ThinLTOOpt{
    "thinlto", "Only write combined global index for ThinLTO backends",
    clv2::cat(LTOCategory)};
inline constexpr auto ThinLTOModeOpt = clv2::makeEnumOption<ThinLTOModes>(
    "thinlto-action", "Perform a single ThinLTO stage:", ThinLTOModeVals,
    clv2::cat(LTOCategory));
inline constexpr clv2::OptionInfo<std::string> ThinLTOIndexOpt{
    "thinlto-index",
    "Provide the index produced by a ThinLink, required "
    "to perform the promotion and/or importing.",
    clv2::cat(LTOCategory)};
inline constexpr clv2::OptionInfo<std::string> ThinLTOPrefixReplaceOpt{
    "thinlto-prefix-replace",
    "Control where files for distributed backends are "
    "created. Expects 'oldprefix;newprefix' and if path "
    "prefix of output file is oldprefix it will be "
    "replaced with newprefix.",
    clv2::cat(LTOCategory)};
inline constexpr clv2::OptionInfo<std::string> ThinLTOModuleIdOpt{
    "thinlto-module-id",
    "For the module ID for the file to process, useful to "
    "match what is in the index.",
    clv2::cat(LTOCategory)};
inline constexpr clv2::OptionInfo<std::string> ThinLTOCacheDirOpt{
    "thinlto-cache-dir", "Enable ThinLTO caching.", clv2::cat(LTOCategory)};
inline constexpr clv2::OptionInfo<int> ThinLTOCachePruningIntervalOpt{
    "thinlto-cache-pruning-interval", "Set ThinLTO cache pruning interval.",
    clv2::cat(LTOCategory), clv2::Init{1200}};
inline constexpr clv2::OptionInfo<uint64_t> ThinLTOCacheMaxSizeBytesOpt{
    "thinlto-cache-max-size-bytes",
    "Set ThinLTO cache pruning directory maximum size in bytes.",
    clv2::cat(LTOCategory)};
inline constexpr clv2::OptionInfo<int> ThinLTOCacheMaxSizeFilesOpt{
    "thinlto-cache-max-size-files",
    "Set ThinLTO cache pruning directory maximum number of files.",
    clv2::cat(LTOCategory), clv2::Init{1000000}};
inline constexpr clv2::OptionInfo<unsigned> ThinLTOCacheEntryExpirationOpt{
    "thinlto-cache-entry-expiration",
    "Set ThinLTO cache entry expiration time.", clv2::cat(LTOCategory),
    clv2::Init{604800u}};
inline constexpr clv2::OptionInfo<std::string> ThinLTOSaveTempsPrefixOpt{
    "thinlto-save-temps",
    "Save ThinLTO temp files using filenames created by adding "
    "suffixes to the given file path prefix.",
    clv2::cat(LTOCategory)};
inline constexpr clv2::OptionInfo<std::string> ThinLTOGeneratedObjectsDirOpt{
    "thinlto-save-objects",
    "Save ThinLTO generated object files using filenames created in "
    "the given directory.",
    clv2::cat(LTOCategory)};
inline constexpr clv2::OptionInfo<bool> SaveLinkedModuleFileOpt{
    "save-linked-module", "Write linked LTO module to file before optimize",
    clv2::cat(LTOCategory)};
inline constexpr clv2::OptionInfo<bool> SaveModuleFileOpt{
    "save-merged-module", "Write merged LTO module to file before CodeGen",
    clv2::cat(LTOCategory)};
inline constexpr clv2::ListOptionInfo<std::string> ExportedSymbolsOpt{
    "exported-symbol",
    "List of symbols to export from the resulting object file",
    clv2::cat(LTOCategory)};
inline constexpr clv2::ListOptionInfo<std::string> DSOSymbolsOpt{
    "dso-symbol", "Symbol to put in the symtab in the resulting dso",
    clv2::cat(LTOCategory)};
inline constexpr clv2::OptionInfo<bool> ListSymbolsOnlyOpt{
    "list-symbols-only",
    "Instead of running LTO, list the symbols in each IR file",
    clv2::cat(LTOCategory)};
inline constexpr clv2::OptionInfo<bool> ListDependentLibrariesOnlyOpt{
    "list-dependent-libraries-only",
    "Instead of running LTO, list the dependent libraries in each IR file",
    clv2::cat(LTOCategory)};
inline constexpr clv2::OptionInfo<bool> QueryHasCtorDtorOpt{
    "query-hasCtorDtor", "Queries LTOModule::hasCtorDtor() on each IR file"};
inline constexpr clv2::OptionInfo<bool> SetMergedModuleOpt{
    "set-merged-module", "Use the first input module as the merged module",
    clv2::cat(LTOCategory)};
inline constexpr clv2::OptionInfo<unsigned> ParallelismOpt{
    "j", "Number of backend threads", clv2::PrefixFormat,
    clv2::cat(LTOCategory), clv2::Init{1u}};
inline constexpr clv2::OptionInfo<bool> RestoreGlobalsLinkageOpt{
    "restore-linkage", "Restore original linkage of globals prior to CodeGen",
    clv2::cat(LTOCategory)};
inline constexpr clv2::OptionInfo<bool> CheckHasObjCOpt{
    "check-for-objc", "Only check if the module has objective-C defined in it",
    clv2::cat(LTOCategory)};
inline constexpr clv2::OptionInfo<bool> PrintMachOCPUOnlyOpt{
    "print-macho-cpu-only",
    "Instead of running LTO, print the mach-o cpu in each IR file",
    clv2::cat(LTOCategory)};
inline constexpr clv2::OptionInfo<bool> DebugPassManagerOpt{
    "debug-pass-manager", "Print pass management debugging information",
    clv2::cat(LTOCategory), clv2::Hidden};
inline constexpr clv2::OptionInfo<bool> LTOSaveBeforeOptOpt{
    "lto-save-before-opt", "Save the IR before running optimizations"};

static constexpr clv2::OptionsRegistry<
    &InputFilenamesOpt, &OutputFilenameOpt, &OptLevelOpt, &IndexStatsOpt,
    &DisableVerifyOpt, &EnableFreestandingOpt, &UseDiagnosticHandlerOpt,
    &ThinLTOOpt, &ThinLTOModeOpt, &ThinLTOIndexOpt, &ThinLTOPrefixReplaceOpt,
    &ThinLTOModuleIdOpt, &ThinLTOCacheDirOpt, &ThinLTOCachePruningIntervalOpt,
    &ThinLTOCacheMaxSizeBytesOpt, &ThinLTOCacheMaxSizeFilesOpt,
    &ThinLTOCacheEntryExpirationOpt, &ThinLTOSaveTempsPrefixOpt,
    &ThinLTOGeneratedObjectsDirOpt, &SaveLinkedModuleFileOpt,
    &SaveModuleFileOpt, &ExportedSymbolsOpt, &DSOSymbolsOpt,
    &ListSymbolsOnlyOpt, &ListDependentLibrariesOnlyOpt, &QueryHasCtorDtorOpt,
    &SetMergedModuleOpt, &ParallelismOpt, &RestoreGlobalsLinkageOpt,
    &CheckHasObjCOpt, &PrintMachOCPUOnlyOpt, &DebugPassManagerOpt,
    &LTOSaveBeforeOptOpt>
    LTOToolReg;

using LTOOpts = decltype(LTOToolReg)::ParsedOptionsT;

namespace {

struct ModuleInfo {
  BitVector CanBeHidden;
};

} // end anonymous namespace

static void handleDiagnostics(lto_codegen_diagnostic_severity_t Severity,
                              const char *Msg, void *) {
  errs() << "llvm-lto: ";
  switch (Severity) {
  case LTO_DS_NOTE:
    errs() << "note: ";
    break;
  case LTO_DS_REMARK:
    errs() << "remark: ";
    break;
  case LTO_DS_ERROR:
    errs() << "error: ";
    break;
  case LTO_DS_WARNING:
    errs() << "warning: ";
    break;
  }
  errs() << Msg << "\n";
}

static std::string CurrentActivity;

namespace {
struct LLVMLTODiagnosticHandler : public DiagnosticHandler {
  bool handleDiagnostics(const DiagnosticInfo &DI) override {
    raw_ostream &OS = errs();
    OS << "llvm-lto: ";
    switch (DI.getSeverity()) {
    case DS_Error:
      OS << "error";
      break;
    case DS_Warning:
      OS << "warning";
      break;
    case DS_Remark:
      OS << "remark";
      break;
    case DS_Note:
      OS << "note";
      break;
    }
    if (!CurrentActivity.empty())
      OS << ' ' << CurrentActivity;
    OS << ": ";

    DiagnosticPrinterRawOStream DP(OS);
    DI.print(DP);
    OS << '\n';

    if (DI.getSeverity() == DS_Error)
      exit(1);
    return true;
  }
};
} // namespace

static void error(const Twine &Msg) {
  errs() << "llvm-lto: " << Msg << '\n';
  exit(1);
}

static void error(std::error_code EC, const Twine &Prefix) {
  if (EC)
    error(Prefix + ": " + EC.message());
}

template <typename T>
static void error(const ErrorOr<T> &V, const Twine &Prefix) {
  error(V.getError(), Prefix);
}

static void maybeVerifyModule(const Module &Mod, const LTOOpts &Opts) {
  if (!Opts.get<&DisableVerifyOpt>() && verifyModule(Mod, &errs()))
    error("Broken Module");
}

static std::unique_ptr<LTOModule>
getLocalLTOModule(StringRef Path, std::unique_ptr<MemoryBuffer> &Buffer,
                  const TargetOptions &Options, const LTOOpts &Opts,
                  clv2::OptionsContext *OptsCtx) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
      MemoryBuffer::getFile(Path);
  error(BufferOrErr, "error loading file '" + Path + "'");
  Buffer = std::move(BufferOrErr.get());
  CurrentActivity = ("loading file '" + Path + "'").str();
  std::unique_ptr<LLVMContext> Context =
      std::make_unique<LLVMContext>(*OptsCtx);
  Context->setDiagnosticHandler(std::make_unique<LLVMLTODiagnosticHandler>(),
                                true);
  ErrorOr<std::unique_ptr<LTOModule>> Ret = LTOModule::createInLocalContext(
      std::move(Context), Buffer->getBufferStart(), Buffer->getBufferSize(),
      Options, Path);
  CurrentActivity = "";
  maybeVerifyModule((*Ret)->getModule(), Opts);
  return std::move(*Ret);
}

/// Print some statistics on the index for each input files.
static void printIndexStats(const LTOOpts &Opts) {
  for (auto &Filename : Opts.get<&InputFilenamesOpt>()) {
    ExitOnError ExitOnErr("llvm-lto: error loading file '" + Filename + "': ");
    std::unique_ptr<ModuleSummaryIndex> Index =
        ExitOnErr(getModuleSummaryIndexForFile(Filename));
    // Skip files without a module summary.
    if (!Index)
      report_fatal_error(Twine(Filename) + " does not contain an index");

    unsigned Calls = 0, Refs = 0, Functions = 0, Alias = 0, Globals = 0;
    for (auto &Summaries : *Index) {
      for (auto &Summary : Summaries.second.getSummaryList()) {
        Refs += Summary->refs().size();
        if (auto *FuncSummary = dyn_cast<FunctionSummary>(Summary.get())) {
          Functions++;
          Calls += FuncSummary->calls().size();
        } else if (isa<AliasSummary>(Summary.get()))
          Alias++;
        else
          Globals++;
      }
    }
    outs() << "Index " << Filename << " contains "
           << (Alias + Globals + Functions) << " nodes (" << Functions
           << " functions, " << Alias << " alias, " << Globals
           << " globals) and " << (Calls + Refs) << " edges (" << Refs
           << " refs and " << Calls << " calls)\n";
  }
}

/// Print the lto symbol attributes.
static void printLTOSymbolAttributes(lto_symbol_attributes Attrs) {
  outs() << "{ ";
  unsigned Permission = Attrs & LTO_SYMBOL_PERMISSIONS_MASK;
  switch (Permission) {
  case LTO_SYMBOL_PERMISSIONS_CODE:
    outs() << "function ";
    break;
  case LTO_SYMBOL_PERMISSIONS_DATA:
    outs() << "data ";
    break;
  case LTO_SYMBOL_PERMISSIONS_RODATA:
    outs() << "constant ";
    break;
  }
  unsigned Definition = Attrs & LTO_SYMBOL_DEFINITION_MASK;
  switch (Definition) {
  case LTO_SYMBOL_DEFINITION_REGULAR:
    outs() << "defined ";
    break;
  case LTO_SYMBOL_DEFINITION_TENTATIVE:
    outs() << "common ";
    break;
  case LTO_SYMBOL_DEFINITION_WEAK:
    outs() << "weak ";
    break;
  case LTO_SYMBOL_DEFINITION_UNDEFINED:
    outs() << "extern ";
    break;
  case LTO_SYMBOL_DEFINITION_WEAKUNDEF:
    outs() << "extern-weak ";
    break;
  }
  unsigned Scope = Attrs & LTO_SYMBOL_SCOPE_MASK;
  switch (Scope) {
  case LTO_SYMBOL_SCOPE_INTERNAL:
    outs() << "internal ";
    break;
  case LTO_SYMBOL_SCOPE_HIDDEN:
    outs() << "hidden ";
    break;
  case LTO_SYMBOL_SCOPE_PROTECTED:
    outs() << "protected ";
    break;
  case LTO_SYMBOL_SCOPE_DEFAULT:
    outs() << "default ";
    break;
  case LTO_SYMBOL_SCOPE_DEFAULT_CAN_BE_HIDDEN:
    outs() << "omitted ";
    break;
  }
  if (Attrs & LTO_SYMBOL_COMDAT)
    outs() << "comdat ";
  if (Attrs & LTO_SYMBOL_ALIAS)
    outs() << "alias ";
  outs() << "}";
}

/// Load each IR file and dump certain information based on active flags.
///
/// The main point here is to provide lit-testable coverage for the LTOModule
/// functionality that's exposed by the C API. Moreover, this provides testing
/// coverage for modules that have been created in their own contexts.
static void testLTOModule(const TargetOptions &Options, const LTOOpts &Opts,
                          clv2::OptionsContext *OptsCtx) {
  for (auto &Filename : Opts.get<&InputFilenamesOpt>()) {
    std::unique_ptr<MemoryBuffer> Buffer;
    std::unique_ptr<LTOModule> Module =
        getLocalLTOModule(Filename, Buffer, Options, Opts, OptsCtx);

    if (Opts.get<&ListSymbolsOnlyOpt>()) {
      // List the symbols.
      outs() << Filename << ":\n";
      for (int I = 0, E = Module->getSymbolCount(); I != E; ++I) {
        outs() << Module->getSymbolName(I) << "    ";
        printLTOSymbolAttributes(Module->getSymbolAttributes(I));
        outs() << "\n";
      }
      for (int I = 0, E = Module->getAsmUndefSymbolCount(); I != E; ++I)
        outs() << Module->getAsmUndefSymbolName(I) << "    { asm extern }\n";
    }
    if (Opts.get<&QueryHasCtorDtorOpt>())
      outs() << Filename
             << ": hasCtorDtor = " << (Module->hasCtorDtor() ? "true" : "false")
             << "\n";
  }
}

static std::unique_ptr<MemoryBuffer> loadFile(StringRef Filename) {
  ExitOnError ExitOnErr("llvm-lto: error loading file '" + Filename.str() +
                        "': ");
  return ExitOnErr(errorOrToExpected(MemoryBuffer::getFileOrSTDIN(Filename)));
}

static void listDependentLibraries(const LTOOpts &Opts) {
  for (auto &Filename : Opts.get<&InputFilenamesOpt>()) {
    auto Buffer = loadFile(Filename);
    std::string E;
    std::unique_ptr<lto::InputFile> Input(LTOModule::createInputFile(
        Buffer->getBufferStart(), Buffer->getBufferSize(), Filename.c_str(),
        E));
    if (!Input)
      error(E);

    // List the dependent libraries.
    outs() << Filename << ":\n";
    for (size_t I = 0, C = LTOModule::getDependentLibraryCount(Input.get());
         I != C; ++I) {
      size_t L = 0;
      const char *S = LTOModule::getDependentLibrary(Input.get(), I, &L);
      assert(S);
      outs() << StringRef(S, L) << "\n";
    }
  }
}

static void printMachOCPUOnly(const LTOOpts &Opts,
                              clv2::OptionsContext *OptsCtx) {
  LLVMContext Context(*OptsCtx);
  Context.setDiagnosticHandler(std::make_unique<LLVMLTODiagnosticHandler>(),
                               true);
  TargetOptions Options =
      codegen::InitTargetOptionsFromCodeGenFlags(Triple(), *OptsCtx);
  for (auto &Filename : Opts.get<&InputFilenamesOpt>()) {
    ErrorOr<std::unique_ptr<LTOModule>> ModuleOrErr =
        LTOModule::createFromFile(Context, Filename, Options);
    if (!ModuleOrErr)
      error(ModuleOrErr, "llvm-lto: ");

    Expected<uint32_t> CPUType = (*ModuleOrErr)->getMachOCPUType();
    Expected<uint32_t> CPUSubType = (*ModuleOrErr)->getMachOCPUSubType();
    if (!CPUType)
      error("Error while printing mach-o cputype: " +
            toString(CPUType.takeError()));
    if (!CPUSubType)
      error("Error while printing mach-o cpusubtype: " +
            toString(CPUSubType.takeError()));
    outs() << llvm::format("%s:\ncputype: %u\ncpusubtype: %u\n",
                           Filename.c_str(), *CPUType, *CPUSubType);
  }
}

/// Create a combined index file from the input IR files and write it.
///
/// This is meant to enable testing of ThinLTO combined index generation,
/// currently available via the gold plugin via -thinlto.
static void
createCombinedModuleSummaryIndex(const LTOOpts &Opts,
                                 const clv2::OptionsContext &OptsCtx) {
  ModuleSummaryIndex CombinedIndex(/*HaveGVs=*/false);
  for (auto &Filename : Opts.get<&InputFilenamesOpt>()) {
    ExitOnError ExitOnErr("llvm-lto: error loading file '" + Filename + "': ");
    std::unique_ptr<MemoryBuffer> MB =
        ExitOnErr(errorOrToExpected(MemoryBuffer::getFileOrSTDIN(Filename)));
    ExitOnErr(readModuleSummaryIndex(*MB, CombinedIndex, OptsCtx));
  }
  // In order to use this index for testing, specifically import testing, we
  // need to update any indirect call edges created from SamplePGO, so that they
  // point to the correct GUIDs.
  updateIndirectCalls(CombinedIndex);
  std::error_code EC;
  const auto &OutputFilename = Opts.get<&OutputFilenameOpt>();
  assert(!OutputFilename.empty());
  raw_fd_ostream OS(OutputFilename + ".thinlto.bc", EC,
                    sys::fs::OpenFlags::OF_None);
  error(EC, "error opening the file '" + OutputFilename + ".thinlto.bc'");
  writeIndexToFile(CombinedIndex, OS, OptsCtx);
  OS.close();
}

/// Parse the thinlto_prefix_replace option into the \p OldPrefix and
/// \p NewPrefix strings, if it was specified.
static void getThinLTOOldAndNewPrefix(std::string &OldPrefix,
                                      std::string &NewPrefix,
                                      const LTOOpts &Opts) {
  const auto &ThinLTOPrefixReplace = Opts.get<&ThinLTOPrefixReplaceOpt>();
  assert(ThinLTOPrefixReplace.empty() ||
         ThinLTOPrefixReplace.find(';') != StringRef::npos);
  StringRef PrefixReplace = ThinLTOPrefixReplace;
  std::pair<StringRef, StringRef> Split = PrefixReplace.split(";");
  OldPrefix = Split.first.str();
  NewPrefix = Split.second.str();
}

/// Given the original \p Path to an output file, replace any path
/// prefix matching \p OldPrefix with \p NewPrefix. Also, create the
/// resulting directory if it does not yet exist.
static std::string getThinLTOOutputFile(StringRef Path, StringRef OldPrefix,
                                        StringRef NewPrefix) {
  if (OldPrefix.empty() && NewPrefix.empty())
    return std::string(Path);
  SmallString<128> NewPath(Path);
  llvm::sys::path::replace_path_prefix(NewPath, OldPrefix, NewPrefix);
  StringRef ParentPath = llvm::sys::path::parent_path(NewPath.str());
  if (!ParentPath.empty()) {
    // Make sure the new directory exists, creating it if necessary.
    if (std::error_code EC = llvm::sys::fs::create_directories(ParentPath))
      error(EC, "error creating the directory '" + ParentPath + "'");
  }
  return std::string(NewPath);
}

namespace thinlto {

std::vector<std::unique_ptr<MemoryBuffer>>
loadAllFilesForIndex(const ModuleSummaryIndex &Index) {
  std::vector<std::unique_ptr<MemoryBuffer>> InputBuffers;

  for (auto &ModPath : Index.modulePaths()) {
    const auto &Filename = ModPath.first();
    std::string CurrentActivity = ("loading file '" + Filename + "'").str();
    auto InputOrErr = MemoryBuffer::getFile(Filename);
    error(InputOrErr, "error " + CurrentActivity);
    InputBuffers.push_back(std::move(*InputOrErr));
  }
  return InputBuffers;
}

std::unique_ptr<ModuleSummaryIndex> loadCombinedIndex(const LTOOpts &Opts) {
  const auto &ThinLTOIndex = Opts.get<&ThinLTOIndexOpt>();
  if (ThinLTOIndex.empty())
    report_fatal_error("Missing -thinlto-index for ThinLTO promotion stage");
  ExitOnError ExitOnErr("llvm-lto: error loading file '" + ThinLTOIndex +
                        "': ");
  return ExitOnErr(getModuleSummaryIndexForFile(ThinLTOIndex));
}

static std::unique_ptr<lto::InputFile>
loadInputFile(MemoryBufferRef Buffer, const clv2::OptionsContext &OptsCtx) {
  ExitOnError ExitOnErr("llvm-lto: error loading input '" +
                        Buffer.getBufferIdentifier().str() + "': ");
  return ExitOnErr(lto::InputFile::create(Buffer, OptsCtx));
}

static std::unique_ptr<Module> loadModuleFromInput(lto::InputFile &File,
                                                   LLVMContext &CTX,
                                                   const LTOOpts &Opts) {
  auto &Mod = File.getSingleBitcodeModule();
  auto ModuleOrErr = Mod.parseModule(CTX);
  if (!ModuleOrErr) {
    handleAllErrors(ModuleOrErr.takeError(), [&](ErrorInfoBase &EIB) {
      SMDiagnostic Err = SMDiagnostic(Mod.getModuleIdentifier(),
                                      SourceMgr::DK_Error, EIB.message());
      Err.print("llvm-lto", errs());
    });
    report_fatal_error("Can't load module, abort.");
  }
  maybeVerifyModule(**ModuleOrErr, Opts);
  if (Opts.occurrences<&ThinLTOModuleIdOpt>()) {
    if (Opts.get<&InputFilenamesOpt>().size() != 1)
      report_fatal_error("Can't override the module id for multiple files");
    (*ModuleOrErr)->setModuleIdentifier(Opts.get<&ThinLTOModuleIdOpt>());
  }
  return std::move(*ModuleOrErr);
}

static void writeModuleToFile(Module &TheModule, StringRef Filename,
                              const LTOOpts &Opts) {
  std::error_code EC;
  raw_fd_ostream OS(Filename, EC, sys::fs::OpenFlags::OF_None);
  error(EC, "error opening the file '" + Filename + "'");
  maybeVerifyModule(TheModule, Opts);
  WriteBitcodeToFile(TheModule, OS, /* ShouldPreserveUseListOrder */ true);
}

class ThinLTOProcessing {
public:
  ThinLTOCodeGenerator ThinGenerator;
  const LTOOpts &Opts;
  clv2::OptionsContext *OptsCtx;

  ThinLTOProcessing(const TargetOptions &Options, const LTOOpts &Opts,
                    clv2::OptionsContext *OptsCtx)
      : Opts(Opts), OptsCtx(OptsCtx) {
    ThinGenerator.setOptionsContext(*OptsCtx);
    ThinGenerator.setCodePICModel(codegen::getExplicitRelocModel(*OptsCtx));
    ThinGenerator.setTargetOptions(Options);
    ThinGenerator.setCacheDir(Opts.get<&ThinLTOCacheDirOpt>());
    ThinGenerator.setCachePruningInterval(
        Opts.get<&ThinLTOCachePruningIntervalOpt>());
    ThinGenerator.setCacheEntryExpiration(
        Opts.get<&ThinLTOCacheEntryExpirationOpt>());
    ThinGenerator.setCacheMaxSizeFiles(
        Opts.get<&ThinLTOCacheMaxSizeFilesOpt>());
    ThinGenerator.setCacheMaxSizeBytes(
        Opts.get<&ThinLTOCacheMaxSizeBytesOpt>());
    ThinGenerator.setFreestanding(Opts.get<&EnableFreestandingOpt>());
    ThinGenerator.setDebugPassManager(Opts.get<&DebugPassManagerOpt>());

    const auto &ExportedSymbols = Opts.get<&ExportedSymbolsOpt>();
    for (unsigned i = 0; i < ExportedSymbols.size(); ++i)
      ThinGenerator.preserveSymbol(ExportedSymbols[i]);
  }

  void run() {
    switch (Opts.get<&ThinLTOModeOpt>()) {
    case THINLINK:
      return thinLink();
    case THINDISTRIBUTE:
      return distributedIndexes();
    case THINEMITIMPORTS:
      return emitImports();
    case THINPROMOTE:
      return promote();
    case THINIMPORT:
      return import();
    case THININTERNALIZE:
      return internalize();
    case THINOPT:
      return optimize();
    case THINCODEGEN:
      return codegen();
    case THINALL:
      return runAll();
    }
  }

private:
  /// Load the input files, create the combined index, and write it out.
  void thinLink() {
    const auto &OutputFilename = Opts.get<&OutputFilenameOpt>();
    if (OutputFilename.empty())
      report_fatal_error(
          "OutputFilename is necessary to store the combined index.\n");

    LLVMContext Ctx(*OptsCtx);
    const auto &InputFilenames = Opts.get<&InputFilenamesOpt>();
    std::vector<std::unique_ptr<MemoryBuffer>> InputBuffers;
    for (unsigned i = 0; i < InputFilenames.size(); ++i) {
      auto &Filename = InputFilenames[i];
      std::string CurrentActivity = "loading file '" + Filename + "'";
      auto InputOrErr = MemoryBuffer::getFile(Filename);
      error(InputOrErr, "error " + CurrentActivity);
      InputBuffers.push_back(std::move(*InputOrErr));
      ThinGenerator.addModule(Filename, InputBuffers.back()->getBuffer());
    }

    auto CombinedIndex = ThinGenerator.linkCombinedIndex();
    if (!CombinedIndex)
      report_fatal_error("ThinLink didn't create an index");
    std::error_code EC;
    raw_fd_ostream OS(OutputFilename, EC, sys::fs::OpenFlags::OF_None);
    error(EC, "error opening the file '" + OutputFilename + "'");
    writeIndexToFile(*CombinedIndex, OS, *OptsCtx);
  }

  void distributedIndexes() {
    const auto &InputFilenames = Opts.get<&InputFilenamesOpt>();
    const auto &OutputFilename = Opts.get<&OutputFilenameOpt>();
    if (InputFilenames.size() != 1 && !OutputFilename.empty())
      report_fatal_error("Can't handle a single output filename and multiple "
                         "input files, do not provide an output filename and "
                         "the output files will be suffixed from the input "
                         "ones.");

    std::string OldPrefix, NewPrefix;
    getThinLTOOldAndNewPrefix(OldPrefix, NewPrefix, Opts);

    auto Index = loadCombinedIndex(Opts);
    for (auto &Filename : InputFilenames) {
      LLVMContext Ctx(*OptsCtx);
      auto Buffer = loadFile(Filename);
      auto Input = loadInputFile(Buffer->getMemBufferRef(), *OptsCtx);
      auto TheModule = loadModuleFromInput(*Input, Ctx, Opts);

      ModuleToSummariesForIndexTy ModuleToSummariesForIndex;
      GVSummaryPtrSet DecSummaries;
      ThinGenerator.gatherImportedSummariesForModule(
          *TheModule, *Index, ModuleToSummariesForIndex, DecSummaries, *Input);

      std::string OutputName = OutputFilename;
      if (OutputName.empty()) {
        OutputName = Filename + ".thinlto.bc";
      }
      OutputName = getThinLTOOutputFile(OutputName, OldPrefix, NewPrefix);
      std::error_code EC;
      raw_fd_ostream OS(OutputName, EC, sys::fs::OpenFlags::OF_None);
      error(EC, "error opening the file '" + OutputName + "'");
      writeIndexToFile(*Index, OS, *OptsCtx, &ModuleToSummariesForIndex,
                       &DecSummaries);
    }
  }

  /// Load the combined index from disk, compute the imports, and emit
  /// the import file lists for each module to disk.
  void emitImports() {
    const auto &InputFilenames = Opts.get<&InputFilenamesOpt>();
    const auto &OutputFilename = Opts.get<&OutputFilenameOpt>();
    if (InputFilenames.size() != 1 && !OutputFilename.empty())
      report_fatal_error("Can't handle a single output filename and multiple "
                         "input files, do not provide an output filename and "
                         "the output files will be suffixed from the input "
                         "ones.");

    std::string OldPrefix, NewPrefix;
    getThinLTOOldAndNewPrefix(OldPrefix, NewPrefix, Opts);

    auto Index = loadCombinedIndex(Opts);
    for (auto &Filename : InputFilenames) {
      LLVMContext Ctx(*OptsCtx);
      auto Buffer = loadFile(Filename);
      auto Input = loadInputFile(Buffer->getMemBufferRef(), *OptsCtx);
      auto TheModule = loadModuleFromInput(*Input, Ctx, Opts);
      std::string OutputName = OutputFilename;
      if (OutputName.empty()) {
        OutputName = Filename + ".imports";
      }
      OutputName = getThinLTOOutputFile(OutputName, OldPrefix, NewPrefix);
      ThinGenerator.emitImports(*TheModule, OutputName, *Index, *Input);
    }
  }

  /// Load the combined index from disk, then load every file referenced by
  /// the index and add them to the generator, finally perform the promotion
  /// on the files mentioned on the command line (these must match the index
  /// content).
  void promote() {
    const auto &InputFilenames = Opts.get<&InputFilenamesOpt>();
    const auto &OutputFilename = Opts.get<&OutputFilenameOpt>();
    if (InputFilenames.size() != 1 && !OutputFilename.empty())
      report_fatal_error("Can't handle a single output filename and multiple "
                         "input files, do not provide an output filename and "
                         "the output files will be suffixed from the input "
                         "ones.");

    auto Index = loadCombinedIndex(Opts);
    for (auto &Filename : InputFilenames) {
      LLVMContext Ctx(*OptsCtx);
      auto Buffer = loadFile(Filename);
      auto Input = loadInputFile(Buffer->getMemBufferRef(), *OptsCtx);
      auto TheModule = loadModuleFromInput(*Input, Ctx, Opts);

      ThinGenerator.promote(*TheModule, *Index, *Input);

      std::string OutputName = OutputFilename;
      if (OutputName.empty()) {
        OutputName = Filename + ".thinlto.promoted.bc";
      }
      writeModuleToFile(*TheModule, OutputName, Opts);
    }
  }

  void import() {
    const auto &InputFilenames = Opts.get<&InputFilenamesOpt>();
    const auto &OutputFilename = Opts.get<&OutputFilenameOpt>();
    if (InputFilenames.size() != 1 && !OutputFilename.empty())
      report_fatal_error("Can't handle a single output filename and multiple "
                         "input files, do not provide an output filename and "
                         "the output files will be suffixed from the input "
                         "ones.");

    auto Index = loadCombinedIndex(Opts);
    auto InputBuffers = loadAllFilesForIndex(*Index);
    for (auto &MemBuffer : InputBuffers)
      ThinGenerator.addModule(MemBuffer->getBufferIdentifier(),
                              MemBuffer->getBuffer());

    for (auto &Filename : InputFilenames) {
      LLVMContext Ctx(*OptsCtx);
      auto Buffer = loadFile(Filename);
      auto Input = loadInputFile(Buffer->getMemBufferRef(), *OptsCtx);
      auto TheModule = loadModuleFromInput(*Input, Ctx, Opts);

      ThinGenerator.crossModuleImport(*TheModule, *Index, *Input);

      std::string OutputName = OutputFilename;
      if (OutputName.empty()) {
        OutputName = Filename + ".thinlto.imported.bc";
      }
      writeModuleToFile(*TheModule, OutputName, Opts);
    }
  }

  void internalize() {
    const auto &InputFilenames = Opts.get<&InputFilenamesOpt>();
    const auto &OutputFilename = Opts.get<&OutputFilenameOpt>();
    if (InputFilenames.size() != 1 && !OutputFilename.empty())
      report_fatal_error("Can't handle a single output filename and multiple "
                         "input files, do not provide an output filename and "
                         "the output files will be suffixed from the input "
                         "ones.");

    if (Opts.get<&ExportedSymbolsOpt>().empty())
      errs() << "Warning: -internalize will not perform without "
                "-exported-symbol\n";

    auto Index = loadCombinedIndex(Opts);
    auto InputBuffers = loadAllFilesForIndex(*Index);
    for (auto &MemBuffer : InputBuffers)
      ThinGenerator.addModule(MemBuffer->getBufferIdentifier(),
                              MemBuffer->getBuffer());

    for (auto &Filename : InputFilenames) {
      LLVMContext Ctx(*OptsCtx);
      auto Buffer = loadFile(Filename);
      auto Input = loadInputFile(Buffer->getMemBufferRef(), *OptsCtx);
      auto TheModule = loadModuleFromInput(*Input, Ctx, Opts);

      ThinGenerator.internalize(*TheModule, *Index, *Input);

      std::string OutputName = OutputFilename;
      if (OutputName.empty()) {
        OutputName = Filename + ".thinlto.internalized.bc";
      }
      writeModuleToFile(*TheModule, OutputName, Opts);
    }
  }

  void optimize() {
    const auto &InputFilenames = Opts.get<&InputFilenamesOpt>();
    const auto &OutputFilename = Opts.get<&OutputFilenameOpt>();
    if (InputFilenames.size() != 1 && !OutputFilename.empty())
      report_fatal_error("Can't handle a single output filename and multiple "
                         "input files, do not provide an output filename and "
                         "the output files will be suffixed from the input "
                         "ones.");
    if (!Opts.get<&ThinLTOIndexOpt>().empty())
      errs() << "Warning: -thinlto-index ignored for optimize stage";

    for (auto &Filename : InputFilenames) {
      LLVMContext Ctx(*OptsCtx);
      auto Buffer = loadFile(Filename);
      auto Input = loadInputFile(Buffer->getMemBufferRef(), *OptsCtx);
      auto TheModule = loadModuleFromInput(*Input, Ctx, Opts);

      ThinGenerator.optimize(*TheModule);

      std::string OutputName = OutputFilename;
      if (OutputName.empty()) {
        OutputName = Filename + ".thinlto.imported.bc";
      }
      writeModuleToFile(*TheModule, OutputName, Opts);
    }
  }

  void codegen() {
    const auto &InputFilenames = Opts.get<&InputFilenamesOpt>();
    const auto &OutputFilename = Opts.get<&OutputFilenameOpt>();
    if (InputFilenames.size() != 1 && !OutputFilename.empty())
      report_fatal_error("Can't handle a single output filename and multiple "
                         "input files, do not provide an output filename and "
                         "the output files will be suffixed from the input "
                         "ones.");
    if (!Opts.get<&ThinLTOIndexOpt>().empty())
      errs() << "Warning: -thinlto-index ignored for codegen stage";

    std::vector<std::unique_ptr<MemoryBuffer>> InputBuffers;
    for (auto &Filename : InputFilenames) {
      LLVMContext Ctx(*OptsCtx);
      auto InputOrErr = MemoryBuffer::getFile(Filename);
      error(InputOrErr, "error " + CurrentActivity);
      InputBuffers.push_back(std::move(*InputOrErr));
      ThinGenerator.addModule(Filename, InputBuffers.back()->getBuffer());
    }
    ThinGenerator.setCodeGenOnly(true);
    ThinGenerator.run();
    for (auto BinName :
         zip(ThinGenerator.getProducedBinaries(), InputFilenames)) {
      std::string OutputName = OutputFilename;
      if (OutputName.empty())
        OutputName = std::get<1>(BinName) + ".thinlto.o";
      else if (OutputName == "-") {
        outs() << std::get<0>(BinName)->getBuffer();
        return;
      }

      std::error_code EC;
      raw_fd_ostream OS(OutputName, EC, sys::fs::OpenFlags::OF_None);
      error(EC, "error opening the file '" + OutputName + "'");
      OS << std::get<0>(BinName)->getBuffer();
    }
  }

  /// Full ThinLTO process
  void runAll() {
    const auto &InputFilenames = Opts.get<&InputFilenamesOpt>();
    const auto &OutputFilename = Opts.get<&OutputFilenameOpt>();
    if (!OutputFilename.empty())
      report_fatal_error("Do not provide an output filename for ThinLTO "
                         " processing, the output files will be suffixed from "
                         "the input ones.");

    if (!Opts.get<&ThinLTOIndexOpt>().empty())
      errs() << "Warning: -thinlto-index ignored for full ThinLTO process";

    LLVMContext Ctx(*OptsCtx);
    std::vector<std::unique_ptr<MemoryBuffer>> InputBuffers;
    for (unsigned i = 0; i < InputFilenames.size(); ++i) {
      auto &Filename = InputFilenames[i];
      std::string CurrentActivity = "loading file '" + Filename + "'";
      auto InputOrErr = MemoryBuffer::getFile(Filename);
      error(InputOrErr, "error " + CurrentActivity);
      InputBuffers.push_back(std::move(*InputOrErr));
      ThinGenerator.addModule(Filename, InputBuffers.back()->getBuffer());
    }

    if (!Opts.get<&ThinLTOSaveTempsPrefixOpt>().empty())
      ThinGenerator.setSaveTempsDir(Opts.get<&ThinLTOSaveTempsPrefixOpt>());

    if (!Opts.get<&ThinLTOGeneratedObjectsDirOpt>().empty()) {
      ThinGenerator.setGeneratedObjectsDirectory(
          Opts.get<&ThinLTOGeneratedObjectsDirOpt>());
      ThinGenerator.run();
      return;
    }

    ThinGenerator.run();

    auto &Binaries = ThinGenerator.getProducedBinaries();
    if (Binaries.size() != InputFilenames.size())
      report_fatal_error("Number of output objects does not match the number "
                         "of inputs");

    for (unsigned BufID = 0; BufID < Binaries.size(); ++BufID) {
      auto OutputName = InputFilenames[BufID] + ".thinlto.o";
      std::error_code EC;
      raw_fd_ostream OS(OutputName, EC, sys::fs::OpenFlags::OF_None);
      error(EC, "error opening the file '" + OutputName + "'");
      OS << Binaries[BufID]->getBuffer();
    }
  }

  /// Load the combined index from disk, then load every file referenced by
};

} // end namespace thinlto

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);
  clv2::OptionParser P;
  P.add<&LTOToolReg>();
  RegisterAllLLVMOptions(P);
  P.add<&clv2::X86OptsReg>();
  P.add<&clv2::AArch64OptsReg>();
  P.add<&clv2::AMDGPUOptsReg>();
  P.add<&clv2::ARMOptsReg>();
  P.add<&clv2::HexagonOptsReg>();
  P.add<&clv2::RISCVOptsReg>();
  P.add<&clv2::PowerPCOptsReg>();
  P.add<&clv2::MipsOptsReg>();
  P.add<&clv2::SystemZOptsReg>();
  P.add<&clv2::SparcOptsReg>();
  P.add<&clv2::WebAssemblyOptsReg>();
  P.add<&clv2::LoongArchOptsReg>();
  P.add<&clv2::NVPTXOptsReg>();
  P.add<&clv2::LanaiOptsReg>();
  P.add<&clv2::BPFOptsReg>();
  P.add<&clv2::SPIRVOptsReg>();
  P.add<&clv2::MSP430OptsReg>();
  P.add<&clv2::XCoreOptsReg>();
  P.hideUnrelatedOptions({&LTOCategory, &clv2::ColorOptionsCategory});
  auto OptsCtx = P.parse(argc, argv, "llvm LTO linker\n");
  const auto &ToolOpts = *OptsCtx->getViewPtr<&LTOToolReg>();

  if (ToolOpts.get<&OptLevelOpt>() > 3)
    error("optimization level must be between 0 and 3");

  // Initialize the configured targets.
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();

  // set up the TargetOptions for the machine
  TargetOptions Options =
      codegen::InitTargetOptionsFromCodeGenFlags(Triple(), *OptsCtx);
  if (OptsCtx)
    Options.OptsCtx = OptsCtx.get();

  if (ToolOpts.get<&ListSymbolsOnlyOpt>() ||
      ToolOpts.get<&QueryHasCtorDtorOpt>()) {
    testLTOModule(Options, ToolOpts, OptsCtx.get());
    return 0;
  }

  if (ToolOpts.get<&ListDependentLibrariesOnlyOpt>()) {
    listDependentLibraries(ToolOpts);
    return 0;
  }

  if (ToolOpts.get<&IndexStatsOpt>()) {
    printIndexStats(ToolOpts);
    return 0;
  }

  if (ToolOpts.get<&CheckHasObjCOpt>()) {
    for (auto &Filename : ToolOpts.get<&InputFilenamesOpt>()) {
      ExitOnError ExitOnErr(std::string(*argv) + ": error loading file '" +
                            Filename + "': ");
      std::unique_ptr<MemoryBuffer> BufferOrErr =
          ExitOnErr(errorOrToExpected(MemoryBuffer::getFile(Filename)));
      auto Buffer = std::move(BufferOrErr.get());
      if (ExitOnErr(isBitcodeContainingObjCCategory(*Buffer)))
        outs() << "Bitcode " << Filename << " contains ObjC\n";
      else
        outs() << "Bitcode " << Filename << " does not contain ObjC\n";
    }
    return 0;
  }

  if (ToolOpts.get<&PrintMachOCPUOnlyOpt>()) {
    printMachOCPUOnly(ToolOpts, OptsCtx.get());
    return 0;
  }

  if (unsigned ThinLTOModeOcc = ToolOpts.occurrences<&ThinLTOModeOpt>()) {
    if (ThinLTOModeOcc > 1)
      report_fatal_error("You can't specify more than one -thinlto-action");
    thinlto::ThinLTOProcessing ThinLTOProcessor(Options, ToolOpts,
                                                OptsCtx.get());
    ThinLTOProcessor.run();
    return 0;
  }

  if (ToolOpts.get<&ThinLTOOpt>()) {
    createCombinedModuleSummaryIndex(ToolOpts, *OptsCtx);
    return 0;
  }

  unsigned BaseArg = 0;

  LLVMContext Context(*OptsCtx);
  Context.setDiagnosticHandler(std::make_unique<LLVMLTODiagnosticHandler>(),
                               true);

  LTOCodeGenerator CodeGen(Context);
  CodeGen.setDisableVerify(ToolOpts.get<&DisableVerifyOpt>());

  if (ToolOpts.get<&UseDiagnosticHandlerOpt>())
    CodeGen.setDiagnosticHandler(handleDiagnostics, nullptr);

  CodeGen.setCodePICModel(codegen::getExplicitRelocModel(*OptsCtx));
  CodeGen.setFreestanding(ToolOpts.get<&EnableFreestandingOpt>());
  CodeGen.setDebugPassManager(ToolOpts.get<&DebugPassManagerOpt>());

  CodeGen.setDebugInfo(LTO_DEBUG_MODEL_DWARF);
  CodeGen.setTargetOptions(Options);
  CodeGen.setShouldRestoreGlobalsLinkage(
      ToolOpts.get<&RestoreGlobalsLinkageOpt>());

  const auto &InputFilenames = ToolOpts.get<&InputFilenamesOpt>();
  const auto &OutputFilename = ToolOpts.get<&OutputFilenameOpt>();
  StringSet<MallocAllocator> DSOSymbolsSet(llvm::from_range,
                                           ToolOpts.get<&DSOSymbolsOpt>());

  std::vector<std::string> KeptDSOSyms;

  for (unsigned i = BaseArg; i < InputFilenames.size(); ++i) {
    CurrentActivity = "loading file '" + InputFilenames[i] + "'";
    ErrorOr<std::unique_ptr<LTOModule>> ModuleOrErr =
        LTOModule::createFromFile(Context, InputFilenames[i], Options);
    std::unique_ptr<LTOModule> &Module = *ModuleOrErr;
    CurrentActivity = "";

    unsigned NumSyms = Module->getSymbolCount();
    for (unsigned I = 0; I < NumSyms; ++I) {
      StringRef Name = Module->getSymbolName(I);
      if (!DSOSymbolsSet.count(Name))
        continue;
      lto_symbol_attributes Attrs = Module->getSymbolAttributes(I);
      unsigned Scope = Attrs & LTO_SYMBOL_SCOPE_MASK;
      if (Scope != LTO_SYMBOL_SCOPE_DEFAULT_CAN_BE_HIDDEN)
        KeptDSOSyms.push_back(std::string(Name));
    }

    // We use the first input module as the destination module when
    // SetMergedModule is true.
    if (ToolOpts.get<&SetMergedModuleOpt>() && i == BaseArg) {
      // Transfer ownership to the code generator.
      CodeGen.setModule(std::move(Module));
    } else if (!CodeGen.addModule(Module.get())) {
      // Print a message here so that we know addModule() did not abort.
      error("error adding file '" + InputFilenames[i] + "'");
    }
  }

  // Add all the exported symbols to the table of symbols to preserve.
  const auto &ExportedSymbols = ToolOpts.get<&ExportedSymbolsOpt>();
  for (unsigned i = 0; i < ExportedSymbols.size(); ++i)
    CodeGen.addMustPreserveSymbol(ExportedSymbols[i]);

  // Add all the dso symbols to the table of symbols to expose.
  for (unsigned i = 0; i < KeptDSOSyms.size(); ++i)
    CodeGen.addMustPreserveSymbol(KeptDSOSyms[i]);

  // Set cpu and attrs strings for the default target/subtarget.
  CodeGen.setCpu(codegen::getMCPU(*OptsCtx));

  CodeGen.setOptLevel(ToolOpts.get<&OptLevelOpt>());
  CodeGen.setAttrs(codegen::getMAttrs(*OptsCtx));

  if (auto FT = codegen::getExplicitFileType(*OptsCtx))
    CodeGen.setFileType(*FT);

  unsigned Parallelism = ToolOpts.get<&ParallelismOpt>();
  if (!OutputFilename.empty()) {
    if (ToolOpts.get<&LTOSaveBeforeOptOpt>())
      CodeGen.setSaveIRBeforeOptPath(OutputFilename + ".0.preopt.bc");

    if (ToolOpts.get<&SaveLinkedModuleFileOpt>()) {
      std::string ModuleFilename = OutputFilename;
      ModuleFilename += ".linked.bc";

      if (!CodeGen.writeMergedModules(ModuleFilename))
        error("writing linked module failed.");
    }

    if (!CodeGen.optimize()) {
      // Diagnostic messages should have been printed by the handler.
      error("error optimizing the code");
    }

    if (ToolOpts.get<&SaveModuleFileOpt>()) {
      std::string ModuleFilename = OutputFilename;
      ModuleFilename += ".merged.bc";

      if (!CodeGen.writeMergedModules(ModuleFilename))
        error("writing merged module failed.");
    }

    auto AddStream =
        [&](size_t Task,
            const Twine &ModuleName) -> std::unique_ptr<CachedFileStream> {
      std::string PartFilename = OutputFilename;
      if (Parallelism != 1)
        PartFilename += "." + utostr(Task);

      std::error_code EC;
      auto S =
          std::make_unique<raw_fd_ostream>(PartFilename, EC, sys::fs::OF_None);
      if (EC)
        error("error opening the file '" + PartFilename + "': " + EC.message());
      return std::make_unique<CachedFileStream>(std::move(S));
    };

    if (!CodeGen.compileOptimized(AddStream, Parallelism))
      // Diagnostic messages should have been printed by the handler.
      error("error compiling the code");

  } else {
    if (Parallelism != 1)
      error("-j must be specified together with -o");

    if (ToolOpts.get<&SaveModuleFileOpt>())
      error(": -save-merged-module must be specified with -o");

    const char *OutputName = nullptr;
    if (!CodeGen.compile_to_file(&OutputName))
      error("error compiling the code");
    // Diagnostic messages should have been printed by the handler.

    outs() << "Wrote native object file '" << OutputName << "'\n";
  }

  return 0;
}
