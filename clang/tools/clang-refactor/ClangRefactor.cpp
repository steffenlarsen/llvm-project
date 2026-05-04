//===--- ClangRefactor.cpp - Clang-based refactoring tool -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a clang-refactor tool that performs various
/// source transformations.
///
//===----------------------------------------------------------------------===//

#include "TestSupport.h"
#include "clang/Frontend/CommandLineSourceLoc.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Refactoring/RefactoringAction.h"
#include "clang/Tooling/Refactoring/RefactoringOptions.h"
#include "clang/Tooling/Refactoring/Rename/RenamingAction.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/raw_ostream.h"
#include <deque>
#include <optional>
#include <string>

using namespace clang;
using namespace tooling;
using namespace refactor;
namespace cl = llvm::cl;

namespace opts {

static bool Verbose = false;
static bool Inplace = false;

static constexpr llvm::clv2::OptionInfo<bool> OI_Verbose{"v",
                                                         "Use verbose output"};
static constexpr llvm::clv2::OptionInfo<bool> OI_Inplace{
    "i", "Inplace edit <file>s"};
static constexpr llvm::clv2::OptionsRegistry<&OI_Verbose, &OI_Inplace>
    RefactorOptsReg;

static void
applyRefactorOpts(const decltype(RefactorOptsReg)::ParsedOptionsT &Opts) {
  Verbose = Opts.get<&OI_Verbose>();
  Inplace = Opts.get<&OI_Inplace>();
}

} // end namespace opts

namespace {

/// Stores the parsed `-selection` argument.
class SourceSelectionArgument {
public:
  virtual ~SourceSelectionArgument() {}

  /// Parse the `-selection` argument.
  ///
  /// \returns A valid argument when the parse succedeed, null otherwise.
  static std::unique_ptr<SourceSelectionArgument> fromString(StringRef Value);

  /// Prints any additional state associated with the selection argument to
  /// the given output stream.
  virtual void print(raw_ostream &OS) {}

  /// Returns a replacement refactoring result consumer (if any) that should
  /// consume the results of a refactoring operation.
  ///
  /// The replacement refactoring result consumer is used by \c
  /// TestSourceSelectionArgument to inject a test-specific result handling
  /// logic into the refactoring operation. The test-specific consumer
  /// ensures that the individual results in a particular test group are
  /// identical.
  virtual std::unique_ptr<ClangRefactorToolConsumerInterface>
  createCustomConsumer() {
    return nullptr;
  }

  /// Runs the give refactoring function for each specified selection.
  ///
  /// \returns true if an error occurred, false otherwise.
  virtual bool
  forAllRanges(const SourceManager &SM,
               llvm::function_ref<void(SourceRange R)> Callback) = 0;
};

/// Stores the parsed -selection=test:<filename> option.
class TestSourceSelectionArgument final : public SourceSelectionArgument {
public:
  TestSourceSelectionArgument(TestSelectionRangesInFile TestSelections)
      : TestSelections(std::move(TestSelections)) {}

  void print(raw_ostream &OS) override { TestSelections.dump(OS); }

  std::unique_ptr<ClangRefactorToolConsumerInterface>
  createCustomConsumer() override {
    return TestSelections.createConsumer();
  }

  /// Testing support: invokes the selection action for each selection range in
  /// the test file.
  bool forAllRanges(const SourceManager &SM,
                    llvm::function_ref<void(SourceRange R)> Callback) override {
    return TestSelections.foreachRange(SM, Callback);
  }

private:
  TestSelectionRangesInFile TestSelections;
};

/// Stores the parsed -selection=filename:line:column[-line:column] option.
class SourceRangeSelectionArgument final : public SourceSelectionArgument {
public:
  SourceRangeSelectionArgument(ParsedSourceRange Range)
      : Range(std::move(Range)) {}

