//===- Main.cpp - Top-Level TableGen implementation -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// TableGen is a tool which can be used to build up a description of something,
// then invoke one or more "tablegen backends" to emit information about the
// description in some predefined format. In practice, this is used by the LLVM
// code generators to automate generation of a code generator through a
// high-level description of the target.
//
//===----------------------------------------------------------------------===//

#include "llvm/TableGen/Main.h"
#include "TGLexer.h"
#include "TGParser.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/SMLoc.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TableGen/Error.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TGTimer.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <memory>
#include <string>
#include <system_error>
#include <utility>
using namespace llvm;

bool llvm::EmitLongStrLiterals = true;

static std::string OutputFilename = "-";
static std::string DependFilename;
static std::string InputFilename = "-";
static std::vector<std::string> IncludeDirs;
static std::vector<std::string> MacroNames;
static bool WriteIfChanged = false;
static bool TimePhases = false;
static bool NoWarnOnUnusedTemplateArgs = false;

static constexpr clv2::OptionInfo<std::string> OI_OutputFilename{
    "o", "Output filename", clv2::value_desc("filename"), clv2::Init{"-"}};
static constexpr clv2::OptionInfo<std::string> OI_DependFilename{
    "d", "Dependency filename", clv2::value_desc("filename")};
static constexpr clv2::OptionInfo<std::string> OI_InputFilename{
    "", "<input file>", clv2::Positional{}, clv2::Init{"-"}};
static constexpr clv2::ListOptionInfo<std::string> OI_IncludeDirs{
    "I",
    "Directory of include files",
    clv2::PrefixFormat,
    clv2::ZeroOrMore,
    clv2::ValueRequired,
    clv2::value_desc("directory")};
static constexpr clv2::ListOptionInfo<std::string> OI_MacroNames{
    "D",
    "Name of the macro to be defined",
    clv2::PrefixFormat,
    clv2::ZeroOrMore,
    clv2::ValueRequired,
    clv2::value_desc("macro name")};
static constexpr clv2::OptionInfo<bool> OI_WriteIfChanged{
    "write-if-changed", "Only write output if it changed"};
static constexpr clv2::OptionInfo<bool> OI_TimePhases{
    "time-phases", "Time phases of parser and backend"};
static constexpr clv2::OptionInfo<bool> OI_NoWarnOnUnusedTemplateArgs{
    "no-warn-on-unused-template-args",
    "Disable unused template argument warnings."};

static constexpr clv2::OptionsRegistry<
    &OI_OutputFilename, &OI_DependFilename, &OI_InputFilename, &OI_IncludeDirs,
    &OI_MacroNames, &OI_WriteIfChanged, &OI_TimePhases,
    &OI_NoWarnOnUnusedTemplateArgs>
    TGMainReg;

static void
applyTGMainOptions(const decltype(TGMainReg)::ParsedOptionsT &Opts) {
  OutputFilename = Opts.get<&OI_OutputFilename>();
  DependFilename = Opts.get<&OI_DependFilename>();
  InputFilename = Opts.get<&OI_InputFilename>();
  IncludeDirs = Opts.get<&OI_IncludeDirs>();
  MacroNames = Opts.get<&OI_MacroNames>();
  WriteIfChanged = Opts.get<&OI_WriteIfChanged>();
  TimePhases = Opts.get<&OI_TimePhases>();
  NoWarnOnUnusedTemplateArgs = Opts.get<&OI_NoWarnOnUnusedTemplateArgs>();
}

void llvm::registerTableGenMainOptions(clv2::OptionParser &P) {
  P.add<&TGMainReg, applyTGMainOptions>();
}

static int reportError(const char *ProgName, Twine Msg) {
  errs() << ProgName << ": " << Msg;
  errs().flush();
  return 1;
}

/// Create a dependency file for `-d` option.
///
/// This functionality is really only for the benefit of the build system.
/// It is similar to GCC's `-M*` family of options.
static int createDependencyFile(const TGParser &Parser, const char *argv0) {
  if (OutputFilename == "-")
    return reportError(argv0, "the option -d must be used together with -o\n");

  std::error_code EC;
  ToolOutputFile DepOut(DependFilename, EC, sys::fs::OF_Text);
  if (EC)
    return reportError(argv0, "error opening " + DependFilename + ":" +
                                  EC.message() + "\n");
  DepOut.os() << OutputFilename << ":";

  // Emit the primary input file as a dependency. This matches C compilers like
  // Clang and GCC. Without it, a .td file with no `include` directives would
  // produce a depfile listing zero dependencies. CMake's
  // `cmake_transform_depfile` then collapses that to a 0-byte file, which Ninja
  // treats as a missing depfile and re-runs the rule on every incremental
  // build.
  if (InputFilename != "-")
    DepOut.os() << ' ' << InputFilename;

  for (const auto &Dep : Parser.getDependencies()) {
    DepOut.os() << ' ' << Dep;
  }
  DepOut.os() << "\n";
  DepOut.keep();
  return 0;
}

