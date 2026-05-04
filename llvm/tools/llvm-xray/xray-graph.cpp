//===-- xray-graph.cpp: XRay Function Call Graph Renderer -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generate a DOT file to represent the function call graph encountered in
// the trace.
//
//===----------------------------------------------------------------------===//

#include "xray-graph.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/XRay/InstrumentationMap.h"
#include "llvm/XRay/Trace.h"

#include <cmath>

using namespace llvm;
using namespace llvm::xray;

std::string GraphInputVal;
bool GraphKeepGoingVal;
std::string GraphOutputVal;
std::string GraphInstrMapVal;
bool GraphDeduceSiblingCallsVal;
GraphRenderer::StatType GraphEdgeLabelVal;
GraphRenderer::StatType GraphVertexLabelVal;
GraphRenderer::StatType GraphEdgeColorTypeVal;
GraphRenderer::StatType GraphVertexColorTypeVal;

template <class T> static T diff(T L, T R) {
  return std::max(L, R) - std::min(L, R);
}

static void updateStat(GraphRenderer::TimeStat &S, int64_t L) {
  S.Count++;
  if (S.Min > L || S.Min == 0)
    S.Min = L;
  if (S.Max < L)
    S.Max = L;
  S.Sum += L;
}

static std::string escapeString(StringRef Label) {
  std::string Str;
  Str.reserve(Label.size());
  for (const auto C : Label) {
    switch (C) {
    case '&':
      Str.append("&amp;");
      break;
    case '<':
      Str.append("&lt;");
      break;
    case '>':
      Str.append("&gt;");
      break;
    default:
      Str.push_back(C);
      break;
    }
  }
  return Str;
}

Error GraphRenderer::accountRecord(const XRayRecord &Record) {
  using std::make_error_code;
  using std::errc;
  if (CurrentMaxTSC == 0)
    CurrentMaxTSC = Record.TSC;

  if (Record.TSC < CurrentMaxTSC)
    return make_error<StringError>("Records not in order",
                                   make_error_code(errc::invalid_argument));

  auto &ThreadStack = PerThreadFunctionStack[Record.TId];
  switch (Record.Type) {
  case RecordTypes::ENTER:
  case RecordTypes::ENTER_ARG: {
    if (Record.FuncId != 0 && G.count(Record.FuncId) == 0)
      G[Record.FuncId].SymbolName = FuncIdHelper.SymbolOrNumber(Record.FuncId);
    ThreadStack.push_back({Record.FuncId, Record.TSC});
    break;
  }
  case RecordTypes::EXIT:
  case RecordTypes::TAIL_EXIT: {
    if (ThreadStack.size() == 0 || ThreadStack.back().FuncId != Record.FuncId) {
      if (!DeduceSiblingCalls)
        return make_error<StringError>("No matching ENTRY record",
                                       make_error_code(errc::invalid_argument));
      bool FoundParent =
          llvm::any_of(llvm::reverse(ThreadStack), [&](const FunctionAttr &A) {
            return A.FuncId == Record.FuncId;
          });
      if (!FoundParent)
        return make_error<StringError>("No matching Entry record in stack",
                                       make_error_code(errc::invalid_argument));
      while (ThreadStack.back().FuncId != Record.FuncId) {
        TimestampT D = diff(ThreadStack.back().TSC, Record.TSC);
        VertexIdentifier TopFuncId = ThreadStack.back().FuncId;
        ThreadStack.pop_back();
        assert(ThreadStack.size() != 0);
        EdgeIdentifier EI(ThreadStack.back().FuncId, TopFuncId);
        auto &EA = G[EI];
        EA.Timings.push_back(D);
        updateStat(EA.S, D);
        updateStat(G[TopFuncId].S, D);
      }
    }
    uint64_t D = diff(ThreadStack.back().TSC, Record.TSC);
    ThreadStack.pop_back();
    VertexIdentifier VI = ThreadStack.empty() ? 0 : ThreadStack.back().FuncId;
    EdgeIdentifier EI(VI, Record.FuncId);
    auto &EA = G[EI];
    EA.Timings.push_back(D);
    updateStat(EA.S, D);
    updateStat(G[Record.FuncId].S, D);
    break;
  }
  case RecordTypes::CUSTOM_EVENT:
  case RecordTypes::TYPED_EVENT:
    break;
  }

  return Error::success();
}