  bool forAllRanges(const SourceManager &SM,
                    llvm::function_ref<void(SourceRange R)> Callback) override {
    auto FE = SM.getFileManager().getOptionalFileRef(Range.FileName);
    FileID FID = FE ? SM.translateFile(*FE) : FileID();
    if (!FE || FID.isInvalid()) {
      llvm::errs() << "error: -selection=" << Range.FileName
                   << ":... : given file is not in the target TU\n";
      return true;
    }

    SourceLocation Start = SM.getMacroArgExpandedLocation(
        SM.translateLineCol(FID, Range.Begin.first, Range.Begin.second));
    SourceLocation End = SM.getMacroArgExpandedLocation(
        SM.translateLineCol(FID, Range.End.first, Range.End.second));
    if (Start.isInvalid() || End.isInvalid()) {
      llvm::errs() << "error: -selection=" << Range.FileName << ':'
                   << Range.Begin.first << ':' << Range.Begin.second << '-'
                   << Range.End.first << ':' << Range.End.second
                   << " : invalid source location\n";
      return true;
    }
    Callback(SourceRange(Start, End));
    return false;
  }

private:
  ParsedSourceRange Range;
};

std::unique_ptr<SourceSelectionArgument>
SourceSelectionArgument::fromString(StringRef Value) {
  if (Value.starts_with("test:")) {
    StringRef Filename = Value.drop_front(strlen("test:"));
    std::optional<TestSelectionRangesInFile> ParsedTestSelection =
        findTestSelectionRanges(Filename);
    if (!ParsedTestSelection)
      return nullptr; // A parsing error was already reported.
    return std::make_unique<TestSourceSelectionArgument>(
        std::move(*ParsedTestSelection));
  }
  std::optional<ParsedSourceRange> Range = ParsedSourceRange::fromString(Value);
  if (Range)
    return std::make_unique<SourceRangeSelectionArgument>(std::move(*Range));
  llvm::errs() << "error: '-selection' option must be specified using "
                  "<file>:<line>:<column> or "
                  "<file>:<line>:<column>-<line>:<column> format, "
                  "where <line> and <column> are integers greater than zero.\n";
  return nullptr;
}

/// A container that stores the command-line options used by a single
/// refactoring option.
class RefactoringActionCommandLineOptions {
public:
  void addStringOption(const RefactoringOption &Option,
                       const std::string &Name) {
    StringOptions[&Option] = Name;
    StringValues[Name] = "";
  }

  const std::string &getStringValue(const RefactoringOption &Opt) const {
    auto NameIt = StringOptions.find(&Opt);
    assert(NameIt != StringOptions.end() && "Option not registered");
    auto ValIt = StringValues.find(NameIt->second);
    assert(ValIt != StringValues.end() && "Option value not found");
    return ValIt->second;
  }

