//===-- clang-import-test.cpp - ASTImporter/ExternalASTSource testbed -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTImporter.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/ExternalASTMerger.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/CodeGen/ModuleBuilder.h"
#include "clang/Driver/Types.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Frontend/TextDiagnosticBuffer.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Parse/ParseAST.h"

#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/TargetParser/Host.h"

#include <memory>
#include <string>
#include <vector>

using namespace clang;

// --- constexpr option descriptors ---
inline constexpr llvm::clv2::OptionInfo<std::string> CITExpressionOpt{
    "expression", "Path to a file containing the expression to parse",
    llvm::clv2::Required};

inline constexpr llvm::clv2::ListOptionInfo<std::string> CITImportOpt{
    "import", "Path to a file containing declarations to import"};

inline constexpr llvm::clv2::OptionInfo<bool> CITDirectOpt{
    "direct", "Use the parsed declarations without indirection"};

inline constexpr llvm::clv2::OptionInfo<bool> CITUseOriginsOpt{
    "use-origins",
    "Use DeclContext origin information for more accurate lookups"};

inline constexpr llvm::clv2::ListOptionInfo<std::string> CITClangArgsOpt{
    "Xcc", "Argument to pass to the CompilerInvocation",
    llvm::clv2::CommaSeparated};

inline constexpr llvm::clv2::OptionInfo<std::string> CITInputOpt{
    "x", "The language to parse (default: c++)", llvm::clv2::Init{"c++"}};

inline constexpr llvm::clv2::OptionInfo<bool> CITObjCARCOpt{"objc-arc",
                                                            "Emable ObjC ARC"};

inline constexpr llvm::clv2::OptionInfo<bool> CITDumpASTOpt{
    "dump-ast", "Dump combined AST"};

inline constexpr llvm::clv2::OptionInfo<bool> CITDumpIROpt{
    "dump-ir", "Dump IR from final parse"};

inline constexpr llvm::clv2::OptionsRegistry<
    &CITExpressionOpt, &CITImportOpt, &CITDirectOpt, &CITUseOriginsOpt,
    &CITClangArgsOpt, &CITInputOpt, &CITObjCARCOpt, &CITDumpASTOpt,
    &CITDumpIROpt>
    ClangImportTestReg;

static std::string Expression;
static std::vector<std::string> Imports;
static bool Direct = false;
static bool UseOrigins = false;
static std::vector<std::string> ClangArgs;
static std::string Input = "c++";
static bool ObjCARC = false;
static bool DumpAST = false;
static bool DumpIR = false;

namespace init_convenience {
class TestDiagnosticConsumer : public DiagnosticConsumer {
private:
  std::unique_ptr<TextDiagnosticBuffer> Passthrough;
  const LangOptions *LangOpts = nullptr;

public:
  TestDiagnosticConsumer()
      : Passthrough(std::make_unique<TextDiagnosticBuffer>()) {}

  void BeginSourceFile(const LangOptions &LangOpts,
                       const Preprocessor *PP = nullptr) override {
    this->LangOpts = &LangOpts;
    return Passthrough->BeginSourceFile(LangOpts, PP);
  }

  void EndSourceFile() override {
    this->LangOpts = nullptr;
    Passthrough->EndSourceFile();
  }

  bool IncludeInDiagnosticCounts() const override {
    return Passthrough->IncludeInDiagnosticCounts();
  }

private:
  static void PrintSourceForLocation(const SourceLocation &Loc,
                                     SourceManager &SM) {
    const char *LocData = SM.getCharacterData(Loc, /*Invalid=*/nullptr);
    unsigned LocColumn =
        SM.getSpellingColumnNumber(Loc, /*Invalid=*/nullptr) - 1;
    FileID FID = SM.getFileID(Loc);
    llvm::MemoryBufferRef Buffer = SM.getBufferOrFake(FID, Loc);

    assert(LocData >= Buffer.getBufferStart() &&
           LocData < Buffer.getBufferEnd());

    const char *LineBegin = LocData - LocColumn;

    assert(LineBegin >= Buffer.getBufferStart());

    const char *LineEnd = nullptr;

    for (LineEnd = LineBegin; *LineEnd != '\n' && *LineEnd != '\r' &&
                              LineEnd < Buffer.getBufferEnd();
         ++LineEnd)
      ;

    llvm::StringRef LineString(LineBegin, LineEnd - LineBegin);

    llvm::errs() << LineString << '\n';
    llvm::errs().indent(LocColumn);
    llvm::errs() << '^';
    llvm::errs() << '\n';
  }