template <typename U>
void GraphRenderer::getStats(U begin, U end, GraphRenderer::TimeStat &S) {
  if (begin == end) return;
  std::ptrdiff_t MedianOff = S.Count / 2;
  std::nth_element(begin, begin + MedianOff, end);
  S.Median = *(begin + MedianOff);
  std::ptrdiff_t Pct90Off = (S.Count * 9) / 10;
  std::nth_element(begin, begin + Pct90Off, end);
  S.Pct90 = *(begin + Pct90Off);
  std::ptrdiff_t Pct99Off = (S.Count * 99) / 100;
  std::nth_element(begin, begin + Pct99Off, end);
  S.Pct99 = *(begin + Pct99Off);
}

void GraphRenderer::updateMaxStats(const GraphRenderer::TimeStat &S,
                                   GraphRenderer::TimeStat &M) {
  M.Count = std::max(M.Count, S.Count);
  M.Min = std::max(M.Min, S.Min);
  M.Median = std::max(M.Median, S.Median);
  M.Pct90 = std::max(M.Pct90, S.Pct90);
  M.Pct99 = std::max(M.Pct99, S.Pct99);
  M.Max = std::max(M.Max, S.Max);
  M.Sum = std::max(M.Sum, S.Sum);
}

void GraphRenderer::calculateEdgeStatistics() {
  assert(!G.edges().empty());
  for (auto &E : G.edges()) {
    auto &A = E.second;
    assert(!A.Timings.empty());
    getStats(A.Timings.begin(), A.Timings.end(), A.S);
    updateMaxStats(A.S, G.GraphEdgeMax);
  }
}

void GraphRenderer::calculateVertexStatistics() {
  std::vector<uint64_t> TempTimings;
  for (auto &V : G.vertices()) {
    if (V.first != 0) {
      for (auto &E : G.inEdges(V.first)) {
        auto &A = E.second;
        llvm::append_range(TempTimings, A.Timings);
      }
      getStats(TempTimings.begin(), TempTimings.end(), G[V.first].S);
      updateMaxStats(G[V.first].S, G.GraphVertexMax);
      TempTimings.clear();
    }
  }
}

static void normalizeTimeStat(GraphRenderer::TimeStat &S,
                              double CycleFrequency) {
  int64_t OldCount = S.Count;
  S = S / CycleFrequency;
  S.Count = OldCount;
}

void GraphRenderer::normalizeStatistics(double CycleFrequency) {
  for (auto &E : G.edges()) {
    auto &S = E.second.S;
    normalizeTimeStat(S, CycleFrequency);
  }
  for (auto &V : G.vertices()) {
    auto &S = V.second.S;
    normalizeTimeStat(S, CycleFrequency);
  }

  normalizeTimeStat(G.GraphEdgeMax, CycleFrequency);
  normalizeTimeStat(G.GraphVertexMax, CycleFrequency);
}

std::string
GraphRenderer::TimeStat::getString(GraphRenderer::StatType T) const {
  std::string St;
  raw_string_ostream S{St};
  double TimeStat::*DoubleStatPtrs[] = {&TimeStat::Min,   &TimeStat::Median,
                                        &TimeStat::Pct90, &TimeStat::Pct99,
                                        &TimeStat::Max,   &TimeStat::Sum};
  switch (T) {
  case GraphRenderer::StatType::NONE:
    break;
  case GraphRenderer::StatType::COUNT:
    S << Count;
    break;
  default:
    S << (*this).*
             DoubleStatPtrs[static_cast<int>(T) -
                            static_cast<int>(GraphRenderer::StatType::MIN)];
    break;
  }
  return St;
}

double GraphRenderer::TimeStat::getDouble(StatType T) const {
  double retval = 0;
  double TimeStat::*DoubleStatPtrs[] = {&TimeStat::Min,   &TimeStat::Median,
                                        &TimeStat::Pct90, &TimeStat::Pct99,
                                        &TimeStat::Max,   &TimeStat::Sum};
  switch (T) {
  case GraphRenderer::StatType::NONE:
    retval = 0.0;
    break;
  case GraphRenderer::StatType::COUNT:
    retval = static_cast<double>(Count);
    break;
  default:
    retval =
        (*this).*DoubleStatPtrs[static_cast<int>(T) -
                                static_cast<int>(GraphRenderer::StatType::MIN)];
    break;
  }
  return retval;
}

