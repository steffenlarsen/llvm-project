//===-------------- RemarkSizeDiff.cpp ------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Diffs instruction count and stack size remarks between two remark files.
///
/// This is intended for use by compiler developers who want to see how their
/// changes impact program code size.
///
//===----------------------------------------------------------------------===//

#include "RemarkUtilHelpers.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/JSON.h"

using namespace llvm;
using namespace remarks;
using namespace llvm::remarkutil;
using namespace sizediff;

std::string InputFileNameA;
std::string InputFileNameB;
sizediff::ReportStyleOptions SDReportStyle;
bool PrettyPrint;

/// Contains information from size remarks.
// This is a little nicer to read than a std::pair.
struct InstCountAndStackSize {
  int64_t InstCount = 0;
  int64_t StackSize = 0;
};

/// Represents which files a function appeared in.
enum FilesPresent { A, B, BOTH };

/// Contains the data from the remarks in file A and file B for some function.
/// E.g. instruction count, stack size...
struct FunctionDiff {
  /// Function name from the remark.
  std::string FuncName;
  // Idx 0 = A, Idx 1 = B.
  int64_t InstCount[2] = {0, 0};
  int64_t StackSize[2] = {0, 0};

  int64_t getInstDiff() const { return InstCount[1] - InstCount[0]; }
  int64_t getStackDiff() const { return StackSize[1] - StackSize[0]; }

  int64_t getInstCountA() const { return InstCount[0]; }
  int64_t getStackSizeA() const { return StackSize[0]; }

  int64_t getInstCountB() const { return InstCount[1]; }
  int64_t getStackSizeB() const { return StackSize[1]; }

  FilesPresent getFilesPresent() const {
    if (getInstCountA() == 0)
      return B;
    if (getInstCountB() == 0)
      return A;
    return BOTH;
  }

  FunctionDiff(StringRef FuncName, const InstCountAndStackSize &A,
               const InstCountAndStackSize &B)
      : FuncName(FuncName) {
    InstCount[0] = A.InstCount;
    InstCount[1] = B.InstCount;
    StackSize[0] = A.StackSize;
    StackSize[1] = B.StackSize;
  }
};

struct DiffsCategorizedByFilesPresent {
  SmallVector<FunctionDiff> OnlyInA;
  SmallVector<FunctionDiff> OnlyInB;
  SmallVector<FunctionDiff> InBoth;

  void addDiff(FunctionDiff &FD) {
    switch (FD.getFilesPresent()) {
    case A:
      OnlyInA.push_back(FD);
      break;
    case B:
      OnlyInB.push_back(FD);
      break;
    case BOTH:
      InBoth.push_back(FD);
      break;
    }
  }
};

static void printFunctionDiff(const FunctionDiff &FD, llvm::raw_ostream &OS) {
  FilesPresent FP = FD.getFilesPresent();
  const std::string &FuncName = FD.FuncName;
  const int64_t InstDiff = FD.getInstDiff();
  assert(InstDiff && "Shouldn't get functions with no size change?");
  const int64_t StackDiff = FD.getStackDiff();
  switch (FP) {
  case FilesPresent::A:
    OS << "-- ";
    break;
  case FilesPresent::B:
    OS << "++ ";
    break;
  case FilesPresent::BOTH:
    OS << "== ";
    break;
  }
  if (InstDiff > 0)
    OS << "> ";
  else
    OS << "< ";
  OS << FuncName << ", ";
  OS << InstDiff << " instrs, ";
  OS << StackDiff << " stack B";
  OS << "\n";
}

static void printSummaryItem(int64_t TotalA, int64_t TotalB, StringRef Metric,
                             llvm::raw_ostream &OS) {
  OS << "  " << Metric << ": ";
  int64_t TotalDiff = TotalB - TotalA;
  if (TotalDiff == 0) {
    OS << "None\n";
    return;
  }
  OS << TotalDiff << " (" << formatv("{0:p}", TotalDiff / (double)TotalA)
     << ")\n";
}

static void printDiffsCategorizedByFilesPresent(
    DiffsCategorizedByFilesPresent &DiffsByFilesPresent,
    llvm::raw_ostream &OS) {
  int64_t InstrsA = 0;
  int64_t InstrsB = 0;
  int64_t StackA = 0;
  int64_t StackB = 0;
  auto PrintDiffList = [&](SmallVector<FunctionDiff> &FunctionDiffList) {
    if (FunctionDiffList.empty())
      return;
    stable_sort(FunctionDiffList,
                [](const FunctionDiff &LHS, const FunctionDiff &RHS) {
                  return LHS.getInstDiff() < RHS.getInstDiff();
                });
    for (const auto &FuncDiff : FunctionDiffList) {
      if (FuncDiff.getInstDiff())
        printFunctionDiff(FuncDiff, OS);
      InstrsA += FuncDiff.getInstCountA();
      InstrsB += FuncDiff.getInstCountB();
      StackA += FuncDiff.getStackSizeA();
      StackB += FuncDiff.getStackSizeB();
    }
  };
  PrintDiffList(DiffsByFilesPresent.OnlyInA);
  PrintDiffList(DiffsByFilesPresent.OnlyInB);
  PrintDiffList(DiffsByFilesPresent.InBoth);
  OS << "\n### Summary ###\n";
  OS << "Total change: \n";
  printSummaryItem(InstrsA, InstrsB, "instruction count", OS);
  printSummaryItem(StackA, StackB, "stack byte usage", OS);
}

