//===--- tools/clang-check/ClangCheck.cpp - Clang check tool --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements a clang-check tool that runs clang based on the info
//  stored in a compilation database.
//
//  This tool uses the Clang Tooling infrastructure, see
//    http://clang.llvm.org/docs/HowToSetupToolingForLLVM.html
//  for details on setting it up with LLVM source tree.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Options/Options.h"
#include "clang/Rewrite/Frontend/FixItRewriter.h"
#include "clang/Rewrite/Frontend/FrontendActions.h"
#include "clang/StaticAnalyzer/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Syntax/BuildTree.h"
#include "clang/Tooling/Syntax/TokenBufferTokenManager.h"
#include "clang/Tooling/Syntax/Tokens.h"
#include "clang/Tooling/Syntax/Tree.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"

using namespace clang::tooling;
using namespace clang;
using namespace llvm;

// ExtraHelp is set via P.setExtraHelp() in configureParser below.

static cl::OptionCategory ClangCheckCategory("clang-check options");
static const opt::OptTable &Options = getDriverOptTable();

// clang-check uses CommonOptionsParser which owns the parse, so we use
// registerAsRuntime with a bridge callback.
inline constexpr clv2::OptionInfo<bool> CCASTDumpOpt{
    "ast-dump", "Build ASTs and then debug dump them"};

inline constexpr clv2::OptionInfo<bool> CCASTListOpt{
    "ast-list",
    "Build ASTs and print the list of declaration node qualified names"};

inline constexpr clv2::OptionInfo<bool> CCASTPrintOpt{
    "ast-print", "Build ASTs and then pretty-print them"};

inline constexpr clv2::OptionInfo<std::string> CCASTDumpFilterOpt{
    "ast-dump-filter",
    "Use with -ast-dump or -ast-print to dump/print only AST declaration"
    " nodes having a certain substring in a qualified name. Use"
    " -ast-list to list all filterable declaration node names."};

inline constexpr clv2::OptionInfo<bool> CCAnalyzeOpt{
    "analyze", "Run static analysis engine"};

inline constexpr clv2::OptionInfo<std::string> CCAnalyzerOutputOpt{
    "analyzer-output-path", "Write output to <file>"};

inline constexpr clv2::OptionInfo<bool> CCFixitOpt{
    "fixit", "Apply fix-it advice to the input source"};

inline constexpr clv2::OptionInfo<bool> CCFixWhatYouCanOpt{
    "fix-what-you-can",
    "Apply fix-it advice even in the presence of unfixable errors"};

inline constexpr clv2::OptionInfo<bool> CCSyntaxTreeDumpOpt{
    "syntax-tree-dump", "dump the syntax tree"};

inline constexpr clv2::OptionInfo<bool> CCTokensDumpOpt{
    "tokens-dump", "dump the preprocessed tokens"};

inline constexpr clv2::OptionsRegistry<
    &CCASTDumpOpt, &CCASTListOpt, &CCASTPrintOpt, &CCASTDumpFilterOpt,
    &CCAnalyzeOpt, &CCAnalyzerOutputOpt, &CCFixitOpt, &CCFixWhatYouCanOpt,
    &CCSyntaxTreeDumpOpt, &CCTokensDumpOpt>
    ClangCheckReg;

namespace {
struct ClangCheckOptions {
  bool ASTDump = false;
  bool ASTList = false;
  bool ASTPrint = false;
  std::string ASTDumpFilter;
  bool Analyze = false;
  std::string AnalyzerOutput;
  bool Fixit = false;
  bool FixWhatYouCan = false;
  bool SyntaxTreeDump = false;
  bool TokensDump = false;
};
} // namespace

static void
applyClangCheckOpts(const decltype(ClangCheckReg)::ParsedOptionsT &Opts,
                    ClangCheckOptions &CheckOpts) {
  CheckOpts.ASTDump = Opts.get<&CCASTDumpOpt>();
  CheckOpts.ASTList = Opts.get<&CCASTListOpt>();
  CheckOpts.ASTPrint = Opts.get<&CCASTPrintOpt>();
  CheckOpts.ASTDumpFilter = Opts.get<&CCASTDumpFilterOpt>();
  CheckOpts.Analyze = Opts.get<&CCAnalyzeOpt>();
  CheckOpts.AnalyzerOutput = Opts.get<&CCAnalyzerOutputOpt>();
  CheckOpts.Fixit = Opts.get<&CCFixitOpt>();
  CheckOpts.FixWhatYouCan = Opts.get<&CCFixWhatYouCanOpt>();
  CheckOpts.SyntaxTreeDump = Opts.get<&CCSyntaxTreeDumpOpt>();
  CheckOpts.TokensDump = Opts.get<&CCTokensDumpOpt>();
}

