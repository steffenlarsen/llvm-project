//===-- xray-graph-diff.cpp: XRay Function Call Graph Renderer ------------===//
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
#include <cassert>
#include <cmath>
#include <limits>
#include <string>

#include "xray-graph-diff.h"
#include "xray-graph.h"

#include "xray-color-helper.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/XRay/Trace.h"

using namespace llvm;
using namespace xray;

std::string GraphDiffInput1Val;
std::string GraphDiffInput2Val;
bool GraphDiffKeepGoing1Val;
bool GraphDiffKeepGoing2Val;
std::string GraphDiffInstrMap1Val;
std::string GraphDiffInstrMap2Val;
bool GraphDiffDeduceSiblingCalls1Val;
bool GraphDiffDeduceSiblingCalls2Val;
GraphRenderer::StatType GraphDiffEdgeLabelVal;
GraphRenderer::StatType GraphDiffEdgeColorVal;
GraphRenderer::StatType GraphDiffVertexLabelVal;
GraphRenderer::StatType GraphDiffVertexColorVal;
int GraphDiffVertexLabelTruncVal;
std::string GraphDiffOutputVal;

Expected<GraphDiffRenderer> GraphDiffRenderer::Factory::getGraphDiffRenderer() {
  GraphDiffRenderer R;

  for (int i = 0; i < N; ++i) {
    const auto &G = this->G[i].get();
    for (const auto &V : G.vertices()) {
      const auto &VAttr = V.second;
      R.G[VAttr.SymbolName].CorrVertexPtr[i] = &V;
    }
    for (const auto &E : G.edges()) {
      auto &EdgeTailID = E.first.first;
      auto &EdgeHeadID = E.first.second;
      auto EdgeTailAttrOrErr = G.at(EdgeTailID);
      auto EdgeHeadAttrOrErr = G.at(EdgeHeadID);
      if (!EdgeTailAttrOrErr)
        return EdgeTailAttrOrErr.takeError();
      if (!EdgeHeadAttrOrErr)
        return EdgeHeadAttrOrErr.takeError();
      GraphT::EdgeIdentifier ID{EdgeTailAttrOrErr->SymbolName,
                                EdgeHeadAttrOrErr->SymbolName};
      R.G[ID].CorrEdgePtr[i] = &E;
    }
  }

  return R;
}

static double statRelDiff(const GraphDiffRenderer::TimeStat &LeftStat,
                          const GraphDiffRenderer::TimeStat &RightStat,
                          GraphDiffRenderer::StatType T) {
  double LeftAttr = LeftStat.getDouble(T);
  double RightAttr = RightStat.getDouble(T);

  return RightAttr / LeftAttr - 1.0;
}

static std::string getColor(const GraphDiffRenderer::GraphT::EdgeValueType &E,
                            const GraphDiffRenderer::GraphT &G, ColorHelper H,
                            GraphDiffRenderer::StatType T) {
  auto &EdgeAttr = E.second;
  if (EdgeAttr.CorrEdgePtr[0] == nullptr)
    return H.getColorString(2.0);
  if (EdgeAttr.CorrEdgePtr[1] == nullptr)
    return H.getColorString(-2.0);

  if (T == GraphDiffRenderer::StatType::NONE)
    return H.getDefaultColorString();

  const auto &LeftStat = EdgeAttr.CorrEdgePtr[0]->second.S;
  const auto &RightStat = EdgeAttr.CorrEdgePtr[1]->second.S;

  double RelDiff = statRelDiff(LeftStat, RightStat, T);
  double CappedRelDiff = std::clamp(RelDiff, -1.0, 1.0);

  return H.getColorString(CappedRelDiff);
}

static std::string getColor(const GraphDiffRenderer::GraphT::VertexValueType &V,
                            const GraphDiffRenderer::GraphT &G, ColorHelper H,
                            GraphDiffRenderer::StatType T) {
  auto &VertexAttr = V.second;
  if (VertexAttr.CorrVertexPtr[0] == nullptr)
    return H.getColorString(2.0);
  if (VertexAttr.CorrVertexPtr[1] == nullptr)
    return H.getColorString(-2.0);

  if (T == GraphDiffRenderer::StatType::NONE)
    return H.getDefaultColorString();

  const auto &LeftStat = VertexAttr.CorrVertexPtr[0]->second.S;
  const auto &RightStat = VertexAttr.CorrVertexPtr[1]->second.S;

  double RelDiff = statRelDiff(LeftStat, RightStat, T);
  double CappedRelDiff = std::clamp(RelDiff, -1.0, 1.0);

  return H.getColorString(CappedRelDiff);
}

static Twine truncateString(const StringRef &S, size_t n) {
  return (S.size() > n) ? Twine(S.substr(0, n)) + "..." : Twine(S);
}

template <typename T> static bool containsNullptr(const T &Collection) {
  return llvm::is_contained(Collection, nullptr);
}

static std::string getLabel(const GraphDiffRenderer::GraphT::EdgeValueType &E,
                            GraphDiffRenderer::StatType EL) {
  auto &EdgeAttr = E.second;
  switch (EL) {
  case GraphDiffRenderer::StatType::NONE:
    return "";
  default:
    if (containsNullptr(EdgeAttr.CorrEdgePtr))
      return "";

    const auto &LeftStat = EdgeAttr.CorrEdgePtr[0]->second.S;
    const auto &RightStat = EdgeAttr.CorrEdgePtr[1]->second.S;

    double RelDiff = statRelDiff(LeftStat, RightStat, EL);
    return std::string(formatv(R"({0:P})", RelDiff));
  }
}