  void HandleDiagnostic(DiagnosticsEngine::Level DiagLevel,
                        const Diagnostic &Info) override {
    if (Info.hasSourceManager() && LangOpts) {
      SourceManager &SM = Info.getSourceManager();

      if (Info.getLocation().isValid()) {
        Info.getLocation().print(llvm::errs(), SM);
        llvm::errs() << ": ";
      }

      SmallString<16> DiagText;
      Info.FormatDiagnostic(DiagText);
      llvm::errs() << DiagText << '\n';

      if (Info.getLocation().isValid()) {
        PrintSourceForLocation(Info.getLocation(), SM);
      }

      for (const CharSourceRange &Range : Info.getRanges()) {
        bool Invalid = true;
        StringRef Ref = Lexer::getSourceText(Range, SM, *LangOpts, &Invalid);
        if (!Invalid) {
          llvm::errs() << Ref << '\n';
        }
      }
    }
    DiagnosticConsumer::HandleDiagnostic(DiagLevel, Info);
  }
};

std::unique_ptr<CompilerInstance> BuildCompilerInstance() {
  DiagnosticOptions DiagOpts;
  auto DC = std::make_unique<TestDiagnosticConsumer>();
  auto Diags = CompilerInstance::createDiagnostics(
      *llvm::vfs::getRealFileSystem(), DiagOpts, DC.get(),
      /*ShouldOwnClient=*/false);

  auto Inv = std::make_unique<CompilerInvocation>();

  std::vector<const char *> ClangArgv(ClangArgs.size());
  std::transform(ClangArgs.begin(), ClangArgs.end(), ClangArgv.begin(),
                 [](const std::string &s) -> const char * { return s.data(); });
  CompilerInvocation::CreateFromArgs(*Inv, ClangArgv, *Diags);

  {
    using namespace driver::types;
    ID Id = lookupTypeForTypeSpecifier(Input.c_str());
    assert(Id != TY_INVALID);
    if (isCXX(Id)) {
      Inv->getLangOpts().CPlusPlus = true;
      Inv->getLangOpts().CPlusPlus11 = true;
      Inv->getHeaderSearchOpts().UseLibcxx = true;
    }
    if (isObjC(Id)) {
      Inv->getLangOpts().ObjC = 1;
    }
  }
  Inv->getLangOpts().ObjCAutoRefCount = ObjCARC;

  Inv->getLangOpts().Bool = true;
  Inv->getLangOpts().WChar = true;
  Inv->getLangOpts().Blocks = true;
  Inv->getLangOpts().DebuggerSupport = true;
  Inv->getLangOpts().SpellChecking = false;
  Inv->getLangOpts().ThreadsafeStatics = false;
  Inv->getLangOpts().AccessControl = false;
  Inv->getLangOpts().DollarIdents = true;
  Inv->getLangOpts().Exceptions = true;
  Inv->getLangOpts().CXXExceptions = true;
  // Needed for testing dynamic_cast.
  Inv->getLangOpts().RTTI = true;
  Inv->getCodeGenOpts().setDebugInfo(llvm::codegenoptions::FullDebugInfo);
  Inv->getTargetOpts().Triple = llvm::sys::getDefaultTargetTriple();

  auto Ins = std::make_unique<CompilerInstance>(std::move(Inv));

  Ins->createVirtualFileSystem(llvm::vfs::getRealFileSystem(), DC.get());
  Ins->createDiagnostics(DC.release(), /*ShouldOwnClient=*/true);

  TargetInfo *TI = TargetInfo::CreateTargetInfo(
      Ins->getDiagnostics(), Ins->getInvocation().getTargetOpts());
  Ins->setTarget(TI);
  Ins->getTarget().adjust(Ins->getDiagnostics(), Ins->getLangOpts(),
                          /*AuxTarget=*/nullptr);
  Ins->createFileManager();
  Ins->createSourceManager();
  Ins->createPreprocessor(TU_Complete);

  return Ins;
}

std::unique_ptr<ASTContext>
BuildASTContext(CompilerInstance &CI, SelectorTable &ST, Builtin::Context &BC) {
  auto &PP = CI.getPreprocessor();
  auto AST =
      std::make_unique<ASTContext>(CI.getLangOpts(), CI.getSourceManager(),
                                   PP.getIdentifierTable(), ST, BC, PP.TUKind);
  AST->InitBuiltinTypes(CI.getTarget());
  return AST;
}

std::unique_ptr<CodeGenerator> BuildCodeGen(CompilerInstance &CI,
                                            llvm::LLVMContext &LLVMCtx) {
  StringRef ModuleName("$__module");
  return CreateLLVMCodeGen(CI, ModuleName, LLVMCtx);
}
} // namespace init_convenience

