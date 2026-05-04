//===- llvm-link.cpp - Low-level LLVM linker ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This utility may be invoked in the following manner:
//  llvm-link a.bc b.bc c.bc -o x.bc
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/STLExtras.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/AutoUpgrade.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Object/Archive.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Transforms/IPO/FunctionImport.h"
#include "llvm/Transforms/IPO/Internalize.h"
#include "llvm/Transforms/Utils/AssignGUID.h"
#include "llvm/Transforms/Utils/FunctionImportUtils.h"

#include <memory>
#include <utility>
using namespace llvm;
using namespace llvm::clv2;

static constexpr OptionCategory LinkCategory{"Link Options"};

static constexpr ListOptionInfo<std::string> InputFilenames{
    "inputs", "<input bitcode files>", Positional{}, OneOrMore,
    cat(LinkCategory)};

static constexpr ListOptionInfo<std::string> OverridingInputs{
    "override",
    "input bitcode file which can override previously defined symbol(s)",
    value_desc("filename"), cat(LinkCategory)};

static constexpr ListOptionInfo<std::string> Imports{
    "import",
    "Pair of function name and filename, where function should be "
    "imported from bitcode in filename",
    value_desc("function:filename"), cat(LinkCategory)};

static constexpr OptionInfo<std::string> SummaryIndex{
    "summary-index", "Module summary index filename", Init{""},
    value_desc("filename"), cat(LinkCategory)};

static constexpr OptionInfo<std::string> OutputFilename{
    "o", "Override output filename", Init{"-"}, value_desc("filename"),
    cat(LinkCategory)};

static constexpr OptionInfo<bool> Internalize{
    "internalize",
    "Internalize linked symbols - maintains existing "
    "linkage for the first input and converts linkage in"
    " all other inputs to `internal`",
    cat(LinkCategory)};

static constexpr OptionInfo<bool> DisableDITypeMap{
    "disable-debug-info-type-map",
    "Don't use a uniquing type map for debug info", cat(LinkCategory)};

static constexpr OptionInfo<bool> OnlyNeeded{
    "only-needed", "Link only needed symbols", cat(LinkCategory)};

static constexpr OptionInfo<bool> Force{
    "f", "Enable binary output on terminals", cat(LinkCategory)};

static constexpr OptionInfo<bool> DisableLazyLoad{
    "disable-lazy-loading", "Disable lazy module loading", cat(LinkCategory)};

static constexpr OptionInfo<bool> OutputAssembly{
    "S", "Write output as LLVM assembly", Hidden, cat(LinkCategory)};

static constexpr OptionInfo<bool> Verbose{
    "v", "Print information about actions taken", cat(LinkCategory)};

static constexpr OptionInfo<bool> DumpAsm{"d", "Print assembly as linked",
                                          Hidden, cat(LinkCategory)};

static constexpr OptionInfo<bool> SuppressWarnings{
    "suppress-warnings", "Suppress all linking warnings", Init{false},
    cat(LinkCategory)};

static constexpr OptionInfo<bool> NoVerify{
    "disable-verify", "Do not run the verifier", Hidden, cat(LinkCategory)};

static constexpr OptionInfo<bool> IgnoreNonBitcode{
    "ignore-non-bitcode",
    "Do not report an error for non-bitcode files in archives", Hidden};

static constexpr OptionsRegistry<
    &InputFilenames, &OverridingInputs, &Imports, &SummaryIndex,
    &OutputFilename, &Internalize, &DisableDITypeMap, &OnlyNeeded, &Force,
    &DisableLazyLoad, &OutputAssembly, &Verbose, &DumpAsm, &SuppressWarnings,
    &NoVerify, &IgnoreNonBitcode>
    LinkToolReg;

static ExitOnError ExitOnErr;

struct LinkOpts {
  bool Verbose;
  bool DisableLazyLoad;
  bool IgnoreNonBitcode;
  bool SuppressWarnings;
  bool NoVerify;
  bool DisableDITypeMap;
  bool Internalize;
  const std::string &SummaryIndexFile;
};

static std::unique_ptr<Module> loadFile(const char *argv0,
                                        std::unique_ptr<MemoryBuffer> Buffer,
                                        LLVMContext &Context,
                                        const LinkOpts &LOpts,
                                        bool MaterializeMetadata = true) {
  SMDiagnostic Err;
  if (LOpts.Verbose)
    errs() << "Loading '" << Buffer->getBufferIdentifier() << "'\n";
  std::unique_ptr<Module> Result;
  if (LOpts.DisableLazyLoad)
    Result = parseIR(*Buffer, Err, Context);
  else
    Result =
        getLazyIRModule(std::move(Buffer), Err, Context, !MaterializeMetadata);

  if (!Result) {
    Err.print(argv0, errs());
    return nullptr;
  }

  if (MaterializeMetadata) {
    ExitOnErr(Result->materializeMetadata());
    UpgradeDebugInfo(*Result);
  }

  return Result;
}

