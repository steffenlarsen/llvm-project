//===- xray-account.h - XRay Function Call Accounting ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements basic function call accounting from an XRay trace.
//
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cassert>
#include <numeric>
#include <system_error>
#include <utility>

#include "xray-account.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/XRay/InstrumentationMap.h"
#include "llvm/XRay/Trace.h"

#include <cmath>

using namespace llvm;
using namespace llvm::xray;

std::string AccountInputVal;
bool AccountKeepGoingVal;
bool AccountRecursiveCallsOnlyVal;
bool AccountDeduceSiblingCallsVal;
std::string AccountOutputVal;
AccountOutputFormats AccountOutputFormatVal;
SortField AccountSortOutputVal;
SortDirection AccountSortOrderVal;
int AccountTopVal;
std::string AccountInstrMapVal;

template <class T, class U> static void setMinMax(std::pair<T, T> &MM, U &&V) {
  if (MM.first == 0 || MM.second == 0)
    MM = std::make_pair(std::forward<U>(V), std::forward<U>(V));
  else
    MM = std::make_pair(std::min(MM.first, V), std::max(MM.second, V));
}

template <class T> static T diff(T L, T R) {
  return std::max(L, R) - std::min(L, R);
}

using RecursionStatus = LatencyAccountant::FunctionStack::RecursionStatus;
RecursionStatus &RecursionStatus::operator++() {
  auto Depth = Bitfield::get<RecursionStatus::Depth>(Storage);
  assert(Depth >= 0 && Depth < std::numeric_limits<decltype(Depth)>::max());
  ++Depth;
  Bitfield::set<RecursionStatus::Depth>(Storage, Depth);
  if (!isRecursive() && Depth == 2)
    Bitfield::set<RecursionStatus::IsRecursive>(Storage, true);
  return *this;
}

RecursionStatus &RecursionStatus::operator--() {
  auto Depth = Bitfield::get<RecursionStatus::Depth>(Storage);
  assert(Depth > 0);
  --Depth;
  Bitfield::set<RecursionStatus::Depth>(Storage, Depth);
  if (isRecursive() && Depth == 0)
    Bitfield::set<RecursionStatus::IsRecursive>(Storage, false);
  return *this;
}

bool RecursionStatus::isRecursive() const {
  return Bitfield::get<RecursionStatus::IsRecursive>(Storage);
}

bool LatencyAccountant::accountRecord(const XRayRecord &Record) {
  setMinMax(PerThreadMinMaxTSC[Record.TId], Record.TSC);
  setMinMax(PerCPUMinMaxTSC[Record.CPU], Record.TSC);

  if (CurrentMaxTSC == 0)
    CurrentMaxTSC = Record.TSC;

  if (Record.TSC < CurrentMaxTSC)
    return false;

  auto &ThreadStack = PerThreadFunctionStack[Record.TId];
  if (RecursiveCallsOnly && !ThreadStack.RecursionDepth)
    ThreadStack.RecursionDepth.emplace();
  switch (Record.Type) {
  case RecordTypes::CUSTOM_EVENT:
  case RecordTypes::TYPED_EVENT:
    return true;
  case RecordTypes::ENTER:
  case RecordTypes::ENTER_ARG: {
    ThreadStack.Stack.emplace_back(Record.FuncId, Record.TSC);
    if (ThreadStack.RecursionDepth)
      ++(*ThreadStack.RecursionDepth)[Record.FuncId];
    break;
  }
  case RecordTypes::EXIT:
  case RecordTypes::TAIL_EXIT: {
    if (ThreadStack.Stack.empty())
      return false;

    if (ThreadStack.Stack.back().first == Record.FuncId) {
      const auto &Top = ThreadStack.Stack.back();
      if (!ThreadStack.RecursionDepth ||
          (*ThreadStack.RecursionDepth)[Top.first].isRecursive())
        recordLatency(Top.first, diff(Top.second, Record.TSC));
      if (ThreadStack.RecursionDepth)
        --(*ThreadStack.RecursionDepth)[Top.first];
      ThreadStack.Stack.pop_back();
      break;
    }

    if (!DeduceSiblingCalls)
      return false;

    auto Parent =
        llvm::find_if(llvm::reverse(ThreadStack.Stack),
                      [&](const std::pair<const int32_t, uint64_t> &E) {
                        return E.first == Record.FuncId;
                      });
    if (Parent == ThreadStack.Stack.rend())
      return false;

    auto R = make_range(std::next(Parent).base(), ThreadStack.Stack.end());
    for (auto &E : R) {
      if (!ThreadStack.RecursionDepth ||
          (*ThreadStack.RecursionDepth)[E.first].isRecursive())
        recordLatency(E.first, diff(E.second, Record.TSC));
    }
    for (auto &Top : reverse(R)) {
      if (ThreadStack.RecursionDepth)
        --(*ThreadStack.RecursionDepth)[Top.first];
      ThreadStack.Stack.pop_back();
    }
    break;
  }
  }

  return true;
}