void GraphRenderer::exportGraphAsDOT(raw_ostream &OS, StatType ET, StatType EC,
                                     StatType VT, StatType VC) {
  OS << "digraph xray {\n";

  if (VT != StatType::NONE)
    OS << "node [shape=record];\n";

  for (const auto &E : G.edges()) {
    const auto &S = E.second.S;
    OS << "F" << E.first.first << " -> "
       << "F" << E.first.second << " [label=\"" << S.getString(ET) << "\"";
    if (EC != StatType::NONE)
      OS << " color=\""
         << CHelper.getColorString(
                std::sqrt(S.getDouble(EC) / G.GraphEdgeMax.getDouble(EC)))
         << "\"";
    OS << "];\n";
  }

  for (const auto &V : G.vertices()) {
    const auto &VA = V.second;
    if (V.first == 0)
      continue;
    OS << "F" << V.first << " [label=\"" << (VT != StatType::NONE ? "{" : "")
       << escapeString(VA.SymbolName.size() > 40
                           ? VA.SymbolName.substr(0, 40) + "..."
                           : VA.SymbolName);
    if (VT != StatType::NONE)
      OS << "|" << VA.S.getString(VT) << "}\"";
    else
      OS << "\"";
    if (VC != StatType::NONE)
      OS << " color=\""
         << CHelper.getColorString(
                std::sqrt(VA.S.getDouble(VC) / G.GraphVertexMax.getDouble(VC)))
         << "\"";
    OS << "];\n";
  }
  OS << "}\n";
}

Expected<GraphRenderer> GraphRenderer::Factory::getGraphRenderer() {
  InstrumentationMap Map;
  if (!InstrMap.empty()) {
    auto InstrumentationMapOrError = loadInstrumentationMap(InstrMap);
    if (!InstrumentationMapOrError)
      return joinErrors(
          make_error<StringError>(
              Twine("Cannot open instrumentation map '") + InstrMap + "'",
              std::make_error_code(std::errc::invalid_argument)),
          InstrumentationMapOrError.takeError());
    Map = std::move(*InstrumentationMapOrError);
  }

  const auto &FunctionAddresses = Map.getFunctionAddresses();

  symbolize::LLVMSymbolizer Symbolizer;
  const auto &Header = Trace.getFileHeader();

  FuncIdConversionHelper FuncIdHelper(InstrMap, Symbolizer, FunctionAddresses);

  GraphRenderer GR(FuncIdHelper, DeduceSiblingCalls);
  for (const auto &Record : Trace) {
    auto E = GR.accountRecord(Record);
    if (!E)
      continue;

    for (const auto &ThreadStack : GR.getPerThreadFunctionStack()) {
      errs() << "Thread ID: " << ThreadStack.first << "\n";
      auto Level = ThreadStack.second.size();
      for (const auto &Entry : llvm::reverse(ThreadStack.second))
        errs() << "#" << Level-- << "\t"
               << FuncIdHelper.SymbolOrNumber(Entry.FuncId) << '\n';
    }

    if (!KeepGoing)
      return joinErrors(make_error<StringError>(
                            "Error encountered generating the call graph.",
                            std::make_error_code(std::errc::invalid_argument)),
                        std::move(E));

    handleAllErrors(std::move(E),
                    [&](const ErrorInfoBase &E) { E.log(errs()); });
  }

  GR.G.GraphEdgeMax = {};
  GR.G.GraphVertexMax = {};
  GR.calculateEdgeStatistics();
  GR.calculateVertexStatistics();

  if (Header.CycleFrequency)
    GR.normalizeStatistics(Header.CycleFrequency);

  return GR;
}

Error tryGraph() {
  GraphRenderer::Factory F;

  F.KeepGoing = GraphKeepGoingVal;
  F.DeduceSiblingCalls = GraphDeduceSiblingCallsVal;
  F.InstrMap = GraphInstrMapVal;

  auto TraceOrErr = loadTraceFile(GraphInputVal, true);

  if (!TraceOrErr)
    return make_error<StringError>(
        Twine("Failed loading input file '") + GraphInputVal + "'",
        make_error_code(llvm::errc::invalid_argument));

  F.Trace = std::move(*TraceOrErr);
  auto GROrError = F.getGraphRenderer();
  if (!GROrError)
    return GROrError.takeError();
  auto &GR = *GROrError;

  std::error_code EC;
  raw_fd_ostream OS(GraphOutputVal, EC, sys::fs::OpenFlags::OF_TextWithCRLF);
  if (EC)
    return make_error<StringError>(
        Twine("Cannot open file '") + GraphOutputVal + "' for writing.", EC);

  GR.exportGraphAsDOT(OS, GraphEdgeLabelVal, GraphEdgeColorTypeVal,
                      GraphVertexLabelVal, GraphVertexColorTypeVal);
  return Error::success();
}
