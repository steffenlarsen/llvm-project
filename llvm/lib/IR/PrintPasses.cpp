//===- PrintPasses.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/PrintPasses.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IROptionsOptInfos.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/IOSandbox.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include <vector>

using namespace llvm;

static const ir_opts::ParsedOpts *getOpts(const LLVMContext &Ctx) {
  return clv2::getView<&clv2::IROptsReg>(Ctx.getOptionsContext());
}

static const ir_opts::ParsedOpts *getOpts(const clv2::OptionsContext &Ctx) {
  return clv2::getView<&clv2::IROptsReg>(Ctx);
}

static std::vector<std::string> getPrintBefore(const ir_opts::ParsedOpts *O) {
  if (O)
    return O->get<&clv2::IR_PrintBefore>();
  return {};
}

static std::vector<std::string> getPrintAfter(const ir_opts::ParsedOpts *O) {
  if (O)
    return O->get<&clv2::IR_PrintAfter>();
  return {};
}

static bool getPrintBeforeAll(const ir_opts::ParsedOpts *O) {
  if (O)
    return O->get<&clv2::IR_PrintBeforeAll>();
  return false;
}

static bool getPrintAfterAll(const ir_opts::ParsedOpts *O) {
  if (O)
    return O->get<&clv2::IR_PrintAfterAll>();
  return false;
}

ChangePrinter llvm::getPrintChanged(const LLVMContext &Ctx) {
  if (auto *O = getOpts(Ctx))
    return O->get<&clv2::IR_PrintChanged>();
  return ChangePrinter::None;
}

static std::string getDiffBinary(const ir_opts::ParsedOpts *O) {
  if (O)
    return O->get<&clv2::IR_DiffBinary>();
  return "diff";
}

static bool getPrintModuleScope(const ir_opts::ParsedOpts *O) {
  if (O)
    return O->get<&clv2::IR_PrintModuleScope>();
  return false;
}

static ChangePrinter getPrintChangedVal(const ir_opts::ParsedOpts *O) {
  if (O)
    return O->get<&clv2::IR_PrintChanged>();
  return ChangePrinter::None;
}

static bool getLoopPrintFuncScope(const ir_opts::ParsedOpts *O) {
  if (O)
    return O->get<&clv2::IR_LoopPrintFuncScope>();
  return false;
}

static std::vector<std::string> getFilterPasses(const ir_opts::ParsedOpts *O) {
  if (O)
    return O->get<&clv2::IR_FilterPasses>();
  return {};
}

static std::vector<std::string>
getPrintFuncsList(const ir_opts::ParsedOpts *O) {
  if (O)
    return O->get<&clv2::IR_PrintFuncsList>();
  return {};
}

static std::vector<std::string>
getPrintSourceLocs(const ir_opts::ParsedOpts *O) {
  if (O)
    return O->get<&clv2::IR_PrintSourceLocs>();
  return {};
}

static bool isFunctionInPrintList(const ir_opts::ParsedOpts *O,
                                  StringRef FunctionName) {
  auto PFL = getPrintFuncsList(O);
  return PFL.empty() || llvm::is_contained(PFL, FunctionName) ||
         llvm::is_contained(PFL, "*");
}

static bool shouldPrintBeforeOrAfterPass(StringRef PassID,
                                         ArrayRef<std::string> PassesToPrint) {
  return llvm::is_contained(PassesToPrint, PassID);
}

bool llvm::shouldPrintBeforeSomePass(const LLVMContext &Ctx) {
  auto *O = getOpts(Ctx);
  return getPrintBeforeAll(O) || !getPrintBefore(O).empty();
}

bool llvm::shouldPrintAfterSomePass(const LLVMContext &Ctx) {
  auto *O = getOpts(Ctx);
  return getPrintAfterAll(O) || !getPrintAfter(O).empty();
}

bool llvm::shouldPrintBeforeAll(const LLVMContext &Ctx) {
  return getPrintBeforeAll(getOpts(Ctx));
}

