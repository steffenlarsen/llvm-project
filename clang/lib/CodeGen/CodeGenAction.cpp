//===--- CodeGenAction.cpp - LLVM Code Generation Frontend Action ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/CodeGen/CodeGenAction.h"
#include "BackendConsumer.h"
#include "CGCall.h"
#include "CodeGenModule.h"
#include "CoverageMappingGen.h"
#include "MacroPPCallbacks.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclGroup.h"
#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LangStandard.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/CodeGen/BackendUtil.h"
#include "clang/CodeGen/ModuleBuilder.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Serialization/ASTWriter.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LLVMRemarkStreamer.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassTimingInfo.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/LTO/LTOBackend.h"
#include "llvm/Linker/Linker.h"
#include "llvm/Object/OffloadBinary.h"
#include "llvm/Pass.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileOutputBuffer.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Mutex.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TimeProfiler.h"
#include "llvm/Support/Timer.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Transforms/IPO/Internalize.h"
#include "llvm/Transforms/Utils/Cloning.h"

#include <optional>
using namespace clang;
using namespace llvm;

#define DEBUG_TYPE "codegenaction"

namespace {
llvm::ManagedStatic<llvm::sys::SmartMutex<true>> TimePassesMutex;
}

namespace clang {
class BackendConsumer;
class ClangDiagnosticHandler final : public DiagnosticHandler {
public:
  ClangDiagnosticHandler(const CodeGenOptions &CGOpts, BackendConsumer *BCon)
      : CodeGenOpts(CGOpts), BackendCon(BCon) {}

  bool handleDiagnostics(const DiagnosticInfo &DI) override;

  bool isAnalysisRemarkEnabled(StringRef PassName) const override {
    return CodeGenOpts.OptimizationRemarkAnalysis.patternMatches(PassName);
  }
  bool isMissedOptRemarkEnabled(StringRef PassName) const override {
    return CodeGenOpts.OptimizationRemarkMissed.patternMatches(PassName);
  }
  bool isPassedOptRemarkEnabled(StringRef PassName) const override {
    return CodeGenOpts.OptimizationRemark.patternMatches(PassName);
  }