  void setValue(const std::string &Name, std::string Value) {
    StringValues[Name] = std::move(Value);
  }

private:
  llvm::DenseMap<const RefactoringOption *, std::string> StringOptions;
  llvm::StringMap<std::string> StringValues;
};

/// Passes the command-line option values to the options used by a single
/// refactoring action rule.
class CommandLineRefactoringOptionVisitor final
    : public RefactoringOptionVisitor {
public:
  CommandLineRefactoringOptionVisitor(
      const RefactoringActionCommandLineOptions &Options)
      : Options(Options) {}

  void visit(const RefactoringOption &Opt,
             std::optional<std::string> &Value) override {
    const std::string &Val = Options.getStringValue(Opt);
    if (!Val.empty()) {
      Value = Val;
      return;
    }
    Value = std::nullopt;
    if (Opt.isRequired())
      MissingRequiredOptions.push_back(&Opt);
  }

  ArrayRef<const RefactoringOption *> getMissingRequiredOptions() const {
    return MissingRequiredOptions;
  }

private:
  llvm::SmallVector<const RefactoringOption *, 4> MissingRequiredOptions;
  const RefactoringActionCommandLineOptions &Options;
};

namespace {
/// Context for a refactoring option's callback: which options object to write
/// to, and under which name.  Held in a deque so addresses stay stable — the
/// parser keeps a pointer to the RuntimeOption inside.
struct RefactorOptState {
  RefactoringActionCommandLineOptions *Opts = nullptr;
  std::string Name;
  std::optional<llvm::clv2::RuntimeOption<std::string>> Opt;
};
} // namespace
static std::deque<RefactorOptState> RefactorOptStates;

static bool setRefactorOptionValue(void *Ctx, const std::string &V) {
  auto *S = static_cast<RefactorOptState *>(Ctx);
  S->Opts->setValue(S->Name, V);
  return true;
}

/// Creates the refactoring options used by all the rules in a single
/// refactoring action.
class CommandLineRefactoringOptionCreator final
    : public RefactoringOptionVisitor {
public:
  CommandLineRefactoringOptionCreator(
      llvm::clv2::RuntimeSubCommandEntry &Sub,
      RefactoringActionCommandLineOptions &Options)
      : Sub(Sub), Options(Options) {}

  void visit(const RefactoringOption &Opt,
             std::optional<std::string> &) override {
    if (Visited.insert(&Opt).second) {
      if (!OptionNames.insert(Opt.getName()).second)
        llvm::report_fatal_error("Multiple identical refactoring options "
                                 "specified for one refactoring action");
      std::string Name = Opt.getName().str();
      Options.addStringOption(Opt, Name);
      // The option name only exists at runtime, so the descriptor is built
      // here; RuntimeOption is immovable, hence emplace-then-fill.
      RefactorOptStates.emplace_back();
      RefactorOptState &St = RefactorOptStates.back();
      St.Opts = &Options;
      St.Name = Name;
      St.Opt.emplace(
          Name, Opt.getDescription(),
          llvm::clv2::CtxCallback<std::string>{&setRefactorOptionValue, &St});
      Sub.Options.push_back(St.Opt->makeEntry());
    }
  }

private:
  llvm::SmallPtrSet<const RefactoringOption *, 8> Visited;
  llvm::StringSet<> OptionNames;
  llvm::clv2::RuntimeSubCommandEntry &Sub;
  RefactoringActionCommandLineOptions &Options;
};

static constexpr llvm::clv2::OptionInfo<std::string> OI_Selection{
    "selection", "The selected source range in which the refactoring should "
                 "be initiated (<file>:<line>:<column>-<line>:<column> or "
                 "<file>:<line>:<column>)"};

/// A subcommand that corresponds to individual refactoring action.
class RefactoringActionSubcommand {
public:
  RefactoringActionSubcommand(std::unique_ptr<RefactoringAction> Action,
                              RefactoringActionRules ActionRules)
      : Action(std::move(Action)), ActionRules(std::move(ActionRules)) {

    llvm::clv2::RuntimeSubCommandEntry Sub;
    Sub.Name = this->Action->getCommand();
    Sub.Desc = this->Action->getDescription();

    // Check if the selection option is supported.
    for (const auto &Rule : this->ActionRules) {
      if (Rule->hasSelectionRequirement()) {
        HasSelection = true;
        Sub.Options.push_back(llvm::clv2::makeEntry<&OI_Selection>(
            SelectionValue, SelectionCount));
        break;
      }
    }
    // Create the refactoring options.
    for (const auto &Rule : this->ActionRules) {
      CommandLineRefactoringOptionCreator OptionCreator(Sub, Options);
      Rule->visitRefactoringOptions(OptionCreator);
    }

    llvm::clv2::registerRuntimeSubcommand(std::move(Sub));
  }

  /// Whether this subcommand was the one named on the command line for the
  /// given parse.  Asks the parse's context rather than a process-wide flag,
  /// so concurrent parses can select different subcommands.
  bool isActivated(const llvm::clv2::OptionsContext &Ctx) const {
    return Ctx.getActiveSubCommand() == getName();
  }

  StringRef getName() const { return Action->getCommand(); }

  const RefactoringActionRules &getActionRules() const { return ActionRules; }

  /// Parses the "-selection" command-line argument.
  ///
  /// \returns true on error, false otherwise.
  bool parseSelectionArgument() {
    if (HasSelection && !SelectionValue.empty()) {
      ParsedSelection = SourceSelectionArgument::fromString(SelectionValue);
      if (!ParsedSelection)
        return true;
    }
    return false;
  }

  SourceSelectionArgument *getSelection() const {
    assert(HasSelection && "selection not supported!");
    return ParsedSelection.get();
  }

  const RefactoringActionCommandLineOptions &getOptions() const {
    return Options;
  }

private:
  std::unique_ptr<RefactoringAction> Action;
  RefactoringActionRules ActionRules;
  bool HasSelection = false;
  std::string SelectionValue;
  unsigned SelectionCount = 0;
  std::unique_ptr<SourceSelectionArgument> ParsedSelection;
  RefactoringActionCommandLineOptions Options;
};

class ClangRefactorConsumer final : public ClangRefactorToolConsumerInterface {
public:
  ClangRefactorConsumer(AtomicChanges &Changes) : SourceChanges(&Changes) {}

