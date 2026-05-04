//===-- ClangApplyReplacementsMain.cpp - Main file for the tool -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides the main function for the
/// clang-apply-replacements tool.
///
//===----------------------------------------------------------------------===//

#include "clang-apply-replacements/Tooling/ApplyReplacements.h"
#include "clang-tools-extra/ClangToolsExtraOptionsOptInfos.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Version.h"
#include "clang/Format/Format.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/RegisterLLVMOptions.h"

using namespace llvm;
using namespace clang;
using namespace clang::replace;

static cl::OptionCategory ReplacementCategory("Replacement Options");
static cl::OptionCategory FormattingCategory("Formatting Options");

const cl::OptionCategory *VisibleCategories[] = {&ReplacementCategory,
                                                 &FormattingCategory};

struct ApplyReplacementsOptions {
  std::string Directory;
  bool RemoveTUReplacementFiles = false;
  bool IgnoreInsertConflict = false;
  bool DoFormat = false;
  std::string FormatStyleConfig = "";
  std::string FormatStyleOpt = "LLVM";
};

inline constexpr clv2::OptionsRegistry<
    &clv2::CTE_AR_Directory, &clv2::CTE_AR_RemoveChangeDescFiles,
    &clv2::CTE_AR_IgnoreInsertConflict, &clv2::CTE_AR_Format,
    &clv2::CTE_AR_StyleConfig, &clv2::CTE_AR_Style>
    ApplyReplacementsOptsReg;

static void applyApplyReplacementsOpts(
    const decltype(ApplyReplacementsOptsReg)::ParsedOptionsT &Opts,
    ApplyReplacementsOptions &ToolOpts) {
  ToolOpts.Directory = Opts.get<&clv2::CTE_AR_Directory>();
  ToolOpts.RemoveTUReplacementFiles =
      Opts.get<&clv2::CTE_AR_RemoveChangeDescFiles>();
  ToolOpts.IgnoreInsertConflict =
      Opts.get<&clv2::CTE_AR_IgnoreInsertConflict>();
  ToolOpts.DoFormat = Opts.get<&clv2::CTE_AR_Format>();
  ToolOpts.FormatStyleConfig = Opts.get<&clv2::CTE_AR_StyleConfig>();
  ToolOpts.FormatStyleOpt = Opts.get<&clv2::CTE_AR_Style>();
}

namespace {
// Helper object to remove the TUReplacement and TUDiagnostic (triggered by
// "remove-change-desc-files" command line option) when exiting current scope.
class ScopedFileRemover {
public:
  ScopedFileRemover(const TUReplacementFiles &Files,
                    clang::DiagnosticsEngine &Diagnostics)
      : TURFiles(Files), Diag(Diagnostics) {}

  ~ScopedFileRemover() { deleteReplacementFiles(TURFiles, Diag); }

private:
  const TUReplacementFiles &TURFiles;
  clang::DiagnosticsEngine &Diag;
};
} // namespace

static void printVersion(raw_ostream &OS) {
  OS << "clang-apply-replacements version " CLANG_VERSION_STRING << "\n";
}

int main(int argc, char **argv) {
  cl::SetVersionPrinter(printVersion);
  ApplyReplacementsOptions ToolOpts;
  clv2::OptionParser P;
  RegisterAllLLVMOptions(P);
  {
    using ParsedT = decltype(ApplyReplacementsOptsReg)::ParsedOptionsT;
    auto *Storage = new ParsedT();
    decltype(ApplyReplacementsOptsReg)::applyDefaultsTo(*Storage);
    std::vector<clv2::detail::OptionEntry> Entries;
    std::vector<clv2::detail::AliasEntry> Aliases;
    std::vector<clv2::detail::SubCommandSpec> SubSpecs;
    decltype(ApplyReplacementsOptsReg)::staticBuildInto(*Storage, Entries,
                                                        Aliases, SubSpecs);
    for (auto &E : Entries) {
      if (!E.Cat) {
        // Assign option categories.
        StringRef Name(E.name());
        if (Name == "format" || Name == "style-config" || Name == "style")
          E.Cat = &FormattingCategory;
        else
          E.Cat = &ReplacementCategory;
      }
      P.addDynamicEntry(std::move(E));
    }
    clv2::registerDynamicPostParseCallback([Storage, &ToolOpts]() {
      applyApplyReplacementsOpts(*Storage, ToolOpts);
    });
  }
  P.hideUnrelatedOptions(ArrayRef(VisibleCategories));
  P.parse(argc, argv);

  DiagnosticOptions DiagOpts;
  DiagnosticsEngine Diagnostics(DiagnosticIDs::create(), DiagOpts);

  // Determine a formatting style from options.
  auto FormatStyleOrError =
      format::getStyle(ToolOpts.FormatStyleOpt, ToolOpts.FormatStyleConfig,
                       format::DefaultFallbackStyle);
  if (!FormatStyleOrError) {
    llvm::errs() << llvm::toString(FormatStyleOrError.takeError()) << "\n";
    return 1;
  }
  format::FormatStyle FormatStyle = std::move(*FormatStyleOrError);

  TUReplacements TURs;
  TUReplacementFiles TUFiles;

  std::error_code ErrorCode = collectReplacementsFromDirectory(
      ToolOpts.Directory, TURs, TUFiles, Diagnostics);

  TUDiagnostics TUDs;
  TUFiles.clear();
  ErrorCode = collectReplacementsFromDirectory(ToolOpts.Directory, TUDs,
                                               TUFiles, Diagnostics);

  if (ErrorCode) {
    errs() << "Trouble iterating over directory '" << ToolOpts.Directory
           << "': " << ErrorCode.message() << "\n";
    return 1;
  }

  // Remove the TUReplacementFiles (triggered by "remove-change-desc-files"
  // command line option) when exiting main().
  std::unique_ptr<ScopedFileRemover> Remover;
  if (ToolOpts.RemoveTUReplacementFiles)
    Remover.reset(new ScopedFileRemover(TUFiles, Diagnostics));

  FileManager Files((FileSystemOptions()));
  SourceManager SM(Diagnostics, Files);

  FileToChangesMap Changes;
  if (!mergeAndDeduplicate(TURs, TUDs, Changes, SM,
                           ToolOpts.IgnoreInsertConflict))
    return 1;

  tooling::ApplyChangesSpec Spec;
  Spec.Cleanup = ToolOpts.DoFormat;
  Spec.Format = ToolOpts.DoFormat ? tooling::ApplyChangesSpec::kAll
                                  : tooling::ApplyChangesSpec::kNone;
  Spec.Style = ToolOpts.DoFormat ? FormatStyle : format::getNoStyle();

  for (const auto &FileChange : Changes) {
    FileEntryRef Entry = FileChange.first;
    StringRef FileName = Entry.getName();
    llvm::Expected<std::string> NewFileData =
        applyChanges(FileName, FileChange.second, Spec, Diagnostics);
    if (!NewFileData) {
      errs() << llvm::toString(NewFileData.takeError()) << "\n";
      continue;
    }

    // Write new file to disk
    std::error_code EC;
    llvm::raw_fd_ostream FileStream(FileName, EC, llvm::sys::fs::OF_None);
    if (EC) {
      llvm::errs() << "Could not open " << FileName << " for writing\n";
      continue;
    }
    FileStream << *NewFileData;
  }

  return 0;
}