  bool isAnyRemarkEnabled() const override {
    return CodeGenOpts.OptimizationRemarkAnalysis.hasValidPattern() ||
           CodeGenOpts.OptimizationRemarkMissed.hasValidPattern() ||
           CodeGenOpts.OptimizationRemark.hasValidPattern();
  }

private:
  const CodeGenOptions &CodeGenOpts;
  BackendConsumer *BackendCon;
};

static void reportOptRecordError(Error E, DiagnosticsEngine &Diags,
                                 const CodeGenOptions &CodeGenOpts) {
  handleAllErrors(
      std::move(E),
    [&](const LLVMRemarkSetupFileError &E) {
        Diags.Report(diag::err_cannot_open_file)
            << CodeGenOpts.OptRecordFile << E.message();
      },
    [&](const LLVMRemarkSetupPatternError &E) {
        Diags.Report(diag::err_drv_optimization_remark_pattern)
            << E.message() << CodeGenOpts.OptRecordPasses;
      },
    [&](const LLVMRemarkSetupFormatError &E) {
        Diags.Report(diag::err_drv_optimization_remark_format)
            << CodeGenOpts.OptRecordFormat;
      });
}

/// PROTOTYPE (Stage 5): drive a second CodeGenModule for the aux target
/// variant (Decl::TargetVariant 2) from the same shared AST as the primary,
/// instead of needing a whole separate cc1 invocation. Off by default; when
/// off BackendConsumer behaves exactly as it does today.
static llvm::cl::opt<bool> MultiTargetCodeGen(
    "multi-target-codegen", llvm::cl::Hidden, llvm::cl::init(false),
    llvm::cl::desc("Prototype: drive a second CodeGenModule for the aux "
                   "target variant from the shared AST"));

/// PROTOTYPE (Stage 5): write each pre-backend CodeGenModule's IR (primary
/// and, if -multi-target-codegen produced one, aux) to
/// <dir>/<input-stem>.{primary,aux}.ll, for comparison against separately
/// invoked single-target compiles of the same TU. A debug/verification aid
/// only; the dumped IR is pre-link, pre-EmbedBitcode, pre-backend.
static llvm::cl::opt<std::string> MultiTargetCodeGenDumpDir(
    "multi-target-codegen-dump-dir", llvm::cl::Hidden, llvm::cl::init(""),
    llvm::cl::desc("Prototype: write each CodeGenModule's pre-backend IR "
                   "under this directory for comparison against separate "
                   "single-target compiles"));

/// PROTOTYPE (Stage 7 Phase 2 Slice A): give one AuxGenEntry a real output
/// file, repeatable -- one flag per aux variant that should actually produce
/// (rather than produce-and-discard, Stage 5's behavior) a `.bc` file.
/// Format: <variant>:<path>. Follows the same file-local, hidden, -mllvm-only
/// convention as every other prototype flag (e.g. CompilerInstance.cpp's
/// -multi-target-aux-target), placed here rather than in
/// CompilerInstance.cpp since this file is what consumes it.
static llvm::cl::list<std::string> MultiTargetAuxOutputSpecs(
    "multi-target-aux-output", llvm::cl::Hidden,
    llvm::cl::desc("Prototype: this aux variant's own bitcode output path, "
                   "repeatable. Format: <variant>:<path>"));

/// PROTOTYPE (Stage 7 Phase 2 Slice A, format extended in Phase 4 Slice D):
/// this aux variant's own -mlink-builtin-bitcode/-mlink-bitcode-file
/// equivalent file list, repeatable per file (a variant may need more than
/// one, e.g. ocml.bc and oclc_isa_version_942.bc). Format:
/// <variant>:<internalize 0|1>:<path>. The internalize bit mirrors
/// AMDGPUToolChain::addClangTargetOptions's own choice of -mlink-builtin-
/// bitcode (internalize) vs -mlink-bitcode-file (don't) per
/// ToolChain::BitCodeLibraryInfo::ShouldInternalize -- real ROCm device-lib
/// lists are not uniformly internalize=true (the ASan device runtime,
/// RocmInstallationDetector::getCommonBitcodeLibs's AddSanBCLibs, is added
/// with ShouldInternalize=false), so hardcoding true here would silently
/// mismatch a real separately-scheduled cc1 job's behavior for that library.
/// A real, separately scheduled cc1 job for this arch would link its own
/// arch-specific set; the one shared CodeGenOpts.LinkBitcodeFiles list this
/// process's primary compile draws from is not a valid source for an aux
/// entry's own arch.
static llvm::cl::list<std::string> MultiTargetAuxLinkBitcodeSpecs(
    "multi-target-aux-link-bitcode", llvm::cl::Hidden,
    llvm::cl::desc("Prototype: this aux variant's own -mlink-builtin-bitcode/"
                   "-mlink-bitcode-file file, repeatable. Format: "
                   "<variant>:<internalize 0|1>:<path>"));

static llvm::DenseMap<unsigned, std::string> parseMultiTargetAuxOutputSpecs() {
  llvm::DenseMap<unsigned, std::string> Paths;
  for (StringRef Entry : MultiTargetAuxOutputSpecs) {
    StringRef VariantStr, Path;
    std::tie(VariantStr, Path) = Entry.split(':');
    unsigned Variant;
    if (VariantStr.getAsInteger(10, Variant) || Path.empty())
      continue; // Malformed -- prototype, not hardened against bad input.
    Paths[Variant] = Path.str();
  }
  return Paths;
}

namespace {
struct AuxLinkBitcodeFile {
  std::string Path;
  bool ShouldInternalize;
};
} // namespace

static llvm::DenseMap<unsigned, std::vector<AuxLinkBitcodeFile>>
parseMultiTargetAuxLinkBitcodeSpecs() {
  llvm::DenseMap<unsigned, std::vector<AuxLinkBitcodeFile>> Files;
  for (StringRef Entry : MultiTargetAuxLinkBitcodeSpecs) {
    StringRef VariantStr, InternalizeStr, Path;
    std::tie(VariantStr, Path) = Entry.split(':');
    std::tie(InternalizeStr, Path) = Path.split(':');
    unsigned Variant, Internalize;
    if (VariantStr.getAsInteger(10, Variant) ||
        InternalizeStr.getAsInteger(10, Internalize) || Path.empty())
      continue; // Malformed -- prototype, not hardened against bad input.
    Files[Variant].push_back({Path.str(), static_cast<bool>(Internalize)});
  }
  return Files;
}

/// PROTOTYPE (Stage 7 Phase 3 Slice A): package every configured aux entry's
/// own bitcode output (Slice A's runAuxBackendTail) into a real .hipfb at this
/// path, once every runAuxBackendTail call has completed. Off by default;
/// when off, no packaging happens and no subprocess is launched, matching
/// every other prototype flag's convention.
static llvm::cl::opt<std::string> MultiTargetPackageFatbin(
    "multi-target-package-fatbin", llvm::cl::Hidden, llvm::cl::init(""),
    llvm::cl::desc("Prototype: package every configured aux entry's own "
                   "bitcode output into a real .hipfb at this path"));

/// Mirrors the real pipeline's own LinkerWrapper::ConstructJob (Clang.cpp),
/// which only forwards "--device-compiler=--rocm-path=<path>" when the user
/// explicitly passed --rocm-path= on the driver command line. Empty (the
/// common case, relying on clang-linker-wrapper's own ROCm auto-detection)
/// unless the driver-side emitIntegratedHipDeviceCodegenFlags saw that arg.
static llvm::cl::opt<std::string> MultiTargetRocmPath(
    "multi-target-rocm-path", llvm::cl::Hidden, llvm::cl::init(""),
    llvm::cl::desc("Prototype: forwarded to clang-linker-wrapper as "
                   "--device-compiler=--rocm-path=<path>, only when the "
                   "driver saw an explicit --rocm-path="));

static void dumpModuleForDiff(StringRef Dir, StringRef InFile,
                              StringRef Suffix, llvm::Module *M) {
  llvm::SmallString<256> Path(Dir);
  llvm::sys::path::append(Path, (llvm::sys::path::stem(InFile) + Suffix));
  std::error_code EC;
  llvm::raw_fd_ostream OS(Path, EC, llvm::sys::fs::OF_Text);
  if (EC)
    return;
  M->print(OS, nullptr);
}

namespace {
/// PROTOTYPE (Stage 5): scopes one dispatch into the aux CodeGenModule to its
/// own target. ASTContext::TargetScope alone is not enough: a plain
/// sizeof(T) in a function body is resolved by CodeGen against whichever
/// target is ambient when CodeGen runs, not the target in scope when the
/// containing template was instantiated -- so each dispatch needs its own
/// live scope, not just one entered at construction. LangOptions::CUDAIsDevice
/// is a second, separate problem TargetScope does not cover: it is a single
/// process-wide flag CodeGen reads to choose host/device paths, and
/// TargetScope deliberately leaves it alone (see its own header comment --
/// swapping it there once broke per-target instantiation, which needs the
/// compilation's own device-ness rather than the ambient lookup target).
/// This is a different, narrower swap: it lasts only for the duration of one
/// call into the aux CodeGenModule, not the whole AST walk.
class MultiTargetCodeGenScope {
  ASTContext::TargetScope TS;
  LangOptions &LangOpts;
  CodeGenOptions &CGO;
  bool SavedCUDAIsDevice;
  bool SavedConvergentFunctions;
  Visibility SavedValueVisibilityMode;
  Visibility SavedTypeVisibilityMode;
  bool SavedSetVisibilityForExternDecls;
  bool SavedLTOUnit;
  bool SavedMathErrno;

public:
  /// Scopes one dispatch into \p Entry's CodeGenModule: its own TargetInfo
  /// (the 3-arg TargetScope form, using Entry.TI directly rather than a
  /// second ASTContext::getTargetForVariant(Entry.Variant) lookup) plus the
  /// handful of LangOptions/CodeGenOptions fields Entry carries its own
  /// per-target value for (see BackendConsumer::AuxGenEntry). All of these
  /// live on the single LangOptions/CodeGenOptions this whole cc1 process
  /// shares across every CodeGenModule, so each is saved and restored here
  /// exactly like the pre-existing CUDAIsDevice swap.
  MultiTargetCodeGenScope(ASTContext &Ctx, LangOptions &LangOpts,
                          CodeGenOptions &CGO,
                          const BackendConsumer::AuxGenEntry &Entry)
      : TS(Ctx, *Entry.TI, Entry.Variant), LangOpts(LangOpts), CGO(CGO),
        SavedCUDAIsDevice(LangOpts.CUDAIsDevice),
        SavedConvergentFunctions(LangOpts.ConvergentFunctions),
        SavedValueVisibilityMode(LangOpts.getValueVisibilityMode()),
        SavedTypeVisibilityMode(LangOpts.getTypeVisibilityMode()),
        SavedSetVisibilityForExternDecls(LangOpts.SetVisibilityForExternDecls),
        SavedLTOUnit(CGO.LTOUnit), SavedMathErrno(LangOpts.MathErrno) {
    LangOpts.CUDAIsDevice = Entry.IsDevice;
    LangOpts.ConvergentFunctions = Entry.ConvergentFunctions;
    LangOpts.setValueVisibilityMode(Entry.ValueVisibilityMode);
    LangOpts.setTypeVisibilityMode(Entry.TypeVisibilityMode);
    LangOpts.SetVisibilityForExternDecls = Entry.SetVisibilityForExternDecls;
    CGO.LTOUnit = Entry.LTOUnit;
    LangOpts.MathErrno = Entry.MathErrno;
  }
  ~MultiTargetCodeGenScope() {
    LangOpts.CUDAIsDevice = SavedCUDAIsDevice;
    LangOpts.ConvergentFunctions = SavedConvergentFunctions;
    LangOpts.setValueVisibilityMode(SavedValueVisibilityMode);
    LangOpts.setTypeVisibilityMode(SavedTypeVisibilityMode);
    LangOpts.SetVisibilityForExternDecls = SavedSetVisibilityForExternDecls;
    CGO.LTOUnit = SavedLTOUnit;
    LangOpts.MathErrno = SavedMathErrno;
  }
  MultiTargetCodeGenScope(const MultiTargetCodeGenScope &) = delete;
};
} // namespace

BackendConsumer::BackendConsumer(CompilerInstance &CI, BackendAction Action,
                                 IntrusiveRefCntPtr<llvm::vfs::FileSystem> VFS,
                                 LLVMContext &C,
                                 SmallVector<LinkModule, 4> LinkModules,
                                 StringRef InFile,
                                 std::unique_ptr<raw_pwrite_stream> OS,
                                 CoverageSourceInfo *CoverageInfo,
                                 llvm::Module *CurLinkModule)
    : CI(CI), Diags(CI.getDiagnostics()), CodeGenOpts(CI.getCodeGenOpts()),
      TargetOpts(CI.getTargetOpts()), LangOpts(CI.getLangOpts()),
      AsmOutStream(std::move(OS)), FS(VFS), Action(Action),
      Gen(CreateLLVMCodeGen(CI, InFile, C, CoverageInfo)),
      InFile(InFile), CoverageInfo(CoverageInfo),
      LinkModules(std::move(LinkModules)), CurLinkModule(CurLinkModule) {
  TimerIsEnabled = CodeGenOpts.TimePasses;
  {
    llvm::sys::SmartScopedLock<true> Lock(*TimePassesMutex);
    llvm::TimePassesIsEnabled = CodeGenOpts.TimePasses;
    llvm::TimePassesPerRun = CodeGenOpts.TimePassesPerRun;
  }
  if (CodeGenOpts.TimePasses)
    LLVMIRGeneration.init("irgen", "LLVM IR generation", CI.getTimerGroup());
}

llvm::Module* BackendConsumer::getModule() const {
  return Gen->GetModule();
}

std::unique_ptr<llvm::Module> BackendConsumer::takeModule() {
  return std::unique_ptr<llvm::Module>(Gen->ReleaseModule());
}

CodeGenerator* BackendConsumer::getCodeGenerator() {
  return Gen.get();
}

void BackendConsumer::HandleCXXStaticMemberVarInstantiation(VarDecl *VD) {
  Gen->HandleCXXStaticMemberVarInstantiation(VD);
  dispatchToAuxGens(
      [&](CodeGenerator &G) { G.HandleCXXStaticMemberVarInstantiation(VD); });
}

void BackendConsumer::dispatchToAuxGens(
    llvm::function_ref<void(CodeGenerator &)> Fn) {
  for (AuxGenEntry &Entry : AuxGens) {
    MultiTargetCodeGenScope Scope(*Context, CI.getLangOpts(),
                                  CI.getCodeGenOpts(), Entry);
    Fn(*Entry.Gen);
  }
}

void BackendConsumer::Initialize(ASTContext &Ctx) {
  assert(!Context && "initialized multiple times");

  Context = &Ctx;

  if (TimerIsEnabled)
    LLVMIRGeneration.startTimer();

  // Ctx's own aux-target list (not CI.getMultiTargetAuxTargets()) is the
  // source of truth here: it also includes the older single implicit "other
  // side of CUDA/OpenMP/SYCL" aux target (populated from -aux-triple, no
  // -multi-target-aux-target flag needed), which every pre-Stage-7 build of
  // this prototype already relies on to get exactly one AuxGen.
  unsigned NumAuxTargets = Ctx.getNumAuxTargets();
  bool HasAuxGens = MultiTargetCodeGen && NumAuxTargets > 0;
  {
    // CodeGenModule captures Context.getCurrentTargetVariant() once, at
    // construction (see CodeGenModule's TargetVariant field), and never
    // again. Gen is variant 1 -- the target this cc1 invocation was actually
    // invoked for -- but without any aux CodeGenModule there is nothing to
    // distinguish it from, so ambient (ordinary compiles never touch
    // CurrentTargetVariant at all) already means the same thing. Scope this
    // only when at least one aux CodeGenModule actually exists: an
    // unconditional scope here would give ordinary single-target compiles a
    // nonzero ambient variant for the first time, which nothing downstream
    // expects.
    std::optional<ASTContext::TargetScope> PrimaryScope;
    if (HasAuxGens)
      PrimaryScope.emplace(Ctx, 1);
    Gen->Initialize(Ctx);
  }

  if (HasAuxGens) {
    llvm::DenseMap<unsigned, std::string> AuxOutputPaths =
        parseMultiTargetAuxOutputSpecs();
    llvm::DenseMap<unsigned, std::vector<AuxLinkBitcodeFile>>
        AuxLinkBitcodeFiles = parseMultiTargetAuxLinkBitcodeSpecs();
    for (unsigned I = 0; I != NumAuxTargets; ++I) {
      AuxGenEntry Entry;
      Entry.Variant = 2 + I;
      Entry.TI = Ctx.getTargetForVariant(Entry.Variant);
      Entry.CPU = Entry.TI->getTargetOpts().CPU;
      // This entry's own values for the handful of driver-decided,
      // triple-dependent LangOptions/CodeGenOptions flags a real, separately
      // scheduled cc1 job for this target would have computed from its own
      // argv (see BackendConsumer::AuxGenEntry's doc comment and
      // auxgen-shared-codegenopts-leak.md). Not "the other side of the
      // primary": two device-arch aux entries alongside a device primary
      // are both devices, so this is computed from Entry.TI's own triple.
      Entry.IsDevice = Entry.TI->getTriple().isGPU();
      Entry.ConvergentFunctions =
          Entry.IsDevice || CI.getLangOpts().OpenCL || CI.getLangOpts().HLSL;
      bool WantsHiddenVisibility = Entry.TI->getTriple().isAMDGPU();
      Entry.ValueVisibilityMode =
          WantsHiddenVisibility ? HiddenVisibility : DefaultVisibility;
      Entry.TypeVisibilityMode = Entry.ValueVisibilityMode;
      Entry.SetVisibilityForExternDecls = WantsHiddenVisibility;
      Entry.LTOUnit = Entry.IsDevice;
      // Mirrors AMDGPUToolChain::IsMathErrnoDefault() (always false) vs. the
      // host toolchain's default (true) -- see AuxGenEntry's doc comment.
      Entry.MathErrno = !Entry.IsDevice;
      Entry.Gen = CreateLLVMCodeGen(CI, InFile, Gen->GetModule()->getContext(),
                                    CoverageInfo);
      // Stage 7 Phase 2 Slice A: only entries a -multi-target-aux-output flag
      // actually configures get a real output file; every other entry stays
      // produced-and-discarded, exactly as Stage 5 left it.
      auto OutputIt = AuxOutputPaths.find(Entry.Variant);
      if (OutputIt != AuxOutputPaths.end()) {
        Entry.OutputPath = OutputIt->second;
        // UseTemporary=false: with atomic-write (UseTemporary=true), the
        // real bytes only land at OutputPath once CompilerInstance's own
        // OutputFiles list is drained via clearOutputFiles(), which happens
        // at EndSourceFile -- well after HandleTranslationUnit (and Phase 3
        // Slice A's packageAuxOutputs, which reads this file back) returns.
        // This entry's own bitcode is a prototype-internal artifact, not a
        // user-facing "-o" output needing atomic-replace robustness, so
        // writing it directly is correct.
        Entry.AsmOutStream =
            CI.createOutputFile(Entry.OutputPath, /*Binary=*/true,
                                /*RemoveFileOnSignal=*/true,
                                /*UseTemporary=*/false);
      }
      auto LinkBitcodeIt = AuxLinkBitcodeFiles.find(Entry.Variant);
      if (LinkBitcodeIt != AuxLinkBitcodeFiles.end()) {
        // Mirror CompilerInvocation.cpp's own OPT_mlink_builtin_bitcode vs
        // OPT_mlink_bitcode_file handling exactly, keyed per-file by the
        // driver-supplied internalize bit (see MultiTargetAuxLinkBitcodeSpecs
        // above): internalize=true gets LinkOnlyNeeded + PropagateAttrs +
        // Internalize, matching -mlink-builtin-bitcode; internalize=false
        // gets none of those, matching -mlink-bitcode-file. Hardcoding the
        // internalize=true case unconditionally would mismatch a real
        // separately-scheduled cc1 job's behavior for any device library
        // added with ShouldInternalize=false (e.g. the AMDGPU ASan device
        // runtime).
        std::vector<CodeGenOptions::BitcodeFileToLink> Files;
        for (const AuxLinkBitcodeFile &File : LinkBitcodeIt->second) {
          CodeGenOptions::BitcodeFileToLink F;
          F.Filename = File.Path;
          if (File.ShouldInternalize) {
            F.PropagateAttrs = true;
            F.Internalize = true;
            F.LinkFlags = llvm::Linker::Flags::LinkOnlyNeeded;
          }
          Files.push_back(F);
        }
        loadLinkModules(CI, Gen->GetModule()->getContext(), Files,
                       Entry.LinkModules);
      }
      AuxGens.push_back(std::move(Entry));
    }
    dispatchToAuxGens([&](CodeGenerator &G) { G.Initialize(Ctx); });
  }

  if (TimerIsEnabled)
    LLVMIRGeneration.stopTimer();
}

bool BackendConsumer::HandleTopLevelDecl(DeclGroupRef D) {
  PrettyStackTraceDecl CrashInfo(*D.begin(), SourceLocation(),
                                 Context->getSourceManager(),
                                 "LLVM IR generation of declaration");

  // Recurse.
  if (TimerIsEnabled && !LLVMIRGenerationRefCount++)
    CI.getFrontendTimer().yieldTo(LLVMIRGeneration);

  Gen->HandleTopLevelDecl(D);
  dispatchToAuxGens([&](CodeGenerator &G) { G.HandleTopLevelDecl(D); });

  if (TimerIsEnabled && !--LLVMIRGenerationRefCount)
    LLVMIRGeneration.yieldTo(CI.getFrontendTimer());

  return true;
}

void BackendConsumer::HandleInlineFunctionDefinition(FunctionDecl *D) {
  PrettyStackTraceDecl CrashInfo(D, SourceLocation(),
                                 Context->getSourceManager(),
                                 "LLVM IR generation of inline function");
  if (TimerIsEnabled)
    CI.getFrontendTimer().yieldTo(LLVMIRGeneration);

  Gen->HandleInlineFunctionDefinition(D);
  dispatchToAuxGens(
      [&](CodeGenerator &G) { G.HandleInlineFunctionDefinition(D); });

  if (TimerIsEnabled)
    LLVMIRGeneration.yieldTo(CI.getFrontendTimer());
}

void BackendConsumer::HandleInterestingDecl(DeclGroupRef D) {
  HandleTopLevelDecl(D);
}

bool BackendConsumer::LinkInModules(llvm::Module *M) {
  return LinkInModules(M, LinkModules);
}

// Links each entry in ModulesToLink into M, clearing it. Returns true on
// error.
bool BackendConsumer::LinkInModules(llvm::Module *M,
                                    SmallVectorImpl<LinkModule> &ModulesToLink) {
  return LinkInModules(M, ModulesToLink, TargetOpts);
}

bool BackendConsumer::LinkInModules(llvm::Module *M,
                                    SmallVectorImpl<LinkModule> &ModulesToLink,
                                    const TargetOptions &MergeTargetOpts) {
  for (auto &LM : ModulesToLink) {
    assert(LM.Module && "LinkModule does not actually have a module");

    if (LM.PropagateAttrs)
      for (Function &F : *LM.Module) {
        // Skip intrinsics. Keep consistent with how intrinsics are created
        // in LLVM IR.
        if (F.isIntrinsic())
          continue;
        CodeGen::mergeDefaultFunctionDefinitionAttributes(
          F, CodeGenOpts, LangOpts, MergeTargetOpts, LM.Internalize);
      }

    CurLinkModule = LM.Module.get();
    bool Err;

    if (LM.Internalize) {
      Err = Linker::linkModules(
          *M, std::move(LM.Module), LM.LinkFlags,
          [](llvm::Module &M, const llvm::StringSet<> &GVS) {
            internalizeModule(M, [&GVS](const llvm::GlobalValue &GV) {
              return !GV.hasName() || (GVS.count(GV.getName()) == 0);
            });
          });
    } else
      Err = Linker::linkModules(*M, std::move(LM.Module), LM.LinkFlags);

    if (Err)
      return true;
  }

  ModulesToLink.clear();
  return false; // success
}

// Stage 7 Phase 2 Slice A: run exactly one AuxGenEntry's own backend tail
// (link its own bitcode-file set, embed-bitcode no-op, emit its own bitcode
// output), mirroring HandleTranslationUnit's primary tail below. Does not
// touch call order relative to Gen -- that is Phase 2 Slice B, not this step.
// A no-op if this entry has no configured output
// (-multi-target-aux-output didn't name its variant), matching Stage 5's
// "produced and discarded" behavior for every other entry.
void BackendConsumer::runAuxBackendTail(AuxGenEntry &Entry) {
  if (!Entry.AsmOutStream)
    return;

  llvm::Module *M = Entry.Gen->GetModule();
  if (!M)
    return;

  MultiTargetCodeGenScope Scope(*Context, CI.getLangOpts(),
                                CI.getCodeGenOpts(), Entry);

  // Pass Entry.TI's own TargetOptions, not this->TargetOpts (the host's) --
  // otherwise mergeDefaultFunctionDefinitionAttributes stamps the host's
  // "target-cpu"="x86-64"/features onto every function in this entry's own
  // linked-in bitcode libraries (e.g. ROCm's ockl.bc) that lacks its own
  // target-cpu attribute, silently downgrading them to a generic AMDGPU
  // subtarget and causing ISel failures on real GPU-only instructions.
  if (LinkInModules(M, Entry.LinkModules, Entry.TI->getTargetOpts()))
    return;

  EmbedBitcode(M, CodeGenOpts, llvm::MemoryBufferRef());

  // Entry.TI->getTargetOpts() overrides CI's own (host-set) TargetOptions --
  // without this, CreateTargetMachine builds this aux entry's TargetMachine
  // using the host's CPU/Features (e.g. "x86-64"/"+cmov") against this
  // entry's own AMDGPU triple, silently falling back to a generic subtarget.
  emitBackendOutput(CI, CI.getCodeGenOpts(), Entry.TI->getDataLayoutString(),
                    M, Entry.Action, FS, std::move(Entry.AsmOutStream), this,
                    &Entry.TI->getTargetOpts());
}

// Stage 7 Phase 3 Slice A: locate a sibling tool binary (clang-linker-wrapper,
// clang-offload-bundler) installed alongside this process's own binary.
// Mirrors clang-linker-wrapper's own getExecutableDir()
// (ClangLinkerWrapper.cpp) -- cc1 has no persisted argv0
// (CompilerInvocation::CreateFromArgs's Argv0 parameter is transient), so
// this resolves via /proc/self/exe on Linux, the same pattern
// clang/lib/Interpreter/Interpreter.cpp already uses for the same reason.
static std::string locateSiblingTool(StringRef ToolName) {
  void *Ptr = reinterpret_cast<void *>(&locateSiblingTool);
  std::string ExePath = llvm::sys::fs::getMainExecutable(nullptr, Ptr);
  llvm::SmallString<256> Path(llvm::sys::path::parent_path(ExePath));
  llvm::sys::path::append(Path, ToolName);
  return std::string(Path);
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

// Stage 7 Phase 3 Slice A: package every configured aux entry's own on-disk
// .bc (Slice A's runAuxBackendTail output) into a single offload-binary
// container at OutputPath, via the same llvm::object::OffloadBinary::write
// library call clang-linker-wrapper itself already makes in-process
// (ClangLinkerWrapper.cpp). Entries with no configured output
// (Entry.OutputPath.empty()) are skipped, mirroring runAuxBackendTail's own
// gating. Returns true on error, reported via Diags.
static bool packageAuxOutputs(
    DiagnosticsEngine &Diags,
    llvm::ArrayRef<BackendConsumer::AuxGenEntry> AuxGens,
    StringRef OutputPath) {
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
    Image.StringData["arch"] = Entry.CPU;
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

// Stage 7 Phase 3 Slice A: run clang-linker-wrapper --emit-fatbin-only over
// the just-packaged offload-binary container, producing a real .hipfb at
// OutputHipfbPath. Kept as a subprocess call rather than an in-process
// library call: LinkerWrapper::ConstructJob (Clang.cpp) forwards a large,
// allowlisted CUDA/OpenMP/HIP/SYCL option set, and the tool itself shells out
// again to a per-arch clang+lld LTO backend -- reimplementing either is out
// of this project's scope (see the Stage 7 plan's Context section). This
// project's scope is non-RDC HIP-only, so the argv below is deliberately
// narrow, not a general re-derivation of ConstructJob. Returns true on error,
// reported via Diags.
static bool runLinkerWrapperFatbinOnly(
    DiagnosticsEngine &Diags,
    llvm::ArrayRef<BackendConsumer::AuxGenEntry> AuxGens,
    StringRef PackagedInput, StringRef OutputHipfbPath) {
  std::string WrapperPath = locateSiblingTool("clang-linker-wrapper");
  std::string BundlerPath = locateSiblingTool("clang-offload-bundler");

  std::vector<std::string> Arches;
  for (const BackendConsumer::AuxGenEntry &Entry : AuxGens)
    if (!Entry.OutputPath.empty())
      Arches.push_back(Entry.CPU);

  std::string ShouldExtract = "--should-extract=" + llvm::join(Arches, ",");
  std::string LinkerPathArg = "--linker-path=" + BundlerPath;

  std::string RocmPathArg;
  SmallVector<StringRef, 8> Argv{
      WrapperPath,
      ShouldExtract,
      "--device-compiler=amdgpu-amd-amdhsa=-flto=full",
  };
  if (!MultiTargetRocmPath.empty()) {
    RocmPathArg = "--device-compiler=--rocm-path=" + MultiTargetRocmPath;
    Argv.push_back(RocmPathArg);
  }
  Argv.append({LinkerPathArg, "--emit-fatbin-only", "-o", OutputHipfbPath,
              PackagedInput});

  std::string ErrMsg;
  bool ExecutionFailed = false;
  int Result = llvm::sys::ExecuteAndWait(
      WrapperPath, Argv, /*Env=*/std::nullopt, /*Redirects=*/{},
      /*SecondsToWait=*/0, /*MemoryLimit=*/0, &ErrMsg, &ExecutionFailed);

  if (ExecutionFailed) {
    Diags.Report(diag::err_drv_command_failure) << ErrMsg;
    return true;
  }
  if (Result != 0) {
    Diags.Report(diag::err_drv_command_failed) << WrapperPath << Result;
    return true;
  }
  return false;
}

void BackendConsumer::HandleTranslationUnit(ASTContext &C) {
  // Stage 7 Phase 2 Slice B: when packaging a real fatbin,
  // CGNVCUDARuntime::makeModuleCtorFunction (called from CodeGenModule::
  // Release(), itself called from Gen->HandleTranslationUnit(C) below) reads
  // CodeGenOpts.OffloadBinaryToEmbedFile off disk -- so the fatbin must exist
  // before Gen (host) dispatches, not merely before Gen's own backend tail.
  // Gated behind the same flag Phase 3 Slice A already gated its own new
  // behavior behind: every path that doesn't pass
  // -multi-target-package-fatbin takes the textually-unchanged branch below
  // and stays byte-for-byte identical to pre-Slice-B behavior.
  bool ReorderForPackaging = !MultiTargetPackageFatbin.empty();

  {
    llvm::TimeTraceScope TimeScope("Frontend");
    PrettyStackTraceString CrashInfo("Per-file LLVM IR generation");
    if (TimerIsEnabled && !LLVMIRGenerationRefCount++)
      CI.getFrontendTimer().yieldTo(LLVMIRGeneration);

    if (!ReorderForPackaging)
      Gen->HandleTranslationUnit(C);
    // Stage 5 stops here: each AuxGens entry's Module is now a second,
    // independently correct in-memory llvm::Module for that aux target.
    // Linking it, embedding bitcode into it, and running it through the LLVM
    // backend (everything below this block) is output-file plumbing that
    // belongs to Stage 7 Phase 2/3, not to producing the Module itself.
    dispatchToAuxGens([&](CodeGenerator &G) { G.HandleTranslationUnit(C); });

    if (TimerIsEnabled && !--LLVMIRGenerationRefCount)
      LLVMIRGeneration.yieldTo(CI.getFrontendTimer());
  }

  // Stage 7 Phase 2 Slice A: run each configured aux entry's own backend
  // tail, producing its own real bitcode output. Runs after Gen's own full
  // tail below when !ReorderForPackaging (no reordering); when
  // ReorderForPackaging, runs before Gen has dispatched at all -- see below.
  for (AuxGenEntry &Entry : AuxGens)
    runAuxBackendTail(Entry);

  // Stage 7 Phase 3 Slice A: package every configured aux entry's own
  // bitcode into a real .hipfb, once every runAuxBackendTail call above has
  // completed. Off by default.
  if (ReorderForPackaging) {
    llvm::SmallString<256> PackagedPath;
    std::error_code EC = llvm::sys::fs::createTemporaryFile(
        "multi-target-package", "bin", PackagedPath);
    if (EC) {
      Diags.Report(diag::err_cannot_open_file)
          << "multi-target-package" << EC.message();
    } else {
      // Stage 7 Phase 2 Slice B: wire the packaged fatbin into the host
      // CodeGenModule's own CodeGenOpts, read back by
      // makeModuleCtorFunction below. On any packaging failure, leave this
      // unset -- a diagnostic was already reported by the failing call, and
      // CGNVCUDARuntime::makeModuleCtorFunction's HIP branch already
      // degrades gracefully to an external __hip_fatbin declaration when
      // this field is empty, so the compile still completes rather than
      // crashing.
      if (!packageAuxOutputs(Diags, AuxGens, PackagedPath) &&
          !runLinkerWrapperFatbinOnly(Diags, AuxGens, PackagedPath,
                                      MultiTargetPackageFatbin))
        CI.getCodeGenOpts().OffloadBinaryToEmbedFile =
            std::string(MultiTargetPackageFatbin);
      llvm::sys::fs::remove(PackagedPath);
    }

    // Gen (host) dispatches only now, after the aux tail + packaging above,
    // so makeModuleCtorFunction's Release()-time read of
    // CodeGenOpts.OffloadBinaryToEmbedFile sees the real, just-packaged path
    // instead of running before it exists.
    llvm::TimeTraceScope TimeScope("Frontend");
    PrettyStackTraceString CrashInfo("Per-file LLVM IR generation");
    if (TimerIsEnabled && !LLVMIRGenerationRefCount++)
      CI.getFrontendTimer().yieldTo(LLVMIRGeneration);
    Gen->HandleTranslationUnit(C);
    if (TimerIsEnabled && !--LLVMIRGenerationRefCount)
      LLVMIRGeneration.yieldTo(CI.getFrontendTimer());
  }

  if (!MultiTargetCodeGenDumpDir.empty()) {
    // A plain HIP `-c` compile drives two separate cc1 jobs (device, then
    // host), each with its own primary/aux pair; tag filenames by this job's
    // own primary triple so the two jobs' dumps don't collide.
    if (llvm::Module *M = Gen->GetModule()) {
      std::string JobTag = ("." + M->getTargetTriple().str());
      dumpModuleForDiff(MultiTargetCodeGenDumpDir, InFile,
                        JobTag + ".primary.ll", M);
      for (AuxGenEntry &Entry : AuxGens)
        if (llvm::Module *AuxM = Entry.Gen->GetModule())
          dumpModuleForDiff(MultiTargetCodeGenDumpDir, InFile,
                            JobTag + ".aux" + std::to_string(Entry.Variant) +
                                ".ll",
                            AuxM);
    }
  }

  // Silently ignore if we weren't initialized for some reason.
  if (!getModule())
    return;

  LLVMContext &Ctx = getModule()->getContext();
  std::unique_ptr<DiagnosticHandler> OldDiagnosticHandler =
    Ctx.getDiagnosticHandler();
  llvm::scope_exit RestoreDiagnosticHandler(
      [&]() { Ctx.setDiagnosticHandler(std::move(OldDiagnosticHandler)); });
  Ctx.setDiagnosticHandler(std::make_unique<ClangDiagnosticHandler>(
      CodeGenOpts, this));

  Ctx.setDefaultTargetCPU(TargetOpts.CPU);
  Ctx.setDefaultTargetFeatures(llvm::join(TargetOpts.Features, ","));

  Expected<LLVMRemarkFileHandle> OptRecordFileOrErr =
      setupLLVMOptimizationRemarks(
          Ctx, CodeGenOpts.OptRecordFile, CodeGenOpts.OptRecordPasses,
          CodeGenOpts.OptRecordFormat, CodeGenOpts.DiagnosticsWithHotness,
          CodeGenOpts.DiagnosticsHotnessThreshold);

  if (Error E = OptRecordFileOrErr.takeError()) {
    reportOptRecordError(std::move(E), Diags, CodeGenOpts);
    return;
  }

  LLVMRemarkFileHandle OptRecordFile = std::move(*OptRecordFileOrErr);

  if (OptRecordFile && CodeGenOpts.getProfileUse() !=
                           llvm::driver::ProfileInstrKind::ProfileNone)
    Ctx.setDiagnosticsHotnessRequested(true);

  if (CodeGenOpts.MisExpect) {
    Ctx.setMisExpectWarningRequested(true);
  }

  if (CodeGenOpts.DiagnosticsMisExpectTolerance) {
    Ctx.setDiagnosticsMisExpectTolerance(
      CodeGenOpts.DiagnosticsMisExpectTolerance);
  }

  // Link each LinkModule into our module.
  if (!CodeGenOpts.LinkBitcodePostopt && LinkInModules(getModule()))
    return;

  for (auto &F : getModule()->functions()) {
    if (const Decl *FD = Gen->GetDeclForMangledName(F.getName())) {
      auto Loc = FD->getASTContext().getFullLoc(FD->getLocation());
      // TODO: use a fast content hash when available.
      auto NameHash = llvm::hash_value(F.getName());
      ManglingFullSourceLocs.push_back(std::make_pair(NameHash, Loc));
    }
  }

  if (CodeGenOpts.ClearASTBeforeBackend) {
    LLVM_DEBUG(llvm::dbgs() << "Clearing AST...\n");
    // Access to the AST is no longer available after this.
    // Other things that the ASTContext manages are still available, e.g.
    // the SourceManager. It'd be nice if we could separate out all the
    // things in ASTContext used after this point and null out the
    // ASTContext, but too many various parts of the ASTContext are still
    // used in various parts.
    C.cleanup();
    C.getAllocator().Reset();
  }

  EmbedBitcode(getModule(), CodeGenOpts, llvm::MemoryBufferRef());

  emitBackendOutput(CI, CI.getCodeGenOpts(),
                    C.getTargetInfo().getDataLayoutString(), getModule(),
                    Action, FS, std::move(AsmOutStream), this);

  if (OptRecordFile)
    OptRecordFile->keep();
}

void BackendConsumer::HandleTagDeclDefinition(TagDecl *D) {
  PrettyStackTraceDecl CrashInfo(D, SourceLocation(),
                                 Context->getSourceManager(),
                                 "LLVM IR generation of declaration");
  Gen->HandleTagDeclDefinition(D);
  dispatchToAuxGens([&](CodeGenerator &G) { G.HandleTagDeclDefinition(D); });
}

void BackendConsumer::HandleTagDeclRequiredDefinition(const TagDecl *D) {
  Gen->HandleTagDeclRequiredDefinition(D);
  dispatchToAuxGens(
      [&](CodeGenerator &G) { G.HandleTagDeclRequiredDefinition(D); });
}

void BackendConsumer::CompleteTentativeDefinition(VarDecl *D) {
  Gen->CompleteTentativeDefinition(D);
  dispatchToAuxGens([&](CodeGenerator &G) { G.CompleteTentativeDefinition(D); });
}

void BackendConsumer::CompleteExternalDeclaration(DeclaratorDecl *D) {
  Gen->CompleteExternalDeclaration(D);
  dispatchToAuxGens(
      [&](CodeGenerator &G) { G.CompleteExternalDeclaration(D); });
}

void BackendConsumer::AssignInheritanceModel(CXXRecordDecl *RD) {
  Gen->AssignInheritanceModel(RD);
  dispatchToAuxGens([&](CodeGenerator &G) { G.AssignInheritanceModel(RD); });
}

void BackendConsumer::HandleVTable(CXXRecordDecl *RD) {
  Gen->HandleVTable(RD);
  dispatchToAuxGens([&](CodeGenerator &G) { G.HandleVTable(RD); });
}

void BackendConsumer::anchor() { }

} // namespace clang

bool ClangDiagnosticHandler::handleDiagnostics(const DiagnosticInfo &DI) {
  BackendCon->DiagnosticHandlerImpl(DI);
  return true;
}

/// ConvertBackendLocation - Convert a location in a temporary llvm::SourceMgr
/// buffer to be a valid FullSourceLoc.
static FullSourceLoc ConvertBackendLocation(const llvm::SMDiagnostic &D,
                                            SourceManager &CSM) {
  // Get both the clang and llvm source managers.  The location is relative to
  // a memory buffer that the LLVM Source Manager is handling, we need to add
  // a copy to the Clang source manager.
  const llvm::SourceMgr &LSM = *D.getSourceMgr();

  // We need to copy the underlying LLVM memory buffer because llvm::SourceMgr
  // already owns its one and clang::SourceManager wants to own its one.
  const MemoryBuffer *LBuf =
  LSM.getMemoryBuffer(LSM.FindBufferContainingLoc(D.getLoc()));

  // Create the copy and transfer ownership to clang::SourceManager.
  // TODO: Avoid copying files into memory.
  std::unique_ptr<llvm::MemoryBuffer> CBuf =
      llvm::MemoryBuffer::getMemBufferCopy(LBuf->getBuffer(),
                                           LBuf->getBufferIdentifier());
  // FIXME: Keep a file ID map instead of creating new IDs for each location.
  FileID FID = CSM.createFileID(std::move(CBuf));

  // Translate the offset into the file.
  unsigned Offset = D.getLoc().getPointer() - LBuf->getBufferStart();
  SourceLocation NewLoc =
  CSM.getLocForStartOfFile(FID).getLocWithOffset(Offset);
  return FullSourceLoc(NewLoc, CSM);
}

#define ComputeDiagID(Severity, GroupName, DiagID)                             \
  do {                                                                         \
    switch (Severity) {                                                        \
    case llvm::DS_Error:                                                       \
      DiagID = diag::err_fe_##GroupName;                                       \
      break;                                                                   \
    case llvm::DS_Warning:                                                     \
      DiagID = diag::warn_fe_##GroupName;                                      \
      break;                                                                   \
    case llvm::DS_Remark:                                                      \
      llvm_unreachable("'remark' severity not expected");                      \
      break;                                                                   \
    case llvm::DS_Note:                                                        \
      DiagID = diag::note_fe_##GroupName;                                      \
      break;                                                                   \
    }                                                                          \
  } while (false)

#define ComputeDiagRemarkID(Severity, GroupName, DiagID)                       \
  do {                                                                         \
    switch (Severity) {                                                        \
    case llvm::DS_Error:                                                       \
      DiagID = diag::err_fe_##GroupName;                                       \
      break;                                                                   \
    case llvm::DS_Warning:                                                     \
      DiagID = diag::warn_fe_##GroupName;                                      \
      break;                                                                   \
    case llvm::DS_Remark:                                                      \
      DiagID = diag::remark_fe_##GroupName;                                    \
      break;                                                                   \
    case llvm::DS_Note:                                                        \
      DiagID = diag::note_fe_##GroupName;                                      \
      break;                                                                   \
    }                                                                          \
  } while (false)

void BackendConsumer::SrcMgrDiagHandler(const llvm::DiagnosticInfoSrcMgr &DI) {
  const llvm::SMDiagnostic &D = DI.getSMDiag();

  unsigned DiagID;
  if (DI.isInlineAsmDiag())
    ComputeDiagID(DI.getSeverity(), inline_asm, DiagID);
  else
    ComputeDiagID(DI.getSeverity(), source_mgr, DiagID);

  // This is for the empty BackendConsumer that uses the clang diagnostic
  // handler for IR input files.
  if (!Context) {
    D.print(nullptr, llvm::errs());
    Diags.Report(DiagID).AddString("cannot compile inline asm");
    return;
  }

  // There are a couple of different kinds of errors we could get here.
  // First, we re-format the SMDiagnostic in terms of a clang diagnostic.

  // Strip "error: " off the start of the message string.
  StringRef Message = D.getMessage();
  (void)Message.consume_front("error: ");

  // If the SMDiagnostic has an inline asm source location, translate it.
  FullSourceLoc Loc;
  if (D.getLoc() != SMLoc())
    Loc = ConvertBackendLocation(D, Context->getSourceManager());

  // If this problem has clang-level source location information, report the
  // issue in the source with a note showing the instantiated
  // code.
  if (DI.isInlineAsmDiag()) {
    SourceLocation LocCookie =
        SourceLocation::getFromRawEncoding(DI.getLocCookie());
    if (LocCookie.isValid()) {
      Diags.Report(LocCookie, DiagID).AddString(Message);

      if (D.getLoc().isValid()) {
        DiagnosticBuilder B = Diags.Report(Loc, diag::note_fe_inline_asm_here);
        // Convert the SMDiagnostic ranges into SourceRange and attach them
        // to the diagnostic.
        for (const std::pair<unsigned, unsigned> &Range : D.getRanges()) {
          unsigned Column = D.getColumnNo();
          B << SourceRange(Loc.getLocWithOffset(Range.first - Column),
                           Loc.getLocWithOffset(Range.second - Column));
        }
      }
      return;
    }
  }

  // Otherwise, report the backend issue as occurring in the generated .s file.
  // If Loc is invalid, we still need to report the issue, it just gets no
  // location info.
  Diags.Report(Loc, DiagID).AddString(Message);
}

bool
BackendConsumer::InlineAsmDiagHandler(const llvm::DiagnosticInfoInlineAsm &D) {
  unsigned DiagID;
  ComputeDiagID(D.getSeverity(), inline_asm, DiagID);
  std::string Message = D.getMsgStr().str();

  // If this problem has clang-level source location information, report the
  // issue as being a problem in the source with a note showing the instantiated
  // code.
  SourceLocation LocCookie =
      SourceLocation::getFromRawEncoding(D.getLocCookie());
  if (LocCookie.isValid())
    Diags.Report(LocCookie, DiagID).AddString(Message);
  else {
    // Otherwise, report the backend diagnostic as occurring in the generated
    // .s file.
    // If Loc is invalid, we still need to report the diagnostic, it just gets
    // no location info.
    FullSourceLoc Loc;
    Diags.Report(Loc, DiagID).AddString(Message);
  }
  // We handled all the possible severities.
  return true;
}

bool
BackendConsumer::StackSizeDiagHandler(const llvm::DiagnosticInfoStackSize &D) {
  if (D.getSeverity() != llvm::DS_Warning)
    // For now, the only support we have for StackSize diagnostic is warning.
    // We do not know how to format other severities.
    return false;

  auto Loc = getFunctionSourceLocation(D.getFunction());
  if (!Loc)
    return false;

  Diags.Report(*Loc, diag::warn_fe_frame_larger_than)
      << D.getStackSize() << D.getStackLimit()
      << llvm::demangle(D.getFunction().getName());
  return true;
}

bool BackendConsumer::ResourceLimitDiagHandler(
    const llvm::DiagnosticInfoResourceLimit &D) {
  auto Loc = getFunctionSourceLocation(D.getFunction());
  if (!Loc)
    return false;
  unsigned DiagID = diag::err_fe_backend_resource_limit;
  ComputeDiagID(D.getSeverity(), backend_resource_limit, DiagID);

  Diags.Report(*Loc, DiagID)
      << D.getResourceName() << D.getResourceSize() << D.getResourceLimit()
      << llvm::demangle(D.getFunction().getName());
  return true;
}

const FullSourceLoc BackendConsumer::getBestLocationFromDebugLoc(
    const llvm::DiagnosticInfoWithLocationBase &D, bool &BadDebugInfo,
    StringRef &Filename, unsigned &Line, unsigned &Column) const {
  SourceManager &SourceMgr = Context->getSourceManager();
  FileManager &FileMgr = SourceMgr.getFileManager();
  SourceLocation DILoc;

  if (D.isLocationAvailable()) {
    D.getLocation(Filename, Line, Column);
    if (Line > 0) {
      auto FE = FileMgr.getOptionalFileRef(Filename);
      if (!FE)
        FE = FileMgr.getOptionalFileRef(D.getAbsolutePath());
      if (FE) {
        // If -gcolumn-info was not used, Column will be 0. This upsets the
        // source manager, so pass 1 if Column is not set.
        DILoc = SourceMgr.translateFileLineCol(*FE, Line, Column ? Column : 1);
      }
    }
    BadDebugInfo = DILoc.isInvalid();
  }

  // If a location isn't available, try to approximate it using the associated
  // function definition. We use the definition's right brace to differentiate
  // from diagnostics that genuinely relate to the function itself.
  FullSourceLoc Loc(DILoc, SourceMgr);
  if (Loc.isInvalid()) {
    if (auto MaybeLoc = getFunctionSourceLocation(D.getFunction()))
      Loc = *MaybeLoc;
  }

  if (DILoc.isInvalid() && D.isLocationAvailable())
    // If we were not able to translate the file:line:col information
    // back to a SourceLocation, at least emit a note stating that
    // we could not translate this location. This can happen in the
    // case of #line directives.
    Diags.Report(Loc, diag::note_fe_backend_invalid_loc)
        << Filename << Line << Column;

  return Loc;
}

std::optional<FullSourceLoc>
BackendConsumer::getFunctionSourceLocation(const Function &F) const {
  auto Hash = llvm::hash_value(F.getName());
  for (const auto &Pair : ManglingFullSourceLocs) {
    if (Pair.first == Hash)
      return Pair.second;
  }
  return std::nullopt;
}

void BackendConsumer::UnsupportedDiagHandler(
    const llvm::DiagnosticInfoUnsupported &D) {
  // We only support warnings or errors.
  assert(D.getSeverity() == llvm::DS_Error ||
         D.getSeverity() == llvm::DS_Warning);

  StringRef Filename;
  unsigned Line, Column;
  bool BadDebugInfo = false;
  FullSourceLoc Loc;
  std::string Msg;
  raw_string_ostream MsgStream(Msg);

  // Context will be nullptr for IR input files, we will construct the diag
  // message from llvm::DiagnosticInfoUnsupported.
  if (Context != nullptr) {
    Loc = getBestLocationFromDebugLoc(D, BadDebugInfo, Filename, Line, Column);
    MsgStream << D.getMessage();
  } else {
    DiagnosticPrinterRawOStream DP(MsgStream);
    D.print(DP);
  }

  auto DiagType = D.getSeverity() == llvm::DS_Error
                      ? diag::err_fe_backend_unsupported
                      : diag::warn_fe_backend_unsupported;
  Diags.Report(Loc, DiagType) << Msg;

  if (BadDebugInfo)
    // If we were not able to translate the file:line:col information
    // back to a SourceLocation, at least emit a note stating that
    // we could not translate this location. This can happen in the
    // case of #line directives.
    Diags.Report(Loc, diag::note_fe_backend_invalid_loc)
        << Filename << Line << Column;
}

void BackendConsumer::UnsupportedTargetIntrinsicDiagHandler(
    const llvm::DiagnosticInfoUnsupportedTargetIntrinsic &D) {
  assert(D.getSeverity() == llvm::DS_Error &&
         "unsupported target intrinsic diagnostic should be an error");

  StringRef Filename;
  unsigned Line, Column;
  bool BadDebugInfo = false;
  FullSourceLoc Loc;
  std::string Msg;
  raw_string_ostream MsgStream(Msg);

  // Context will be nullptr for IR input files, so construct the diagnostic
  // message from llvm::DiagnosticInfoUnsupportedTargetIntrinsic.
  if (Context != nullptr) {
    Loc = getBestLocationFromDebugLoc(D, BadDebugInfo, Filename, Line, Column);
    MsgStream << D.getMessage();
  } else {
    DiagnosticPrinterRawOStream DP(MsgStream);
    D.print(DP);
  }

  Diags.Report(Loc, diag::err_fe_backend_unsupported) << Msg;

  if (BadDebugInfo) {
    // If we were not able to translate the file:line:col information
    // back to a SourceLocation, at least emit a note stating that
    // we could not translate this location. This can happen in the
    // case of #line directives.
    Diags.Report(Loc, diag::note_fe_backend_invalid_loc)
        << Filename << Line << Column;
  }
}

void BackendConsumer::EmitOptimizationMessage(
    const llvm::DiagnosticInfoOptimizationBase &D, unsigned DiagID) {
  // We only support warnings and remarks.
  assert(D.getSeverity() == llvm::DS_Remark ||
         D.getSeverity() == llvm::DS_Warning);

  StringRef Filename;
  unsigned Line, Column;
  bool BadDebugInfo = false;
  FullSourceLoc Loc;
  std::string Msg;
  raw_string_ostream MsgStream(Msg);

  // Context will be nullptr for IR input files, we will construct the remark
  // message from llvm::DiagnosticInfoOptimizationBase.
  if (Context != nullptr) {
    Loc = getBestLocationFromDebugLoc(D, BadDebugInfo, Filename, Line, Column);
    MsgStream << D.getMsg();
  } else {
    DiagnosticPrinterRawOStream DP(MsgStream);
    D.print(DP);
  }

  if (D.getHotness())
    MsgStream << " (hotness: " << *D.getHotness() << ")";

  Diags.Report(Loc, DiagID) << AddFlagValue(D.getPassName()) << Msg;

  if (BadDebugInfo)
    // If we were not able to translate the file:line:col information
    // back to a SourceLocation, at least emit a note stating that
    // we could not translate this location. This can happen in the
    // case of #line directives.
    Diags.Report(Loc, diag::note_fe_backend_invalid_loc)
        << Filename << Line << Column;
}

void BackendConsumer::OptimizationRemarkHandler(
    const llvm::DiagnosticInfoOptimizationBase &D) {
  // Without hotness information, don't show noisy remarks.
  if (D.isVerbose() && !D.getHotness())
    return;

  if (D.isPassed()) {
    // Optimization remarks are active only if the -Rpass flag has a regular
    // expression that matches the name of the pass name in \p D.
    if (CodeGenOpts.OptimizationRemark.patternMatches(D.getPassName()))
      EmitOptimizationMessage(D, diag::remark_fe_backend_optimization_remark);
  } else if (D.isMissed()) {
    // Missed optimization remarks are active only if the -Rpass-missed
    // flag has a regular expression that matches the name of the pass
    // name in \p D.
    if (CodeGenOpts.OptimizationRemarkMissed.patternMatches(D.getPassName()))
      EmitOptimizationMessage(
          D, diag::remark_fe_backend_optimization_remark_missed);
  } else {
    assert(D.isAnalysis() && "Unknown remark type");

    bool ShouldAlwaysPrint = false;
    if (auto *ORA = dyn_cast<llvm::OptimizationRemarkAnalysis>(&D))
      ShouldAlwaysPrint = ORA->shouldAlwaysPrint();

    if (ShouldAlwaysPrint ||
        CodeGenOpts.OptimizationRemarkAnalysis.patternMatches(D.getPassName()))
      EmitOptimizationMessage(
          D, diag::remark_fe_backend_optimization_remark_analysis);
  }
}

void BackendConsumer::OptimizationRemarkHandler(
    const llvm::OptimizationRemarkAnalysisFPCommute &D) {
  // Optimization analysis remarks are active if the pass name is set to
  // llvm::DiagnosticInfo::AlwasyPrint or if the -Rpass-analysis flag has a
  // regular expression that matches the name of the pass name in \p D.

  if (D.shouldAlwaysPrint() ||
      CodeGenOpts.OptimizationRemarkAnalysis.patternMatches(D.getPassName()))
    EmitOptimizationMessage(
        D, diag::remark_fe_backend_optimization_remark_analysis_fpcommute);
}

void BackendConsumer::OptimizationRemarkHandler(
    const llvm::OptimizationRemarkAnalysisAliasing &D) {
  // Optimization analysis remarks are active if the pass name is set to
  // llvm::DiagnosticInfo::AlwasyPrint or if the -Rpass-analysis flag has a
  // regular expression that matches the name of the pass name in \p D.

  if (D.shouldAlwaysPrint() ||
      CodeGenOpts.OptimizationRemarkAnalysis.patternMatches(D.getPassName()))
    EmitOptimizationMessage(
        D, diag::remark_fe_backend_optimization_remark_analysis_aliasing);
}

void BackendConsumer::OptimizationFailureHandler(
    const llvm::DiagnosticInfoOptimizationFailure &D) {
  EmitOptimizationMessage(D, diag::warn_fe_backend_optimization_failure);
}

void BackendConsumer::DontCallDiagHandler(const DiagnosticInfoDontCall &D) {
  SourceLocation LocCookie =
      SourceLocation::getFromRawEncoding(D.getLocCookie());

  // FIXME: we can't yet diagnose indirect calls. When/if we can, we
  // should instead assert that LocCookie.isValid().
  if (!LocCookie.isValid())
    return;

  Diags.Report(LocCookie, D.getSeverity() == DiagnosticSeverity::DS_Error
                              ? diag::err_fe_backend_error_attr
                              : diag::warn_fe_backend_warning_attr)
      << llvm::demangle(D.getFunctionName()) << D.getNote();

  if (!CodeGenOpts.ShowInliningChain)
    return;

  auto EmitNote = [&](SourceLocation Loc, StringRef FuncName, bool IsFirst) {
    if (!Loc.isValid())
      Loc = LocCookie;
    unsigned DiagID =
        IsFirst ? diag::note_fe_backend_in : diag::note_fe_backend_inlined;
    Diags.Report(Loc, DiagID) << llvm::demangle(FuncName.str());
  };

  // Try debug info first for accurate source locations.
  if (!D.getDebugInlineChain().empty()) {
    SourceManager &SM = Context->getSourceManager();
    FileManager &FM = SM.getFileManager();
    for (const auto &[I, Info] : llvm::enumerate(D.getDebugInlineChain())) {
      SourceLocation Loc;
      if (Info.Line > 0)
        if (auto FE = FM.getOptionalFileRef(Info.Filename))
          Loc = SM.translateFileLineCol(*FE, Info.Line,
                                        Info.Column ? Info.Column : 1);
      EmitNote(Loc, Info.FuncName, I == 0);
    }
    return;
  }

  // Fall back to heuristic (srcloc metadata) when debug info is unavailable.
  auto InliningDecisions = D.getInliningDecisions();
  if (InliningDecisions.empty())
    return;

  for (const auto &[I, Entry] : llvm::enumerate(InliningDecisions)) {
    SourceLocation Loc =
        I == 0 ? LocCookie : SourceLocation::getFromRawEncoding(Entry.second);
    EmitNote(Loc, Entry.first, I == 0);
  }

  // Suggest enabling debug info (at least -gline-directives-only) for more
  // accurate locations.
  Diags.Report(LocCookie, diag::note_fe_backend_inlining_debug_info);
}

void BackendConsumer::MisExpectDiagHandler(
    const llvm::DiagnosticInfoMisExpect &D) {
  StringRef Filename;
  unsigned Line, Column;
  bool BadDebugInfo = false;
  FullSourceLoc Loc =
      getBestLocationFromDebugLoc(D, BadDebugInfo, Filename, Line, Column);

  Diags.Report(Loc, diag::warn_profile_data_misexpect) << D.getMsg().str();

  if (BadDebugInfo)
    // If we were not able to translate the file:line:col information
    // back to a SourceLocation, at least emit a note stating that
    // we could not translate this location. This can happen in the
    // case of #line directives.
    Diags.Report(Loc, diag::note_fe_backend_invalid_loc)
        << Filename << Line << Column;
}

/// This function is invoked when the backend needs
/// to report something to the user.
void BackendConsumer::DiagnosticHandlerImpl(const DiagnosticInfo &DI) {
  unsigned DiagID = diag::err_fe_inline_asm;
  llvm::DiagnosticSeverity Severity = DI.getSeverity();
  // Get the diagnostic ID based.
  switch (DI.getKind()) {
  case llvm::DK_InlineAsm:
    if (InlineAsmDiagHandler(cast<DiagnosticInfoInlineAsm>(DI)))
      return;
    ComputeDiagID(Severity, inline_asm, DiagID);
    break;
  case llvm::DK_SrcMgr:
    SrcMgrDiagHandler(cast<DiagnosticInfoSrcMgr>(DI));
    return;
  case llvm::DK_StackSize:
    if (StackSizeDiagHandler(cast<DiagnosticInfoStackSize>(DI)))
      return;
    ComputeDiagID(Severity, backend_frame_larger_than, DiagID);
    break;
  case llvm::DK_ResourceLimit:
    if (ResourceLimitDiagHandler(cast<DiagnosticInfoResourceLimit>(DI)))
      return;
    ComputeDiagID(Severity, backend_resource_limit, DiagID);
    break;
  case DK_Linker:
    ComputeDiagID(Severity, linking_module, DiagID);
    break;
  case llvm::DK_OptimizationRemark:
    // Optimization remarks are always handled completely by this
    // handler. There is no generic way of emitting them.
    OptimizationRemarkHandler(cast<OptimizationRemark>(DI));
    return;
  case llvm::DK_OptimizationRemarkMissed:
    // Optimization remarks are always handled completely by this
    // handler. There is no generic way of emitting them.
    OptimizationRemarkHandler(cast<OptimizationRemarkMissed>(DI));
    return;
  case llvm::DK_OptimizationRemarkAnalysis:
    // Optimization remarks are always handled completely by this
    // handler. There is no generic way of emitting them.
    OptimizationRemarkHandler(cast<OptimizationRemarkAnalysis>(DI));
    return;
  case llvm::DK_OptimizationRemarkAnalysisFPCommute:
    // Optimization remarks are always handled completely by this
    // handler. There is no generic way of emitting them.
    OptimizationRemarkHandler(cast<OptimizationRemarkAnalysisFPCommute>(DI));
    return;
  case llvm::DK_OptimizationRemarkAnalysisAliasing:
    // Optimization remarks are always handled completely by this
    // handler. There is no generic way of emitting them.
    OptimizationRemarkHandler(cast<OptimizationRemarkAnalysisAliasing>(DI));
    return;
  case llvm::DK_MachineOptimizationRemark:
    // Optimization remarks are always handled completely by this
    // handler. There is no generic way of emitting them.
    OptimizationRemarkHandler(cast<MachineOptimizationRemark>(DI));
    return;
  case llvm::DK_MachineOptimizationRemarkMissed:
    // Optimization remarks are always handled completely by this
    // handler. There is no generic way of emitting them.
    OptimizationRemarkHandler(cast<MachineOptimizationRemarkMissed>(DI));
    return;
  case llvm::DK_MachineOptimizationRemarkAnalysis:
    // Optimization remarks are always handled completely by this
    // handler. There is no generic way of emitting them.
    OptimizationRemarkHandler(cast<MachineOptimizationRemarkAnalysis>(DI));
    return;
  case llvm::DK_OptimizationFailure:
    // Optimization failures are always handled completely by this
    // handler.
    OptimizationFailureHandler(cast<DiagnosticInfoOptimizationFailure>(DI));
    return;
  case llvm::DK_Unsupported:
    UnsupportedDiagHandler(cast<DiagnosticInfoUnsupported>(DI));
    return;
  case llvm::DK_UnsupportedTargetIntrinsic:
    UnsupportedTargetIntrinsicDiagHandler(
        cast<DiagnosticInfoUnsupportedTargetIntrinsic>(DI));
    return;
  case llvm::DK_DontCall:
    DontCallDiagHandler(cast<DiagnosticInfoDontCall>(DI));
    return;
  case llvm::DK_MisExpect:
    MisExpectDiagHandler(cast<DiagnosticInfoMisExpect>(DI));
    return;
  default:
    // Plugin IDs are not bound to any value as they are set dynamically.
    ComputeDiagRemarkID(Severity, backend_plugin, DiagID);
    break;
  }
  std::string MsgStorage;
  {
    raw_string_ostream Stream(MsgStorage);
    DiagnosticPrinterRawOStream DP(Stream);
    DI.print(DP);
  }

  if (DI.getKind() == DK_Linker) {
    assert(CurLinkModule && "CurLinkModule must be set for linker diagnostics");
    Diags.Report(DiagID) << CurLinkModule->getModuleIdentifier() << MsgStorage;
    return;
  }

  // Report the backend message using the usual diagnostic mechanism.
  FullSourceLoc Loc;
  Diags.Report(Loc, DiagID).AddString(MsgStorage);
}
#undef ComputeDiagID

CodeGenAction::CodeGenAction(unsigned _Act, LLVMContext *_VMContext)
    : Act(_Act), VMContext(_VMContext ? _VMContext : new LLVMContext),
      OwnsVMContext(!_VMContext) {}

CodeGenAction::~CodeGenAction() {
  TheModule.reset();
  if (OwnsVMContext)
    delete VMContext;
}

bool CodeGenAction::hasIRSupport() const { return true; }

void CodeGenAction::EndSourceFileAction() {
  ASTFrontendAction::EndSourceFileAction();

  // If the consumer creation failed, do nothing.
  if (!getCompilerInstance().hasASTConsumer())
    return;

  // Steal the module from the consumer.
  TheModule = BEConsumer->takeModule();
}

std::unique_ptr<llvm::Module> CodeGenAction::takeModule() {
  return std::move(TheModule);
}

llvm::LLVMContext *CodeGenAction::takeLLVMContext() {
  OwnsVMContext = false;
  return VMContext;
}

CodeGenerator *CodeGenAction::getCodeGenerator() const {
  return BEConsumer->getCodeGenerator();
}

bool CodeGenAction::BeginSourceFileAction(CompilerInstance &CI) {
  if (CI.getFrontendOpts().GenReducedBMI)
    CI.getLangOpts().setCompilingModule(LangOptions::CMK_ModuleInterface);
  return ASTFrontendAction::BeginSourceFileAction(CI);
}

static std::unique_ptr<raw_pwrite_stream>
GetOutputStream(CompilerInstance &CI, StringRef InFile, BackendAction Action) {
  switch (Action) {
  case Backend_EmitAssembly:
    return CI.createDefaultOutputFile(false, InFile, "s");
  case Backend_EmitLL:
    return CI.createDefaultOutputFile(false, InFile, "ll");
  case Backend_EmitBC:
    return CI.createDefaultOutputFile(true, InFile, "bc");
  case Backend_EmitNothing:
    return nullptr;
  case Backend_EmitMCNull:
    return CI.createNullOutputFile();
  case Backend_EmitObj:
    return CI.createDefaultOutputFile(true, InFile, "o");
  }

  llvm_unreachable("Invalid action!");
}

std::unique_ptr<ASTConsumer>
CodeGenAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  BackendAction BA = static_cast<BackendAction>(Act);
  std::unique_ptr<raw_pwrite_stream> OS = CI.takeOutputStream();
  if (!OS)
    OS = GetOutputStream(CI, InFile, BA);

  if (BA != Backend_EmitNothing && !OS)
    return nullptr;

  // Load bitcode modules to link with, if we need to.
  if (clang::loadLinkModules(CI, *VMContext, LinkModules))
    return nullptr;

  CoverageSourceInfo *CoverageInfo = nullptr;
  // Add the preprocessor callback only when the coverage mapping is generated.
  if (CI.getCodeGenOpts().CoverageMapping)
    CoverageInfo = CodeGen::CoverageMappingModuleGen::setUpCoverageCallbacks(
        CI.getPreprocessor());

  std::unique_ptr<BackendConsumer> Result(new BackendConsumer(
      CI, BA, CI.getVirtualFileSystemPtr(), *VMContext, std::move(LinkModules),
      InFile, std::move(OS), CoverageInfo));
  BEConsumer = Result.get();

  // Enable generating macro debug info only when debug info is not disabled and
  // also macro debug info is enabled.
  if (CI.getCodeGenOpts().getDebugInfo() != codegenoptions::NoDebugInfo &&
      CI.getCodeGenOpts().MacroDebugInfo) {
    std::unique_ptr<PPCallbacks> Callbacks =
        std::make_unique<MacroPPCallbacks>(BEConsumer->getCodeGenerator(),
                                            CI.getPreprocessor());
    CI.getPreprocessor().addPPCallbacks(std::move(Callbacks));
  }

  if (CI.getFrontendOpts().GenReducedBMI &&
      !CI.getFrontendOpts().ModuleOutputPath.empty()) {
    std::vector<std::unique_ptr<ASTConsumer>> Consumers(2);
    Consumers[0] = std::make_unique<ReducedBMIGenerator>(
        CI.getPreprocessor(), CI.getModuleCache(),
        CI.getFrontendOpts().ModuleOutputPath, CI.getCodeGenOpts());
    Consumers[1] = std::move(Result);
    return std::make_unique<MultiplexConsumer>(std::move(Consumers));
  }

  return std::move(Result);
}

std::unique_ptr<llvm::Module>
CodeGenAction::loadModule(MemoryBufferRef MBRef) {
  CompilerInstance &CI = getCompilerInstance();
  SourceManager &SM = CI.getSourceManager();

  auto DiagErrors = [&](Error E) -> std::unique_ptr<llvm::Module> {
    unsigned DiagID =
        CI.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, "%0");
    handleAllErrors(std::move(E), [&](ErrorInfoBase &EIB) {
      CI.getDiagnostics().Report(DiagID) << EIB.message();
    });
    return {};
  };

