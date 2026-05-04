//===-- ClangChangeNamespace.cpp - Standalone change namespace ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This tool can be used to change the surrounding namespaces of class/function
// definitions.
//
// Example: test.cc
//    namespace na {
//    class X {};
//    namespace nb {
//    class Y { X x; };
//    } // namespace nb
//    } // namespace na
// To move the definition of class Y from namespace "na::nb" to "x::y", run:
//    clang-change-namespace --old_namespace "na::nb" \
//      --new_namespace "x::y" --file_pattern "test.cc" test.cc --
// Output:
//    namespace na {
//    class X {};
//    } // namespace na
//    namespace x {
//    namespace y {
//    class Y { na::X x; };
//    } // namespace y
//    } // namespace x

#include "ChangeNamespace.h"
#include "clang-tools-extra/ClangToolsExtraOptionsOptInfos.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/YAMLTraits.h"

using namespace clang;
using namespace llvm;

static cl::OptionCategory ChangeNamespaceCategory("Change namespace.");

struct ChangeNamespaceOptions {
  std::string OldNamespace;
  std::string NewNamespace;
  std::string FilePattern;
  bool Inplace = false;
  bool DumpYAML = false;
  std::string Style = "LLVM";
  std::string AllowedFile;
};

inline constexpr clv2::OptionsRegistry<
    &clv2::CTE_CN_OldNamespace, &clv2::CTE_CN_NewNamespace,
    &clv2::CTE_CN_FilePattern, &clv2::CTE_CN_Inplace, &clv2::CTE_CN_DumpResult,
    &clv2::CTE_CN_Style, &clv2::CTE_CN_AllowedFile>
    ToolOptsReg;

static void applyToolOpts(const decltype(ToolOptsReg)::ParsedOptionsT &Opts,
                          ChangeNamespaceOptions &ToolOpts) {
  ToolOpts.OldNamespace = Opts.get<&clv2::CTE_CN_OldNamespace>();
  ToolOpts.NewNamespace = Opts.get<&clv2::CTE_CN_NewNamespace>();
  ToolOpts.FilePattern = Opts.get<&clv2::CTE_CN_FilePattern>();
  ToolOpts.Inplace = Opts.get<&clv2::CTE_CN_Inplace>();
  ToolOpts.DumpYAML = Opts.get<&clv2::CTE_CN_DumpResult>();
  ToolOpts.Style = Opts.get<&clv2::CTE_CN_Style>();
  ToolOpts.AllowedFile = Opts.get<&clv2::CTE_CN_AllowedFile>();
}

static void configureParser(clv2::OptionParser &P,
                            ChangeNamespaceOptions &ToolOpts) {
  using ParsedT = decltype(ToolOptsReg)::ParsedOptionsT;
  auto *Storage = new ParsedT();
  decltype(ToolOptsReg)::applyDefaultsTo(*Storage);
  std::vector<clv2::detail::OptionEntry> Entries;
  std::vector<clv2::detail::AliasEntry> Aliases;
  std::vector<clv2::detail::SubCommandSpec> SubSpecs;
  decltype(ToolOptsReg)::staticBuildInto(*Storage, Entries, Aliases, SubSpecs);
  for (auto &E : Entries) {
    if (!E.Cat)
      E.Cat = &ChangeNamespaceCategory;
    P.addDynamicEntry(std::move(E));
  }
  clv2::registerDynamicPostParseCallback(
      [Storage, &ToolOpts]() { applyToolOpts(*Storage, ToolOpts); });
}

namespace {

llvm::ErrorOr<std::vector<std::string>>
GetAllowedSymbolPatterns(const std::string &AllowedFile) {
  std::vector<std::string> Patterns;
  if (AllowedFile.empty())
    return Patterns;

  llvm::SmallVector<StringRef, 8> Lines;
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> File =
      llvm::MemoryBuffer::getFile(AllowedFile);
  if (!File)
    return File.getError();
  llvm::StringRef Content = File.get()->getBuffer();
  Content.split(Lines, '\n', /*MaxSplit=*/-1, /*KeepEmpty=*/false);
  for (auto Line : Lines)
    Patterns.push_back(std::string(Line.trim()));
  return Patterns;
}

} // anonymous namespace

