//===---- ClangQuery.cpp - clang-query tool -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tool is for interactive exploration of the Clang AST using AST matchers.
// It currently allows the user to enter a matcher at an interactive prompt and
// view the resulting bindings as diagnostics, AST pretty prints or AST dumps.
// Example session:
//
// $ cat foo.c
// void foo(void) {}
// $ clang-query foo.c --
// clang-query> match functionDecl()
//
// Match #1:
//
// foo.c:1:1: note: "root" binds here
// void foo(void) {}
// ^~~~~~~~~~~~~~~~~
// 1 match.
//
//===----------------------------------------------------------------------===//

#include "Query.h"
#include "QueryParser.h"
#include "QuerySession.h"
#include "clang-tools-extra/ClangToolsExtraOptionsOptInfos.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/LineEditor/LineEditor.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/WithColor.h"
#include <optional>
#include <string>

using namespace clang;
using namespace clang::ast_matchers;
using namespace clang::ast_matchers::dynamic;
using namespace clang::query;
using namespace clang::tooling;
using namespace llvm;

static cl::OptionCategory ClangQueryCategory("clang-query options");

struct ClangQueryOptions {
  bool UseColor = false;
  bool UseColorSet = false;
  std::vector<std::string> Commands;
  std::vector<std::string> CommandFiles;
  std::string PreloadFile;
};

inline constexpr clv2::OptionsRegistry<
    &clv2::CTE_CQ_UseColor, &clv2::CTE_CQ_Command, &clv2::CTE_CQ_CommandFile,
    &clv2::CTE_CQ_Preload>
    ClangQueryOptsReg;

static void
applyClangQueryOpts(const decltype(ClangQueryOptsReg)::ParsedOptionsT &Opts,
                    ClangQueryOptions &ToolOpts) {
  bool ParsedUseColor = Opts.get<&clv2::CTE_CQ_UseColor>();
  ToolOpts.UseColor = ParsedUseColor;
  if (ParsedUseColor)
    ToolOpts.UseColorSet = true;
  ToolOpts.Commands = Opts.get<&clv2::CTE_CQ_Command>();
  ToolOpts.CommandFiles = Opts.get<&clv2::CTE_CQ_CommandFile>();
  ToolOpts.PreloadFile = Opts.get<&clv2::CTE_CQ_Preload>();
}

static void configureParser(clv2::OptionParser &P,
                            ClangQueryOptions &ToolOpts) {
  using ParsedT = decltype(ClangQueryOptsReg)::ParsedOptionsT;
  auto *Storage = new ParsedT();
  decltype(ClangQueryOptsReg)::applyDefaultsTo(*Storage);
  std::vector<clv2::detail::OptionEntry> Entries;
  std::vector<clv2::detail::AliasEntry> Aliases;
  std::vector<clv2::detail::SubCommandSpec> SubSpecs;
  decltype(ClangQueryOptsReg)::staticBuildInto(*Storage, Entries, Aliases,
                                               SubSpecs);
  for (auto &E : Entries) {
    if (!E.Cat)
      E.Cat = &ClangQueryCategory;
    P.addDynamicEntry(std::move(E));
  }
  clv2::registerDynamicPostParseCallback(
      [Storage, &ToolOpts]() { applyClangQueryOpts(*Storage, ToolOpts); });
  P.setExtraHelp(clang::tooling::CommonOptionsParser::HelpMessage);
}

bool runCommandsInFile(const char *ExeName, std::string const &FileName,
                       QuerySession &QS) {
  FileQuery Query(FileName, ExeName);
  return !Query.run(llvm::errs(), QS);
}

int main(int argc, const char **argv) {
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);

  ClangQueryOptions ToolOpts;
  llvm::Expected<CommonOptionsParser> OptionsParser =
      CommonOptionsParser::create(
          argc, argv, ClangQueryCategory,
          [&ToolOpts](clv2::OptionParser &P) { configureParser(P, ToolOpts); },
          llvm::cl::OneOrMore);

  if (!OptionsParser) {
    llvm::WithColor::error() << llvm::toString(OptionsParser.takeError());
    return 1;
  }

  if (!ToolOpts.Commands.empty() && !ToolOpts.CommandFiles.empty()) {
    llvm::errs() << argv[0] << ": cannot specify both -c and -f\n";
    return 1;
  }

  if ((!ToolOpts.Commands.empty() || !ToolOpts.CommandFiles.empty()) &&
      !ToolOpts.PreloadFile.empty()) {
    llvm::errs() << argv[0]
                 << ": cannot specify both -c or -f with --preload\n";
    return 1;
  }

  ClangTool Tool(OptionsParser->getCompilations(),
                 OptionsParser->getSourcePathList());

  if (ToolOpts.UseColorSet) {
    ArgumentsAdjuster colorAdjustor =
        [UseColor = ToolOpts.UseColor](const CommandLineArguments &Args,
                                       StringRef /*unused*/) {
          CommandLineArguments AdjustedArgs = Args;
          if (UseColor)
            AdjustedArgs.push_back("-fdiagnostics-color");
          else
            AdjustedArgs.push_back("-fno-diagnostics-color");
          return AdjustedArgs;
        };
    Tool.appendArgumentsAdjuster(colorAdjustor);
  }

  std::vector<std::unique_ptr<ASTUnit>> ASTs;
  int ASTStatus = 0;
  switch (Tool.buildASTs(ASTs)) {
  case 0:
    break;
  case 1: // Building ASTs failed.
    return 1;
  case 2:
    ASTStatus |= 1;
    llvm::errs() << "Failed to build AST for some of the files, "
                 << "results may be incomplete."
                 << "\n";
    break;
  default:
    llvm_unreachable("Unexpected status returned");
  }

  QuerySession QS(ASTs);

  if (!ToolOpts.Commands.empty()) {
    for (auto &Command : ToolOpts.Commands) {
      QueryRef Q = QueryParser::parse(Command, QS);
      if (!Q->run(llvm::outs(), QS))
        return 1;
    }
  } else if (!ToolOpts.CommandFiles.empty()) {
    for (auto &CommandFile : ToolOpts.CommandFiles) {
      if (runCommandsInFile(argv[0], CommandFile, QS))
        return 1;
    }
  } else {
    if (!ToolOpts.PreloadFile.empty()) {
      if (runCommandsInFile(argv[0], ToolOpts.PreloadFile, QS))
        return 1;
    }
    LineEditor LE("clang-query");
    LE.setListCompleter([&QS](StringRef Line, size_t Pos) {
      return QueryParser::complete(Line, Pos, QS);
    });
    while (std::optional<std::string> Line = LE.readLine()) {
      QueryRef Q = QueryParser::parse(*Line, QS);
      Q->run(llvm::outs(), QS);
      llvm::outs().flush();
      if (QS.Terminate)
        break;
    }
  }

  return ASTStatus;
}
