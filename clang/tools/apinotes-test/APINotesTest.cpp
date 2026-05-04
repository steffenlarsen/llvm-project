//===-- APINotesTest.cpp - API Notes Testing Tool ------------------ C++ --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/APINotes/APINotesYAMLCompiler.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"

inline constexpr llvm::clv2::ListOptionInfo<std::string> ANApiNotesOpt{
    "", "[<apinotes> ...]", llvm::clv2::Positional{}, llvm::clv2::OneOrMore};

inline constexpr llvm::clv2::OptionInfo<std::string> ANOutputOpt{
    "o", "output filename", llvm::clv2::value_desc("filename"),
    llvm::clv2::Init{"-"}};

inline constexpr llvm::clv2::OptionsRegistry<&ANApiNotesOpt, &ANOutputOpt>
    APINotesTestReg;

static std::vector<std::string> APINotes;
static std::string OutputFileName = "-";

int main(int argc, const char **argv) {
  const bool DisableCrashReporting = true;
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0], DisableCrashReporting);
  llvm::clv2::OptionParser P;
  P.add<&APINotesTestReg>();
  auto OptsCtx = P.parse(argc, argv);
  auto *Opts = OptsCtx->getViewPtr<&APINotesTestReg>();

  APINotes = Opts->get<&ANApiNotesOpt>();
  OutputFileName = Opts->get<&ANOutputOpt>();

  auto Error = [](const llvm::Twine &Msg) {
    llvm::WithColor::error(llvm::errs(), "apinotes-test") << Msg << '\n';
  };

  std::error_code EC;
  auto Out = std::make_unique<llvm::ToolOutputFile>(OutputFileName, EC,
                                                    llvm::sys::fs::OF_None);
  if (EC) {
    Error("failed to open '" + OutputFileName + "': " + EC.message());
    return EXIT_FAILURE;
  }

  for (const std::string &Notes : APINotes) {
    llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> NotesOrError =
        llvm::MemoryBuffer::getFileOrSTDIN(Notes);
    if (std::error_code EC = NotesOrError.getError()) {
      llvm::errs() << EC.message() << '\n';
      return EXIT_FAILURE;
    }

    clang::api_notes::parseAndDumpAPINotes((*NotesOrError)->getBuffer(),
                                           Out->os());
  }

  return EXIT_SUCCESS;
}