namespace {

/// A container for a CompilerInstance (possibly with an ExternalASTMerger
/// attached to its ASTContext).
///
/// Provides an accessor for the DeclContext origins associated with the
/// ExternalASTMerger (or an empty list of origins if no ExternalASTMerger is
/// attached).
///
/// This is the main unit of parsed source code maintained by clang-import-test.
struct CIAndOrigins {
  using OriginMap = clang::ExternalASTMerger::OriginMap;
  std::unique_ptr<CompilerInstance> CI;

  ASTContext &getASTContext() { return CI->getASTContext(); }
  FileManager &getFileManager() { return CI->getFileManager(); }
  const OriginMap &getOriginMap() {
    static const OriginMap EmptyOriginMap{};
    if (ExternalASTSource *Source = CI->getASTContext().getExternalSource())
      return static_cast<ExternalASTMerger *>(Source)->GetOrigins();
    return EmptyOriginMap;
  }
  DiagnosticConsumer &getDiagnosticClient() {
    return CI->getDiagnosticClient();
  }
  CompilerInstance &getCompilerInstance() { return *CI; }
};

void AddExternalSource(CIAndOrigins &CI,
                       llvm::MutableArrayRef<CIAndOrigins> Imports) {
  ExternalASTMerger::ImporterTarget Target(
      {CI.getASTContext(), CI.getFileManager()});
  llvm::SmallVector<ExternalASTMerger::ImporterSource, 3> Sources;
  for (CIAndOrigins &Import : Imports)
    Sources.emplace_back(Import.getASTContext(), Import.getFileManager(),
                         Import.getOriginMap());
  auto ES = std::make_unique<ExternalASTMerger>(Target, Sources);
  CI.getASTContext().setExternalSource(ES.release());
  CI.getASTContext().getTranslationUnitDecl()->setHasExternalVisibleStorage();
}

CIAndOrigins BuildIndirect(CIAndOrigins &CI) {
  CIAndOrigins IndirectCI{init_convenience::BuildCompilerInstance()};
  auto ST = std::make_unique<SelectorTable>();
  auto BC = std::make_unique<Builtin::Context>();
  std::unique_ptr<ASTContext> AST = init_convenience::BuildASTContext(
      IndirectCI.getCompilerInstance(), *ST, *BC);
  IndirectCI.getCompilerInstance().setASTContext(AST.release());
  AddExternalSource(IndirectCI, CI);
  return IndirectCI;
}

llvm::Error ParseSource(const std::string &Path, CompilerInstance &CI,
                        ASTConsumer &Consumer) {
  SourceManager &SM = CI.getSourceManager();
  auto FE = CI.getFileManager().getFileRef(Path);
  if (!FE) {
    llvm::consumeError(FE.takeError());
    return llvm::make_error<llvm::StringError>(
        llvm::Twine("No such file or directory: ", Path), std::error_code());
  }
  SM.setMainFileID(SM.createFileID(*FE, SourceLocation(), SrcMgr::C_User));
  ParseAST(CI.getPreprocessor(), &Consumer, CI.getASTContext());
  return llvm::Error::success();
}

llvm::Expected<CIAndOrigins> Parse(const std::string &Path,
                                   llvm::MutableArrayRef<CIAndOrigins> Imports,
                                   bool ShouldDumpAST, bool ShouldDumpIR) {
  CIAndOrigins CI{init_convenience::BuildCompilerInstance()};
  auto ST = std::make_unique<SelectorTable>();
  auto BC = std::make_unique<Builtin::Context>();
  std::unique_ptr<ASTContext> AST =
      init_convenience::BuildASTContext(CI.getCompilerInstance(), *ST, *BC);
  CI.getCompilerInstance().setASTContext(AST.release());
  if (Imports.size())
    AddExternalSource(CI, Imports);

  std::vector<std::unique_ptr<ASTConsumer>> ASTConsumers;

  auto LLVMCtx =
      std::make_unique<llvm::LLVMContext>(llvm::clv2::defaultOptionsContext());
  ASTConsumers.push_back(
      init_convenience::BuildCodeGen(CI.getCompilerInstance(), *LLVMCtx));
  auto &CG = *static_cast<CodeGenerator *>(ASTConsumers.back().get());

  if (ShouldDumpAST)
    ASTConsumers.push_back(CreateASTDumper(nullptr /*Dump to stdout.*/, "",
                                           true, false, false, false,
                                           clang::ADOF_Default));

  CI.getDiagnosticClient().BeginSourceFile(
      CI.getCompilerInstance().getLangOpts(),
      &CI.getCompilerInstance().getPreprocessor());
  MultiplexConsumer Consumers(std::move(ASTConsumers));
  Consumers.Initialize(CI.getASTContext());

  if (llvm::Error PE = ParseSource(Path, CI.getCompilerInstance(), Consumers))
    return std::move(PE);
  CI.getDiagnosticClient().EndSourceFile();
  if (ShouldDumpIR)
    CG.GetModule()->print(llvm::outs(), nullptr);
  if (CI.getDiagnosticClient().getNumErrors())
    return llvm::make_error<llvm::StringError>(
        "Errors occurred while parsing the expression.", std::error_code());
  return std::move(CI);
}

void Forget(CIAndOrigins &CI, llvm::MutableArrayRef<CIAndOrigins> Imports) {
  llvm::SmallVector<ExternalASTMerger::ImporterSource, 3> Sources;
  for (CIAndOrigins &Import : Imports)
    Sources.push_back({Import.getASTContext(), Import.getFileManager(),
                       Import.getOriginMap()});
  ExternalASTSource *Source = CI.CI->getASTContext().getExternalSource();
  auto *Merger = static_cast<ExternalASTMerger *>(Source);
  Merger->RemoveSources(Sources);
}

} // end namespace