bool llvm::shouldPrintAfterAll(const LLVMContext &Ctx) {
  return getPrintAfterAll(getOpts(Ctx));
}

bool llvm::shouldPrintBeforePass(const LLVMContext &Ctx, StringRef PassID) {
  auto *O = getOpts(Ctx);
  return getPrintBeforeAll(O) ||
         shouldPrintBeforeOrAfterPass(PassID, getPrintBefore(O));
}

bool llvm::shouldPrintAfterPass(const LLVMContext &Ctx, StringRef PassID) {
  auto *O = getOpts(Ctx);
  return getPrintAfterAll(O) ||
         shouldPrintBeforeOrAfterPass(PassID, getPrintAfter(O));
}

// Overloads without LLVMContext for legacy pass manager use.
// These build an OptionsContext from CLI args each call; acceptable since
// they're only invoked during pass scheduling, not in hot loops.
bool llvm::shouldPrintBeforePass(StringRef PassID) {
  return shouldPrintBeforePass(PassID, clv2::defaultOptionsContext());
}

bool llvm::shouldPrintAfterPass(StringRef PassID) {
  return shouldPrintAfterPass(PassID, clv2::defaultOptionsContext());
}

bool llvm::shouldPrintBeforePass(StringRef PassID,
                                 const clv2::OptionsContext &Ctx) {
  auto *O = clv2::getView<&clv2::IROptsReg>(Ctx);
  return getPrintBeforeAll(O) ||
         shouldPrintBeforeOrAfterPass(PassID, getPrintBefore(O));
}

bool llvm::shouldPrintAfterPass(StringRef PassID,
                                const clv2::OptionsContext &Ctx) {
  auto *O = clv2::getView<&clv2::IROptsReg>(Ctx);
  return getPrintAfterAll(O) ||
         shouldPrintBeforeOrAfterPass(PassID, getPrintAfter(O));
}

std::vector<std::string> llvm::printBeforePasses(const LLVMContext &Ctx) {
  return getPrintBefore(getOpts(Ctx));
}

std::vector<std::string> llvm::printAfterPasses(const LLVMContext &Ctx) {
  return getPrintAfter(getOpts(Ctx));
}

bool llvm::forcePrintModuleIR(const LLVMContext &Ctx) {
  return getPrintModuleScope(getOpts(Ctx));
}

bool llvm::forcePrintFuncIR(const LLVMContext &Ctx) {
  return getLoopPrintFuncScope(getOpts(Ctx));
}

bool llvm::isPassInPrintList(const LLVMContext &Ctx, StringRef PassName) {
  auto FP = getFilterPasses(getOpts(Ctx));
  return FP.empty() || llvm::is_contained(FP, PassName);
}

bool llvm::isFilterPassesEmpty(const LLVMContext &Ctx) {
  return getFilterPasses(getOpts(Ctx)).empty();
}

bool llvm::isFunctionInPrintList(const LLVMContext &Ctx,
                                 StringRef FunctionName) {
  return ::isFunctionInPrintList(getOpts(Ctx), FunctionName);
}

