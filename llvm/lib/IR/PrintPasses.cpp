//===- PrintPasses.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/PrintPasses.h"
#include "llvm/IR/IROptionsOptInfos.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/IOSandbox.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

static const ir_opts::ParsedOpts *getOpts(const LLVMContext &Ctx) {
  return clv2::getView<&clv2::IROptsReg>(Ctx.getOptionsContext());
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
  auto PFL = getPrintFuncsList(getOpts(Ctx));
  return PFL.empty() || llvm::is_contained(PFL, FunctionName);
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