  void handleError(llvm::Error Err) override {
    std::optional<PartialDiagnosticAt> Diag = DiagnosticError::take(Err);
    if (!Diag) {
      llvm::errs() << llvm::toString(std::move(Err)) << "\n";
      return;
    }
    llvm::cantFail(std::move(Err)); // This is a success.
    DiagnosticBuilder DB(
        getDiags().Report(Diag->first, Diag->second.getDiagID()));
    Diag->second.Emit(DB);
  }

  void handle(AtomicChanges Changes) override {
    SourceChanges->insert(SourceChanges->begin(), Changes.begin(),
                          Changes.end());
  }

  void handle(SymbolOccurrences Occurrences) override {
    llvm_unreachable("symbol occurrence results are not handled yet");
  }

private:
  AtomicChanges *SourceChanges;
};

class ClangRefactorTool {
public:
  ClangRefactorTool()
      : SelectedSubcommand(nullptr), MatchingRule(nullptr),
        Consumer(new ClangRefactorConsumer(Changes)), HasFailed(false) {
    std::vector<std::unique_ptr<RefactoringAction>> Actions =
        createRefactoringActions();

    // Actions must have unique command names so that we can map them to one
    // subcommand.
    llvm::StringSet<> CommandNames;
    for (const auto &Action : Actions) {
      if (!CommandNames.insert(Action->getCommand()).second) {
        llvm::errs() << "duplicate refactoring action command '"
                     << Action->getCommand() << "'!";
        exit(1);
      }
    }

    // Create subcommands and command-line options.
    for (auto &Action : Actions) {
      SubCommands.push_back(std::make_unique<RefactoringActionSubcommand>(
          std::move(Action), Action->createActiveActionRules()));
    }
  }

  // Initializes the selected subcommand and refactoring rule based on the
  // command line options.
  llvm::Error Init(const llvm::clv2::OptionsContext &Ctx) {
    auto Subcommand = getSelectedSubcommand(Ctx);
    if (!Subcommand)
      return Subcommand.takeError();
    auto Rule = getMatchingRule(**Subcommand);
    if (!Rule)
      return Rule.takeError();

    SelectedSubcommand = *Subcommand;
    MatchingRule = *Rule;

    return llvm::Error::success();
  }

  bool hasFailed() const { return HasFailed; }

  using TUCallbackType = std::function<void(ASTContext &)>;

  // Callback of an AST action. This invokes the matching rule on the given AST.
  void callback(ASTContext &AST) {
    assert(SelectedSubcommand && MatchingRule && Consumer);
    RefactoringRuleContext Context(AST.getSourceManager());
    Context.setASTContext(AST);

    // If the selection option is test specific, we use a test-specific
    // consumer.
    std::unique_ptr<ClangRefactorToolConsumerInterface> TestConsumer;
    bool HasSelection = MatchingRule->hasSelectionRequirement();
    if (HasSelection)
      TestConsumer = SelectedSubcommand->getSelection()->createCustomConsumer();
    ClangRefactorToolConsumerInterface *ActiveConsumer =
        TestConsumer ? TestConsumer.get() : Consumer.get();
    ActiveConsumer->beginTU(AST);

    auto InvokeRule = [&](RefactoringResultConsumer &Consumer) {
      if (opts::Verbose)
        logInvocation(*SelectedSubcommand, Context);
      MatchingRule->invoke(*ActiveConsumer, Context);
    };
    if (HasSelection) {
      assert(SelectedSubcommand->getSelection() &&
             "Missing selection argument?");
      if (opts::Verbose)
        SelectedSubcommand->getSelection()->print(llvm::outs());
      if (SelectedSubcommand->getSelection()->forAllRanges(
              Context.getSources(), [&](SourceRange R) {
                Context.setSelectionRange(R);
                InvokeRule(*ActiveConsumer);
              }))
        HasFailed = true;
      ActiveConsumer->endTU();
      return;
    }
    InvokeRule(*ActiveConsumer);
    ActiveConsumer->endTU();
  }