namespace {

struct PrintLineRange {
  unsigned First;
  unsigned Last;
};

struct PrintSourceLocFilter {
  std::string File;
  SmallVector<PrintLineRange, 4> Lines;
};

[[noreturn]] void reportBadSourceLocFilter(StringRef Filter) {
  report_fatal_error(Twine("Invalid -filter-print-source-locs value '") +
                     Filter + "'. Expected file:line[,line-line][,line].");
}

std::string normalizeSlashes(StringRef Path) {
  return sys::path::convert_to_slash(Path, sys::path::Style::windows_backslash);
}

bool parseLineNumber(StringRef LineText, unsigned &Line) {
  return !LineText.empty() && !LineText.getAsInteger(10, Line);
}

PrintLineRange parseLineRange(StringRef RangeText, StringRef FullFilter) {
  auto [FirstText, LastText] = RangeText.split('-');

  unsigned First;
  if (!parseLineNumber(FirstText, First))
    reportBadSourceLocFilter(FullFilter);

  if (!RangeText.contains('-'))
    return {First, First};

  unsigned Last;
  if (!parseLineNumber(LastText, Last) || Last < First)
    reportBadSourceLocFilter(FullFilter);

  return {First, Last};
}

std::vector<PrintSourceLocFilter>
parseSourceLocFilters(ArrayRef<std::string> RawFilters) {
  std::vector<PrintSourceLocFilter> Result;
  for (const std::string &RawFilter : RawFilters) {
    StringRef Filter(RawFilter);
    auto [File, LineList] = Filter.rsplit(':');
    if (File.empty() || LineList.empty())
      reportBadSourceLocFilter(Filter);

    PrintSourceLocFilter Parsed;
    Parsed.File = normalizeSlashes(File);
    for (StringRef RangeText : llvm::split(LineList, ",")) {
      Parsed.Lines.push_back(parseLineRange(RangeText, Filter));
    }
    Result.push_back(std::move(Parsed));
  }
  return Result;
}

std::string makeDebugLocPath(StringRef Directory, StringRef Filename) {
  std::string NormalizedFilename = normalizeSlashes(Filename);
  if (Directory.empty() || sys::path::is_absolute(NormalizedFilename))
    return NormalizedFilename;

  std::string NormalizedDirectory = normalizeSlashes(Directory);
  if (NormalizedDirectory.empty())
    return NormalizedFilename;
  if (NormalizedDirectory.back() == '/')
    return NormalizedDirectory + NormalizedFilename;
  return NormalizedDirectory + "/" + NormalizedFilename;
}

bool matchesFile(StringRef FilterFile, StringRef Directory,
                 StringRef Filename) {
  std::string LocFile = normalizeSlashes(Filename);
  std::string LocPath = makeDebugLocPath(Directory, Filename);

  // Accept an exact filename or path, a basename, or a path suffix so the
  // filter may omit leading directories.
  if (FilterFile == LocFile || FilterFile == LocPath)
    return true;

  StringRef LocFileRef(LocFile);
  StringRef LocPathRef(LocPath);
  if (sys::path::filename(LocFileRef) == FilterFile)
    return true;

  std::string Suffix = (Twine("/") + FilterFile).str();
  return LocFileRef.ends_with(Suffix) || LocPathRef.ends_with(Suffix);
}

bool matchesLine(ArrayRef<PrintLineRange> Ranges, unsigned Line) {
  return any_of(Ranges, [Line](const PrintLineRange &Range) {
    return Range.First <= Line && Line <= Range.Last;
  });
}

bool matchesSourceLocFilter(const DebugLoc &Loc,
                            const PrintSourceLocFilter &Filter) {
  auto *Scope = dyn_cast_or_null<DIScope>(Loc.getScope());
  return Scope &&
         matchesFile(Filter.File, Scope->getDirectory(),
                     Scope->getFilename()) &&
         matchesLine(Filter.Lines, Loc.getLine());
}

bool locMatchesFilters(ArrayRef<PrintSourceLocFilter> Filters,
                       const DebugLoc &Loc) {
  if (Filters.empty())
    return true;

  for (DebugLoc CurLoc = Loc; CurLoc; CurLoc = CurLoc.getInlinedAt()) {
    if (any_of(Filters, [&CurLoc](const PrintSourceLocFilter &Filter) {
          return matchesSourceLocFilter(CurLoc, Filter);
        }))
      return true;
  }
  return false;
}

} // namespace

static std::vector<PrintSourceLocFilter>
getSourceLocFilters(const ir_opts::ParsedOpts *O) {
  return parseSourceLocFilters(getPrintSourceLocs(O));
}

bool llvm::isSourceLocInPrintList(const LLVMContext &Ctx, const DebugLoc &Loc) {
  return locMatchesFilters(getSourceLocFilters(getOpts(Ctx)), Loc);
}

