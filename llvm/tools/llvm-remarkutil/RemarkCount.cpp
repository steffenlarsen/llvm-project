//===- RemarkCount.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Count remarks using `instruction-count` for asm-printer remarks and
// `annotation-count` for annotation-remarks
//
//===----------------------------------------------------------------------===//
#include "RemarkUtilHelpers.h"

using namespace llvm;
using namespace remarks;
using namespace llvm::remarkutil;

std::string AnnotationTypeToCollect;

static bool shouldSkipRemark(bool UseDebugLoc, Remark &Remark) {
  return UseDebugLoc && !Remark.Loc.has_value();
}

namespace instructioncount {
/// Outputs all instruction count remarks in the file as a CSV.
/// \returns Error::success() on success, and an Error otherwise.
Error tryInstructionCount() {
  auto MaybeOF = getOutputFileWithFlags(OutputFileName,
                                        /*Flags = */ sys::fs::OF_TextWithCRLF);
  if (!MaybeOF)
    return MaybeOF.takeError();
  auto OF = std::move(*MaybeOF);
  auto MaybeBuf = getInputMemoryBuffer(InputFileName);
  if (!MaybeBuf)
    return MaybeBuf.takeError();
  auto MaybeParser = createRemarkParser(InputFormat, (*MaybeBuf)->getBuffer());
  if (!MaybeParser)
    return MaybeParser.takeError();
  if (UseDebugLoc)
    OF->os() << "Source,";
  OF->os() << "Function,InstructionCount\n";
  auto &Parser = **MaybeParser;
  auto MaybeRemark = Parser.next();
  for (; MaybeRemark; MaybeRemark = Parser.next()) {
    auto &Remark = **MaybeRemark;
    if (Remark.RemarkName != "InstructionCount")
      continue;
    if (shouldSkipRemark(UseDebugLoc, Remark))
      continue;
    auto *InstrCountArg = find_if(Remark.Args, [](const Argument &Arg) {
      return Arg.Key == "NumInstructions";
    });
    assert(InstrCountArg != Remark.Args.end() &&
           "Expected instruction count remarks to have a NumInstructions key?");
    if (UseDebugLoc) {
      std::string Loc = Remark.Loc->SourceFilePath.str() + ":" +
                        std::to_string(Remark.Loc->SourceLine) + +":" +
                        std::to_string(Remark.Loc->SourceColumn);
      OF->os() << Loc << ",";
    }
    OF->os() << Remark.FunctionName << "," << InstrCountArg->Val << "\n";
  }
  auto E = MaybeRemark.takeError();
  if (!E.isA<EndOfFileError>())
    return E;
  consumeError(std::move(E));
  OF->keep();
  return Error::success();
}
} // namespace instructioncount

namespace annotationcount {
Error tryAnnotationCount() {
  auto MaybeOF = getOutputFileWithFlags(OutputFileName,
                                        /*Flags = */ sys::fs::OF_TextWithCRLF);
  if (!MaybeOF)
    return MaybeOF.takeError();
  auto OF = std::move(*MaybeOF);
  auto MaybeBuf = getInputMemoryBuffer(InputFileName);
  if (!MaybeBuf)
    return MaybeBuf.takeError();
  auto MaybeParser = createRemarkParser(InputFormat, (*MaybeBuf)->getBuffer());
  if (!MaybeParser)
    return MaybeParser.takeError();
  if (UseDebugLoc)
    OF->os() << "Source,";
  OF->os() << "Function,Count\n";
  auto &Parser = **MaybeParser;
  auto MaybeRemark = Parser.next();
  for (; MaybeRemark; MaybeRemark = Parser.next()) {
    auto &Remark = **MaybeRemark;
    if (Remark.RemarkName != "AnnotationSummary")
      continue;
    if (shouldSkipRemark(UseDebugLoc, Remark))
      continue;
    auto *RemarkNameArg = find_if(Remark.Args, [](const Argument &Arg) {
      return Arg.Key == "type" && Arg.Val == AnnotationTypeToCollect;
    });
    if (RemarkNameArg == Remark.Args.end())
      continue;
    auto *CountArg = find_if(
        Remark.Args, [](const Argument &Arg) { return Arg.Key == "count"; });
    assert(CountArg != Remark.Args.end() &&
           "Expected annotation-type remark to have a count key?");
    if (UseDebugLoc) {
      std::string Loc = Remark.Loc->SourceFilePath.str() + ":" +
                        std::to_string(Remark.Loc->SourceLine) + +":" +
                        std::to_string(Remark.Loc->SourceColumn);
      OF->os() << Loc << ",";
    }
    OF->os() << Remark.FunctionName << "," << CountArg->Val << "\n";
  }
  auto E = MaybeRemark.takeError();
  if (!E.isA<EndOfFileError>())
    return E;
  consumeError(std::move(E));
  OF->keep();
  return Error::success();
}
} // namespace annotationcount