static Expected<int64_t> getIntValFromKey(const remarks::Remark &Remark,
                                          unsigned ArgIdx,
                                          StringRef ExpectedKeyName) {
  auto KeyName = Remark.Args[ArgIdx].Key;
  if (KeyName != ExpectedKeyName)
    return createStringError(
        inconvertibleErrorCode(),
        Twine("Unexpected key at argument index " + std::to_string(ArgIdx) +
              ": Expected '" + ExpectedKeyName + "', got '" + KeyName + "'"));
  long long Val;
  auto ValStr = Remark.Args[ArgIdx].Val;
  if (getAsSignedInteger(ValStr, 0, Val))
    return createStringError(
        inconvertibleErrorCode(),
        Twine("Could not convert string to signed integer: " + ValStr));
  return static_cast<int64_t>(Val);
}

static Error processRemark(const remarks::Remark &Remark,
                           StringMap<InstCountAndStackSize> &FuncNameToSizeInfo,
                           unsigned &NumInstCountRemarksParsed) {
  const auto &RemarkName = Remark.RemarkName;
  const auto &PassName = Remark.PassName;
  if (PassName == "asm-printer" && RemarkName == "InstructionCount") {
    auto MaybeInstCount =
        getIntValFromKey(Remark, /*ArgIdx = */ 0, "NumInstructions");
    if (!MaybeInstCount)
      return MaybeInstCount.takeError();
    FuncNameToSizeInfo[Remark.FunctionName].InstCount = *MaybeInstCount;
    ++NumInstCountRemarksParsed;
  } else if (PassName == "prolog-epilog" && RemarkName == "StackSize") {
    auto MaybeStackSize =
        getIntValFromKey(Remark, /*ArgIdx = */ 0, "NumStackBytes");
    if (!MaybeStackSize)
      return MaybeStackSize.takeError();
    FuncNameToSizeInfo[Remark.FunctionName].StackSize = *MaybeStackSize;
  }
  return Error::success();
}

static Error readFileAndProcessRemarks(
    StringRef FileName, StringMap<InstCountAndStackSize> &FuncNameToSizeInfo) {

  auto MaybeBuf = getInputMemoryBuffer(FileName);
  if (!MaybeBuf)
    return MaybeBuf.takeError();
  auto MaybeParser =
      createRemarkParserFromMeta(InputFormat, (*MaybeBuf)->getBuffer());
  if (!MaybeParser)
    return MaybeParser.takeError();
  auto &Parser = **MaybeParser;
  auto MaybeRemark = Parser.next();
  unsigned NumInstCountRemarksParsed = 0;
  for (; MaybeRemark; MaybeRemark = Parser.next()) {
    if (auto E = processRemark(**MaybeRemark, FuncNameToSizeInfo,
                               NumInstCountRemarksParsed))
      return E;
  }
  auto E = MaybeRemark.takeError();
  if (!E.isA<remarks::EndOfFileError>())
    return E;
  consumeError(std::move(E));
  if (NumInstCountRemarksParsed == 0)
    return createStringError(
        inconvertibleErrorCode(),
        "File '" + FileName +
            "' did not contain any instruction-count remarks!");
  return Error::success();
}

static Error tryReadFileAndProcessRemarks(
    StringRef FileName, StringMap<InstCountAndStackSize> &FuncNameToSizeInfo) {
  if (Error E = readFileAndProcessRemarks(FileName, FuncNameToSizeInfo)) {
    return E;
  }
  return Error::success();
}

static void
computeDiff(const StringMap<InstCountAndStackSize> &FuncNameToSizeInfoA,
            const StringMap<InstCountAndStackSize> &FuncNameToSizeInfoB,
            DiffsCategorizedByFilesPresent &DiffsByFilesPresent) {
  SmallSet<std::string, 10> FuncNames;
  for (const auto &FuncName : FuncNameToSizeInfoA.keys())
    FuncNames.insert(FuncName.str());
  for (const auto &FuncName : FuncNameToSizeInfoB.keys())
    FuncNames.insert(FuncName.str());
  for (const std::string &FuncName : FuncNames) {
    const auto &SizeInfoA = FuncNameToSizeInfoA.lookup(FuncName);
    const auto &SizeInfoB = FuncNameToSizeInfoB.lookup(FuncName);
    FunctionDiff FuncDiff(FuncName, SizeInfoA, SizeInfoB);
    DiffsByFilesPresent.addDiff(FuncDiff);
  }
}