bool llvm::isSourceLocFilterEmpty(const LLVMContext &Ctx) {
  return getSourceLocFilters(getOpts(Ctx)).empty();
}

// Overloads without LLVMContext for legacy pass manager use.
bool llvm::isSourceLocInPrintList(const DebugLoc &Loc) {
  return isSourceLocInPrintList(Loc, clv2::defaultOptionsContext());
}

bool llvm::isSourceLocFilterEmpty() {
  return isSourceLocFilterEmpty(clv2::defaultOptionsContext());
}

bool llvm::isSourceLocInPrintList(const DebugLoc &Loc,
                                  const clv2::OptionsContext &Ctx) {
  return locMatchesFilters(getSourceLocFilters(getOpts(Ctx)), Loc);
}

bool llvm::isSourceLocFilterEmpty(const clv2::OptionsContext &Ctx) {
  return getSourceLocFilters(getOpts(Ctx)).empty();
}

bool llvm::shouldPrintAllFunctions(const LLVMContext &Ctx) {
  return isSourceLocFilterEmpty(Ctx) &&
         ::isFunctionInPrintList(getOpts(Ctx), "*");
}

bool llvm::shouldPrintAllFunctions() {
  return shouldPrintAllFunctions(clv2::defaultOptionsContext());
}

bool llvm::shouldPrintAllFunctions(const clv2::OptionsContext &Ctx) {
  return isSourceLocFilterEmpty(Ctx) &&
         ::isFunctionInPrintList(getOpts(Ctx), "*");
}

bool llvm::shouldPrintFunction(const Function &F) {
  auto *O = getOpts(F.getContext());
  if (!::isFunctionInPrintList(O, F.getName()))
    return false;

  std::vector<PrintSourceLocFilter> Filters = getSourceLocFilters(O);
  if (Filters.empty())
    return true;

  for (const BasicBlock &BB : F)
    for (const Instruction &I : BB)
      if (locMatchesFilters(Filters, I.getDebugLoc()))
        return true;
  return false;
}

std::error_code cleanUpTempFilesImpl(ArrayRef<std::string> FileName,
                                     unsigned N) {
  std::error_code RC;
  for (unsigned I = 0; I < N; ++I) {
    std::error_code EC = sys::fs::remove(FileName[I]);
    if (EC)
      RC = EC;
  }
  return RC;
}

std::error_code llvm::prepareTempFiles(SmallVector<int> &FD,
                                       ArrayRef<StringRef> SR,
                                       SmallVector<std::string> &FileName) {
  assert(FD.size() >= SR.size() && FileName.size() == FD.size() &&
         "Unexpected array sizes");
  std::error_code EC;
  unsigned I = 0;
  for (; I < FD.size(); ++I) {
    if (FD[I] == -1) {
      SmallVector<char, 200> SV;
      EC = sys::fs::createTemporaryFile("tmpfile", "txt", FD[I], SV);
      if (EC)
        break;
      FileName[I] = Twine(SV).str();
    }
    if (I < SR.size()) {
      EC = sys::fs::openFileForWrite(FileName[I], FD[I]);
      if (EC)
        break;
      raw_fd_ostream OutStream(FD[I], /*shouldClose=*/true);
      if (FD[I] == -1) {
        EC = make_error_code(errc::io_error);
        break;
      }
      OutStream << SR[I];
    }
  }
  if (EC && I > 0)
    cleanUpTempFilesImpl(FileName, I);
  return EC;
}

std::error_code llvm::cleanUpTempFiles(ArrayRef<std::string> FileName) {
  return cleanUpTempFilesImpl(FileName, FileName.size());
}

