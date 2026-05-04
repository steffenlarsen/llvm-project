//===- xray-stacks.cpp: XRay Function Call Stack Accounting ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements stack-based accounting. It takes XRay traces, and
// collates statistics across these traces to show a breakdown of time spent
// at various points of the stack to provide insight into which functions
// spend the most time in terms of a call stack. We provide a few
// sorting/filtering options for zero'ing in on the useful stacks.
//
//===----------------------------------------------------------------------===//

#include <forward_list>
#include <numeric>

#include "func-id-helper.h"
#include "trie-node.h"
#include "xray-stacks.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormatAdapters.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/XRay/Graph.h"
#include "llvm/XRay/InstrumentationMap.h"
#include "llvm/XRay/Trace.h"

using namespace llvm;
using namespace llvm::xray;

std::vector<std::string> StackInputsVal;
bool StackKeepGoingVal;
std::string StacksInstrMapVal;
bool SeparateThreadStacksVal;
bool AggregateThreadsVal;
bool DumpAllStacksVal;
StackOutputFormat StacksOutputFormatVal;
AggregationType RequestedAggregationVal;

namespace {

struct format_xray_record : public FormatAdapter<XRayRecord> {
  explicit format_xray_record(XRayRecord record,
                              const FuncIdConversionHelper &conv)
      : FormatAdapter<XRayRecord>(std::move(record)), Converter(&conv) {}
  void format(raw_ostream &Stream, StringRef Style) {
    Stream << formatv(
        "{FuncId: \"{0}\", ThreadId: \"{1}\", RecordType: \"{2}\"}",
        Converter->SymbolOrNumber(Item.FuncId), Item.TId,
        DecodeRecordType(Item.RecordType));
  }

private:
  Twine DecodeRecordType(uint16_t recordType) {
    switch (recordType) {
    case 0:
      return Twine("Fn Entry");
    case 1:
      return Twine("Fn Exit");
    default:
      return Twine("Unknown");
    }
  }

  const FuncIdConversionHelper *Converter;
};

struct StackDuration {
  SmallVector<int64_t, 4> TerminalDurations;
  SmallVector<int64_t, 4> IntermediateDurations;
};
} // namespace

static StackDuration mergeStackDuration(const StackDuration &Left,
                                        const StackDuration &Right) {
  StackDuration Data{};
  Data.TerminalDurations.reserve(Left.TerminalDurations.size() +
                                 Right.TerminalDurations.size());
  Data.IntermediateDurations.reserve(Left.IntermediateDurations.size() +
                                     Right.IntermediateDurations.size());
  llvm::append_range(Data.TerminalDurations, Left.TerminalDurations);
  llvm::append_range(Data.TerminalDurations, Right.TerminalDurations);

  llvm::append_range(Data.IntermediateDurations, Left.IntermediateDurations);
  llvm::append_range(Data.IntermediateDurations, Right.IntermediateDurations);
  return Data;
}

using StackTrieNode = TrieNode<StackDuration>;

template <AggregationType AggType>
static std::size_t GetValueForStack(const StackTrieNode *Node);

template <>
std::size_t
GetValueForStack<AggregationType::TOTAL_TIME>(const StackTrieNode *Node) {
  auto TopSum = std::accumulate(Node->ExtraData.TerminalDurations.begin(),
                                Node->ExtraData.TerminalDurations.end(), 0uLL);
  return std::accumulate(Node->ExtraData.IntermediateDurations.begin(),
                         Node->ExtraData.IntermediateDurations.end(), TopSum);
}

template <>
std::size_t
GetValueForStack<AggregationType::INVOCATION_COUNT>(const StackTrieNode *Node) {
  return Node->ExtraData.TerminalDurations.size() +
         Node->ExtraData.IntermediateDurations.size();
}

template <AggregationType T> struct DependentFalseType : std::false_type {};

template <AggregationType AggType>
std::size_t GetValueForStack(const StackTrieNode *Node) {
  static_assert(DependentFalseType<AggType>::value,
                "No implementation found for aggregation type provided.");
  return 0;
}

namespace {
class StackTrie {
  using RootVector = SmallVector<StackTrieNode *, 4>;

