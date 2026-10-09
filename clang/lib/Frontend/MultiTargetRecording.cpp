//===- MultiTargetRecording.cpp - Per-target token streams ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/MultiTargetRecording.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/TextDiagnosticBuffer.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "clang/Parse/Parser.h"
#include "clang/Sema/Sema.h"
#include "clang/Serialization/MultiTargetASTExtension.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

//===----------------------------------------------------------------------===//
// Recording
//===----------------------------------------------------------------------===//

using PrefixState = MultiTargetASTExtension::PrefixState;

/// \p MI as the definition of a -D option.
static std::string macroDefinition(const Preprocessor &PP,
                                   const IdentifierInfo &Name,
                                   const MacroInfo &MI) {
  std::string Definition = Name.getName().str();
  if (MI.isFunctionLike()) {
    Definition += '(';
    llvm::ListSeparator LS(",");
    for (const IdentifierInfo *Param : MI.params()) {
      Definition += LS;
      if (MI.isC99Varargs() && Param == MI.params().back()) {
        Definition += "...";
        continue;
      }
      Definition += Param->getName();
      if (MI.isGNUVarargs() && Param == MI.params().back())
        Definition += "...";
    }
    Definition += ')';
  }
  Definition += '=';
  for (const Token &T : MI.tokens()) {
    if (T.hasLeadingSpace() && &T != MI.tokens().begin())
      Definition += ' ';
    Definition += PP.getSpelling(T);
  }
  return Definition;
}

/// A path that names \p File from any working directory.
static std::string absolutePath(FileManager &FM, FileEntryRef File) {
  StringRef Real = File.getFileEntry().tryGetRealPathName();
  if (!Real.empty())
    return Real.str();
  SmallString<256> Path(File.getName());
  FM.makeAbsolutePath(Path);
  return std::string(Path);
}

/// What \p PP knows at the end of its input.
static PrefixState capturePrefixState(Preprocessor &PP) {
  PrefixState State;
  SmallVector<std::pair<StringRef, std::string>, 0> Macros;
  for (const auto &Entry : PP.macros()) {
    const IdentifierInfo *Name = Entry.first;
    const MacroInfo *MI = PP.getMacroInfo(Name);
    if (MI && !MI->isBuiltinMacro())
      Macros.push_back({Name->getName(), macroDefinition(PP, *Name, *MI)});
  }
  llvm::sort(Macros);
  for (auto &Macro : Macros)
    State.Macros.push_back(std::move(Macro.second));

  SourceManager &SM = PP.getSourceManager();
  HeaderSearch &HS = PP.getHeaderSearchInfo();
  SmallVector<std::pair<std::string, StringRef>, 0> Guards;
  for (auto It = SM.fileinfo_begin(), E = SM.fileinfo_end(); It != E; ++It) {
    HeaderFileInfo *HFI = HS.getExistingFileInfo(It->first)
                              ? &HS.getFileInfo(It->first)
                              : nullptr;
    if (!HFI)
      continue;
    if (HFI->isPragmaOnce && PP.alreadyIncluded(It->first))
      State.IncludeOnceFiles.push_back(
          absolutePath(PP.getFileManager(), It->first));
    if (const IdentifierInfo *Guard =
            HFI->getControllingMacro(PP.getExternalSource()))
      Guards.push_back(
          {absolutePath(PP.getFileManager(), It->first), Guard->getName()});
  }
  llvm::sort(State.IncludeOnceFiles);
  llvm::sort(Guards);
  for (const auto &[Path, Guard] : Guards) {
    State.ControllingMacros.push_back(Path);
    State.ControllingMacros.push_back(Guard.str());
  }
  return State;
}

/// Rewrites a recording pass's invocation to start where \p State says its
/// target is after \p P: its macros instead of the predefined ones, and none
/// of the force-included headers \p P already contains.
static void startAfterPrefix(CompilerInvocation &Invocation,
                             const MultiTargetASTExtension::Prefix &P,
                             const PrefixState &State) {
  PreprocessorOptions &PPO = Invocation.getPreprocessorOpts();
  PPO.UsePredefines = false;
  std::vector<std::pair<std::string, bool>> Macros;
  Macros.reserve(State.Macros.size() + PPO.Macros.size());
  for (const std::string &Macro : State.Macros)
    Macros.push_back({Macro, /*IsUndef=*/false});
  // The command line's own come after, as when the prefix is loaded.
  llvm::append_range(Macros, PPO.Macros);
  PPO.Macros = std::move(Macros);
  llvm::erase_if(PPO.Includes, [&](const std::string &Include) {
    SmallString<256> Absolute(Include);
    llvm::sys::fs::make_absolute(Absolute);
    llvm::sys::path::remove_dots(Absolute, /*remove_dot_dot=*/true);
    return llvm::is_contained(P.Headers, Include) ||
           llvm::is_contained(P.Headers, Absolute.str());
  });
}