  // For ThinLTO backend invocations, ensure that the context
  // merges types based on ODR identifiers. We also need to read
  // the correct module out of a multi-module bitcode file.
  if (!CI.getCodeGenOpts().ThinLTOIndexFile.empty()) {
    VMContext->enableDebugTypeODRUniquing();

    Expected<std::vector<BitcodeModule>> BMsOrErr = getBitcodeModuleList(MBRef);
    if (!BMsOrErr)
      return DiagErrors(BMsOrErr.takeError());
    BitcodeModule *Bm = llvm::lto::findThinLTOModule(*BMsOrErr);
    // We have nothing to do if the file contains no ThinLTO module. This is
    // possible if ThinLTO compilation was not able to split module. Content of
    // the file was already processed by indexing and will be passed to the
    // linker using merged object file.
    if (!Bm) {
      auto M = std::make_unique<llvm::Module>("empty", *VMContext);
      M->setTargetTriple(Triple(CI.getTargetOpts().Triple));
      return M;
    }
    Expected<std::unique_ptr<llvm::Module>> MOrErr =
        Bm->parseModule(*VMContext);
    if (!MOrErr)
      return DiagErrors(MOrErr.takeError());
    return std::move(*MOrErr);
  }

  // Load bitcode modules to link with, if we need to.
  if (clang::loadLinkModules(CI, *VMContext, LinkModules))
    return nullptr;