static std::unique_ptr<Module> loadArFile(const char *Argv0,
                                          std::unique_ptr<MemoryBuffer> Buffer,
                                          LLVMContext &Context,
                                          const LinkOpts &LOpts) {
  std::unique_ptr<Module> Result(new Module("ArchiveModule", Context));
  StringRef ArchiveName = Buffer->getBufferIdentifier();
  if (LOpts.Verbose)
    errs() << "Reading library archive file '" << ArchiveName
           << "' to memory\n";
  Expected<std::unique_ptr<object::Archive>> ArchiveOrError =
      object::Archive::create(Buffer->getMemBufferRef());
  if (!ArchiveOrError)
    ExitOnErr(ArchiveOrError.takeError());

  std::unique_ptr<object::Archive> Archive = std::move(ArchiveOrError.get());

  Linker L(*Result);
  Error Err = Error::success();
  for (const object::Archive::Child &C : Archive->children(Err)) {
    Expected<StringRef> Ename = C.getName();
    if (Error E = Ename.takeError()) {
      errs() << Argv0 << ": ";
      WithColor::error() << " failed to read name of archive member"
                         << ArchiveName << "'\n";
      return nullptr;
    }
    std::string ChildName = Ename.get().str();
    if (LOpts.Verbose)
      errs() << "Parsing member '" << ChildName
             << "' of archive library to module.\n";
    SMDiagnostic ParseErr;
    Expected<MemoryBufferRef> MemBuf = C.getMemoryBufferRef();
    if (Error E = MemBuf.takeError()) {
      errs() << Argv0 << ": ";
      WithColor::error() << " loading memory for member '" << ChildName
                         << "' of archive library failed'" << ArchiveName
                         << "'\n";
      return nullptr;
    };

    if (!isBitcode(reinterpret_cast<const unsigned char *>(
                       MemBuf.get().getBufferStart()),
                   reinterpret_cast<const unsigned char *>(
                       MemBuf.get().getBufferEnd()))) {
      if (LOpts.IgnoreNonBitcode)
        continue;
      errs() << Argv0 << ": ";
      WithColor::error() << "  member of archive is not a bitcode file: '"
                         << ChildName << "'\n";
      return nullptr;
    }

    std::unique_ptr<Module> M;
    if (LOpts.DisableLazyLoad)
      M = parseIR(MemBuf.get(), ParseErr, Context);
    else
      M = getLazyIRModule(MemoryBuffer::getMemBuffer(MemBuf.get(), false),
                          ParseErr, Context);

    if (!M) {
      errs() << Argv0 << ": ";
      WithColor::error() << " parsing member '" << ChildName
                         << "' of archive library failed'" << ArchiveName
                         << "'\n";
      return nullptr;
    }
    if (LOpts.Verbose)
      errs() << "Linking member '" << ChildName << "' of archive library.\n";
    if (L.linkInModule(std::move(M)))
      return nullptr;
  }
  ExitOnErr(std::move(Err));
  return Result;
}

namespace {

class ModuleLazyLoaderCache {
  StringMap<std::unique_ptr<Module>> ModuleMap;

  std::function<std::unique_ptr<Module>(const char *argv0,
                                        const std::string &FileName)>
      createLazyModule;

public:
  ModuleLazyLoaderCache(std::function<std::unique_ptr<Module>(
                            const char *argv0, const std::string &FileName)>
                            createLazyModule)
      : createLazyModule(std::move(createLazyModule)) {}

  Module &operator()(const char *argv0, const std::string &FileName);

  std::unique_ptr<Module> takeModule(const std::string &FileName) {
    auto I = ModuleMap.find(FileName);
    assert(I != ModuleMap.end());
    std::unique_ptr<Module> Ret = std::move(I->second);
    ModuleMap.erase(I);
    return Ret;
  }
};

Module &ModuleLazyLoaderCache::operator()(const char *argv0,
                                          const std::string &Identifier) {
  auto &Module = ModuleMap[Identifier];
  if (!Module) {
    Module = createLazyModule(argv0, Identifier);
    assert(Module && "Failed to create lazy module!");
  }
  return *Module;
}
} // anonymous namespace