static void configureParser(clv2::OptionParser &P,
                            ClangCheckOptions &CheckOpts) {
  using ParsedT = decltype(ClangCheckReg)::ParsedOptionsT;
  auto *Storage = new ParsedT();
  decltype(ClangCheckReg)::applyDefaultsTo(*Storage);
  std::vector<clv2::detail::OptionEntry> Entries;
  std::vector<clv2::detail::AliasEntry> Aliases;
  std::vector<clv2::detail::SubCommandSpec> SubSpecs;
  decltype(ClangCheckReg)::staticBuildInto(*Storage, Entries, Aliases,
                                           SubSpecs);
  for (auto &E : Entries) {
    if (!E.Cat)
      E.Cat = &ClangCheckCategory;
    P.addDynamicEntry(std::move(E));
  }
  clv2::registerDynamicPostParseCallback(
      [Storage, &CheckOpts]() { applyClangCheckOpts(*Storage, CheckOpts); });
  P.setExtraHelp(
      std::string(clang::tooling::CommonOptionsParser::HelpMessage) +
      "\tFor example, to run clang-check on all files in a subtree of the\n"
      "\tsource tree, use:\n"
      "\n"
      "\t  find path/in/subtree -name '*.cpp'|xargs clang-check\n"
      "\n"
      "\tor using a specific build path:\n"
      "\n"
      "\t  find path/in/subtree -name '*.cpp'|xargs clang-check -p build/path\n"
      "\n"
      "\tNote, that path/in/subtree and current directory should follow the\n"
      "\trules described above.\n"
      "\n");
}

namespace {

// FIXME: Move FixItRewriteInPlace from lib/Rewrite/Frontend/FrontendActions.cpp
// into a header file and reuse that.
class FixItOptions : public clang::FixItOptions {
public:
  FixItOptions(bool FixWhatYouCan) { this->FixWhatYouCan = FixWhatYouCan; }

  std::string RewriteFilename(const std::string& filename, int &fd) override {
    // We don't need to do permission checking here since clang will diagnose
    // any I/O errors itself.

    fd = -1;  // No file descriptor for file.

    return filename;
  }
};

/// Subclasses \c clang::FixItRewriter to not count fixed errors/warnings
/// in the final error counts.
///
/// This has the side-effect that clang-check -fixit exits with code 0 on
/// successfully fixing all errors.
class FixItRewriter : public clang::FixItRewriter {
public:
  FixItRewriter(clang::DiagnosticsEngine& Diags,
                clang::SourceManager& SourceMgr,
                const clang::LangOptions& LangOpts,
                clang::FixItOptions* FixItOpts)
      : clang::FixItRewriter(Diags, SourceMgr, LangOpts, FixItOpts) {
  }

  bool IncludeInDiagnosticCounts() const override { return false; }
};

/// Subclasses \c clang::FixItAction so that we can install the custom
/// \c FixItRewriter.
class ClangCheckFixItAction : public clang::FixItAction {
public:
  ClangCheckFixItAction(bool FixWhatYouCan) : FixWhatYouCan(FixWhatYouCan) {}

  bool BeginSourceFileAction(clang::CompilerInstance& CI) override {
    FixItOpts.reset(new FixItOptions(FixWhatYouCan));
    Rewriter.reset(new FixItRewriter(CI.getDiagnostics(), CI.getSourceManager(),
                                     CI.getLangOpts(), FixItOpts.get()));
    return true;
  }

private:
  bool FixWhatYouCan;
};

class DumpSyntaxTree : public clang::ASTFrontendAction {
public:
  DumpSyntaxTree(bool TokensDump) : TokensDump(TokensDump) {}

  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &CI, StringRef InFile) override {
    class Consumer : public clang::ASTConsumer {
    public:
      Consumer(clang::CompilerInstance &CI, bool TokensDump)
          : Collector(CI.getPreprocessor()), TokensDump(TokensDump) {}

      void HandleTranslationUnit(clang::ASTContext &AST) override {
        clang::syntax::TokenBuffer TB = std::move(Collector).consume();
        if (TokensDump)
          llvm::outs() << TB.dumpForTests();
        clang::syntax::TokenBufferTokenManager TBTM(TB, AST.getLangOpts(),
                                                    AST.getSourceManager());
        clang::syntax::Arena A;
        llvm::outs()
            << clang::syntax::buildSyntaxTree(A, TBTM, AST)->dump(TBTM);
      }

    private:
      clang::syntax::TokenCollector Collector;
      bool TokensDump;
    };
    return std::make_unique<Consumer>(CI, TokensDump);
  }

private:
  bool TokensDump;
};

