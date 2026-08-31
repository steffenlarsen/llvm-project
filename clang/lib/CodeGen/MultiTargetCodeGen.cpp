//===--- MultiTargetCodeGen.cpp - Code generation for several targets -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The parts of BackendConsumer that generate code for the aux targets of a
// compilation for several targets at once. That is one CodeGenModule per target
// over the shared AST, each target's backend, and the package of their bitcode.
//
//===----------------------------------------------------------------------===//

#include "BackendConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/MultiTargetContext.h"
#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/Basic/MultiTargetDebugOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CodeGen/BackendUtil.h"
#include "clang/CodeGen/ModuleBuilder.h"
#include "clang/Frontend/CompilerInstance.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/IR/Module.h"
#include "llvm/Object/OffloadBinary.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/IOSandbox.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

static llvm::DenseMap<unsigned, std::string>
parseMultiTargetAuxSpecs(ArrayRef<std::string> Specs) {
  llvm::DenseMap<unsigned, std::string> Values;
  for (StringRef Entry : Specs) {
    StringRef VariantStr, Value;
    std::tie(VariantStr, Value) = Entry.split(':');
    unsigned Variant;
    if (VariantStr.getAsInteger(10, Variant) || Value.empty())
      continue; // Malformed input is silently skipped.
    Values[Variant] = Value.str();
  }
  return Values;
}

static void dumpModuleForDiff(StringRef Dir, StringRef InFile, StringRef Suffix,
                              llvm::Module *M) {
  llvm::SmallString<256> Path(Dir);
  llvm::sys::path::append(Path, (llvm::sys::path::stem(InFile) + Suffix));
  std::error_code EC;
  llvm::raw_fd_ostream OS(Path, EC, llvm::sys::fs::OF_Text);
  if (EC)
    return;
  M->print(OS, nullptr);
}

namespace {
enum LangOptionField : unsigned {
#define LANGOPT(Name, Bits, Default, Compatibility, Description) LOF_##Name,
#include "clang/Basic/LangOptions.def"
};
} // namespace