namespace {
struct LLVMLinkDiagnosticHandler : public DiagnosticHandler {
  bool SuppressWarnings;
  LLVMLinkDiagnosticHandler(bool SW) : SuppressWarnings(SW) {}
  bool handleDiagnostics(const DiagnosticInfo &DI) override {
    unsigned Severity = DI.getSeverity();
    switch (Severity) {
    case DS_Error:
      WithColor::error();
      break;
    case DS_Warning:
      if (SuppressWarnings)
        return true;
      WithColor::warning();
      break;
    case DS_Remark:
    case DS_Note:
      llvm_unreachable("Only expecting warnings and errors");
    }

    DiagnosticPrinterRawOStream DP(errs());
    DI.print(DP);
    errs() << '\n';
    return true;
  }
};
} // namespace

static bool importFunctions(const char *argv0, Module &DestModule,
                            const std::vector<std::string> &ImportsOpt,
                            const std::string &SummaryIndexFile,
                            const LinkOpts &LOpts) {
  if (SummaryIndexFile.empty())
    return true;
  std::unique_ptr<ModuleSummaryIndex> Index =
      ExitOnErr(llvm::getModuleSummaryIndexForFile(SummaryIndexFile));

  FunctionImporter::ImportIDTable ImportIDs;
  FunctionImporter::ImportMapTy ImportList(ImportIDs);

  auto ModuleLoader = [&DestModule, &LOpts](const char *argv0,
                                            const std::string &Identifier) {
    std::unique_ptr<MemoryBuffer> Buffer = ExitOnErr(errorOrToExpected(
        MemoryBuffer::getFileOrSTDIN(Identifier, /*IsText=*/true)));
    return loadFile(argv0, std::move(Buffer), DestModule.getContext(), LOpts,
                    false);
  };

  ModuleLazyLoaderCache ModuleLoaderCache(ModuleLoader);
  StringSet<> FileNameStringCache;
  for (const auto &Import : ImportsOpt) {
    size_t Idx = Import.find(':');
    if (Idx == std::string::npos) {
      errs() << "Import parameter bad format: " << Import << "\n";
      return false;
    }
    std::string FunctionName = Import.substr(0, Idx);
    std::string FileName = Import.substr(Idx + 1, std::string::npos);

    auto &SrcModule = ModuleLoaderCache(argv0, FileName);

    if (!LOpts.NoVerify && verifyModule(SrcModule, &errs())) {
      errs() << argv0 << ": " << FileName;
      WithColor::error() << "input module is broken!\n";
      return false;
    }

    Function *F = SrcModule.getFunction(FunctionName);
    if (!F) {
      errs() << "Ignoring import request for non-existent function "
             << FunctionName << " from " << FileName << "\n";
      continue;
    }
    if (F->hasWeakAnyLinkage()) {
      errs() << "Ignoring import request for weak-any function " << FunctionName
             << " from " << FileName << "\n";
      continue;
    }

    if (LOpts.Verbose)
      errs() << "Importing " << FunctionName << " from " << FileName << "\n";

    // `-import` specifies the `<filename,function-name>` pairs to import as
    // definition, so make the import type definition directly.
    // FIXME: A follow-up patch should add test coverage for import declaration
    // in `llvm-link` CLI (e.g., by introducing a new command line option).
    const auto GUID = F->getGUIDOrFallback();
    ImportList.addDefinition(
        FileNameStringCache.insert(FileName).first->getKey(), GUID);
  }
  auto CachedModuleLoader = [&](StringRef Identifier) {
    return ModuleLoaderCache.takeModule(std::string(Identifier));
  };
  AssignGUIDPass::runOnModule(DestModule);
  FunctionImporter Importer(*Index, CachedModuleLoader,
                            /*ClearDSOLocalOnDeclarations=*/false);
  ExitOnErr(Importer.importFunctions(DestModule, ImportList));

  return true;
}