static int WriteOutput(const char *argv0, StringRef Filename,
                       StringRef Content) {
  if (WriteIfChanged) {
    // Only updates the real output file if there are any differences.
    // This prevents recompilation of all the files depending on it if there
    // aren't any.
    if (auto ExistingOrErr = MemoryBuffer::getFile(Filename, /*IsText=*/true))
      if (std::move(ExistingOrErr.get())->getBuffer() == Content)
        return 0;
  }
  std::error_code EC;
  ToolOutputFile OutFile(Filename, EC, sys::fs::OF_Text);
  if (EC)
    return reportError(argv0, "error opening " + Filename + ": " +
                                  EC.message() + "\n");
  OutFile.os() << Content;
  if (ErrorsPrinted == 0)
    OutFile.keep();

  return 0;
}

int llvm::TableGenMain(const char *argv0, MultiFileTableGenMainFn MainFn) {
  RecordKeeper Records;
  TGTimer &Timer = Records.getTimer();

  if (TimePhases)
    Timer.startPhaseTiming();

  // Parse the input file.

  Timer.startTimer("Parse, build records");
  ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
      MemoryBuffer::getFileOrSTDIN(InputFilename, /*IsText=*/true);
  if (std::error_code EC = FileOrErr.getError())
    return reportError(argv0, "Could not open input file '" + InputFilename +
                                  "': " + EC.message() + "\n");

  Records.saveInputFilename(InputFilename);

  // Tell SrcMgr about this buffer, which is what TGParser will pick up.
  SrcMgr.AddNewSourceBuffer(std::move(*FileOrErr), SMLoc());

  // Record the location of the include directory so that the lexer can find
  // it later.
  SrcMgr.setIncludeDirs(IncludeDirs);
  SrcMgr.setVirtualFileSystem(vfs::getRealFileSystem());

  TGParser Parser(SrcMgr, MacroNames, Records, NoWarnOnUnusedTemplateArgs);

  if (Parser.ParseFile())
    return 1;
  Timer.stopTimer();

  // Return early if any other errors were generated during parsing
  // (e.g., assert failures).
  if (ErrorsPrinted > 0)
    return reportError(argv0, Twine(ErrorsPrinted) + " errors.\n");

  // Write output to memory.
  Timer.startBackendTimer("Backend overall");
  TableGenOutputFiles OutFiles;
  unsigned status = 0;
  // ApplyCallback will return true if it did not apply any callback. In that
  // case, attempt to apply the MainFn.
  StringRef FilenamePrefix(sys::path::stem(OutputFilename));
  if (TableGen::Emitter::ApplyCallback(Records, OutFiles, FilenamePrefix))
    status = MainFn ? MainFn(OutFiles, Records) : 1;
  Timer.stopBackendTimer();
  if (status)
    return 1;

  // Always write the depfile, even if the main output hasn't changed.
  // If it's missing, Ninja considers the output dirty. If this was below
  // the early exit below and someone deleted the .inc.d file but not the .inc
  // file, tablegen would never write the depfile.
  if (!DependFilename.empty()) {
    if (int Ret = createDependencyFile(Parser, argv0))
      return Ret;
  }

  Timer.startTimer("Write output");
  if (int Ret = WriteOutput(argv0, OutputFilename, OutFiles.MainFile))
    return Ret;
  for (auto [Suffix, Content] : OutFiles.AdditionalFiles) {
    SmallString<128> Filename(OutputFilename);
    // TODO: Format using the split-file convention when writing to stdout?
    if (Filename != "-") {
      sys::path::replace_extension(Filename, "");
      Filename.append(Suffix);
    }
    if (int Ret = WriteOutput(argv0, Filename, Content))
      return Ret;
  }

  Timer.stopTimer();
  Timer.stopPhaseTiming();

  if (ErrorsPrinted > 0)
    return reportError(argv0, Twine(ErrorsPrinted) + " errors.\n");
  return 0;
}

int llvm::TableGenMain(const char *argv0, TableGenMainFn MainFn) {
  return TableGenMain(argv0, [&MainFn](TableGenOutputFiles &OutFiles,
                                       const RecordKeeper &Records) {
    std::string S;
    raw_string_ostream OS(S);
    int Res = MainFn(OS, Records);
    OutFiles = {std::move(S), {}};
    return Res;
  });
}