  DenseMap<uint32_t, RootVector> Roots;

  std::forward_list<StackTrieNode> NodeStore;

  DenseMap<uint32_t, SmallVector<std::pair<StackTrieNode *, uint64_t>, 8>>
      ThreadStackMap;

  StackTrieNode *createTrieNode(uint32_t ThreadId, int32_t FuncId,
                                StackTrieNode *Parent) {
    NodeStore.push_front(StackTrieNode{FuncId, Parent, {}, {{}, {}}});
    auto I = NodeStore.begin();
    auto *Node = &*I;
    if (!Parent)
      Roots[ThreadId].push_back(Node);
    return Node;
  }

  StackTrieNode *findRootNode(uint32_t ThreadId, int32_t FuncId) {
    const auto &RootsByThread = Roots[ThreadId];
    auto I = find_if(RootsByThread,
                     [&](StackTrieNode *N) { return N->FuncId == FuncId; });
    return (I == RootsByThread.end()) ? nullptr : *I;
  }

public:
  enum class AccountRecordStatus { OK, ENTRY_NOT_FOUND, UNKNOWN_RECORD_TYPE };

  struct AccountRecordState {
    bool wasLastRecordExit;

    static AccountRecordState CreateInitialState() { return {false}; }
  };

  AccountRecordStatus accountRecord(const XRayRecord &R,
                                    AccountRecordState *state) {
    auto &TS = ThreadStackMap[R.TId];
    switch (R.Type) {
    case RecordTypes::CUSTOM_EVENT:
    case RecordTypes::TYPED_EVENT:
      return AccountRecordStatus::OK;
    case RecordTypes::ENTER:
    case RecordTypes::ENTER_ARG: {
      state->wasLastRecordExit = false;
      if (TS.empty()) {
        auto *Root = findRootNode(R.TId, R.FuncId);
        TS.emplace_back(Root ? Root : createTrieNode(R.TId, R.FuncId, nullptr),
                        R.TSC);
        return AccountRecordStatus::OK;
      }

      auto &Top = TS.back();
      auto I = find_if(Top.first->Callees,
                       [&](StackTrieNode *N) { return N->FuncId == R.FuncId; });
      if (I == Top.first->Callees.end()) {
        auto N = createTrieNode(R.TId, R.FuncId, Top.first);
        Top.first->Callees.emplace_back(N);

        TS.emplace_back(N, R.TSC);
      } else {
        TS.emplace_back(*I, R.TSC);
      }
      return AccountRecordStatus::OK;
    }
    case RecordTypes::EXIT:
    case RecordTypes::TAIL_EXIT: {
      bool wasLastRecordExit = state->wasLastRecordExit;
      state->wasLastRecordExit = true;
      if (TS.empty()) {
        return AccountRecordStatus::ENTRY_NOT_FOUND;
      }

      auto FunctionEntryMatch = find_if(
          reverse(TS), [&](const std::pair<StackTrieNode *, uint64_t> &E) {
            return E.first->FuncId == R.FuncId;
          });
      auto status = AccountRecordStatus::OK;
      if (FunctionEntryMatch == TS.rend()) {
        status = AccountRecordStatus::ENTRY_NOT_FOUND;
      } else {
        ++FunctionEntryMatch;
      }
      auto I = FunctionEntryMatch.base();
      for (auto &E : make_range(I, TS.end() - 1))
        E.first->ExtraData.IntermediateDurations.push_back(
            std::max(E.second, R.TSC) - std::min(E.second, R.TSC));
      auto &Deepest = TS.back();
      if (wasLastRecordExit)
        Deepest.first->ExtraData.IntermediateDurations.push_back(
            std::max(Deepest.second, R.TSC) - std::min(Deepest.second, R.TSC));
      else
        Deepest.first->ExtraData.TerminalDurations.push_back(
            std::max(Deepest.second, R.TSC) - std::min(Deepest.second, R.TSC));
      TS.erase(I, TS.end());
      return status;
    }
    }
    return AccountRecordStatus::UNKNOWN_RECORD_TYPE;
  }

  bool isEmpty() const { return Roots.empty(); }