static bool linkFiles(const char *argv0, LLVMContext &Context, Linker &L,
                      const std::vector<std::string> &Files, unsigned Flags,
                      const LinkOpts &LOpts) {
  unsigned ApplicableFlags = Flags & Linker::Flags::OverrideFromSrc;
  bool InternalizeLinkedSymbols = false;
  for (const auto &File : Files) {
    auto BufferOrErr = MemoryBuffer::getFileOrSTDIN(File, /*IsText=*/true);

    if (auto EC = BufferOrErr.getError())
      if (EC == std::errc::no_such_file_or_directory)
        ExitOnErr(createStringError(EC, "No such file or directory: '%s'",
                                    File.c_str()));

    std::unique_ptr<MemoryBuffer> Buffer =
        ExitOnErr(errorOrToExpected(std::move(BufferOrErr)));

    std::unique_ptr<Module> M =
        identify_magic(Buffer->getBuffer()) == file_magic::archive
            ? loadArFile(argv0, std::move(Buffer), Context, LOpts)
            : loadFile(argv0, std::move(Buffer), Context, LOpts);
    if (!M) {
      errs() << argv0 << ": ";
      WithColor::error() << " loading file '" << File << "'\n";
      return false;
    }

    if (LOpts.DisableDITypeMap && !LOpts.NoVerify &&
        verifyModule(*M, &errs())) {
      errs() << argv0 << ": " << File << ": ";
      WithColor::error() << "input module is broken!\n";
      return false;
    }

    if (!LOpts.SummaryIndexFile.empty()) {
      std::unique_ptr<ModuleSummaryIndex> Index =
          ExitOnErr(llvm::getModuleSummaryIndexForFile(LOpts.SummaryIndexFile));

      for (auto &I : *Index) {
        for (auto &S : I.second.getSummaryList()) {
          if (GlobalValue::isLocalLinkage(S->linkage()))
            S->setExternalLinkageForTest();
        }
      }

      renameModuleForThinLTO(*M, *Index,
                             /*ClearDSOLocalOnDeclarations=*/false);
    }

    if (LOpts.Verbose)
      errs() << "Linking in '" << File << "'\n";

    bool Err = false;
    if (InternalizeLinkedSymbols) {
      Err = L.linkInModule(
          std::move(M), ApplicableFlags, [](Module &M, const StringSet<> &GVS) {
            internalizeModule(M, [&GVS](const GlobalValue &GV) {
              return !GV.hasName() || (GVS.count(GV.getName()) == 0);
            });
          });
    } else {
      Err = L.linkInModule(std::move(M), ApplicableFlags);
    }

    if (Err)
      return false;

    InternalizeLinkedSymbols = LOpts.Internalize;
    ApplicableFlags = Flags;
  }

  return true;
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);
  ExitOnErr.setBanner(std::string(argv[0]) + ": ");

  clv2::OptionParser P;
  P.add<&LinkToolReg>();
  RegisterAllLLVMOptions(P);
  P.hideUnrelatedOptions({&LinkCategory, &getColorCategory()});
  auto OptsCtx = P.parse(argc, argv, "llvm linker\n");
  auto *Opts = OptsCtx->getViewPtr<&LinkToolReg>();

  LinkOpts LOpts{
      Opts->get<&Verbose>(),          Opts->get<&DisableLazyLoad>(),
      Opts->get<&IgnoreNonBitcode>(), Opts->get<&SuppressWarnings>(),
      Opts->get<&NoVerify>(),         Opts->get<&DisableDITypeMap>(),
      Opts->get<&Internalize>(),      Opts->get<&SummaryIndex>(),
  };

  LLVMContext Context(*OptsCtx);
  Context.setDiagnosticHandler(
      std::make_unique<LLVMLinkDiagnosticHandler>(LOpts.SuppressWarnings),
      true);

  if (!LOpts.DisableDITypeMap)
    Context.enableDebugTypeODRUniquing();

  auto Composite = std::make_unique<Module>("llvm-link", Context);
  Linker L(*Composite);

  unsigned Flags = Linker::Flags::None;
  if (Opts->get<&OnlyNeeded>())
    Flags |= Linker::Flags::LinkOnlyNeeded;

  if (!linkFiles(argv[0], Context, L, Opts->get<&InputFilenames>(), Flags,
                 LOpts))
    return 1;

  if (!linkFiles(argv[0], Context, L, Opts->get<&OverridingInputs>(),
                 Flags | Linker::Flags::OverrideFromSrc, LOpts))
    return 1;

  if (!importFunctions(argv[0], *Composite, Opts->get<&Imports>(),
                       Opts->get<&SummaryIndex>(), LOpts))
    return 1;

  if (Opts->get<&DumpAsm>())
    errs() << "Here's the assembly:\n" << *Composite;

  std::error_code EC;
  ToolOutputFile Out(Opts->get<&OutputFilename>(), EC,
                     Opts->get<&OutputAssembly>() ? sys::fs::OF_TextWithCRLF
                                                  : sys::fs::OF_None);
  if (EC) {
    WithColor::error() << EC.message() << '\n';
    return 1;
  }

  if (!LOpts.NoVerify && verifyModule(*Composite, &errs())) {
    errs() << argv[0] << ": ";
    WithColor::error() << "linked module is broken!\n";
    return 1;
  }

  if (LOpts.Verbose)
    errs() << "Writing bitcode...\n";
  Composite->removeDebugIntrinsicDeclarations();
  if (Opts->get<&OutputAssembly>()) {
    Composite->print(Out.os(), nullptr, /* ShouldPreserveUseListOrder */ false);
  } else if (Opts->get<&Force>() || !CheckBitcodeOutputToConsole(Out.os())) {
    WriteBitcodeToFile(*Composite, Out.os(),
                       /* ShouldPreserveUseListOrder */ true);
  }

  Out.keep();

  return 0;
}