class ClangCheckActionFactory {
public:
  ClangCheckActionFactory(const ClangCheckOptions &CheckOpts)
      : CheckOpts(CheckOpts) {}

  std::unique_ptr<clang::ASTConsumer> newASTConsumer() {
    if (CheckOpts.ASTList)
      return clang::CreateASTDeclNodeLister();
    if (CheckOpts.ASTDump)
      return clang::CreateASTDumper(
          nullptr /*Dump to stdout.*/, CheckOpts.ASTDumpFilter,
          /*DumpDecls=*/true,
          /*Deserialize=*/false,
          /*DumpLookups=*/false,
          /*DumpDeclTypes=*/false, clang::ADOF_Default);
    if (CheckOpts.ASTPrint)
      return clang::CreateASTPrinter(nullptr, CheckOpts.ASTDumpFilter);
    return std::make_unique<clang::ASTConsumer>();
  }

private:
  const ClangCheckOptions &CheckOpts;
};

template <typename ActionT>
class ClangCheckActionFactoryWithBoolArg : public FrontendActionFactory {
public:
  ClangCheckActionFactoryWithBoolArg(bool Arg) : Arg(Arg) {}

  std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<ActionT>(Arg);
  }

private:
  bool Arg;
};

} // namespace

int main(int argc, const char **argv) {
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);

  // Initialize targets for clang module support.
  llvm::InitializeAllTargets();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllAsmParsers();

  ClangCheckOptions CheckOpts;
  auto ExpectedParser = CommonOptionsParser::create(
      argc, argv, ClangCheckCategory,
      [&CheckOpts](clv2::OptionParser &P) { configureParser(P, CheckOpts); });
  if (!ExpectedParser) {
    llvm::errs() << llvm::toString(ExpectedParser.takeError());
    return 1;
  }
  CommonOptionsParser &OptionsParser = ExpectedParser.get();
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  if (CheckOpts.Analyze) {
    // Set output path if is provided by user.
    //
    // As the original -o options have been removed by default via the
    // strip-output adjuster, we only need to add the analyzer -o options here
    // when it is provided by users.
    if (!CheckOpts.AnalyzerOutput.empty())
      Tool.appendArgumentsAdjuster(getInsertArgumentAdjuster(
          CommandLineArguments{"-o", CheckOpts.AnalyzerOutput},
          ArgumentInsertPosition::END));

    // Running the analyzer requires --analyze. Other modes can work with the
    // -fsyntax-only option.
    //
    // The syntax-only adjuster is installed by default.
    // Good: It also strips options that trigger extra output, like -save-temps.
    // Bad:  We don't want the -fsyntax-only when executing the static analyzer.
    //
    // To enable the static analyzer, we first strip all -fsyntax-only options
    // and then add an --analyze option to the front.
    Tool.appendArgumentsAdjuster(
        [&](const CommandLineArguments &Args, StringRef /*unused*/) {
          CommandLineArguments AdjustedArgs;
          for (const std::string &Arg : Args)
            if (Arg != "-fsyntax-only")
              AdjustedArgs.emplace_back(Arg);
          return AdjustedArgs;
        });
    Tool.appendArgumentsAdjuster(
        getInsertArgumentAdjuster("--analyze", ArgumentInsertPosition::BEGIN));
  }

  ClangCheckActionFactory CheckFactory(CheckOpts);
  std::unique_ptr<FrontendActionFactory> FrontendFactory;

  // Choose the correct factory based on the selected mode.
  if (CheckOpts.Analyze)
    FrontendFactory = newFrontendActionFactory<clang::ento::AnalysisAction>();
  else if (CheckOpts.Fixit)
    FrontendFactory = std::make_unique<
        ClangCheckActionFactoryWithBoolArg<ClangCheckFixItAction>>(
        CheckOpts.FixWhatYouCan);
  else if (CheckOpts.SyntaxTreeDump || CheckOpts.TokensDump)
    FrontendFactory =
        std::make_unique<ClangCheckActionFactoryWithBoolArg<DumpSyntaxTree>>(
            CheckOpts.TokensDump);
  else
    FrontendFactory = newFrontendActionFactory(&CheckFactory);

  return Tool.run(FrontendFactory.get());
}