namespace {

/// Drains the preprocessor and keeps what it produced.
///
/// This mirrors ParseAST's prologue rather than being a plain preprocessor
/// action, because the Parser installs pragma handlers that turn directives
/// into annotation tokens. Without them a `#pragma unroll` stays raw, and the
/// ggml-cuda headers contain 521 of those: the recordings would differ from the
/// primary stream everywhere a pragma appears, and the alignment would report
/// divergence that is an artefact of how the streams were made.
class RecordStreamAction : public ASTFrontendAction {
  TargetRecording &Out;
  /// How many tokens the stream is expected to have, if known.
  size_t ExpectedTokens;
  /// The state at the end of a precompiled prefix to start from, if any.
  const PrefixState *Start;
  /// Where to record the state at the end of the input, if anywhere.
  PrefixState *End;

public:
  RecordStreamAction(TargetRecording &Out, size_t ExpectedTokens,
                     const PrefixState *Start, PrefixState *End)
      : Out(Out), ExpectedTokens(ExpectedTokens), Start(Start), End(End) {}

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &,
                                                 StringRef) override {
    return std::make_unique<ASTConsumer>();
  }

  void ExecuteAction() override {
    CompilerInstance &CI = getCompilerInstance();
    if (!CI.hasSema())
      CI.createSema(getTranslationUnitKind(), nullptr);
    Preprocessor &PP = CI.getPreprocessor();

    auto Owned = std::make_unique<ConditionalRegionRecorder>(Out.Tokens);
    auto *Regions = Owned.get();
    PP.addPPCallbacks(std::move(Owned));

    if (Start) {
      FileManager &FM = CI.getFileManager();
      HeaderSearch &HS = PP.getHeaderSearchInfo();
      for (const std::string &Path : Start->IncludeOnceFiles)
        if (OptionalFileEntryRef File = FM.getOptionalFileRef(Path)) {
          HS.MarkFileIncludeOnce(*File);
          PP.markIncluded(*File);
        }
      const std::vector<std::string> &Guards = Start->ControllingMacros;
      for (size_t I = 0; I + 1 < Guards.size(); I += 2)
        if (OptionalFileEntryRef File = FM.getOptionalFileRef(Guards[I]))
          HS.SetFileControllingMacro(*File,
                                     PP.getIdentifierInfo(Guards[I + 1]));
    }

    PP.EnterMainSourceFile();
    if (!PP.getCurrentLexer())
      return;

    Parser P(PP, CI.getSema(), /*SkipFunctionBodies=*/true);
    P.Initialize();
    Out.Tokens.reserve(ExpectedTokens);
    Out.Tokens.push_back(P.getCurToken());

    // The primed token is the end of the input when nothing follows a
    // precompiled prefix, and lexing past it crashes.
    Token T = P.getCurToken();
    while (T.isNot(tok::eof) && T.isNot(tok::annot_repl_input_end)) {
      PP.Lex(T);
      Out.Tokens.push_back(T);
    }

    if (End)
      *End = capturePrefixState(PP);

    Out.Hashes.reserve(Out.Tokens.size());
    for (const Token &Tok : Out.Tokens)
      Out.Hashes.push_back(hashToken(Tok, PP));

    const SourceManager &SM = CI.getSourceManager();
    Out.Regions.assign(Regions->regions().begin(), Regions->regions().end());
    Out.RegionKeys.reserve(Out.Regions.size());
    for (const ConditionalRegion &R : Out.Regions) {
      PresumedLoc PL = SM.getPresumedLoc(R.IfLoc);
      Out.RegionKeys.push_back(PL.isInvalid()
                                   ? std::string()
                                   : (llvm::Twine(PL.getFilename()) + ":" +
                                      llvm::Twine(PL.getLine()) + ":" +
                                      llvm::Twine(PL.getColumn()))
                                         .str());
    }
  }
};

} // namespace

namespace {
/// One target besides the primary to record a pass for.
struct RecordTarget {
  /// The target's own invocation, from -multi-target-aux-invocation. Without
  /// one, the fields below retarget a copy of the primary's.
  std::shared_ptr<const CompilerInvocation> Invocation;
  std::string Triple;
  std::string CPU;
  std::vector<std::string> Features;
  bool CUDAIsDevice;
};
} // namespace

