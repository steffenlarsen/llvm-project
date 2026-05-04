//===- RemarkCounter.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic tool to count remarks based on properties
//
//===----------------------------------------------------------------------===//

#include "RemarkCounter.h"
#include "llvm/Support/InterleavedRange.h"
#include "llvm/Support/Regex.h"

using namespace llvm;
using namespace remarks;
using namespace llvm::remarkutil;

std::vector<std::string> Keys;
std::vector<std::string> RKeys;
CountBy CountByVal;
GroupBy GroupByVal;

/// Look for matching argument with \p Key in \p Remark and return the parsed
/// integer value or 0 if it is has no integer value.
static unsigned getValForKey(StringRef Key, const Remark &Remark) {
  auto *RemarkArg = find_if(Remark.Args, [&Key](const Argument &Arg) {
    return Arg.Key == Key && Arg.getValAsInt<unsigned>();
  });
  if (RemarkArg == Remark.Args.end())
    return 0;
  return *RemarkArg->getValAsInt<unsigned>();
}

Error ArgumentCounter::getAllMatchingArgumentsInRemark(
    StringRef Buffer, ArrayRef<FilterMatcher> Arguments, Filters &Filter) {
  auto MaybeParser = createRemarkParser(InputFormat, Buffer);
  if (!MaybeParser)
    return MaybeParser.takeError();
  auto &Parser = **MaybeParser;
  auto MaybeRemark = Parser.next();
  for (; MaybeRemark; MaybeRemark = Parser.next()) {
    auto &Remark = **MaybeRemark;
    if (!Filter.filterRemark(Remark))
      continue;
    for (auto &Key : Arguments) {
      for (Argument Arg : Remark.Args)
        if (Key.match(Arg.Key) && Arg.getValAsInt<unsigned>())
          ArgumentSetIdxMap.insert({Arg.Key, ArgumentSetIdxMap.size()});
    }
  }

  auto E = MaybeRemark.takeError();
  if (!E.isA<EndOfFileError>())
    return E;
  consumeError(std::move(E));
  return Error::success();
}

std::optional<std::string> Counter::getGroupByKey(const Remark &Remark) {
  switch (Group) {
  case GroupBy::PER_FUNCTION:
    return Remark.FunctionName.str();
  case GroupBy::TOTAL:
    return "Total";
  case GroupBy::PER_SOURCE:
  case GroupBy::PER_FUNCTION_WITH_DEBUG_LOC:
    if (!Remark.Loc.has_value())
      return std::nullopt;

    if (Group == GroupBy::PER_FUNCTION_WITH_DEBUG_LOC)
      return Remark.Loc->SourceFilePath.str() + ":" + Remark.FunctionName.str();
    return Remark.Loc->SourceFilePath.str();
  }
  llvm_unreachable("Fully covered switch above!");
}

void ArgumentCounter::collect(const Remark &Remark) {
  SmallVector<unsigned, 4> Row(ArgumentSetIdxMap.size());
  std::optional<std::string> GroupByKey = getGroupByKey(Remark);
  if (!GroupByKey)
    return;
  auto GroupVal = *GroupByKey;
  CountByKeysMap.insert({GroupVal, Row});
  for (auto [Key, Idx] : ArgumentSetIdxMap) {
    auto Count = getValForKey(Key, Remark);
    CountByKeysMap[GroupVal][Idx] += Count;
  }
}

void RemarkCounter::collect(const Remark &Remark) {
  if (std::optional<std::string> Key = getGroupByKey(Remark))
    ++CountedByRemarksMap[*Key];
}

Error ArgumentCounter::print(StringRef OutputFileName) {
  auto MaybeOF =
      getOutputFileWithFlags(OutputFileName, sys::fs::OF_TextWithCRLF);
  if (!MaybeOF)
    return MaybeOF.takeError();

  auto OF = std::move(*MaybeOF);
  OF->os() << groupByToStr(Group) << ",";
  OF->os() << llvm::interleaved(llvm::make_first_range(ArgumentSetIdxMap), ",");
  OF->os() << "\n";
  for (auto [Header, CountVector] : CountByKeysMap) {
    OF->os() << Header << ",";
    OF->os() << llvm::interleaved(CountVector, ",");
    OF->os() << "\n";
  }
  return Error::success();
}

Error RemarkCounter::print(StringRef OutputFileName) {
  auto MaybeOF =
      getOutputFileWithFlags(OutputFileName, sys::fs::OF_TextWithCRLF);
  if (!MaybeOF)
    return MaybeOF.takeError();

  auto OF = std::move(*MaybeOF);
  OF->os() << groupByToStr(Group) << ","
           << "Count\n";
  for (auto [Key, Count] : CountedByRemarksMap)
    OF->os() << Key << "," << Count << "\n";
  OF->keep();
  return Error::success();
}

Error useCollectRemark(StringRef Buffer, Counter &Counter, Filters &Filter) {
  auto MaybeParser = createRemarkParser(InputFormat, Buffer);
  if (!MaybeParser)
    return MaybeParser.takeError();
  auto &Parser = **MaybeParser;
  auto MaybeRemark = Parser.next();
  for (; MaybeRemark; MaybeRemark = Parser.next()) {
    const Remark &Remark = **MaybeRemark;
    if (Filter.filterRemark(Remark))
      Counter.collect(Remark);
  }

  if (auto E = Counter.print(OutputFileName))
    return E;
  auto E = MaybeRemark.takeError();
  if (!E.isA<EndOfFileError>())
    return E;
  consumeError(std::move(E));
  return Error::success();
}

Error collectRemarks() {
  auto MaybeBuf = getInputMemoryBuffer(InputFileName);
  if (!MaybeBuf)
    return MaybeBuf.takeError();
  StringRef Buffer = (*MaybeBuf)->getBuffer();
  auto MaybeFilter = getRemarkFilters();
  if (!MaybeFilter)
    return MaybeFilter.takeError();
  auto &Filter = *MaybeFilter;
  if (CountByVal == CountBy::REMARK) {
    RemarkCounter RC(GroupByVal);
    if (auto E = useCollectRemark(Buffer, RC, Filter))
      return E;
  } else if (CountByVal == CountBy::ARGUMENT) {
    SmallVector<FilterMatcher, 4> ArgumentsVector;
    if (!Keys.empty()) {
      for (auto &Key : Keys)
        ArgumentsVector.push_back(FilterMatcher::createExact(Key));
    } else if (!RKeys.empty())
      for (auto Key : RKeys) {
        auto FM = FilterMatcher::createRE("rargs", Key);
        if (!FM)
          return FM.takeError();
        ArgumentsVector.push_back(std::move(*FM));
      }
    else
      ArgumentsVector.push_back(FilterMatcher::createAny());

    Expected<ArgumentCounter> AC = ArgumentCounter::createArgumentCounter(
        GroupByVal, ArgumentsVector, Buffer, Filter);
    if (!AC)
      return AC.takeError();
    if (auto E = useCollectRemark(Buffer, *AC, Filter))
      return E;
  }
  return Error::success();
}
