//===-- FindAllSymbolsMain.cpp - find all symbols tool ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "FindAllSymbolsAction.h"
#include "STLPostfixHeaderMap.h"
#include "SymbolInfo.h"
#include "SymbolReporter.h"
#include "clang-tools-extra/ClangToolsExtraOptionsOptInfos.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/ThreadPool.h"
#include "llvm/Support/raw_ostream.h"
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <system_error>
#include <vector>

using namespace clang::tooling;
using namespace llvm;
using SymbolInfo = clang::find_all_symbols::SymbolInfo;

// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static cl::OptionCategory FindAllSymbolsCategory("find_all_symbols options");

// CommonOptionsParser declares HelpMessage with a description of the common
// ExtraHelp is set via P.setExtraHelp() in configureParser below.

struct FindAllSymbolsOptions {
  std::string OutputDir = ".";
  std::string MergeDir;
};

inline constexpr clv2::OptionsRegistry<&clv2::CTE_FAS_OutputDir,
                                       &clv2::CTE_FAS_MergeDir>
    ToolOptsReg;

static void applyToolOpts(const decltype(ToolOptsReg)::ParsedOptionsT &Parsed,
                          FindAllSymbolsOptions &Opts) {
  Opts.OutputDir = Parsed.get<&clv2::CTE_FAS_OutputDir>();
  Opts.MergeDir = Parsed.get<&clv2::CTE_FAS_MergeDir>();
}

static void configureParser(clv2::OptionParser &P,
                            FindAllSymbolsOptions &Opts) {
  using ParsedT = decltype(ToolOptsReg)::ParsedOptionsT;
  auto *Storage = new ParsedT();
  decltype(ToolOptsReg)::applyDefaultsTo(*Storage);
  std::vector<clv2::detail::OptionEntry> Entries;
  std::vector<clv2::detail::AliasEntry> Aliases;
  std::vector<clv2::detail::SubCommandSpec> SubSpecs;
  decltype(ToolOptsReg)::staticBuildInto(*Storage, Entries, Aliases, SubSpecs);
  for (auto &E : Entries) {
    if (!E.Cat)
      E.Cat = &FindAllSymbolsCategory;
    P.addDynamicEntry(std::move(E));
  }
  clv2::registerDynamicPostParseCallback(
      [Storage, &Opts]() { applyToolOpts(*Storage, Opts); });
  std::string FullHelp = clang::tooling::CommonOptionsParser::HelpMessage;
  FullHelp += "\nMore help text...";
  P.setExtraHelp(FullHelp);
}

namespace clang {
namespace find_all_symbols {

class YamlReporter : public SymbolReporter {
public:
  YamlReporter(const std::string &OutputDir) : OutputDir(OutputDir) {}

  void reportSymbols(StringRef FileName,
                     const SymbolInfo::SignalMap &Symbols) override {
    int FD;
    SmallString<128> ResultPath;
    llvm::sys::fs::createUniqueFile(
        OutputDir + "/" + llvm::sys::path::filename(FileName) + "-%%%%%%.yaml",
        FD, ResultPath);
    llvm::raw_fd_ostream OS(FD, /*shouldClose=*/true);
    WriteSymbolInfosToStream(OS, Symbols);
  }

private:
  const std::string &OutputDir;
};

bool Merge(llvm::StringRef MergeDir, llvm::StringRef OutputFile) {
  std::error_code EC;
  SymbolInfo::SignalMap Symbols;
  std::mutex SymbolMutex;
  auto AddSymbols = [&](ArrayRef<SymbolAndSignals> NewSymbols) {
    // Synchronize set accesses.
    std::unique_lock<std::mutex> LockGuard(SymbolMutex);
    for (const auto &Symbol : NewSymbols) {
      Symbols[Symbol.Symbol] += Symbol.Signals;
    }
  };

  // Load all symbol files in MergeDir.
  {
    llvm::DefaultThreadPool Pool;
    for (llvm::sys::fs::directory_iterator Dir(MergeDir, EC), DirEnd;
         Dir != DirEnd && !EC; Dir.increment(EC)) {
      // Parse YAML files in parallel.
      Pool.async(
          [&AddSymbols](std::string Path) {
            auto Buffer = llvm::MemoryBuffer::getFile(Path, /*IsText=*/true);
            if (!Buffer) {
              llvm::errs() << "Can't open " << Path << "\n";
              return;
            }
            std::vector<SymbolAndSignals> Symbols =
                ReadSymbolInfosFromYAML(Buffer.get()->getBuffer());
            for (auto &Symbol : Symbols) {
              // Only count one occurrence per file, to avoid spam.
              Symbol.Signals.Seen = std::min(Symbol.Signals.Seen, 1u);
              Symbol.Signals.Used = std::min(Symbol.Signals.Used, 1u);
            }
            // FIXME: Merge without creating such a heavy contention point.
            AddSymbols(Symbols);
          },
          Dir->path());
    }
  }

  llvm::raw_fd_ostream OS(OutputFile, EC, llvm::sys::fs::OF_Text);
  if (EC) {
    llvm::errs() << "Can't open '" << OutputFile << "': " << EC.message()
                 << '\n';
    return false;
  }
  WriteSymbolInfosToStream(OS, Symbols);
  return true;
}

} // namespace clang
} // namespace find_all_symbols

int main(int argc, const char **argv) {
  FindAllSymbolsOptions Opts;
  auto ExpectedParser = CommonOptionsParser::create(
      argc, argv, FindAllSymbolsCategory,
      [&Opts](clv2::OptionParser &P) { configureParser(P, Opts); });
  if (!ExpectedParser) {
    llvm::errs() << llvm::toString(ExpectedParser.takeError());
    return 1;
  }

  CommonOptionsParser &OptionsParser = ExpectedParser.get();
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());

  std::vector<std::string> sources = OptionsParser.getSourcePathList();
  if (sources.empty()) {
    llvm::errs() << "Must specify at least one one source file.\n";
    return 1;
  }
  if (!Opts.MergeDir.empty()) {
    clang::find_all_symbols::Merge(Opts.MergeDir, sources[0]);
    return 0;
  }

  clang::find_all_symbols::YamlReporter Reporter(Opts.OutputDir);

  auto Factory =
      std::make_unique<clang::find_all_symbols::FindAllSymbolsActionFactory>(
          &Reporter, clang::find_all_symbols::getSTLPostfixHeaderMap());
  return Tool.run(Factory.get());
}
