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
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Frontend/Utils.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Parse/Parser.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

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
  /// How many tokens the stream is expected to have, if known.
  size_t ExpectedTokens;
  /// Where to print the stream as -E does, if anywhere.
  raw_ostream *Preprocessed;

public:
  RecordStreamAction(TargetRecording &Out, size_t ExpectedTokens,
                     raw_ostream *Preprocessed)
      : Out(Out), ExpectedTokens(ExpectedTokens), Preprocessed(Preprocessed) {}

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
    std::optional<PreprocessedOutputPrinter> Printer;
    if (Preprocessed)
      Printer.emplace(PP, *Preprocessed);

    PP.EnterMainSourceFile();
    if (!PP.getCurrentLexer())
      return;

    Parser P(PP, CI.getSema(), /*SkipFunctionBodies=*/true);
    P.Initialize();
    Out.Tokens.reserve(ExpectedTokens);
    Out.Tokens.push_back(P.getCurToken());
    if (Printer)
      Printer->printToken(P.getCurToken());

    // The first token is the end of the file for a target that sees nothing.
    Token T = P.getCurToken();
    while (T.isNot(tok::eof) && T.isNot(tok::annot_repl_input_end)) {
      PP.Lex(T);
      Out.Tokens.push_back(T);
      if (Printer)
        Printer->printToken(T);
    }
    if (Printer)
      Printer->finish();

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

static llvm::cl::list<std::string> PreprocessedOutputs(
    "multi-target-preprocessed-output", llvm::cl::Hidden,
    llvm::cl::desc("<variant>:<path>: print a target's preprocessed source to "
                   "<path>, as -E does. Variant 1 is the primary target, 2 "
                   "and up the -multi-target-aux-invocation targets in order"));

std::unique_ptr<llvm::raw_ostream>
clang::openMultiTargetPreprocessedOutput(CompilerInstance &CI,
                                         unsigned Variant) {
  for (StringRef Spec : PreprocessedOutputs) {
    auto [Number, Path] = Spec.split(':');
    unsigned V;
    if (Number.getAsInteger(10, V) || V != Variant)
      continue;
    std::error_code EC;
    auto OS = std::make_unique<llvm::raw_fd_ostream>(Path, EC,
                                                     llvm::sys::fs::OF_Text);
    if (!EC)
      return OS;
    CI.getDiagnostics().Report(diag::err_fe_unable_to_open_output)
        << Path << EC.message();
    return nullptr;
  }
  return nullptr;
}

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

MultiTargetRecordings clang::recordTargetStreams(CompilerInstance &CI) {
  MultiTargetRecordings Out;

  for (auto [Index, RT] : llvm::enumerate(targetsToRecord(CI))) {
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
    // -verify checks the diagnostics of the compilation itself. A pass that
    // only preprocesses would miss every diagnostic expected from parsing.
    Invocation->getDiagnosticOpts().VerifyDiagnostics = false;

    auto Nested = std::make_unique<CompilerInstance>(
        std::move(Invocation), CI.getPCHContainerOperations());
    Nested->setVirtualFileSystem(CI.getVirtualFileSystemPtr());
    // The pass reports what preprocessing for its target reports, as that
    // target's own compilation would.
    Nested->createDiagnostics(
        new TextDiagnosticPrinter(llvm::errs(), Nested->getDiagnosticOpts()),
        /*ShouldOwnClient=*/true);
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
    std::unique_ptr<llvm::raw_ostream> Preprocessed =
        openMultiTargetPreprocessedOutput(CI, 2 + Index);
    // Every target's stream is about as long as the one recorded before it.
    RecordStreamAction Action(
        R, Out.Targets.empty() ? 0 : Out.Targets.back().Tokens.size() * 9 / 8,
        Preprocessed.get());
    // Compiling every target from the primary's tokens instead would succeed
    // where the target's own compilation fails.
    if (!Nested->ExecuteAction(Action)) {
      const TargetOptions &TO = Nested->getTargetOpts();
      CI.getDiagnostics().Report(diag::err_fe_multi_target_recording_failed)
          << (TO.CPU.empty() ? TO.Triple : TO.CPU);
      return {};
    }
    if (R.Tokens.empty()) {
      llvm::errs() << "multi-target recording: could not record " << RT.Triple
                   << ": no tokens produced\n";
      return {};
    }
    R.SM = &Nested->getSourceManager();
    Out.Targets.push_back(std::move(R));
    Out.Owners.push_back(std::move(Nested));
  }
  return Out;
}