  void printStack(raw_ostream &OS, const StackTrieNode *Top,
                  FuncIdConversionHelper &FN) {
    SmallVector<const StackTrieNode *, 8> CurrentStack;
    for (auto *F = Top; F != nullptr; F = F->Parent)
      CurrentStack.push_back(F);
    int Level = 0;
    OS << formatv("{0,-5} {1,-60} {2,+12} {3,+16}\n", "lvl", "function",
                  "count", "sum");
    for (auto *F : reverse(drop_begin(CurrentStack))) {
      auto Sum = std::accumulate(F->ExtraData.IntermediateDurations.begin(),
                                 F->ExtraData.IntermediateDurations.end(), 0LL);
      auto FuncId = FN.SymbolOrNumber(F->FuncId);
      OS << formatv("#{0,-4} {1,-60} {2,+12} {3,+16}\n", Level++,
                    FuncId.size() > 60 ? FuncId.substr(0, 57) + "..." : FuncId,
                    F->ExtraData.IntermediateDurations.size(), Sum);
    }
    auto *Leaf = *CurrentStack.begin();
    auto LeafSum =
        std::accumulate(Leaf->ExtraData.TerminalDurations.begin(),
                        Leaf->ExtraData.TerminalDurations.end(), 0LL);
    auto LeafFuncId = FN.SymbolOrNumber(Leaf->FuncId);
    OS << formatv("#{0,-4} {1,-60} {2,+12} {3,+16}\n", Level++,
                  LeafFuncId.size() > 60 ? LeafFuncId.substr(0, 57) + "..."
                                         : LeafFuncId,
                  Leaf->ExtraData.TerminalDurations.size(), LeafSum);
    OS << "\n";
  }

  void printPerThread(raw_ostream &OS, FuncIdConversionHelper &FN) {
    for (const auto &iter : Roots) {
      OS << "Thread " << iter.first << ":\n";
      print(OS, FN, iter.second);
      OS << "\n";
    }
  }

  template <AggregationType AggType>
  void printAllPerThread(raw_ostream &OS, FuncIdConversionHelper &FN,
                         StackOutputFormat format) {
    for (const auto &iter : Roots)
      printAll<AggType>(OS, FN, iter.second, iter.first, true);
  }

  void printIgnoringThreads(raw_ostream &OS, FuncIdConversionHelper &FN) {
    RootVector RootValues;

    for (const auto &RootNodeRange : make_second_range(Roots))
      llvm::append_range(RootValues, RootNodeRange);

    print(OS, FN, RootValues);
  }

  RootVector mergeAcrossThreads(std::forward_list<StackTrieNode> &NodeStore) {
    RootVector MergedByThreadRoots;
    for (const auto &MapIter : Roots) {
      const auto &RootNodeVector = MapIter.second;
      for (auto *Node : RootNodeVector) {
        auto MaybeFoundIter =
            find_if(MergedByThreadRoots, [Node](StackTrieNode *elem) {
              return Node->FuncId == elem->FuncId;
            });
        if (MaybeFoundIter == MergedByThreadRoots.end()) {
          MergedByThreadRoots.push_back(Node);
        } else {
          MergedByThreadRoots.push_back(mergeTrieNodes(
              **MaybeFoundIter, *Node, nullptr, NodeStore, mergeStackDuration));
          MergedByThreadRoots.erase(MaybeFoundIter);
        }
      }
    }
    return MergedByThreadRoots;
  }

  template <AggregationType AggType>
  void printAllAggregatingThreads(raw_ostream &OS, FuncIdConversionHelper &FN,
                                  StackOutputFormat format) {
    std::forward_list<StackTrieNode> AggregatedNodeStore;
    RootVector MergedByThreadRoots = mergeAcrossThreads(AggregatedNodeStore);
    bool reportThreadId = false;
    printAll<AggType>(OS, FN, MergedByThreadRoots,
                      /*threadId*/ 0, reportThreadId);
  }

  void printAggregatingThreads(raw_ostream &OS, FuncIdConversionHelper &FN) {
    std::forward_list<StackTrieNode> AggregatedNodeStore;
    RootVector MergedByThreadRoots = mergeAcrossThreads(AggregatedNodeStore);
    print(OS, FN, MergedByThreadRoots);
  }

