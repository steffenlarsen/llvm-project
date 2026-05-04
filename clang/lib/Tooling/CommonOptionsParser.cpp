//===--- CommonOptionsParser.cpp - common options for clang tools ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the CommonOptionsParser class used to parse common
//  command-line options for clang tools, so that they can be run as separate
//  command-line applications with a consistent common interface for handling
//  compilation database and input files.
//
//  It provides a common subset of command-line options, common algorithm
//  for locating a compilation database and source files, and help messages
//  for the basic command-line interface.
//
//  It creates a CompilationDatabase and reads common command-line options.
//
//  This class uses the Clang Tooling infrastructure, see
//    http://clang.llvm.org/docs/HowToSetupToolingForLLVM.html
//  for details on setting it up with LLVM source tree.
//
//===----------------------------------------------------------------------===//

#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Execution.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/RegisterLLVMOptions.h"

using namespace clang::tooling;
using namespace llvm;

const char *const CommonOptionsParser::HelpMessage =
    "\n"
    "-p <build-path> is used to read a compile command database.\n"
    "\n"
    "\tFor example, it can be a CMake build directory in which a file named\n"
    "\tcompile_commands.json exists (use -DCMAKE_EXPORT_COMPILE_COMMANDS=ON\n"
    "\tCMake option to get this output). When no build path is specified,\n"
    "\ta search for compile_commands.json will be attempted through all\n"
    "\tparent paths of the first input file . See:\n"
    "\thttps://clang.llvm.org/docs/HowToSetupToolingForLLVM.html for an\n"
    "\texample of setting up Clang Tooling on a source tree.\n"
    "\n"
    "<source0> ... specify the paths of source files. These paths are\n"
    "\tlooked up in the compile command database. If the path of a file is\n"
    "\tabsolute, it needs to point into CMake's source tree. If the path is\n"
    "\trelative, the current working directory needs to be in the CMake\n"
    "\tsource tree and the file must be in a subdirectory of the current\n"
    "\tworking directory. \"./\" prefixes in the relative files will be\n"
    "\tautomatically removed, but the rest of a relative path must be a\n"
    "\tsuffix of a path in the compile command database.\n"
    "\n";

void ArgumentsAdjustingCompilations::appendArgumentsAdjuster(
    ArgumentsAdjuster Adjuster) {
  Adjusters.push_back(std::move(Adjuster));
}

std::vector<CompileCommand> ArgumentsAdjustingCompilations::getCompileCommands(
    StringRef FilePath) const {
  return adjustCommands(Compilations->getCompileCommands(FilePath));
}

std::vector<std::string>
ArgumentsAdjustingCompilations::getAllFiles() const {
  return Compilations->getAllFiles();
}

std::vector<CompileCommand>
ArgumentsAdjustingCompilations::getAllCompileCommands() const {
  return adjustCommands(Compilations->getAllCompileCommands());
}

std::vector<CompileCommand> ArgumentsAdjustingCompilations::adjustCommands(
    std::vector<CompileCommand> Commands) const {
  for (CompileCommand &Command : Commands)
    for (const auto &Adjuster : Adjusters)
      Command.CommandLine = Adjuster(Command.CommandLine, Command.Filename);
  return Commands;
}

// The category is set dynamically in init() to match the tool's category.
static constexpr clv2::OptionInfo<std::string> OI_BuildPath{
    "p", "Build path", clv2::ValueRequired};
static constexpr clv2::ListOptionInfo<std::string> OI_SourcePaths{
    "", "<source0> [... <sourceN>]", clv2::Positional{}, clv2::ZeroOrMore};
static constexpr clv2::ListOptionInfo<std::string> OI_ExtraArg{
    "extra-arg", "Additional argument to append to the compiler command line",
    clv2::ZeroOrMore};
static constexpr clv2::ListOptionInfo<std::string> OI_ExtraArgBefore{
    "extra-arg-before",
    "Additional argument to prepend to the compiler command line",
    clv2::ZeroOrMore};
static constexpr clv2::OptionInfo<std::string> OI_Executor{
    "executor", "The name of the executor to use.", clv2::Init{"standalone"}};
static constexpr clv2::OptionsRegistry<&OI_BuildPath, &OI_SourcePaths,
                                       &OI_ExtraArg, &OI_ExtraArgBefore,
                                       &OI_Executor>
    CommonToolingOptsReg;

void CommonOptionsParser::printHelp(llvm::raw_ostream &OS) const {
  if (Parser)
    Parser->printHelp(OS, HelpOverview, ProgName);
}