  // Handle textual IR and bitcode file with one single module.
  llvm::SMDiagnostic Err;
  if (std::unique_ptr<llvm::Module> M = parseIR(MBRef, Err, *VMContext)) {
    // For LLVM IR files, always verify the input and report the error in a way
    // that does not ask people to report an issue for it.
    std::string VerifierErr;
    raw_string_ostream VerifierErrStream(VerifierErr);
    if (llvm::verifyModule(*M, &VerifierErrStream)) {
      CI.getDiagnostics().Report(diag::err_invalid_llvm_ir) << VerifierErr;
      return {};
    }
    return M;
  }

  // If MBRef is a bitcode with multiple modules (e.g., -fsplit-lto-unit
  // output), place the extra modules (actually only one, a regular LTO module)
  // into LinkModules as if we are using -mlink-bitcode-file.
  Expected<std::vector<BitcodeModule>> BMsOrErr = getBitcodeModuleList(MBRef);
  if (BMsOrErr && BMsOrErr->size()) {
    std::unique_ptr<llvm::Module> FirstM;
    for (auto &BM : *BMsOrErr) {
      Expected<std::unique_ptr<llvm::Module>> MOrErr =
          BM.parseModule(*VMContext);
      if (!MOrErr)
        return DiagErrors(MOrErr.takeError());
      if (FirstM)
        LinkModules.push_back({std::move(*MOrErr), /*PropagateAttrs=*/false,
                               /*Internalize=*/false, /*LinkFlags=*/{}});
      else
        FirstM = std::move(*MOrErr);
    }
    if (FirstM)
      return FirstM;
  }
  // If BMsOrErr fails, consume the error and use the error message from
  // parseIR.
  consumeError(BMsOrErr.takeError());

