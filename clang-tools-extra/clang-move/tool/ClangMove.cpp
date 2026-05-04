//===-- ClangMove.cpp - move definition to new file -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Move.h"
#include "clang-tools-extra/ClangToolsExtraOptionsOptInfos.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/ArgumentsAdjusters.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/YAMLTraits.h"
#include <set>
#include <string>

using namespace clang;
using namespace llvm;

namespace {

std::error_code CreateNewFile(const llvm::Twine &path) {
  int fd = 0;
  if (std::error_code ec = llvm::sys::fs::openFileForWrite(
          path, fd, llvm::sys::fs::CD_CreateAlways,
          llvm::sys::fs::OF_TextWithCRLF))
    return ec;

  return llvm::sys::Process::SafelyCloseFileDescriptor(fd);
}

cl::OptionCategory ClangMoveCategory("clang-move options");

static std::vector<std::string> Names;
static std::string OldHeader;
static std::string OldCC;
static std::string NewHeader;
static std::string NewCC;
static bool OldDependOnNew = false;
static bool NewDependOnOld = false;
static std::string Style = "llvm";
static bool Dump = false;
static bool DumpDecls = false;

inline constexpr clv2::OptionsRegistry<
    &clv2::CTE_CM_Names, &clv2::CTE_CM_OldHeader, &clv2::CTE_CM_OldCC,
    &clv2::CTE_CM_NewHeader, &clv2::CTE_CM_NewCC, &clv2::CTE_CM_OldDependOnNew,
    &clv2::CTE_CM_NewDependOnOld, &clv2::CTE_CM_Style, &clv2::CTE_CM_DumpResult,
    &clv2::CTE_CM_DumpDecls>
    ToolOptsReg;

static void applyToolOpts(const decltype(ToolOptsReg)::ParsedOptionsT &Opts) {
  Names = Opts.get<&clv2::CTE_CM_Names>();
  OldHeader = Opts.get<&clv2::CTE_CM_OldHeader>();
  OldCC = Opts.get<&clv2::CTE_CM_OldCC>();
  NewHeader = Opts.get<&clv2::CTE_CM_NewHeader>();
  NewCC = Opts.get<&clv2::CTE_CM_NewCC>();
  OldDependOnNew = Opts.get<&clv2::CTE_CM_OldDependOnNew>();
  NewDependOnOld = Opts.get<&clv2::CTE_CM_NewDependOnOld>();
  Style = Opts.get<&clv2::CTE_CM_Style>();
  Dump = Opts.get<&clv2::CTE_CM_DumpResult>();
  DumpDecls = Opts.get<&clv2::CTE_CM_DumpDecls>();
}

static void configureParser(clv2::OptionParser &P) {
  using ParsedT = decltype(ToolOptsReg)::ParsedOptionsT;
  auto *Storage = new ParsedT();
  decltype(ToolOptsReg)::applyDefaultsTo(*Storage);
  std::vector<clv2::detail::OptionEntry> Entries;
  std::vector<clv2::detail::AliasEntry> Aliases;
  std::vector<clv2::detail::SubCommandSpec> SubSpecs;
  decltype(ToolOptsReg)::staticBuildInto(*Storage, Entries, Aliases, SubSpecs);
  for (auto &E : Entries) {
    if (!E.Cat)
      E.Cat = &ClangMoveCategory;
    P.addDynamicEntry(std::move(E));
  }
  clv2::registerDynamicPostParseCallback(
      [Storage]() { applyToolOpts(*Storage); });
}

} // namespace

