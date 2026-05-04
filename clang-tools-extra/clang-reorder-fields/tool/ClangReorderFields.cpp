//===-- tools/extra/clang-reorder-fields/tool/ClangReorderFields.cpp -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the implementation of clang-reorder-fields tool
///
//===----------------------------------------------------------------------===//

#include "../ReorderFieldsAction.h"
#include "clang-tools-extra/ClangToolsExtraOptionsOptInfos.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Refactoring.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/FileSystem.h"
#include <cstdlib>
#include <string>
#include <system_error>

using namespace llvm;
using namespace clang;

cl::OptionCategory ClangReorderFieldsCategory("clang-reorder-fields options");

struct ReorderFieldsOptions {
  std::string RecordName;
  std::vector<std::string> FieldsOrder;
  bool Inplace = false;
};

inline constexpr clv2::OptionsRegistry<
    &clv2::CTE_RF_RecordName, &clv2::CTE_RF_FieldsOrder, &clv2::CTE_RF_Inplace>
    ReorderFieldsOptsReg;

static void applyReorderFieldsOpts(
    const decltype(ReorderFieldsOptsReg)::ParsedOptionsT &Opts,
    ReorderFieldsOptions &ToolOpts) {
  ToolOpts.RecordName = Opts.get<&clv2::CTE_RF_RecordName>();
  ToolOpts.FieldsOrder = Opts.get<&clv2::CTE_RF_FieldsOrder>();
  ToolOpts.Inplace = Opts.get<&clv2::CTE_RF_Inplace>();
}

static void configureParser(clv2::OptionParser &P,
                            ReorderFieldsOptions &ToolOpts) {
  using ParsedT = decltype(ReorderFieldsOptsReg)::ParsedOptionsT;
  auto *Storage = new ParsedT();
  decltype(ReorderFieldsOptsReg)::applyDefaultsTo(*Storage);
  std::vector<clv2::detail::OptionEntry> Entries;
  std::vector<clv2::detail::AliasEntry> Aliases;
  std::vector<clv2::detail::SubCommandSpec> SubSpecs;
  decltype(ReorderFieldsOptsReg)::staticBuildInto(*Storage, Entries, Aliases,
                                                  SubSpecs);
  for (auto &E : Entries) {
    if (!E.Cat)
      E.Cat = &ClangReorderFieldsCategory;
    P.addDynamicEntry(std::move(E));
  }
  clv2::registerDynamicPostParseCallback(
      [Storage, &ToolOpts]() { applyReorderFieldsOpts(*Storage, ToolOpts); });
}

const char Usage[] = "A tool to reorder fields in C/C++ structs/classes.\n";

int main(int argc, const char **argv) {
  ReorderFieldsOptions ToolOpts;
  auto ExpectedParser = tooling::CommonOptionsParser::create(
      argc, argv, ClangReorderFieldsCategory,
      [&ToolOpts](clv2::OptionParser &P) { configureParser(P, ToolOpts); },
      cl::OneOrMore, Usage);
  if (!ExpectedParser) {
    llvm::errs() << llvm::toString(ExpectedParser.takeError());
    return 1;
  }

  tooling::CommonOptionsParser &OP = ExpectedParser.get();

  auto Files = OP.getSourcePathList();
  tooling::RefactoringTool Tool(OP.getCompilations(), Files);

  reorder_fields::ReorderFieldsAction Action(
      ToolOpts.RecordName, ToolOpts.FieldsOrder, Tool.getReplacements());

  auto Factory = tooling::newFrontendActionFactory(&Action);

  if (ToolOpts.Inplace)
    return Tool.runAndSave(Factory.get());

  int ExitCode = Tool.run(Factory.get());
  LangOptions DefaultLangOptions;
  DiagnosticOptions DiagOpts;
  TextDiagnosticPrinter DiagnosticPrinter(errs(), DiagOpts);
  DiagnosticsEngine Diagnostics(DiagnosticIDs::create(), DiagOpts,
                                &DiagnosticPrinter, false);

  auto &FileMgr = Tool.getFiles();
  SourceManager Sources(Diagnostics, FileMgr);
  Rewriter Rewrite(Sources, DefaultLangOptions);
  Tool.applyAllReplacements(Rewrite);

  for (const auto &File : Files) {
    auto Entry = llvm::cantFail(FileMgr.getFileRef(File));
    const auto ID = Sources.getOrCreateFileID(Entry, SrcMgr::C_User);
    Rewrite.getEditBuffer(ID).write(outs());
  }

  return ExitCode;
}