  // Translate from the diagnostic info to the SourceManager location if
  // available.
  // TODO: Unify this with ConvertBackendLocation()
  SourceLocation Loc;
  if (Err.getLineNo() > 0) {
    assert(Err.getColumnNo() >= 0);
    Loc = SM.translateFileLineCol(SM.getFileEntryForID(SM.getMainFileID()),
                                  Err.getLineNo(), Err.getColumnNo() + 1);
  }

  // Strip off a leading diagnostic code if there is one.
  StringRef Msg = Err.getMessage();
  Msg.consume_front("error: ");

  unsigned DiagID =
      CI.getDiagnostics().getCustomDiagID(DiagnosticsEngine::Error, "%0");

  CI.getDiagnostics().Report(Loc, DiagID) << Msg;
  return {};
}

void CodeGenAction::ExecuteAction() {
  if (getCurrentFileKind().getLanguage() != Language::LLVM_IR) {
    this->ASTFrontendAction::ExecuteAction();
    return;
  }

  // If this is an IR file, we have to treat it specially.
  BackendAction BA = static_cast<BackendAction>(Act);
  CompilerInstance &CI = getCompilerInstance();
  auto &CodeGenOpts = CI.getCodeGenOpts();
  auto &Diagnostics = CI.getDiagnostics();
  std::unique_ptr<raw_pwrite_stream> OS =
      GetOutputStream(CI, getCurrentFileOrBufferName(), BA);
  if (BA != Backend_EmitNothing && !OS)
    return;

  SourceManager &SM = CI.getSourceManager();
  FileID FID = SM.getMainFileID();
  std::optional<MemoryBufferRef> MainFile = SM.getBufferOrNone(FID);
  if (!MainFile)
    return;

  TheModule = loadModule(*MainFile);
  if (!TheModule)
    return;

  const TargetOptions &TargetOpts = CI.getTargetOpts();
  if (TheModule->getTargetTriple().str() != TargetOpts.Triple) {
    Diagnostics.Report(SourceLocation(), diag::warn_fe_override_module)
        << TargetOpts.Triple;
    TheModule->setTargetTriple(Triple(TargetOpts.Triple));
  }

  EmbedObject(TheModule.get(), CodeGenOpts, CI.getVirtualFileSystem(),
              Diagnostics);
  EmbedBitcode(TheModule.get(), CodeGenOpts, *MainFile);

  LLVMContext &Ctx = TheModule->getContext();

  // Restore any diagnostic handler previously set before returning from this
  // function.
  struct RAII {
    LLVMContext &Ctx;
    std::unique_ptr<DiagnosticHandler> PrevHandler = Ctx.getDiagnosticHandler();
    ~RAII() { Ctx.setDiagnosticHandler(std::move(PrevHandler)); }
  } _{Ctx};

  // Set clang diagnostic handler. To do this we need to create a fake
  // BackendConsumer.
  BackendConsumer Result(CI, BA, CI.getVirtualFileSystemPtr(), *VMContext,
                         std::move(LinkModules), "", nullptr, nullptr,
                         TheModule.get());

  // Link in each pending link module.
  if (!CodeGenOpts.LinkBitcodePostopt && Result.LinkInModules(&*TheModule))
    return;

  // PR44896: Force DiscardValueNames as false. DiscardValueNames cannot be
  // true here because the valued names are needed for reading textual IR.
  Ctx.setDiscardValueNames(false);
  Ctx.setDiagnosticHandler(
      std::make_unique<ClangDiagnosticHandler>(CodeGenOpts, &Result));

  Ctx.setDefaultTargetCPU(TargetOpts.CPU);
  Ctx.setDefaultTargetFeatures(llvm::join(TargetOpts.Features, ","));

  Expected<LLVMRemarkFileHandle> OptRecordFileOrErr =
      setupLLVMOptimizationRemarks(
          Ctx, CodeGenOpts.OptRecordFile, CodeGenOpts.OptRecordPasses,
          CodeGenOpts.OptRecordFormat, CodeGenOpts.DiagnosticsWithHotness,
          CodeGenOpts.DiagnosticsHotnessThreshold);

  if (Error E = OptRecordFileOrErr.takeError()) {
    reportOptRecordError(std::move(E), Diagnostics, CodeGenOpts);
    return;
  }
  LLVMRemarkFileHandle OptRecordFile = std::move(*OptRecordFileOrErr);

  emitBackendOutput(CI, CI.getCodeGenOpts(),
                    CI.getTarget().getDataLayoutString(), TheModule.get(), BA,
                    CI.getFileManager().getVirtualFileSystemPtr(),
                    std::move(OS));
  if (OptRecordFile)
    OptRecordFile->keep();
}