int main(int argc, const char **argv) {
  const bool DisableCrashReporting = true;
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0], DisableCrashReporting);
  llvm::clv2::OptionParser P;
  P.add<&ClangImportTestReg>();
  llvm::RegisterCommonLLVMOptionsHidden(P);
  // Auto-generated showOptions for clang-import-test
  P.showOptions({
      "abort-on-max-devirt-iterations-reached",
      "arc-contract-use-objc-claim-rv",
      "atomic-counter-update-promoted",
      "atomic-first-counter",
      "basic-block-section-match-infer",
      "bounds-checking-single-trap",
      "cfg-hide-cold-paths",
      "cfg-hide-deoptimize-paths",
      "cfg-hide-unreachable-paths",
      "check-functions-filter",
      "conditional-counter-update",
      "cost-kind",
      "ir2vec-arg-weight",
      "ir2vec-kind",
      "ir2vec-opc-weight",
      "ir2vec-type-weight",
      "ir2vec-vocab-path",
      "mir2vec-common-operand-weight",
      "mir2vec-kind",
      "mir2vec-opc-weight",
      "mir2vec-print-all-vocab-entries",
      "mir2vec-reg-operand-weight",
      "mir2vec-vocab-path",
      "ctx-profile-force-is-specialized",
      "debugify-atoms",
      "debugify-func-limit",
      "debugify-level",
      "debugify-quiet",
      "devirtualize-speculatively",
      "direct",
      "disable-auto-upgrade-debug-info",
      "disable-i2p-p2i-opt",
      "do-counter-promotion",
      "dot-cfg-mssa",
      "dump-ast",
      "dump-ir",
      "elide-all-zero-branch-weights",
      "emit-bb-hash",
      "enable-devirtualize-speculatively",
      "enable-gvn-hoist",
      "enable-gvn-memdep",
      "enable-gvn-memoryssa",
      "enable-gvn-sink",
      "enable-jump-table-to-switch",
      "enable-load-in-loop-pre",
      "enable-load-pre",
      "enable-loop-simplifycfg-term-folding",
      "enable-name-compression",
      "enable-poison-reuse-guard",
      "enable-split-backedge-in-load-pre",
      "enable-split-loopiv-heuristic",
      "enable-vtable-profile-use",
      "enable-vtable-value-profiling",
      "expand-variadics-override",
      "experimental-debug-variable-locations",
      "expression",
      "force-tail-folding-style",
      "fs-profile-debug-bw-threshold",
      "fs-profile-debug-prob-diff-threshold",
      "generate-merged-base-profiles",
      "hash-based-counter-split",
      "hot-cold-split",
      "hwasan-percentile-cutoff-hot",
      "hwasan-random-rate",
      "import",
      "import-all-index",
      "instcombine-code-sinking",
      "instcombine-guard-widening-window",
      "instcombine-maxarray-size",
      "instcombine-max-num-phis",
      "instcombine-max-sink-users",
      "instcombine-negator-enabled",
      "instcombine-negator-max-depth",
      "instrprof-atomic-counter-update-all",
      "internalize-public-api-file",
      "internalize-public-api-list",
      "intrinsic-cost-strategy",
      "iterative-counter-promotion",
      "lower-allow-check-percentile-cutoff-hot",
      "lower-allow-check-random-rate",
      "lto-embed-bitcode",
      "matrix-default-layout",
      "matrix-print-after-transpose-opt",
      "max-counter-promotions",
      "max-counter-promotions-per-loop",
      "mir-strip-debugify-only",
      "misexpect-tolerance",
      "ms-secure-hotpatch-functions-file",
      "ms-secure-hotpatch-functions-list",
      "no-discriminators",
      "objc-arc",
      "object-size-offset-visitor-max-visit-instructions",
      "pgo-block-coverage",
      "pgo-temporal-instrumentation",
      "pgo-view-block-coverage-graph",
      "polly",
      "polly-2nd-level-tiling",
      "polly-annotate-metadata-vectorize",
      "polly-ast-print-accesses",
      "polly-context",
      "polly-dce-precise-steps",
      "polly-delicm-max-ops",
      "polly-detect-full-functions",
      "polly-dump-after",
      "polly-dump-after-file",
      "polly-dump-before",
      "polly-dump-before-file",
      "polly-enable-simplify",
      "polly-ignore-func",
      "polly-isl-arg",
      "polly-matmul-opt",
      "polly-on-isl-error-abort",
      "polly-only-func",
      "polly-only-region",
      "polly-only-scop-detection",
      "polly-optimized-scops",
      "polly-parallel",
      "polly-parallel-force",
      "polly-pattern-matching-based-opts",
      "polly-postopts",
      "polly-pragma-based-opts",
      "polly-pragma-ignore-depcheck",
      "polly-print-ast",
      "polly-print-delicm",
      "polly-print-deps",
      "polly-print-detect",
      "polly-print-flatten-schedule",
      "polly-print-import-jscop",
      "polly-print-mse",
      "polly-print-opt-isl",
      "polly-print-optree",
      "polly-print-scops",
      "polly-print-simplify",
      "polly-process-unprofitable",
      "polly-register-tiling",
      "polly-report",
      "polly-reschedule",
      "polly-show",
      "polly-show-only",
      "polly-stmt-granularity",
      "polly-tc-opt",
      "polly-tiling",
      "polly-vectorizer",
      "print-pipeline-passes",
      "profcheck-annotate-select",
      "profcheck-default-function-entry-count",
      "profcheck-default-select-false-weight",
      "profcheck-default-select-true-weight",
      "profcheck-weights-for-test",
      "profile-correlate",
      "propeller-infer-threshold",
      "runtime-counter-relocation",
      "safepoint-ir-verifier-print-only",
      "sampled-instr-burst-duration",
      "sampled-instr-period",
      "sampled-instrumentation",
      "sample-profile-check-record-coverage",
      "sample-profile-check-sample-coverage",
      "sample-profile-max-propagate-iterations",
      "sanitizer-early-opt-ep",
      "skip-ret-exit-block",
      "speculative-counter-promotion-max-exiting",
      "speculative-counter-promotion-to-loop",
      "summary-file",
      "thinlto-assume-merged",
      "ubsan-guard-checks",
      "use-origins",
      "verify-region-info",
      "vp-counters-per-site",
      "vp-static-alloc",
      "x",
      "polly",
      "polly-2nd-level-tiling",
      "polly-annotate-metadata-vectorize",
      "polly-ast-print-accesses",
      "polly-context",
      "polly-dce-precise-steps",
      "polly-delicm-max-ops",
      "polly-detect-full-functions",
      "polly-dump-after",
      "polly-dump-after-file",
      "polly-dump-before",
      "polly-dump-before-file",
      "polly-enable-simplify",
      "polly-ignore-func",
      "polly-isl-arg",
      "polly-matmul-opt",
      "polly-on-isl-error-abort",
      "polly-only-func",
      "polly-only-region",
      "polly-only-scop-detection",
      "polly-optimized-scops",
      "polly-parallel",
      "polly-parallel-force",
      "polly-pattern-matching-based-opts",
      "polly-postopts",
      "polly-pragma-based-opts",
      "polly-pragma-ignore-depcheck",
      "polly-print-ast",
      "polly-print-delicm",
      "polly-print-deps",
      "polly-print-detect",
      "polly-print-flatten-schedule",
      "polly-print-import-jscop",
      "polly-print-mse",
      "polly-print-opt-isl",
      "polly-print-optree",
      "polly-print-scops",
      "polly-print-simplify",
      "polly-process-unprofitable",
      "polly-register-tiling",
      "polly-report",
      "polly-reschedule",
      "polly-show",
      "polly-show-only",
      "polly-stmt-granularity",
      "polly-tc-opt",
      "polly-tiling",
      "polly-vectorizer",

      "Xcc",
  });

  auto OptsCtx = P.parse(argc, argv);
  auto *Opts = OptsCtx->getViewPtr<&ClangImportTestReg>();

  Expression = Opts->get<&CITExpressionOpt>();
  Imports = Opts->get<&CITImportOpt>();
  Direct = Opts->get<&CITDirectOpt>();
  UseOrigins = Opts->get<&CITUseOriginsOpt>();
  ClangArgs = Opts->get<&CITClangArgsOpt>();
  Input = Opts->get<&CITInputOpt>();
  if (Input.empty())
    Input = "c++";
  ObjCARC = Opts->get<&CITObjCARCOpt>();
  DumpAST = Opts->get<&CITDumpASTOpt>();
  DumpIR = Opts->get<&CITDumpIROpt>();
  std::vector<CIAndOrigins> ImportCIs;
  for (auto I : Imports) {
    llvm::Expected<CIAndOrigins> ImportCI = Parse(I, {}, false, false);
    if (auto E = ImportCI.takeError()) {
      llvm::errs() << "error: " << llvm::toString(std::move(E)) << "\n";
      exit(-1);
    }
    ImportCIs.push_back(std::move(*ImportCI));
  }
  std::vector<CIAndOrigins> IndirectCIs;
  if (!Direct || UseOrigins) {
    for (auto &ImportCI : ImportCIs) {
      CIAndOrigins IndirectCI = BuildIndirect(ImportCI);
      IndirectCIs.push_back(std::move(IndirectCI));
    }
  }
  if (UseOrigins)
    for (auto &ImportCI : ImportCIs)
      IndirectCIs.push_back(std::move(ImportCI));
  llvm::Expected<CIAndOrigins> ExpressionCI =
      Parse(Expression, (Direct && !UseOrigins) ? ImportCIs : IndirectCIs,
            DumpAST, DumpIR);
  if (auto E = ExpressionCI.takeError()) {
    llvm::errs() << "error: " << llvm::toString(std::move(E)) << "\n";
    exit(-1);
  }
  Forget(*ExpressionCI, (Direct && !UseOrigins) ? ImportCIs : IndirectCIs);
  return 0;
}