std::string llvm::doSystemDiff(const clv2::OptionsContext &Ctx,
                               StringRef Before, StringRef After,
                               StringRef OldLineFormat, StringRef NewLineFormat,
                               StringRef UnchangedLineFormat) {
  static SmallVector<int> FD{-1, -1, -1};
  SmallVector<StringRef> SR{Before, After};
  static SmallVector<std::string> FileName{"", "", ""};
  if (prepareTempFiles(FD, SR, FileName))
    return "Unable to create temporary file.";

  std::string DiffBin = getDiffBinary(clv2::getView<&clv2::IROptsReg>(Ctx));
  ErrorOr<std::string> DiffExe = sys::findProgramByName(DiffBin);
  if (!DiffExe)
    return "Unable to find diff executable.";

  SmallString<128> OLF, NLF, ULF;
  ("--old-line-format=" + OldLineFormat).toVector(OLF);
  ("--new-line-format=" + NewLineFormat).toVector(NLF);
  ("--unchanged-line-format=" + UnchangedLineFormat).toVector(ULF);

  StringRef Args[] = {DiffBin, "-w", "-d",        OLF,
                      NLF,     ULF,  FileName[0], FileName[1]};
  std::optional<StringRef> Redirects[] = {std::nullopt, StringRef(FileName[2]),
                                          std::nullopt};
  int Result = sys::ExecuteAndWait(*DiffExe, Args, std::nullopt, Redirects);
  if (Result < 0)
    return "Error executing system diff.";
  std::string Diff;
  auto B = MemoryBuffer::getFile(FileName[2]);
  if (B && *B)
    Diff = (*B)->getBuffer().str();
  else
    return "Unable to read result.";

  if (cleanUpTempFiles(FileName))
    return "Unable to remove temporary file.";

  return Diff;
}

void llvm::reportChangedIR(const LLVMContext &Ctx, StringRef Before,
                           StringRef After, StringRef PassName,
                           StringRef PassID, StringRef IRName,
                           bool IsInteresting, bool ShouldReport) {
  if (!ShouldReport && IsInteresting)
    return;

  auto *O = clv2::getView<&clv2::IROptsReg>(Ctx.getOptionsContext());
  ChangePrinter PC = getPrintChangedVal(O);

  if (IsInteresting && Before != After) {
    if (After.empty() &&
        llvm::is_contained({ChangePrinter::Quiet, ChangePrinter::Verbose,
                            ChangePrinter::DotCfgQuiet,
                            ChangePrinter::DotCfgVerbose},
                           PC)) {
      errs() << ("*** IR Deleted After " + PassName + " (" + PassID + ") on " +
                 IRName + " ***\n");
      return;
    }

    errs() << ("*** IR Dump After " + PassName + " (" + PassID + ") on " +
               IRName + " ***\n");
    switch (PC) {
    case ChangePrinter::None:
      llvm_unreachable("");
    case ChangePrinter::Quiet:
    case ChangePrinter::Verbose:
    case ChangePrinter::DotCfgQuiet:   // unimplemented
    case ChangePrinter::DotCfgVerbose: // unimplemented
      errs() << After;
      break;
    case ChangePrinter::DiffQuiet:
    case ChangePrinter::DiffVerbose:
    case ChangePrinter::ColourDiffQuiet:
    case ChangePrinter::ColourDiffVerbose: {
      bool Color = llvm::is_contained(
          {ChangePrinter::ColourDiffQuiet, ChangePrinter::ColourDiffVerbose},
          PC);
      StringRef Removed = Color ? "\033[31m-%l\033[0m\n" : "-%l\n";
      StringRef Added = Color ? "\033[32m+%l\033[0m\n" : "+%l\n";
      StringRef NoChange = " %l\n";
      errs() << doSystemDiff(Ctx.getOptionsContext(), Before, After, Removed,
                             Added, NoChange);
      break;
    }
    }
  } else if (llvm::is_contained({ChangePrinter::Verbose,
                                 ChangePrinter::DiffVerbose,
                                 ChangePrinter::ColourDiffVerbose},
                                PC)) {
    const char *Reason =
        IsInteresting ? " omitted because no change" : " filtered out";
    errs() << "*** IR Dump After " << PassName;
    if (!PassID.empty())
      errs() << " (" << PassID << ")";
    errs() << " on " << IRName + Reason + " ***\n";
  }
}