  llvm::Expected<std::unique_ptr<FrontendActionFactory>>
  getFrontendActionFactory() {
    class ToolASTConsumer : public ASTConsumer {
    public:
      TUCallbackType Callback;
      ToolASTConsumer(TUCallbackType Callback)
          : Callback(std::move(Callback)) {}

      void HandleTranslationUnit(ASTContext &Context) override {
        Callback(Context);
      }
    };
    class ToolASTAction : public ASTFrontendAction {
    public:
      explicit ToolASTAction(TUCallbackType Callback)
          : Callback(std::move(Callback)) {}

    protected:
      std::unique_ptr<clang::ASTConsumer>
      CreateASTConsumer(clang::CompilerInstance &compiler,
                        StringRef /* dummy */) override {
        std::unique_ptr<clang::ASTConsumer> Consumer{
            new ToolASTConsumer(Callback)};
        return Consumer;
      }

    private:
      TUCallbackType Callback;
    };

    class ToolActionFactory : public FrontendActionFactory {
    public:
      ToolActionFactory(TUCallbackType Callback)
          : Callback(std::move(Callback)) {}

      std::unique_ptr<FrontendAction> create() override {
        return std::make_unique<ToolASTAction>(Callback);
      }

    private:
      TUCallbackType Callback;
    };

    return std::make_unique<ToolActionFactory>(
        [this](ASTContext &AST) { return callback(AST); });
  }

  // FIXME(ioeric): this seems to only works for changes in a single file at
  // this point.
  bool applySourceChanges() {
    std::set<std::string> Files;
    for (const auto &Change : Changes)
      Files.insert(Change.getFilePath());
    // FIXME: Add automatic formatting support as well.
    tooling::ApplyChangesSpec Spec;
    // FIXME: We should probably cleanup the result by default as well.
    Spec.Cleanup = false;
    for (const auto &File : Files) {
      llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> BufferErr =
          llvm::MemoryBuffer::getFile(File);
      if (!BufferErr) {
        llvm::errs() << "error: failed to open " << File << " for rewriting\n";
        return true;
      }
      auto Result = tooling::applyAtomicChanges(File, (*BufferErr)->getBuffer(),
                                                Changes, Spec);
      if (!Result) {
        llvm::errs() << toString(Result.takeError());
        return true;
      }

      if (opts::Inplace) {
        std::error_code EC;
        llvm::raw_fd_ostream OS(File, EC, llvm::sys::fs::OF_TextWithCRLF);
        if (EC) {
          llvm::errs() << EC.message() << "\n";
          return true;
        }
        OS << *Result;
        continue;
      }

      llvm::outs() << *Result;
    }
    return false;
  }

private:
  /// Logs an individual refactoring action invocation to STDOUT.
  void logInvocation(RefactoringActionSubcommand &Subcommand,
                     const RefactoringRuleContext &Context) {
    llvm::outs() << "invoking action '" << Subcommand.getName() << "':\n";
    if (Context.getSelectionRange().isValid()) {
      SourceRange R = Context.getSelectionRange();
      llvm::outs() << "  -selection=";
      R.getBegin().print(llvm::outs(), Context.getSources());
      llvm::outs() << " -> ";
      R.getEnd().print(llvm::outs(), Context.getSources());
      llvm::outs() << "\n";
    }
  }

