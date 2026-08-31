//===--- MultiTargetCodeGen.cpp - Code generation for several targets -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The parts of BackendConsumer that generate code for the aux targets of a
// compilation for several targets at once: one CodeGenModule per target over
// the shared AST, each target's backend, and the package of their bitcode.
//
//===----------------------------------------------------------------------===//

#include "BackendConsumer.h"
#include "clang/AST/ASTContext.h"
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
/// ASTContext::TargetScope alone is not enough: a plain sizeof(T) in a
/// function body is resolved by CodeGen against whichever target is ambient
/// when CodeGen runs, not the target in scope when the containing template
/// was instantiated -- so each dispatch needs its own live scope, not just
/// one entered at construction. The LangOptions CodeGen reads (CUDAIsDevice
/// among them) are the single set the shared AST was built with, and
/// TargetScope deliberately leaves them alone (see its own header comment),
/// so the entry's per-target values are swapped in too. The swap lasts only
/// for the duration of one call into the aux CodeGenModule, not the whole
/// AST walk.
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

void BackendConsumer::dispatchToAuxGens(
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
    // Only entries a -multi-target-aux-output flag actually configures get
    // a real output file; every other entry stays produced-and-discarded.
    auto OutputIt = AuxOutputPaths.find(Entry.Variant);
    if (OutputIt != AuxOutputPaths.end()) {
      Entry.OutputPath = OutputIt->second;
      // UseTemporary=false: with atomic-write (UseTemporary=true), the
      // real bytes only land at OutputPath once CompilerInstance's own
      // OutputFiles list is drained via clearOutputFiles(), which happens
      // at EndSourceFile -- well after HandleTranslationUnit (and
      // packageAuxOutputs, which reads this file back) returns. This
      // entry's own bitcode is an internal artifact, not a user-facing
      // "-o" output needing atomic-replace robustness, so writing it
      // directly is correct.
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

// Runs exactly one AuxGenEntry's own backend tail (link its own
// bitcode-file set, embed-bitcode no-op, emit its own bitcode output),
// mirroring HandleTranslationUnit's primary tail below. Does not touch call
// order relative to Gen. A no-op if this entry has no configured output
// (-multi-target-aux-output didn't name its variant); every other entry
// stays produced-and-discarded.
void BackendConsumer::runAuxBackendTail(AuxGenEntry &Entry) {
  if (!Entry.AsmOutStream)
    return;

  llvm::Module *M = Entry.Gen->GetModule();
  if (!M)
    return;

  MultiTargetCodeGenScope Scope(*Context, CI.getLangOpts(), Entry);
  CodeGenOptions &AuxCodeGenOpts = Entry.Invocation->getCodeGenOpts();

  // Pass this entry's own options, not the host's -- otherwise
  // mergeDefaultFunctionDefinitionAttributes stamps the host's
  // "target-cpu"="x86-64"/features and its denormal/frame-pointer defaults
  // onto every function in this entry's linked-in bitcode libraries (e.g.
  // ROCm's ockl.bc) that lacks its own, silently downgrading them to a
  // generic AMDGPU subtarget and causing ISel failures on real GPU-only
  // instructions.
  if (LinkInModules(M, Entry.LinkModules, AuxCodeGenOpts,
                    Entry.TI->getTargetOpts()))
    return;

  EmbedBitcode(M, AuxCodeGenOpts, llvm::MemoryBufferRef());

  // Entry.TI->getTargetOpts() overrides CI's own (host-set) TargetOptions --
  // without this, CreateTargetMachine builds this aux entry's TargetMachine
  // using the host's CPU/Features (e.g. "x86-64"/"+cmov") against this
  // entry's own AMDGPU triple, silently falling back to a generic subtarget.
  // No BackendConsumer: its LinkInModulesPass would link the primary's
  // modules, and this entry's were linked above.
  emitBackendOutput(CI, AuxCodeGenOpts, M, Entry.Action, FS,
                    std::move(Entry.AsmOutStream), /*BC=*/nullptr,
                    &Entry.TI->getTargetOpts());
}

// Mirrors llvm-offload-binary.cpp's own writeFile() helper.
static llvm::Error writeFile(StringRef Filename, StringRef Data) {
  llvm::Expected<std::unique_ptr<llvm::FileOutputBuffer>> OutputOrErr =
      llvm::FileOutputBuffer::create(Filename, Data.size());
  if (!OutputOrErr)
    return OutputOrErr.takeError();
  std::unique_ptr<llvm::FileOutputBuffer> Output = std::move(*OutputOrErr);
  llvm::copy(Data, Output->getBufferStart());
  return Output->commit();
}

// Packages every configured aux entry's own on-disk .bc (runAuxBackendTail's
// output) into a single offload-binary container at OutputPath, via the
// same llvm::object::OffloadBinary::write library call clang-linker-wrapper
// itself already makes in-process (ClangLinkerWrapper.cpp). Entries with no
// configured output (Entry.OutputPath.empty()) are skipped, mirroring
// runAuxBackendTail's own gating. Returns true on error, reported via Diags.
static bool
packageAuxOutputs(DiagnosticsEngine &Diags,
                  llvm::ArrayRef<BackendConsumer::AuxGenEntry> AuxGens,
                  StringRef OutputPath) {
  // The aux outputs are this compilation's own, written by runAuxBackendTail.
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
  // Run each configured aux entry's own backend tail, producing its own
  // real bitcode output. Runs after Gen's own full tail below when
  // !ReorderForPackaging (no reordering); when ReorderForPackaging, runs
  // before Gen has dispatched at all -- see below.
  for (AuxGenEntry &Entry : AuxGens)
    runAuxBackendTail(Entry);

  // Package every configured aux entry's own bitcode, unlinked, for the host
  // to embed, once every runAuxBackendTail call above has completed. Off by
  // default. Not after an error: no device code was generated to package.
  StringRef Package = CI.getFrontendOpts().MultiTargetPackageOffload;
  if (!Diags.hasErrorOccurred() && !Package.empty())
    packageAuxOutputs(Diags, AuxGens, Package);
}

void BackendConsumer::dumpModulesForDiff() {
  if (!multitarget::MultiTargetCodeGenDumpDir.empty()) {
    // A plain HIP `-c` compile drives two separate cc1 jobs (device, then
    // host), each with its own primary/aux pair; tag filenames by this job's
    // own primary triple so the two jobs' dumps don't collide.
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
