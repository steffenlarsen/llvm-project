//===- RemarkInstructionMix.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic tool to extract instruction mix from asm-printer remarks.
//
//===----------------------------------------------------------------------===//

#include "RemarkUtilHelpers.h"

#include "llvm/Support/Format.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/Regex.h"

#include <cmath>
#include <numeric>

using namespace llvm;
using namespace remarks;
using namespace llvm::remarkutil;

namespace instructionmix {

std::string FunctionFilter;
std::string FunctionFilterRE;
ReportStyleOptions ReportStyle;

Error tryInstructionMix() {
  auto MaybeOF =
      getOutputFileWithFlags(OutputFileName, sys::fs::OF_TextWithCRLF);
  if (!MaybeOF)
    return MaybeOF.takeError();

  auto OF = std::move(*MaybeOF);
  auto MaybeBuf = getInputMemoryBuffer(InputFileName);
  if (!MaybeBuf)
    return MaybeBuf.takeError();
  auto MaybeParser = createRemarkParser(InputFormat, (*MaybeBuf)->getBuffer());
  if (!MaybeParser)
    return MaybeParser.takeError();

  Expected<std::optional<FilterMatcher>> Filter =
      FilterMatcher::createExactOrRE(FunctionFilter, FunctionFilterRE, "filter",
                                     "rfilter");
  if (!Filter)
    return Filter.takeError();

  llvm::DenseMap<StringRef, unsigned> Histogram;
  auto &Parser = **MaybeParser;
  auto MaybeRemark = Parser.next();
  for (; MaybeRemark; MaybeRemark = Parser.next()) {
    Remark &Remark = **MaybeRemark;
    if (Remark.RemarkName != "InstructionMix")
      continue;
    if (*Filter && !(*Filter)->match(Remark.FunctionName))
      continue;
    for (auto &Arg : Remark.Args) {
      StringRef Key = Arg.Key;
      if (!Key.consume_front("INST_"))
        continue;
      unsigned Val = 0;
      bool ParseError = Arg.Val.getAsInteger(10, Val);
      assert(!ParseError);
      (void)ParseError;
      Histogram[Key] += Val;
    }
  }

  using MixEntry = std::pair<StringRef, unsigned>;
  llvm::SmallVector<MixEntry> Mix(Histogram.begin(), Histogram.end());
  std::sort(Mix.begin(), Mix.end(), [](const auto &LHS, const auto &RHS) {
    return LHS.second > RHS.second;
  });

  switch (ReportStyle) {
  case human_output: {
    formatted_raw_ostream FOS(OF->os());
    size_t MaxMnemonic =
        std::accumulate(Mix.begin(), Mix.end(), StringRef("Instruction").size(),
                        [](size_t MaxMnemonic, const MixEntry &Elt) {
                          return std::max(MaxMnemonic, Elt.first.size());
                        });
    unsigned MaxValue = std::accumulate(
        Mix.begin(), Mix.end(), 1, [](unsigned MaxValue, const MixEntry &Elt) {
          return std::max(MaxValue, Elt.second);
        });
    unsigned ValueWidth = NumDigitsBase10(MaxValue);
    FOS << "Instruction";
    FOS.PadToColumn(MaxMnemonic + 1) << "Count\n";
    FOS << "-----------";
    FOS.PadToColumn(MaxMnemonic + 1) << "-----\n";
    for (const auto &[Inst, Count] : Mix) {
      FOS << Inst;
      FOS.PadToColumn(MaxMnemonic + 1)
          << " " << format_decimal(Count, ValueWidth) << "\n";
    }
  } break;
  case csv_output: {
    OF->os() << "Instruction,Count\n";
    for (const auto &[Inst, Count] : Mix)
      OF->os() << Inst << "," << Count << "\n";
  } break;
  }

  auto E = MaybeRemark.takeError();
  if (!E.isA<EndOfFileError>())
    return E;
  consumeError(std::move(E));
  OF->keep();
  return Error::success();
}

} // namespace instructionmix