int main(int argc, const char **argv) {
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);
  ChangeNamespaceOptions ToolOpts;
  auto ExpectedParser = tooling::CommonOptionsParser::create(
      argc, argv, ChangeNamespaceCategory,
      [&ToolOpts](llvm::clv2::OptionParser &P) {
        configureParser(P, ToolOpts);
      });
  if (!ExpectedParser) {
    llvm::errs() << llvm::toString(ExpectedParser.takeError());
    return 1;
  }
  tooling::CommonOptionsParser &OptionsParser = ExpectedParser.get();
  const auto &Files = OptionsParser.getSourcePathList();
  tooling::RefactoringTool Tool(OptionsParser.getCompilations(), Files);
  llvm::ErrorOr<std::vector<std::string>> AllowedPatterns =
      GetAllowedSymbolPatterns(ToolOpts.AllowedFile);
  if (!AllowedPatterns) {
    llvm::errs() << "Failed to open allow file " << ToolOpts.AllowedFile << ". "
                 << AllowedPatterns.getError().message() << "\n";
    return 1;
  }
  change_namespace::ChangeNamespaceTool NamespaceTool(
      ToolOpts.OldNamespace, ToolOpts.NewNamespace, ToolOpts.FilePattern,
      *AllowedPatterns, &Tool.getReplacements(), ToolOpts.Style);
  ast_matchers::MatchFinder Finder;
  NamespaceTool.registerMatchers(&Finder);
  std::unique_ptr<tooling::FrontendActionFactory> Factory =
      tooling::newFrontendActionFactory(&Finder);

  if (int Result = Tool.run(Factory.get()))
    return Result;
  LangOptions DefaultLangOptions;
  DiagnosticOptions DiagOpts;
  clang::TextDiagnosticPrinter DiagnosticPrinter(errs(), DiagOpts);
  DiagnosticsEngine Diagnostics(DiagnosticIDs::create(), DiagOpts,
                                &DiagnosticPrinter, false);
  auto &FileMgr = Tool.getFiles();
  SourceManager Sources(Diagnostics, FileMgr);
  Rewriter Rewrite(Sources, DefaultLangOptions);

  if (!formatAndApplyAllReplacements(Tool.getReplacements(), Rewrite,
                                     ToolOpts.Style)) {
    llvm::errs() << "Failed applying all replacements.\n";
    return 1;
  }
  if (ToolOpts.Inplace)
    return Rewrite.overwriteChangedFiles();

  std::set<llvm::StringRef> ChangedFiles;
  for (const auto &it : Tool.getReplacements())
    ChangedFiles.insert(it.first);

  if (ToolOpts.DumpYAML) {
    auto WriteToYAML = [&](llvm::raw_ostream &OS) {
      OS << "[\n";
      for (auto I = ChangedFiles.begin(), E = ChangedFiles.end(); I != E; ++I) {
        OS << "  {\n";
        OS << "    \"FilePath\": \"" << *I << "\",\n";
        auto Entry = llvm::cantFail(FileMgr.getFileRef(*I));
        auto ID = Sources.getOrCreateFileID(Entry, SrcMgr::C_User);
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
    WriteToYAML(llvm::outs());
    return 0;
  }

  for (const auto &File : ChangedFiles) {
    auto Entry = llvm::cantFail(FileMgr.getFileRef(File));

    auto ID = Sources.getOrCreateFileID(Entry, SrcMgr::C_User);
    outs() << "============== " << File << " ==============\n";
    Rewrite.getEditBuffer(ID).write(llvm::outs());
    outs() << "\n============================================\n";
  }

  return 0;
}