namespace {

struct ResultRow {
  uint64_t Count;
  double Min;
  double Median;
  double Pct90;
  double Pct99;
  double Max;
  double Sum;
  std::string DebugInfo;
  std::string Function;
};
} // namespace

static ResultRow getStats(MutableArrayRef<uint64_t> Timings) {
  assert(!Timings.empty());
  ResultRow R;
  R.Sum = std::accumulate(Timings.begin(), Timings.end(), 0.0);
  auto MinMax = std::minmax_element(Timings.begin(), Timings.end());
  R.Min = *MinMax.first;
  R.Max = *MinMax.second;
  R.Count = Timings.size();

  auto MedianOff = Timings.size() / 2;
  std::nth_element(Timings.begin(), Timings.begin() + MedianOff, Timings.end());
  R.Median = Timings[MedianOff];

  auto Pct90Off = std::floor(Timings.size() * 0.9);
  std::nth_element(Timings.begin(), Timings.begin() + (uint64_t)Pct90Off,
                   Timings.end());
  R.Pct90 = Timings[Pct90Off];

  auto Pct99Off = std::floor(Timings.size() * 0.99);
  std::nth_element(Timings.begin(), Timings.begin() + (uint64_t)Pct99Off,
                   Timings.end());
  R.Pct99 = Timings[Pct99Off];
  return R;
}

using TupleType = std::tuple<int32_t, uint64_t, ResultRow>;

template <typename F>
static void sortByKey(std::vector<TupleType> &Results, F Fn) {
  bool ASC = AccountSortOrderVal == SortDirection::ASCENDING;
  llvm::sort(Results, [=](const TupleType &L, const TupleType &R) {
    return ASC ? Fn(L) < Fn(R) : Fn(L) > Fn(R);
  });
}

template <class F>
void LatencyAccountant::exportStats(const XRayFileHeader &Header, F Fn) const {
  std::vector<TupleType> Results;
  Results.reserve(FunctionLatencies.size());
  for (auto FT : FunctionLatencies) {
    const auto &FuncId = FT.first;
    auto &Timings = FT.second;
    Results.emplace_back(FuncId, Timings.size(), getStats(Timings));
    auto &Row = std::get<2>(Results.back());
    if (Header.CycleFrequency) {
      double CycleFrequency = Header.CycleFrequency;
      Row.Min /= CycleFrequency;
      Row.Median /= CycleFrequency;
      Row.Pct90 /= CycleFrequency;
      Row.Pct99 /= CycleFrequency;
      Row.Max /= CycleFrequency;
      Row.Sum /= CycleFrequency;
    }

    Row.Function = FuncIdHelper.SymbolOrNumber(FuncId);
    Row.DebugInfo = FuncIdHelper.FileLineAndColumn(FuncId);
  }

  switch (AccountSortOutputVal) {
  case SortField::FUNCID:
    sortByKey(Results, [](const TupleType &X) { return std::get<0>(X); });
    break;
  case SortField::COUNT:
    sortByKey(Results, [](const TupleType &X) { return std::get<1>(X); });
    break;
  case SortField::MIN:
    sortByKey(Results, [](const TupleType &X) { return std::get<2>(X).Min; });
    break;
  case SortField::MED:
    sortByKey(Results, [](const TupleType &X) { return std::get<2>(X).Median; });
    break;
  case SortField::PCT90:
    sortByKey(Results, [](const TupleType &X) { return std::get<2>(X).Pct90; });
    break;
  case SortField::PCT99:
    sortByKey(Results, [](const TupleType &X) { return std::get<2>(X).Pct99; });
    break;
  case SortField::MAX:
    sortByKey(Results, [](const TupleType &X) { return std::get<2>(X).Max; });
    break;
  case SortField::SUM:
    sortByKey(Results, [](const TupleType &X) { return std::get<2>(X).Sum; });
    break;
  case SortField::FUNC:
    llvm_unreachable("Not implemented");
  }

  if (AccountTopVal > 0) {
    auto MaxTop = std::min(AccountTopVal, static_cast<int>(Results.size()));
    Results.erase(Results.begin() + MaxTop, Results.end());
  }

  for (const auto &R : Results)
    Fn(std::get<0>(R), std::get<1>(R), std::get<2>(R));
}

void LatencyAccountant::exportStatsAsText(raw_ostream &OS,
                                          const XRayFileHeader &Header) const {
  OS << "Functions with latencies: " << FunctionLatencies.size() << "\n";

  static constexpr char StatsHeaderFormat[] =
      "{0,+9} {1,+10} [{2,+9}, {3,+9}, {4,+9}, {5,+9}, {6,+9}] {7,+9}";
  static constexpr char StatsFormat[] =
      R"({0,+9} {1,+10} [{2,+9:f6}, {3,+9:f6}, {4,+9:f6}, {5,+9:f6}, {6,+9:f6}] {7,+9:f6})";
  OS << llvm::formatv(StatsHeaderFormat, "funcid", "count", "min", "med", "90p",
                      "99p", "max", "sum")
     << llvm::formatv("  {0,-12}\n", "function");
  exportStats(Header, [&](int32_t FuncId, size_t Count, const ResultRow &Row) {
    OS << llvm::formatv(StatsFormat, FuncId, Count, Row.Min, Row.Median,
                        Row.Pct90, Row.Pct99, Row.Max, Row.Sum)
       << "  " << Row.DebugInfo << ": " << Row.Function << "\n";
  });
}