  llvm::Expected<RefactoringActionRule *>
  getMatchingRule(RefactoringActionSubcommand &Subcommand) {
    SmallVector<RefactoringActionRule *, 4> MatchingRules;
    llvm::StringSet<> MissingOptions;

    for (const auto &Rule : Subcommand.getActionRules()) {
      CommandLineRefactoringOptionVisitor Visitor(Subcommand.getOptions());
      Rule->visitRefactoringOptions(Visitor);
      if (Visitor.getMissingRequiredOptions().empty()) {
        if (!Rule->hasSelectionRequirement()) {
          MatchingRules.push_back(Rule.get());
        } else {
          Subcommand.parseSelectionArgument();
          if (Subcommand.getSelection()) {
            MatchingRules.push_back(Rule.get());
          } else {
            MissingOptions.insert("selection");
          }
        }
      }
      for (const RefactoringOption *Opt : Visitor.getMissingRequiredOptions())
        MissingOptions.insert(Opt->getName());
    }
    if (MatchingRules.empty()) {
      std::string Error;
      llvm::raw_string_ostream OS(Error);
      OS << "ERROR: '" << Subcommand.getName()
         << "' can't be invoked with the given arguments:\n";
      for (const auto &Opt : MissingOptions)
        OS << "  missing '-" << Opt.getKey() << "' option\n";
      return llvm::make_error<llvm::StringError>(
          Error, llvm::inconvertibleErrorCode());
    }
    if (MatchingRules.size() != 1) {
      return llvm::make_error<llvm::StringError>(
          llvm::Twine("ERROR: more than one matching rule of action") +
              Subcommand.getName() + "was found with given options.",
          llvm::inconvertibleErrorCode());
    }
    return MatchingRules.front();
  }
  // Figure out which action is specified by the user. The user must specify the
  // action using a command-line subcommand, e.g. the invocation `clang-refactor
  // local-rename` corresponds to the `LocalRename` refactoring action. All
  // subcommands must have a unique names. This allows us to figure out which
  // refactoring action should be invoked by looking at the first subcommand
  // that's enabled by LLVM's command-line parser.
  llvm::Expected<RefactoringActionSubcommand *>
  getSelectedSubcommand(const llvm::clv2::OptionsContext &Ctx) {
    auto It = llvm::find_if(
        SubCommands,
        [&Ctx](const std::unique_ptr<RefactoringActionSubcommand> &SubCommand) {
          return SubCommand->isActivated(Ctx);
        });
    if (It == SubCommands.end()) {
      std::string Error;
      llvm::raw_string_ostream OS(Error);
      OS << "error: no refactoring action given\n";
      OS << "note: the following actions are supported:\n";
      for (const auto &Subcommand : SubCommands)
        OS.indent(2) << Subcommand->getName() << "\n";
      return llvm::make_error<llvm::StringError>(
          Error, llvm::inconvertibleErrorCode());
    }
    RefactoringActionSubcommand *Subcommand = &(**It);
    return Subcommand;
  }

  std::vector<std::unique_ptr<RefactoringActionSubcommand>> SubCommands;
  RefactoringActionSubcommand *SelectedSubcommand;
  RefactoringActionRule *MatchingRule;
  std::unique_ptr<ClangRefactorToolConsumerInterface> Consumer;
  AtomicChanges Changes;
  bool HasFailed;
};

} // end anonymous namespace

int main(int argc, const char **argv) {
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);

  ClangRefactorTool RefactorTool;

  auto ExpectedParser = CommonOptionsParser::create(
      argc, argv, cl::getGeneralCategory(),
      [](llvm::clv2::OptionParser &P) {
        P.add<&opts::RefactorOptsReg, opts::applyRefactorOpts>();
        P.showOptions({
            "cfg-hide-cold-paths",
            "cfg-hide-deoptimize-paths",
            "cfg-hide-unreachable-paths",
            "disable-auto-upgrade-debug-info",
            "disable-i2p-p2i-opt",
            "dot-cfg-mssa",
            "elide-all-zero-branch-weights",
            "enable-name-compression",
            "enable-vtable-profile-use",
            "enable-vtable-value-profiling",
            "generate-merged-base-profiles",
            "object-size-offset-visitor-max-visit-instructions",
            "i",
            "v",
        });
      },
      cl::ZeroOrMore,
      "Clang-based refactoring tool for C, C++ and Objective-C");
  if (!ExpectedParser) {
    llvm::errs() << llvm::toString(ExpectedParser.takeError());
    return 1;
  }
  CommonOptionsParser &Options = ExpectedParser.get();

  if (auto Err = RefactorTool.Init(Options.getOptionsContext())) {
    llvm::errs() << llvm::toString(std::move(Err)) << "\n";
    return 1;
  }

  auto ActionFactory = RefactorTool.getFrontendActionFactory();
  if (!ActionFactory) {
    llvm::errs() << llvm::toString(ActionFactory.takeError()) << "\n";
    return 1;
  }
  ClangTool Tool(Options.getCompilations(), Options.getSourcePathList());
  bool Failed = false;
  if (Tool.run(ActionFactory->get()) != 0) {
    llvm::errs() << "Failed to run refactoring action on files\n";
    // It is possible that TUs are broken while changes are generated correctly,
    // so we still try applying changes.
    Failed = true;
  }
  return RefactorTool.applySourceChanges() || Failed ||
         RefactorTool.hasFailed();
}