//

void EmitAssemblyAction::anchor() { }
EmitAssemblyAction::EmitAssemblyAction(llvm::LLVMContext *_VMContext)
  : CodeGenAction(Backend_EmitAssembly, _VMContext) {}

void EmitBCAction::anchor() { }
EmitBCAction::EmitBCAction(llvm::LLVMContext *_VMContext)
  : CodeGenAction(Backend_EmitBC, _VMContext) {}

void EmitLLVMAction::anchor() { }
EmitLLVMAction::EmitLLVMAction(llvm::LLVMContext *_VMContext)
  : CodeGenAction(Backend_EmitLL, _VMContext) {}

void EmitLLVMOnlyAction::anchor() { }
EmitLLVMOnlyAction::EmitLLVMOnlyAction(llvm::LLVMContext *_VMContext)
  : CodeGenAction(Backend_EmitNothing, _VMContext) {}

void EmitCodeGenOnlyAction::anchor() { }
EmitCodeGenOnlyAction::EmitCodeGenOnlyAction(llvm::LLVMContext *_VMContext)
  : CodeGenAction(Backend_EmitMCNull, _VMContext) {}

void EmitObjAction::anchor() { }
EmitObjAction::EmitObjAction(llvm::LLVMContext *_VMContext)
  : CodeGenAction(Backend_EmitObj, _VMContext) {}