/// The targets to record *besides* the one being compiled.
///
/// The primary target's stream comes from the compilation itself, not from a
/// nested pass: its tokens have to belong to the identifier table and
/// SourceManager the parser is using, and only the real preprocessor produces
/// those.
///
/// Each -multi-target-aux-invocation is recorded with its own invocation, in
/// the same order as ASTContext's AuxTargets (element 0 is variant 2, element
/// 1 is variant 3, etc).
///
/// Those never cover the host/device AuxTriple pairing (the CUDA/OpenMP/SYCL
/// "other side") -- append that too, so a real "1 host + N device archs" HIP
/// compile gets a recording for the host as well, not just the extra device
/// archs. Skip it if a target on that side is already being recorded.
static std::vector<RecordTarget> targetsToRecord(CompilerInstance &CI) {
  std::vector<RecordTarget> Targets;

  for (const std::shared_ptr<CompilerInvocation> &Invocation :
       CI.getMultiTargetAuxInvocations()) {
    RecordTarget RT;
    RT.Invocation = Invocation;
    RT.Triple = Invocation->getTargetOpts().Triple;
    RT.CUDAIsDevice = Invocation->getLangOpts().CUDAIsDevice;
    Targets.push_back(std::move(RT));
  }

  const std::string &Aux = CI.getFrontendOpts().AuxTriple;
  bool OtherSideIsDevice = !CI.getLangOpts().CUDAIsDevice;
  if (!Aux.empty() && Aux != CI.getTargetOpts().Triple &&
      llvm::none_of(Targets, [&](const RecordTarget &RT) {
        return RT.CUDAIsDevice == OtherSideIsDevice;
      })) {
    RecordTarget RT;
    RT.Triple = Aux;
    if (CI.getFrontendOpts().AuxTargetCPU)
      RT.CPU = *CI.getFrontendOpts().AuxTargetCPU;
    if (CI.getFrontendOpts().AuxTargetFeatures)
      RT.Features = *CI.getFrontendOpts().AuxTargetFeatures;
    RT.CUDAIsDevice = OtherSideIsDevice;
    Targets.push_back(std::move(RT));
  }
  return Targets;
}

/// A copy of the primary's invocation, retargeted at \p RT, the primary's
/// "other side" (see targetsToRecord()).
static std::shared_ptr<CompilerInvocation>
retargetPrimaryInvocation(CompilerInstance &CI, const RecordTarget &RT) {
  auto Invocation = std::make_shared<CompilerInvocation>(CI.getInvocation());
  TargetOptions &TO = Invocation->getTargetOpts();
  TO.Triple = RT.Triple;
  // The primary's own triple becomes this nested pass's aux triple. Left
  // alone, FrontendOpts.AuxTriple would self-reference the same target this
  // pass is now recording (host recording device, aux still names the
  // device). That starves the recorded pass's AuxTargetInfo of the real
  // other side, so architecture-identity macros (e.g. __x86_64__) it should
  // inherit from the aux target never get defined, producing spurious
  // conditional divergence in target-agnostic system headers (e.g. glibc's
  // bits/pthreadtypes-arch.h, which branches on __x86_64__).
  Invocation->getFrontendOpts().AuxTriple = CI.getTargetOpts().Triple;
  // Same swap for the CPU/features that describe that new aux triple:
  // left alone, FrontendOpts.AuxTargetCPU/AuxTargetFeatures still name the
  // primary compile's own aux target's hardware (e.g. "x86-64" when the
  // primary is the device), and CompilerInstance::createTarget() applies
  // them verbatim to whatever triple AuxTriple now names -- here, the
  // primary's own triple (e.g. amdgcn). An x86 CPU name is not a valid
  // AMDGPU one, so this nested pass's own createTarget() would otherwise
  // fail its AuxTargetInfo with "unknown target CPU".
  Invocation->getFrontendOpts().AuxTargetCPU = CI.getTargetOpts().CPU;
  Invocation->getFrontendOpts().AuxTargetFeatures =
      CI.getTargetOpts().FeaturesAsWritten;
  // CPU, tune-CPU and features name the target this pass is recording (RT),
  // from the AuxTargetCPU/AuxTargetFeatures scalars. TuneCPU isn't part of
  // that source, so it's always cleared.
  TO.TuneCPU.clear();
  TO.CPU = RT.CPU;
  TO.Features = RT.Features;
  TO.FeaturesAsWritten = RT.Features;

  if (CI.getLangOpts().CUDA)
    Invocation->getLangOpts().CUDAIsDevice = RT.CUDAIsDevice;
  return Invocation;
}

static MultiTargetASTExtension *findMultiTargetExtension(CompilerInstance &CI) {
  for (const auto &Ext : CI.getFrontendOpts().ModuleFileExtensions)
    if (auto *MT = dyn_cast<MultiTargetASTExtension>(Ext.get()))
      return MT;
  return nullptr;
}