static ErrorOr<std::unique_ptr<ToolOutputFile>> getOutputStream() {
  std::string OutFile = OutputFileName;
  if (OutFile == "")
    OutFile = "-";
  std::error_code EC;
  auto Out =
      std::make_unique<ToolOutputFile>(OutFile, EC, sys::fs::OF_TextWithCRLF);
  if (!EC)
    return std::move(Out);
  return EC;
}

json::Array
getFunctionDiffListAsJSON(const SmallVector<FunctionDiff> &FunctionDiffs,
                          const FilesPresent &WhichFiles) {
  json::Array FunctionDiffsAsJSON;
  int64_t InstCountA, InstCountB, StackSizeA, StackSizeB;
  for (auto &Diff : FunctionDiffs) {
    InstCountA = InstCountB = StackSizeA = StackSizeB = 0;
    switch (WhichFiles) {
    case BOTH:
      [[fallthrough]];
    case A:
      InstCountA = Diff.getInstCountA();
      StackSizeA = Diff.getStackSizeA();
      if (WhichFiles != BOTH)
        break;
      [[fallthrough]];
    case B:
      InstCountB = Diff.getInstCountB();
      StackSizeB = Diff.getStackSizeB();
      break;
    }
    json::Object FunctionObject({{"FunctionName", Diff.FuncName},
                                 {"InstCount", {InstCountA, InstCountB}},
                                 {"StackSize", {StackSizeA, StackSizeB}}});
    FunctionDiffsAsJSON.push_back(std::move(FunctionObject));
  }
  return FunctionDiffsAsJSON;
}

static void
outputJSONForAllDiffs(StringRef FileNameA, StringRef FileNameB,
                      const DiffsCategorizedByFilesPresent &DiffsByFilesPresent,
                      llvm::raw_ostream &OS) {
  json::Object Output;
  json::Object Files({{"A", FileNameA.str()}, {"B", FileNameB.str()}});
  Output["Files"] = std::move(Files);
  Output["OnlyInA"] = getFunctionDiffListAsJSON(DiffsByFilesPresent.OnlyInA, A);
  Output["OnlyInB"] = getFunctionDiffListAsJSON(DiffsByFilesPresent.OnlyInB, B);
  Output["InBoth"] =
      getFunctionDiffListAsJSON(DiffsByFilesPresent.InBoth, BOTH);
  json::OStream JOS(OS, PrettyPrint ? 2 : 0);
  JOS.value(std::move(Output));
  OS << '\n';
}

static Error
outputAllDiffs(StringRef FileNameA, StringRef FileNameB,
               DiffsCategorizedByFilesPresent &DiffsByFilesPresent) {
  auto MaybeOF = getOutputStream();
  if (std::error_code EC = MaybeOF.getError())
    return errorCodeToError(EC);
  std::unique_ptr<ToolOutputFile> OF = std::move(*MaybeOF);
  switch (SDReportStyle) {
  case human_output:
    printDiffsCategorizedByFilesPresent(DiffsByFilesPresent, OF->os());
    break;
  case json_output:
    outputJSONForAllDiffs(FileNameA, FileNameB, DiffsByFilesPresent, OF->os());
    break;
  }
  OF->keep();
  return Error::success();
}

static Error
tryOutputAllDiffs(StringRef FileNameA, StringRef FileNameB,
                  DiffsCategorizedByFilesPresent &DiffsByFilesPresent) {
  if (Error E = outputAllDiffs(FileNameA, FileNameB, DiffsByFilesPresent)) {
    return E;
  }
  return Error::success();
}

Error trySizeDiff() {
  StringMap<InstCountAndStackSize> FuncNameToSizeInfoA;
  StringMap<InstCountAndStackSize> FuncNameToSizeInfoB;
  if (auto E =
          tryReadFileAndProcessRemarks(InputFileNameA, FuncNameToSizeInfoA))
    return E;
  if (auto E =
          tryReadFileAndProcessRemarks(InputFileNameB, FuncNameToSizeInfoB))
    return E;
  DiffsCategorizedByFilesPresent DiffsByFilesPresent;
  computeDiff(FuncNameToSizeInfoA, FuncNameToSizeInfoB, DiffsByFilesPresent);
  if (auto E = tryOutputAllDiffs(InputFileNameA, InputFileNameB,
                                 DiffsByFilesPresent))
    return E;
  return Error::success();
}