static std::string getLabel(const GraphDiffRenderer::GraphT::VertexValueType &V,
                            GraphDiffRenderer::StatType VL, int TrunLen) {
  const auto &VertexId = V.first;
  const auto &VertexAttr = V.second;
  switch (VL) {
  case GraphDiffRenderer::StatType::NONE:
    return std::string(
        formatv(R"({0})", truncateString(VertexId, TrunLen).str()));
  default:
    if (containsNullptr(VertexAttr.CorrVertexPtr))
      return std::string(
          formatv(R"({0})", truncateString(VertexId, TrunLen).str()));

    const auto &LeftStat = VertexAttr.CorrVertexPtr[0]->second.S;
    const auto &RightStat = VertexAttr.CorrVertexPtr[1]->second.S;

    double RelDiff = statRelDiff(LeftStat, RightStat, VL);
    return std::string(formatv(
        R"({{{0}|{1:P}})", truncateString(VertexId, TrunLen).str(), RelDiff));
  }
}

static double getLineWidth(const GraphDiffRenderer::GraphT::EdgeValueType &E,
                           GraphDiffRenderer::StatType EL) {
  auto &EdgeAttr = E.second;
  switch (EL) {
  case GraphDiffRenderer::StatType::NONE:
    return 1.0;
  default:
    if (containsNullptr(EdgeAttr.CorrEdgePtr))
      return 1.0;

    const auto &LeftStat = EdgeAttr.CorrEdgePtr[0]->second.S;
    const auto &RightStat = EdgeAttr.CorrEdgePtr[1]->second.S;

    double RelDiff = statRelDiff(LeftStat, RightStat, EL);
    return (RelDiff > 1.0) ? RelDiff : 1.0;
  }
}

void GraphDiffRenderer::exportGraphAsDOT(raw_ostream &OS, StatType EdgeLabel,
                                         StatType EdgeColor,
                                         StatType VertexLabel,
                                         StatType VertexColor, int TruncLen) {
  StringMap<int32_t> VertexNo;

  int i = 0;
  for (const auto &V : G.vertices())
    VertexNo[V.first] = i++;

  ColorHelper H(ColorHelper::DivergingScheme::PiYG);

  OS << "digraph xrayDiff {\n";

  if (VertexLabel != StatType::NONE)
    OS << "node [shape=record]\n";

  for (const auto &E : G.edges()) {
    const auto &HeadId = E.first.first;
    const auto &TailId = E.first.second;
    OS << formatv(R"(F{0} -> F{1} [tooltip="{2} -> {3}" label="{4}" )"
                  R"(color="{5}" labelfontcolor="{5}" penwidth={6}])"
                  "\n",
                  VertexNo[HeadId], VertexNo[TailId],
                  HeadId.empty() ? static_cast<StringRef>("F0") : HeadId,
                  TailId, getLabel(E, EdgeLabel), getColor(E, G, H, EdgeColor),
                  getLineWidth(E, EdgeColor));
  }

  for (const auto &V : G.vertices()) {
    const auto &VertexId = V.first;
    if (VertexId.empty()) {
      OS << formatv(R"(F{0} [label="F0"])"
                    "\n",
                    VertexNo[VertexId]);
      continue;
    }
    OS << formatv(R"(F{0} [label="{1}" color="{2}"])"
                  "\n",
                  VertexNo[VertexId], getLabel(V, VertexLabel, TruncLen),
                  getColor(V, G, H, VertexColor));
  }

  OS << "}\n";
}

Error tryGraphDiff() {
  std::array<GraphRenderer::Factory, 2> Factories{{
      {GraphDiffKeepGoing1Val, GraphDiffDeduceSiblingCalls1Val, {}, Trace()},
      {GraphDiffKeepGoing2Val, GraphDiffDeduceSiblingCalls2Val, {}, Trace()},
  }};

  std::array<std::string, 2> Inputs{{GraphDiffInput1Val, GraphDiffInput2Val}};

  std::array<GraphRenderer::GraphT, 2> Graphs;

  for (int i = 0; i < 2; i++) {
    auto TraceOrErr = loadTraceFile(Inputs[i], true);
    if (!TraceOrErr)
      return make_error<StringError>(
          Twine("Failed Loading Input File '") + Inputs[i] + "'",
          make_error_code(llvm::errc::invalid_argument));
    Factories[i].Trace = std::move(*TraceOrErr);

    auto GraphRendererOrErr = Factories[i].getGraphRenderer();

    if (!GraphRendererOrErr)
      return GraphRendererOrErr.takeError();

    auto GraphRenderer = *GraphRendererOrErr;

    Graphs[i] = GraphRenderer.getGraph();
  }

  GraphDiffRenderer::Factory DGF(Graphs[0], Graphs[1]);

  auto GDROrErr = DGF.getGraphDiffRenderer();
  if (!GDROrErr)
    return GDROrErr.takeError();

  auto &GDR = *GDROrErr;

  std::error_code EC;
  raw_fd_ostream OS(GraphDiffOutputVal, EC,
                    sys::fs::OpenFlags::OF_TextWithCRLF);
  if (EC)
    return make_error<StringError>(Twine("Cannot open file '") +
                                       GraphDiffOutputVal + "' for writing.",
                                   EC);

  GDR.exportGraphAsDOT(OS, GraphDiffEdgeLabelVal, GraphDiffEdgeColorVal,
                       GraphDiffVertexLabelVal, GraphDiffVertexColorVal,
                       GraphDiffVertexLabelTruncVal);

  return Error::success();
}