int main(int argc, const char **argv) {
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);
  auto ExpectedParser = tooling::CommonOptionsParser::create(
      argc, argv, ClangMoveCategory, configureParser);
  if (!ExpectedParser) {
    llvm::errs() << llvm::toString(ExpectedParser.takeError());
    return 1;
  }
  tooling::CommonOptionsParser &OptionsParser = ExpectedParser.get();

  if (OldDependOnNew && NewDependOnOld) {
    llvm::errs() << "Provide either --old_depend_on_new or "
                    "--new_depend_on_old. clang-move doesn't support these two "
                    "options at same time (It will introduce include cycle).\n";
    return 1;
  }

  tooling::RefactoringTool Tool(OptionsParser.getCompilations(),
                                OptionsParser.getSourcePathList());
  // Add "-fparse-all-comments" compile option to make clang parse all comments.
  Tool.appendArgumentsAdjuster(tooling::getInsertArgumentAdjuster(
      "-fparse-all-comments", tooling::ArgumentInsertPosition::BEGIN));
  move::MoveDefinitionSpec Spec;
  Spec.Names = {Names.begin(), Names.end()};
  Spec.OldHeader = OldHeader;
  Spec.NewHeader = NewHeader;
  Spec.OldCC = OldCC;
  Spec.NewCC = NewCC;
  Spec.OldDependOnNew = OldDependOnNew;
  Spec.NewDependOnOld = NewDependOnOld;

  llvm::SmallString<128> InitialDirectory;
  if (std::error_code EC = llvm::sys::fs::current_path(InitialDirectory))
    llvm::report_fatal_error("Cannot detect current path: " +
                             Twine(EC.message()));

  move::ClangMoveContext Context{Spec, Tool.getReplacements(),
                                 std::string(InitialDirectory), Style,
                                 DumpDecls};
  move::DeclarationReporter Reporter;
  move::ClangMoveActionFactory Factory(&Context, &Reporter);

  int CodeStatus = Tool.run(&Factory);
  if (CodeStatus)
    return CodeStatus;

  if (DumpDecls) {
    llvm::outs() << "[\n";
    const auto &Declarations = Reporter.getDeclarationList();
    for (auto I = Declarations.begin(), E = Declarations.end(); I != E; ++I) {
      llvm::outs() << "  {\n";
      llvm::outs() << "    \"DeclarationName\": \"" << I->QualifiedName
                   << "\",\n";
      llvm::outs() << "    \"DeclarationType\": \"" << I->Kind << "\",\n";
      llvm::outs() << "    \"Templated\": " << (I->Templated ? "true" : "false")
                   << "\n";
      llvm::outs() << "  }";
      // Don't print trailing "," at the end of last element.
      if (I != std::prev(E))
        llvm::outs() << ",\n";
    }
    llvm::outs() << "\n]\n";
    return 0;
  }

  if (!NewCC.empty()) {
    std::error_code EC = CreateNewFile(NewCC);
    if (EC) {
      llvm::errs() << "Failed to create " << NewCC << ": " << EC.message()
                   << "\n";
      return EC.value();
    }
  }
  if (!NewHeader.empty()) {
    std::error_code EC = CreateNewFile(NewHeader);
    if (EC) {
      llvm::errs() << "Failed to create " << NewHeader << ": " << EC.message()
                   << "\n";
      return EC.value();
    }
  }

  DiagnosticOptions DiagOpts;
  clang::TextDiagnosticPrinter DiagnosticPrinter(errs(), DiagOpts);
  DiagnosticsEngine Diagnostics(DiagnosticIDs::create(), DiagOpts,
                                &DiagnosticPrinter, false);
  auto &FileMgr = Tool.getFiles();
  SourceManager SM(Diagnostics, FileMgr);
  Rewriter Rewrite(SM, LangOptions());

  if (!formatAndApplyAllReplacements(Tool.getReplacements(), Rewrite, Style)) {
    llvm::errs() << "Failed applying all replacements.\n";
    return 1;
  }

  if (Dump) {
    std::set<llvm::StringRef> Files;
    for (const auto &it : Tool.getReplacements())
      Files.insert(it.first);
    auto WriteToJson = [&](llvm::raw_ostream &OS) {
      OS << "[\n";
      for (auto I = Files.begin(), E = Files.end(); I != E; ++I) {
        OS << "  {\n";
        OS << "    \"FilePath\": \"" << *I << "\",\n";
        const auto Entry = FileMgr.getOptionalFileRef(*I);
        auto ID = SM.translateFile(*Entry);
        std::string Content;
        llvm::raw_string_ostream ContentStream(Content);
        Rewrite.getEditBuffer(ID).write(ContentStream);
        OS << "    \"SourceText\": \""
           << llvm::yaml::escape(ContentStream.str()) << "\"\n";
        OS << "  }";
        if (I != std::prev(E))
          OS << ",\n";
      }
      OS << "\n]\n";
    };
    WriteToJson(llvm::outs());
    return 0;
  }

  return Rewrite.overwriteChangedFiles();
}