llvm::Error CommonOptionsParser::init(
    int &argc, const char **argv, cl::OptionCategory &Category,
    llvm::cl::NumOccurrencesFlag OccurrencesFlag, const char *Overview,
    llvm::function_ref<void(clv2::OptionParser &)> ConfigureParser) {

  // ExecutorName is a process-wide global owned by Execution.cpp; reset it so a
  // failed parse cannot leave the previous invocation's choice behind.
  ExecutorName = "standalone";

  cl::ResetAllOptionOccurrences();

  std::string ErrorMessage;
  Compilations =
      FixedCompilationDatabase::loadFromCommandLine(argc, argv, ErrorMessage);
  if (!ErrorMessage.empty())
    ErrorMessage.append("\n");
  llvm::raw_string_ostream OS(ErrorMessage);
  Parser = std::make_unique<clv2::OptionParser>();
  clv2::OptionParser &P = *Parser;
  RegisterAllLLVMOptions(P);
  // Holds the parsed values for the options added below.  The entries point
  // into it, so it has to outlive P.parse(); reading it directly afterwards is
  // why no post-parse callback is needed.
  decltype(CommonToolingOptsReg)::ParsedOptionsT Storage;
  // Tag CommonTooling options with the tool's category so they survive
  // hideUnrelatedOptions.
  {
    decltype(CommonToolingOptsReg)::applyDefaultsTo(Storage);
    std::vector<clv2::detail::OptionEntry> Entries;
    std::vector<clv2::detail::AliasEntry> Aliases;
    std::vector<clv2::detail::SubCommandSpec> SubSpecs;
    decltype(CommonToolingOptsReg)::staticBuildInto(Storage, Entries, Aliases,
                                                    SubSpecs);
    for (auto &E : Entries) {
      if (!E.Cat)
        E.Cat = &Category;
      P.addDynamicEntry(std::move(E));
    }
  }
  if (ConfigureParser)
    ConfigureParser(P);
  P.hideUnrelatedOptions({&Category});
  HelpOverview = Overview ? Overview : "";
  ProgName =
      argc > 0 && argv[0] ? llvm::sys::path::filename(argv[0]).str() : "";
  size_t ErrorLenBefore = ErrorMessage.size();
  auto ParseResult = P.parse(argc, argv, Overview, &OS);

  if (!ParseResult) {
    // Help/version was printed, or a fatal parse error occurred.
    // If help was printed (no error message), exit cleanly.
    if (ErrorMessage.size() == ErrorLenBefore)
      std::exit(0);
    return llvm::make_error<llvm::StringError>(ErrorMessage,
                                               llvm::inconvertibleErrorCode());
  }

  if (ErrorMessage.size() > ErrorLenBefore)
    return llvm::make_error<llvm::StringError>(ErrorMessage,
                                               llvm::inconvertibleErrorCode());

  // Retain the parsed options so callers can hand them to ClangTool and have
  // them reach each compilation's ASTContext.
  OptionsCtx = std::move(ParseResult);

  ExecutorName = Storage.get<&OI_Executor>();
  SourcePathList = Storage.get<&OI_SourcePaths>();
  if ((OccurrencesFlag == cl::ZeroOrMore || OccurrencesFlag == cl::Optional) &&
      SourcePathList.empty())
    return llvm::Error::success();
  if (SourcePathList.empty())
    return llvm::make_error<llvm::StringError>(
        "Not enough positional command line arguments specified!\n"
        "Must specify at least 1 positional argument: See: '" +
            llvm::Twine(argv[0]) + " --help'\n",
        llvm::inconvertibleErrorCode());
  if (!Compilations) {
    if (!Storage.get<&OI_BuildPath>().empty()) {
      Compilations = CompilationDatabase::autoDetectFromDirectory(
          Storage.get<&OI_BuildPath>(), ErrorMessage);
    } else {
      Compilations = CompilationDatabase::autoDetectFromSource(
          Storage.get<&OI_SourcePaths>()[0], ErrorMessage);
    }
    if (!Compilations) {
      llvm::errs() << "Error while trying to load a compilation database:\n"
                   << ErrorMessage << "Running without flags.\n";
      Compilations.reset(
          new FixedCompilationDatabase(".", std::vector<std::string>()));
    }
  }
  Compilations = inferToolLocation(std::move(Compilations));
  auto AdjustingCompilations =
      std::make_unique<ArgumentsAdjustingCompilations>(
          std::move(Compilations));
  Adjuster = getInsertArgumentAdjuster(Storage.get<&OI_ExtraArgBefore>(),
                                       ArgumentInsertPosition::BEGIN);
  Adjuster =
      combineAdjusters(std::move(Adjuster),
                       getInsertArgumentAdjuster(Storage.get<&OI_ExtraArg>(),
                                                 ArgumentInsertPosition::END));
  AdjustingCompilations->appendArgumentsAdjuster(Adjuster);
  Compilations = std::move(AdjustingCompilations);
  return llvm::Error::success();
}

llvm::Expected<CommonOptionsParser> CommonOptionsParser::create(
    int &argc, const char **argv, llvm::cl::OptionCategory &Category,
    llvm::cl::NumOccurrencesFlag OccurrencesFlag, const char *Overview) {
  CommonOptionsParser Parser;
  llvm::Error Err =
      Parser.init(argc, argv, Category, OccurrencesFlag, Overview);
  if (Err)
    return std::move(Err);
  return std::move(Parser);
}

llvm::Expected<CommonOptionsParser> CommonOptionsParser::create(
    int &argc, const char **argv, llvm::cl::OptionCategory &Category,
    llvm::function_ref<void(llvm::clv2::OptionParser &)> ConfigureParser,
    llvm::cl::NumOccurrencesFlag OccurrencesFlag, const char *Overview) {
  CommonOptionsParser Parser;
  llvm::Error Err = Parser.init(argc, argv, Category, OccurrencesFlag, Overview,
                                ConfigureParser);
  if (Err)
    return std::move(Err);
  return std::move(Parser);
}

CommonOptionsParser::CommonOptionsParser(
    int &argc, const char **argv, cl::OptionCategory &Category,
    llvm::cl::NumOccurrencesFlag OccurrencesFlag, const char *Overview) {
  llvm::Error Err = init(argc, argv, Category, OccurrencesFlag, Overview);
  if (Err) {
    llvm::report_fatal_error(
        Twine("CommonOptionsParser: failed to parse command-line arguments. ") +
        llvm::toString(std::move(Err)));
  }
}
