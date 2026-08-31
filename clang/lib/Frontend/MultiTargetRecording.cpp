//===- MultiTargetRecording.cpp - Per-target token streams ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/MultiTargetRecording.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Frontend/TextDiagnosticBuffer.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Parse/Parser.h"
#include "clang/Sema/Sema.h"
#include "clang/AST/ASTConsumer.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

//===----------------------------------------------------------------------===//
// Recording
//===----------------------------------------------------------------------===//

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

public:
  explicit RecordStreamAction(TargetRecording &Out) : Out(Out) {}

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

    PP.EnterMainSourceFile();
    if (!PP.getCurrentLexer())
      return;

    Parser P(PP, CI.getSema(), /*SkipFunctionBodies=*/true);
    P.Initialize();
    Out.Tokens.push_back(P.getCurToken());

    Token T;
    do {
      PP.Lex(T);
      Out.Tokens.push_back(T);
    } while (T.isNot(tok::eof) && T.isNot(tok::annot_repl_input_end));

    Out.Hashes.reserve(Out.Tokens.size());
    for (const Token &Tok : Out.Tokens)
      Out.Hashes.push_back(hashToken(Tok));

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

/// The targets to record *besides* the one being compiled.
///
/// The primary target's stream comes from the compilation itself, not from a
/// nested pass: its tokens have to belong to the identifier table and
/// SourceManager the parser is using, and only the real preprocessor produces
/// those. Only the aux target is known today; Stage 7 supplies the driver's
/// full offload target list.
static std::vector<std::string> targetsToRecord(CompilerInstance &CI) {
  std::vector<std::string> Triples;
  const std::string &Aux = CI.getFrontendOpts().AuxTriple;
  if (!Aux.empty() && Aux != CI.getTargetOpts().Triple)
    Triples.push_back(Aux);
  return Triples;
}

MultiTargetRecordings clang::recordTargetStreams(CompilerInstance &CI) {
  MultiTargetRecordings Out;

  for (StringRef Triple : targetsToRecord(CI)) {
    auto Invocation = std::make_shared<CompilerInvocation>(CI.getInvocation());
    TargetOptions &TO = Invocation->getTargetOpts();
    TO.Triple = Triple.str();
    // The primary's own triple becomes this nested pass's aux triple: without
    // this, FrontendOpts.AuxTriple is left as whatever the primary's aux was,
    // which after the swap above self-references the same target this pass
    // is now recording (host recording device, aux still names the device).
    // That starves the recorded pass's AuxTargetInfo of the real other side,
    // so architecture-identity macros (e.g. __x86_64__) it should inherit
    // from the aux target never get defined, producing spurious conditional
    // divergence in target-agnostic system headers (e.g. glibc's
    // bits/pthreadtypes-arch.h, which branches on __x86_64__).
    Invocation->getFrontendOpts().AuxTriple = CI.getTargetOpts().Triple;
    {
      // CPU, tune-CPU and features name the *primary* target's hardware and
      // mean nothing to another architecture -- keeping them is an immediate
      // "unknown target CPU" error. Clearing them records the aux target
      // generically, which is enough to align conditional structure but not
      // exact: a #if on a CPU feature macro would resolve differently from the
      // real aux compilation. Stage 7 supplies the driver's real per-target
      // options and removes the approximation.
      TO.CPU.clear();
      TO.TuneCPU.clear();
      TO.Features.clear();
      TO.FeaturesAsWritten.clear();
    }
    // Recording is the whole job; nothing downstream of the preprocessor runs.
    Invocation->getFrontendOpts().ProgramAction = frontend::ParseSyntaxOnly;
    Invocation->getFrontendOpts().OutputFile.clear();

    // Offload compilations put the two targets on opposite sides of this flag,
    // and it changes both the predefines and which declarations are visible.
    if (CI.getLangOpts().CUDA)
      Invocation->getLangOpts().CUDAIsDevice = !CI.getLangOpts().CUDAIsDevice;

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
    R.Triple = Triple.str();
    RecordStreamAction Action(R);
    if (!Nested->ExecuteAction(Action) || R.Tokens.empty()) {
      llvm::errs() << "multi-target recording: could not record " << Triple;
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
  return Out;
}