MultiTargetRecordings clang::recordTargetStreams(CompilerInstance &CI,
                                                 bool CapturePrefix) {
  MultiTargetRecordings Out;
  MultiTargetASTExtension *Ext = findMultiTargetExtension(CI);
  const MultiTargetASTExtension::Prefix *Loaded =
      Ext ? Ext->getLoadedPrefix() : nullptr;
  MultiTargetASTExtension::Prefix Captured;
  // Without its targets' state, the prefix would have to be preprocessed again
  // for each of them, and its declarations would then be made twice.
  StringRef PCH = CI.getPreprocessorOpts().ImplicitPCHInclude;
  if (!PCH.empty() && Ext && !Loaded &&
      !CI.getDiagnostics().hasErrorOccurred()) {
    CI.getDiagnostics().Report(diag::err_fe_multi_target_pch) << PCH;
    return Out;
  }

  for (const RecordTarget &RT : targetsToRecord(CI)) {
    std::shared_ptr<CompilerInvocation> Invocation;
    if (RT.Invocation) {
      Invocation = std::make_shared<CompilerInvocation>(*RT.Invocation);
      // The driver strips the input file from each aux invocation.
      Invocation->getFrontendOpts().Inputs = CI.getFrontendOpts().Inputs;
    } else {
      Invocation = retargetPrimaryInvocation(CI, RT);
    }
    // Recording is the whole job; nothing downstream of the preprocessor runs.
    Invocation->getFrontendOpts().ProgramAction = frontend::ParseSyntaxOnly;
    Invocation->getFrontendOpts().OutputFile.clear();
    Invocation->getFrontendOpts().MultiTargetAuxInvocations.clear();
    // The primary alone loads a precompiled prefix, for every target.
    PreprocessorOptions &PPO = Invocation->getPreprocessorOpts();
    PPO.ImplicitPCHInclude.clear();
    PPO.ImplicitPCHIsOptional = false;
    PPO.ImplicitPCHAutoGenerateHeader.clear();
    // Aux targets come first, in the order the precompiled prefix has them.
    const size_t Index = Out.Targets.size();
    const PrefixState *Start = nullptr;
    if (Loaded && RT.Invocation && Index < Loaded->States.size()) {
      Start = &Loaded->States[Index];
      startAfterPrefix(*Invocation, *Loaded, *Start);
    }
    PrefixState *End = nullptr;
    if (CapturePrefix && Ext && RT.Invocation)
      End = &Captured.States.emplace_back();

    auto Nested = std::make_unique<CompilerInstance>(
        std::move(Invocation), CI.getPCHContainerOperations());
    Nested->setVirtualFileSystem(CI.getVirtualFileSystemPtr());
    // Buffered rather than discarded: the primary compilation reports for the
    // primary target, and repeating every diagnostic once per target is worse
    // than saying nothing -- but a failure to record has to be explicable.
    auto *Buffered = new TextDiagnosticBuffer();
    Nested->createDiagnostics(Buffered, /*ShouldOwnClient=*/true);
    Nested->setFileManager(&CI.getFileManager());
    // Deliberately *not* sharing CI's SourceManager. It looks like the way to
    // give recorded tokens locations the primary compilation can resolve, and
    // it does not work: DiagnosticsEngine::DiagStateMap is keyed by FileID and
    // include chain and asserts "state transitions added out of order", so a
    // second pass over the same files through one SourceManager crashes on any
    // input containing #pragma clang diagnostic. Locations are translated when
    // the merged stream is built instead.

    TargetRecording R;
    R.Triple = RT.Triple;
    // Every target's stream is about as long as the one recorded before it.
    RecordStreamAction Action(
        R, Out.Targets.empty() ? 0 : Out.Targets.back().Tokens.size() * 9 / 8,
        Start, End);
    if (!Nested->ExecuteAction(Action) || R.Tokens.empty()) {
      llvm::errs() << "multi-target recording: could not record " << RT.Triple;
      if (Buffered->err_begin() != Buffered->err_end())
        llvm::errs() << ": " << Buffered->err_begin()->second;
      else if (R.Tokens.empty())
        llvm::errs() << ": no tokens produced";
      llvm::errs() << "\n";
      return {};
    }
    R.SM = &Nested->getSourceManager();
    Out.Targets.push_back(std::move(R));
    Out.Owners.push_back(std::move(Nested));
  }

  if (CapturePrefix && Ext && !CI.getFrontendOpts().Inputs.empty()) {
    Captured.Headers = CI.getPreprocessorOpts().Includes;
    SmallString<256> Input(CI.getFrontendOpts().Inputs[0].getFile());
    CI.getFileManager().makeAbsolutePath(Input);
    llvm::sys::path::remove_dots(Input, /*remove_dot_dot=*/true);
    Captured.Headers.push_back(std::string(Input));
    Ext->setPrefixToWrite(std::move(Captured));
  }
  return Out;
}