static unsigned getLangOpt(const LangOptions &LangOpts, unsigned Field) {
  switch (Field) {
#define LANGOPT(Name, Bits, Default, Compatibility, Description)               \
  case LOF_##Name:                                                             \
    return LangOpts.Name;
#define ENUM_LANGOPT(Name, Type, Bits, Default, Compatibility, Description)    \
  case LOF_##Name:                                                             \
    return static_cast<unsigned>(LangOpts.get##Name());
#include "clang/Basic/LangOptions.def"
  }
  llvm_unreachable("unknown LangOptions field");
}

static void setLangOpt(LangOptions &LangOpts, unsigned Field, unsigned Value) {
  switch (Field) {
#define LANGOPT(Name, Bits, Default, Compatibility, Description)               \
  case LOF_##Name:                                                             \
    LangOpts.Name = Value;                                                     \
    return;
#define ENUM_LANGOPT(Name, Type, Bits, Default, Compatibility, Description)    \
  case LOF_##Name:                                                             \
    LangOpts.set##Name(static_cast<LangOptions::Type>(Value));                 \
    return;
#include "clang/Basic/LangOptions.def"
  }
  llvm_unreachable("unknown LangOptions field");
}

/// The fields that may differ per target and that \p Aux sets differently
/// from \p Primary.
static void diffPerTargetLangOpts(
    const LangOptions &Primary, const LangOptions &Aux,
    SmallVectorImpl<BackendConsumer::AuxGenEntry::LangOptValue> &Diffs) {
#define LANGOPT(Name, Bits, Default, Compatibility, Description)               \
  if (getLangOpt(Primary, LOF_##Name) != getLangOpt(Aux, LOF_##Name) &&        \
      CompilerInstance::langOptMayDifferPerTarget(#Name))                      \
    Diffs.push_back({LOF_##Name, getLangOpt(Aux, LOF_##Name)});
#include "clang/Basic/LangOptions.def"
}

namespace {
/// Scopes one dispatch into the aux CodeGenModule to its own target.
/// A TargetScope entered once is not enough. CodeGen resolves a plain
/// sizeof(T) in a function body against whichever target is ambient when it
/// runs, not the target in scope when the template was instantiated, so each
/// dispatch needs its own live scope. The LangOptions CodeGen reads
/// (CUDAIsDevice among them) are the single set the shared AST was built with,
/// and TargetScope deliberately leaves them alone, so the entry's per-target
/// values are swapped in too. The swap lasts only for one call into the aux
/// CodeGenModule, not the whole AST walk.
class MultiTargetCodeGenScope {
  ASTContext::TargetScope TS;
  LangOptions &LangOpts;
  const BackendConsumer::AuxGenEntry &Entry;
  SmallVector<unsigned, 8> Saved;
  // The driver enables a sanitizer only for the targets that support it.
  SanitizerSet SavedSanitize;

public:
  MultiTargetCodeGenScope(ASTContext &Ctx, LangOptions &LangOpts,
                          const BackendConsumer::AuxGenEntry &Entry)
      : TS(Ctx, *Entry.TI, Entry.Variant), LangOpts(LangOpts), Entry(Entry),
        SavedSanitize(LangOpts.Sanitize) {
    for (const auto &Diff : Entry.LangOptDiffs) {
      Saved.push_back(getLangOpt(LangOpts, Diff.Field));
      setLangOpt(LangOpts, Diff.Field, Diff.Value);
    }
    LangOpts.Sanitize = Entry.Invocation->getLangOpts().Sanitize;
  }
  ~MultiTargetCodeGenScope() {
    LangOpts.Sanitize = SavedSanitize;
    for (unsigned I = Saved.size(); I--;)
      setLangOpt(LangOpts, Entry.LangOptDiffs[I].Field, Saved[I]);
  }
  MultiTargetCodeGenScope(const MultiTargetCodeGenScope &) = delete;
};
} // namespace

void BackendConsumer::PrimaryCodeGenScope::enter(ASTContext &Ctx) {
  Generating = &Ctx.getMultiTarget().GeneratingPrimaryCode;
  Saved = std::exchange(*Generating, true);
}

void BackendConsumer::dispatchToAuxGensImpl(
    llvm::function_ref<void(CodeGenerator &)> Fn) {
  for (AuxGenEntry &Entry : AuxGens) {
    MultiTargetCodeGenScope Scope(*Context, CI.getLangOpts(), Entry);
    Fn(*Entry.Gen);
  }
}

void BackendConsumer::initializeAuxGens(ASTContext &Ctx) {
  // Ctx's aux-target list is the -multi-target-aux-invocation targets when
  // there are any, and otherwise the implicit "other side of
  // CUDA/OpenMP/SYCL" aux target populated from -aux-triple.
  unsigned NumAuxTargets = Ctx.getNumAuxTargets();
  ArrayRef<std::shared_ptr<CompilerInvocation>> AuxInvocations =
      CI.getMultiTargetAuxInvocations();
  llvm::DenseMap<unsigned, std::string> AuxOutputPaths =
      parseMultiTargetAuxSpecs(CI.getFrontendOpts().MultiTargetAuxOutputs);
  llvm::DenseMap<unsigned, std::string> AuxTargetIDs =
      parseMultiTargetAuxSpecs(CI.getFrontendOpts().MultiTargetAuxTargetIDs);
  assert(AuxInvocations.size() == NumAuxTargets &&
         "createTarget pairs aux invocations with aux targets");
  for (unsigned I = 0; I != NumAuxTargets; ++I) {
    AuxGenEntry Entry;
    Entry.Variant = 2 + I;
    Entry.TI = Ctx.getTargetForVariant(Entry.Variant);
    auto TargetIDIt = AuxTargetIDs.find(Entry.Variant);
    Entry.TargetID = TargetIDIt != AuxTargetIDs.end()
                         ? TargetIDIt->second
                         : Entry.TI->getTargetOpts().CPU;
    Entry.Invocation = AuxInvocations[I];
    diffPerTargetLangOpts(CI.getLangOpts(), Entry.Invocation->getLangOpts(),
                          Entry.LangOptDiffs);
    Entry.Gen = CreateLLVMCodeGen(CI.getDiagnostics(), InFile,
                                  CI.getVirtualFileSystemPtr(),
                                  Entry.Invocation->getHeaderSearchOpts(),
                                  Entry.Invocation->getPreprocessorOpts(),
                                  Entry.Invocation->getCodeGenOpts(),
                                  Gen->GetModule()->getContext(), CoverageInfo);
    // Only entries a -multi-target-aux-output flag configures get a real
    // output file. Every other entry is produced and discarded.
    auto OutputIt = AuxOutputPaths.find(Entry.Variant);
    if (OutputIt != AuxOutputPaths.end()) {
      Entry.OutputPath = OutputIt->second;
      // Write directly rather than atomically. An atomic write lands the
      // bytes at OutputPath only once the source file ends, but packaging
      // reads this file back before then. The bitcode is an internal
      // artifact, not a user-facing -o output that needs atomic replacement.
      Entry.AsmOutStream =
          CI.createOutputFile(Entry.OutputPath, /*Binary=*/true,
                              /*RemoveFileOnSignal=*/true,
                              /*UseTemporary=*/false);
    }
    loadLinkModules(CI, Gen->GetModule()->getContext(),
                    Entry.Invocation->getCodeGenOpts().LinkBitcodeFiles,
                    Entry.LinkModules);
    AuxGens.push_back(std::move(Entry));
  }
  dispatchToAuxGens([&](CodeGenerator &G) { G.Initialize(Ctx); });
}

// Runs one AuxGenEntry's backend tail, as for the primary module. It links the
// entry's own bitcode-file set and emits its bitcode output. A no-op if
// -multi-target-aux-output did not name the entry's variant.
void BackendConsumer::runAuxBackendTail(AuxGenEntry &Entry) {
  if (!Entry.AsmOutStream)
    return;

  llvm::Module *M = Entry.Gen->GetModule();
  if (!M)
    return;

  MultiTargetCodeGenScope Scope(*Context, CI.getLangOpts(), Entry);
  CodeGenOptions &AuxCodeGenOpts = Entry.Invocation->getCodeGenOpts();

  // Pass this entry's own options, not the primary's. Otherwise linking stamps
  // the primary's target CPU, features and denormal and frame-pointer
  // defaults onto every function in the entry's linked-in bitcode libraries
  // that lacks its own. That silently downgrades them to a generic subtarget
  // and makes instruction selection fail on target-only instructions.
  if (LinkInModules(M, Entry.LinkModules, AuxCodeGenOpts,
                    Entry.TI->getTargetOpts()))
    return;

  EmbedBitcode(M, AuxCodeGenOpts, llvm::MemoryBufferRef());

  // The entry's target options override CI's own. Otherwise the target
  // machine is built with the primary's CPU and features against the entry's
  // triple, silently falling back to a generic subtarget. No BackendConsumer
  // is passed, as it would link the primary's modules, and this entry's were
  // linked above.
  emitBackendOutput(CI, AuxCodeGenOpts, M, Entry.Action, FS,
                    std::move(Entry.AsmOutStream), /*BC=*/nullptr,
                    &Entry.TI->getTargetOpts());
}

static llvm::Error writeFile(StringRef Filename, StringRef Data) {
  llvm::Expected<std::unique_ptr<llvm::FileOutputBuffer>> OutputOrErr =
      llvm::FileOutputBuffer::create(Filename, Data.size());
  if (!OutputOrErr)
    return OutputOrErr.takeError();
  std::unique_ptr<llvm::FileOutputBuffer> Output = std::move(*OutputOrErr);
  llvm::copy(Data, Output->getBufferStart());
  return Output->commit();
}

// Packages every configured aux entry's on-disk bitcode into a single
// offload-binary container at OutputPath. Entries with no configured output
// are skipped. Returns true on error, reported via Diags.
static bool
packageAuxOutputs(DiagnosticsEngine &Diags,
                  llvm::ArrayRef<BackendConsumer::AuxGenEntry> AuxGens,
                  StringRef OutputPath) {
  // The aux outputs are files this compilation wrote itself.
  auto BypassSandbox = llvm::sys::sandbox::scopedDisable();
  llvm::SmallVector<llvm::object::OffloadBinary::OffloadingImage, 4> Images;
  for (const BackendConsumer::AuxGenEntry &Entry : AuxGens) {
    if (Entry.OutputPath.empty())
      continue;
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> BufferOrErr =
        llvm::MemoryBuffer::getFile(Entry.OutputPath);
    if (!BufferOrErr) {
      Diags.Report(diag::err_cannot_open_file)
          << Entry.OutputPath << BufferOrErr.getError().message();
      return true;
    }
    llvm::object::OffloadBinary::OffloadingImage Image;
    Image.TheImageKind = llvm::object::ImageKind::IMG_Bitcode;
    Image.TheOffloadKind = llvm::object::OffloadKind::OFK_HIP;
    Image.StringData["triple"] = Entry.TI->getTriple().getTriple();
    Image.StringData["arch"] = Entry.TargetID;
    Image.Image = std::move(*BufferOrErr);
    Images.push_back(std::move(Image));
  }

  llvm::SmallString<0> Packaged = llvm::object::OffloadBinary::write(Images);
  if (llvm::Error E = writeFile(OutputPath, Packaged)) {
    Diags.Report(diag::err_cannot_open_file)
        << OutputPath << llvm::toString(std::move(E));
    return true;
  }
  return false;
}

void BackendConsumer::finishAuxGens() {
  // With packaging, the aux backends run before the primary module's,
  // otherwise after it.
  for (AuxGenEntry &Entry : AuxGens)
    runAuxBackendTail(Entry);

  // Package the aux entries' unlinked bitcode for the host to embed. Not
  // after an error, as no device code was generated to package.
  StringRef Package = CI.getFrontendOpts().MultiTargetPackageOffload;
  if (!Diags.hasErrorOccurred() && !Package.empty())
    packageAuxOutputs(Diags, AuxGens, Package);
}

void BackendConsumer::dumpModulesForDiff() {
  if (!multitarget::MultiTargetCodeGenDumpDir.empty()) {
    // A plain HIP `-c` compile drives two separate cc1 jobs (device, then
    // host), each with its own primary/aux pair. Tag filenames by this job's
    // primary triple so the two jobs' dumps don't collide.
    if (llvm::Module *M = Gen->GetModule()) {
      std::string JobTag = ("." + M->getTargetTriple().str());
      dumpModuleForDiff(multitarget::MultiTargetCodeGenDumpDir, InFile,
                        JobTag + ".primary.ll", M);
      for (AuxGenEntry &Entry : AuxGens)
        if (llvm::Module *AuxM = Entry.Gen->GetModule())
          dumpModuleForDiff(
              multitarget::MultiTargetCodeGenDumpDir, InFile,
              JobTag + ".aux" + std::to_string(Entry.Variant) + ".ll", AuxM);
    }
  }
}

bool BackendConsumer::reorderForPackaging() const {
  return !CI.getFrontendOpts().MultiTargetPackageOffload.empty();
}