  template <AggregationType AggType>
  void printAll(raw_ostream &OS, FuncIdConversionHelper &FN,
                RootVector RootValues, uint32_t ThreadId, bool ReportThread) {
    SmallVector<const StackTrieNode *, 16> S;
    for (const auto *N : RootValues) {
      S.clear();
      S.push_back(N);
      while (!S.empty()) {
        auto *Top = S.pop_back_val();
        printSingleStack<AggType>(OS, FN, ReportThread, ThreadId, Top);
        llvm::append_range(S, Top->Callees);
      }
    }
  }

  template <AggregationType AggType>
  void printSingleStack(raw_ostream &OS, FuncIdConversionHelper &Converter,
                        bool ReportThread, uint32_t ThreadId,
                        const StackTrieNode *Node) {
    if (ReportThread)
      OS << "thread_" << ThreadId << ";";
    SmallVector<const StackTrieNode *, 5> lineage{};
    lineage.push_back(Node);
    while (lineage.back()->Parent != nullptr)
      lineage.push_back(lineage.back()->Parent);
    while (!lineage.empty()) {
      OS << Converter.SymbolOrNumber(lineage.back()->FuncId) << ";";
      lineage.pop_back();
    }
    OS << " " << GetValueForStack<AggType>(Node) << "\n";
  }

  void print(raw_ostream &OS, FuncIdConversionHelper &FN,
             RootVector RootValues) {
    SmallVector<std::pair<const StackTrieNode *, uint64_t>, 11>
        TopStacksByCount;
    SmallVector<std::pair<const StackTrieNode *, uint64_t>, 11> TopStacksBySum;
    auto greater_second =
        [](const std::pair<const StackTrieNode *, uint64_t> &A,
           const std::pair<const StackTrieNode *, uint64_t> &B) {
          return A.second > B.second;
        };
    uint64_t UniqueStacks = 0;
    for (const auto *N : RootValues) {
      SmallVector<const StackTrieNode *, 16> S;
      S.emplace_back(N);

      while (!S.empty()) {
        auto *Top = S.pop_back_val();

        if (!Top->ExtraData.TerminalDurations.empty()) {
          ++UniqueStacks;
          auto TopSum =
              std::accumulate(Top->ExtraData.TerminalDurations.begin(),
                              Top->ExtraData.TerminalDurations.end(), 0uLL);
          {
            auto E = std::make_pair(Top, TopSum);
            TopStacksBySum.insert(
                llvm::lower_bound(TopStacksBySum, E, greater_second), E);
            if (TopStacksBySum.size() == 11)
              TopStacksBySum.pop_back();
          }
          {
            auto E =
                std::make_pair(Top, Top->ExtraData.TerminalDurations.size());
            TopStacksByCount.insert(
                llvm::lower_bound(TopStacksByCount, E, greater_second), E);
            if (TopStacksByCount.size() == 11)
              TopStacksByCount.pop_back();
          }
        }
        llvm::append_range(S, Top->Callees);
      }
    }

    OS << "\n";
    OS << "Unique Stacks: " << UniqueStacks << "\n";
    OS << "Top 10 Stacks by leaf sum:\n\n";
    for (const auto &P : TopStacksBySum) {
      OS << "Sum: " << P.second << "\n";
      printStack(OS, P.first, FN);
    }
    OS << "\n";
    OS << "Top 10 Stacks by leaf count:\n\n";
    for (const auto &P : TopStacksByCount) {
      OS << "Count: " << P.second << "\n";
      printStack(OS, P.first, FN);
    }
    OS << "\n";
  }
};
} // namespace

static std::string CreateErrorMessage(StackTrie::AccountRecordStatus Error,
                                      const XRayRecord &Record,
                                      const FuncIdConversionHelper &Converter) {
  switch (Error) {
  case StackTrie::AccountRecordStatus::ENTRY_NOT_FOUND:
    return std::string(
        formatv("Found record {0} with no matching function entry\n",
                format_xray_record(Record, Converter)));
  default:
    return std::string(formatv("Unknown error type for record {0}\n",
                               format_xray_record(Record, Converter)));
  }
}

