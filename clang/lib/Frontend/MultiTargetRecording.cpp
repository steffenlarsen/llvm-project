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
#include "llvm/ADT/STLExtras.h"
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

namespace {
/// PROTOTYPE (Stage 7): one target besides the primary to record a pass for.
/// Was just a triple through Stage 1-5, when only one aux target (the "other
/// side" of a host/device pair) was ever configured; generalized to carry its
/// own CPU/features/device-ness once -multi-target-aux-target can configure
/// more than one.
struct RecordTarget {
  std::string Triple;
  std::string CPU;
  std::vector<std::string> Features;
  /// Explicit rather than inferred by copying or flipping the primary's
  /// CUDAIsDevice: CI.getMultiTargetAuxTargets() (see below) is always the
  /// AMDGPU/device side, regardless of which side the primary itself sits on,
  /// so this is unconditionally true for those entries -- see the ROOT CAUSE
  /// comment on targetsToRecord() below.
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
/// PROTOTYPE (Stage 7): when -multi-target-aux-target configured N device
/// archs, that list (CompilerInstance::getMultiTargetAuxTargets()) mirrors
/// CompilerInstance::createASTContext()'s own exclusive branching on the same
/// list, so this function's AuxRecordings count always matches ASTContext's
/// AuxTargets count (both derive from the same list, in the same order:
/// element 0 is variant 2, element 1 is variant 3, etc).
///
/// ROOT CAUSE (found while chasing a real-header miscompile under
/// -fintegrated-hip-device-codegen): CI.getMultiTargetAuxTargets() is always
/// the AMDGPU/device side -- CompilerInstance::createASTContext() builds it
/// from "AMDGPUSideTriple", its own comment spelling out that this is device
/// regardless of whether the *primary* job is device (--cuda-device-only,
/// where getTarget() is itself AMDGPU) or host (the common
/// -fintegrated-hip-device-codegen case, where the AMDGPU triple lives on
/// getAuxTarget() instead and the primary compiles in host mode). A previous
/// version of this loop set RT.CUDAIsDevice to the *primary's own*
/// CUDAIsDevice, reasoning that an aux device arch "sits on the same side as
/// the primary" -- true only by coincidence in the device-primary case this
/// was tested against. For a host-primary job it recorded gfx90a/gfx942
/// aux passes with CUDAIsDevice=false, i.e. in *host* mode: each nested pass
/// then took the host branch of __clang_hip_runtime_wrapper.h and pulled in
/// real libstdc++/glibc headers a device compile never touches, duplicating
/// declarations like glibc's pthread_attr_t typedef into the merged AST
/// alongside the real host recording's own copy ("typedef redefinition with
/// different types") -- confirmed by comparing against a real
/// --offload-device-only compile of the same source, which never reaches
/// pthreadtypes.h at all. So this is unconditionally true, not tied to the
/// primary's own mode.
///
/// It never covers the legacy host/device AuxTriple pairing (the
/// CUDA/OpenMP/SYCL "other side") -- append that too, so a real "1 host + N
/// device archs" HIP compile gets a recording for the host as well, not just
/// the extra device archs. Skip it if it's already been redundantly named as
/// one more -multi-target-aux-target spec.
static std::vector<RecordTarget> targetsToRecord(CompilerInstance &CI) {
  std::vector<RecordTarget> Targets;

  for (const IntrusiveRefCntPtr<TargetInfo> &TI :
       CI.getMultiTargetAuxTargets()) {
    RecordTarget RT;
    RT.Triple = TI->getTargetOpts().Triple;
    RT.CPU = TI->getTargetOpts().CPU;
    RT.Features = TI->getTargetOpts().FeaturesAsWritten;
    RT.CUDAIsDevice = true;
    Targets.push_back(std::move(RT));
  }

  const std::string &Aux = CI.getFrontendOpts().AuxTriple;
  if (!Aux.empty() && Aux != CI.getTargetOpts().Triple &&
      llvm::none_of(Targets, [&](const RecordTarget &RT) {
        return RT.Triple == Aux;
      })) {
    RecordTarget RT;
    RT.Triple = Aux;
    if (CI.getFrontendOpts().AuxTargetCPU)
      RT.CPU = *CI.getFrontendOpts().AuxTargetCPU;
    if (CI.getFrontendOpts().AuxTargetFeatures)
      RT.Features = *CI.getFrontendOpts().AuxTargetFeatures;
    RT.CUDAIsDevice = !CI.getLangOpts().CUDAIsDevice;
    Targets.push_back(std::move(RT));
  }
  return Targets;
}

MultiTargetRecordings clang::recordTargetStreams(CompilerInstance &CI) {
  MultiTargetRecordings Out;

  for (const RecordTarget &RT : targetsToRecord(CI)) {
    auto Invocation = std::make_shared<CompilerInvocation>(CI.getInvocation());
    TargetOptions &TO = Invocation->getTargetOpts();
    TO.Triple = RT.Triple;
    // The primary's own triple becomes this nested pass's aux triple only when
    // RT sits on the *opposite* side of CUDAIsDevice from the primary (the
    // legacy host/device pairing): without this, FrontendOpts.AuxTriple is
    // left as whatever the primary's aux was, which after the swap above
    // self-references the same target this pass is now recording (host
    // recording device, aux still names the device). That starves the
    // recorded pass's AuxTargetInfo of the real other side, so
    // architecture-identity macros (e.g. __x86_64__) it should inherit from
    // the aux target never get defined, producing spurious conditional
    // divergence in target-agnostic system headers (e.g. glibc's
    // bits/pthreadtypes-arch.h, which branches on __x86_64__).
    //
    // When RT sits on the *same* side (the N-aux-device-arch case, e.g.
    // recording gfx1100 alongside a gfx942 primary), this swap must NOT
    // happen: InitPreprocessor.cpp unconditionally predefines both a target's
    // own macros and (when set) its AuxTargetInfo's macros
    // (`(LangOpts.CUDA || LangOpts.isTargetDevice()) && PP.getAuxTargetInfo()`).
    // Pointing this pass's aux at the primary's own device triple/CPU would
    // make it predefine the *primary's* arch-identity macro (__gfx942__)
    // alongside its own (__gfx1100__) -- both defined at once, so an #if
    // chain testing one before the other always picks the primary's branch,
    // making every device-arch-vs-device-arch recording collapse to the
    // primary's content. Confirmed via a real `#if defined(__gfx942__)
    // ... #elif defined(__gfx1100__)` smoke test: leaving the primary's own
    // aux (the real host triple/CPU, already present via the deep-copied
    // Invocation) untouched here fixed it. Left alone, this pass's aux stays
    // whatever the primary's own aux already was -- correct, since a real
    // `--offload-arch=gfx1100` compile of this same file would have the same
    // host aux, never the primary's own device arch.
    if (RT.CUDAIsDevice != CI.getLangOpts().CUDAIsDevice) {
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
    }
    // CPU, tune-CPU and features name the target this pass is recording (RT),
    // already resolved by targetsToRecord() -- from the legacy
    // AuxTargetCPU/AuxTargetFeatures scalars for the single-aux case, or from
    // the aux TargetInfo's own TargetOpts for the new N-aux-target case.
    // TuneCPU isn't part of either source, so it's always cleared.
    TO.TuneCPU.clear();
    TO.CPU = RT.CPU;
    TO.Features = RT.Features;
    TO.FeaturesAsWritten = RT.Features;
    // Recording is the whole job; nothing downstream of the preprocessor runs.
    Invocation->getFrontendOpts().ProgramAction = frontend::ParseSyntaxOnly;
    Invocation->getFrontendOpts().OutputFile.clear();

    // Offload compilations can put two targets on opposite sides of this flag
    // (host vs. single device, the legacy pairing) or on the same side (two
    // device archs, the new N-aux-target case) -- RT.CUDAIsDevice already
    // encodes which, computed once in targetsToRecord().
    if (CI.getLangOpts().CUDA)
      Invocation->getLangOpts().CUDAIsDevice = RT.CUDAIsDevice;

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
    RecordStreamAction Action(R);
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
  return Out;
}