void LatencyAccountant::exportStatsAsCSV(raw_ostream &OS,
                                         const XRayFileHeader &Header) const {
  OS << "funcid,count,min,median,90%ile,99%ile,max,sum,debug,function\n";
  exportStats(Header, [&](int32_t FuncId, size_t Count, const ResultRow &Row) {
    OS << FuncId << ',' << Count << ',' << Row.Min << ',' << Row.Median << ','
       << Row.Pct90 << ',' << Row.Pct99 << ',' << Row.Max << "," << Row.Sum
       << ",\"" << Row.DebugInfo << "\",\"" << Row.Function << "\"\n";
  });
}

template <> struct llvm::format_provider<RecordTypes> {
  static void format(const RecordTypes &T, raw_ostream &Stream,
                     StringRef Style) {
    switch (T) {
    case RecordTypes::ENTER:
      Stream << "enter";
      break;
    case RecordTypes::ENTER_ARG:
      Stream << "enter-arg";
      break;
    case RecordTypes::EXIT:
      Stream << "exit";
      break;
    case RecordTypes::TAIL_EXIT:
      Stream << "tail-exit";
      break;
    case RecordTypes::CUSTOM_EVENT:
      Stream << "custom-event";
      break;
    case RecordTypes::TYPED_EVENT:
      Stream << "typed-event";
      break;
    }
  }
};

Error tryAccount() {
  InstrumentationMap Map;
  if (!AccountInstrMapVal.empty()) {
    auto InstrumentationMapOrError = loadInstrumentationMap(AccountInstrMapVal);
    if (!InstrumentationMapOrError)
      return joinErrors(make_error<StringError>(
                            Twine("Cannot open instrumentation map '") +
                                AccountInstrMapVal + "'",
                            std::make_error_code(std::errc::invalid_argument)),
                        InstrumentationMapOrError.takeError());
    Map = std::move(*InstrumentationMapOrError);
  }

  std::error_code EC;
  raw_fd_ostream OS(AccountOutputVal, EC, sys::fs::OpenFlags::OF_TextWithCRLF);
  if (EC)
    return make_error<StringError>(
        Twine("Cannot open file '") + AccountOutputVal + "' for writing.", EC);

  const auto &FunctionAddresses = Map.getFunctionAddresses();
  symbolize::LLVMSymbolizer Symbolizer;
  FuncIdConversionHelper FuncIdHelper(AccountInstrMapVal, Symbolizer,
                                      FunctionAddresses);
  LatencyAccountant FCA(FuncIdHelper, AccountRecursiveCallsOnlyVal,
                        AccountDeduceSiblingCallsVal);
  auto TraceOrErr = loadTraceFile(AccountInputVal);
  if (!TraceOrErr)
    return joinErrors(
        make_error<StringError>(
            Twine("Failed loading input file '") + AccountInputVal + "'",
            std::make_error_code(std::errc::executable_format_error)),
        TraceOrErr.takeError());

  auto &T = *TraceOrErr;
  for (const auto &Record : T) {
    if (FCA.accountRecord(Record))
      continue;
    errs()
        << "Error processing record: "
        << llvm::formatv(
               R"({{type: {0}; cpu: {1}; record-type: {2}; function-id: {3}; tsc: {4}; thread-id: {5}; process-id: {6}}})",
               Record.RecordType, Record.CPU, Record.Type, Record.FuncId,
               Record.TSC, Record.TId, Record.PId)
        << '\n';
    for (const auto &ThreadStack : FCA.getPerThreadFunctionStack()) {
      errs() << "Thread ID: " << ThreadStack.first << "\n";
      if (ThreadStack.second.Stack.empty()) {
        errs() << "  (empty stack)\n";
        continue;
      }
      auto Level = ThreadStack.second.Stack.size();
      for (const auto &Entry : llvm::reverse(ThreadStack.second.Stack))
        errs() << "  #" << Level-- << "\t"
               << FuncIdHelper.SymbolOrNumber(Entry.first) << '\n';
    }
    if (!AccountKeepGoingVal)
      return make_error<StringError>(
          Twine("Failed accounting function calls in file '") +
              AccountInputVal + "'.",
          std::make_error_code(std::errc::executable_format_error));
  }
  switch (AccountOutputFormatVal) {
  case AccountOutputFormats::TEXT:
    FCA.exportStatsAsText(OS, T.getFileHeader());
    break;
  case AccountOutputFormats::CSV:
    FCA.exportStatsAsCSV(OS, T.getFileHeader());
    break;
  }

  return Error::success();
}