Error tryStack() {
  StackTrie ST;
  InstrumentationMap Map;
  if (!StacksInstrMapVal.empty()) {
    auto InstrumentationMapOrError = loadInstrumentationMap(StacksInstrMapVal);
    if (!InstrumentationMapOrError)
      return joinErrors(
          make_error<StringError>(
              Twine("Cannot open instrumentation map: ") + StacksInstrMapVal,
              std::make_error_code(std::errc::invalid_argument)),
          InstrumentationMapOrError.takeError());
    Map = std::move(*InstrumentationMapOrError);
  }

  if (SeparateThreadStacksVal && AggregateThreadsVal)
    return make_error<StringError>(
        Twine("Can't specify options for per thread reporting and reporting "
              "that aggregates threads."),
        std::make_error_code(std::errc::invalid_argument));

  if (!DumpAllStacksVal && StacksOutputFormatVal != HUMAN)
    return make_error<StringError>(
        Twine("Can't specify a non-human format without -all-stacks."),
        std::make_error_code(std::errc::invalid_argument));

  if (DumpAllStacksVal && StacksOutputFormatVal == HUMAN)
    return make_error<StringError>(
        Twine("You must specify a non-human format when reporting with "
              "-all-stacks."),
        std::make_error_code(std::errc::invalid_argument));

  symbolize::LLVMSymbolizer Symbolizer;
  FuncIdConversionHelper FuncIdHelper(StacksInstrMapVal, Symbolizer,
                                      Map.getFunctionAddresses());
  for (const auto &Filename : StackInputsVal) {
    auto TraceOrErr = loadTraceFile(Filename);
    if (!TraceOrErr) {
      if (!StackKeepGoingVal)
        return joinErrors(
            make_error<StringError>(
                Twine("Failed loading input file '") + Filename + "'",
                std::make_error_code(std::errc::invalid_argument)),
            TraceOrErr.takeError());
      logAllUnhandledErrors(TraceOrErr.takeError(), errs());
      continue;
    }
    auto &T = *TraceOrErr;
    StackTrie::AccountRecordState AccountRecordState =
        StackTrie::AccountRecordState::CreateInitialState();
    for (const auto &Record : T) {
      auto error = ST.accountRecord(Record, &AccountRecordState);
      if (error != StackTrie::AccountRecordStatus::OK) {
        if (!StackKeepGoingVal)
          return make_error<StringError>(
              CreateErrorMessage(error, Record, FuncIdHelper),
              make_error_code(errc::illegal_byte_sequence));
        errs() << CreateErrorMessage(error, Record, FuncIdHelper);
      }
    }
  }
  if (ST.isEmpty()) {
    return make_error<StringError>(
        "No instrumented calls were accounted in the input file.",
        make_error_code(errc::result_out_of_range));
  }

  if (DumpAllStacksVal) {
    if (AggregateThreadsVal) {
      switch (RequestedAggregationVal) {
      case AggregationType::TOTAL_TIME:
        ST.printAllAggregatingThreads<AggregationType::TOTAL_TIME>(
            outs(), FuncIdHelper, StacksOutputFormatVal);
        break;
      case AggregationType::INVOCATION_COUNT:
        ST.printAllAggregatingThreads<AggregationType::INVOCATION_COUNT>(
            outs(), FuncIdHelper, StacksOutputFormatVal);
        break;
      }
    } else {
      switch (RequestedAggregationVal) {
      case AggregationType::TOTAL_TIME:
        ST.printAllPerThread<AggregationType::TOTAL_TIME>(
            outs(), FuncIdHelper, StacksOutputFormatVal);
        break;
      case AggregationType::INVOCATION_COUNT:
        ST.printAllPerThread<AggregationType::INVOCATION_COUNT>(
            outs(), FuncIdHelper, StacksOutputFormatVal);
        break;
      }
    }
    return Error::success();
  }

  if (AggregateThreadsVal) {
    ST.printAggregatingThreads(outs(), FuncIdHelper);
  } else if (SeparateThreadStacksVal) {
    ST.printPerThread(outs(), FuncIdHelper);
  } else {
    ST.printIgnoringThreads(outs(), FuncIdHelper);
  }
  return Error::success();
}
