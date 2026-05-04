//===- xray-converter.cpp: XRay Trace Conversion --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Implements the trace conversion functions.
//
//===----------------------------------------------------------------------===//
#include "xray-converter.h"

#include "trie-node.h"
#include "llvm/DebugInfo/Symbolize/Symbolize.h"
#include "llvm/Support/EndianStream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/ScopedPrinter.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/XRay/InstrumentationMap.h"
#include "llvm/XRay/Trace.h"
#include "llvm/XRay/YAMLXRayRecord.h"

using namespace llvm;
using namespace xray;

std::string ConvertInputVal;
ConvertFormats ConvertOutputFormatVal;
std::string ConvertOutputVal;
bool ConvertSymbolizeVal;
bool ConvertNoDemangleVal;
std::string ConvertInstrMapVal;
bool ConvertSortInputVal;

using llvm::yaml::Output;

void TraceConverter::exportAsYAML(const Trace &Records, raw_ostream &OS) {
  YAMLXRayTrace Trace;
  const auto &FH = Records.getFileHeader();
  Trace.Header = {FH.Version, FH.Type, FH.ConstantTSC, FH.NonstopTSC,
                  FH.CycleFrequency};
  Trace.Records.reserve(Records.size());
  for (const auto &R : Records) {
    Trace.Records.push_back({R.RecordType, R.CPU, R.Type, R.FuncId,
                             Symbolize ? FuncIdHelper.SymbolOrNumber(R.FuncId)
                                       : llvm::to_string(R.FuncId),
                             R.TSC, R.TId, R.PId, R.CallArgs, R.Data});
  }
  Output Out(OS, nullptr, 0);
  Out.setWriteDefaultValues(false);
  Out << Trace;
}

void TraceConverter::exportAsRAWv1(const Trace &Records, raw_ostream &OS) {
  support::endian::Writer Writer(OS, llvm::endianness::little);
  const auto &FH = Records.getFileHeader();
  Writer.write(FH.Version);
  Writer.write(FH.Type);
  uint32_t Bitfield{0};
  if (FH.ConstantTSC)
    Bitfield |= 1uL;
  if (FH.NonstopTSC)
    Bitfield |= 1uL << 1;
  Writer.write(Bitfield);
  Writer.write(FH.CycleFrequency);

  static constexpr uint32_t Padding4B = 0;
  Writer.write(Padding4B);
  Writer.write(Padding4B);
  Writer.write(Padding4B);
  Writer.write(Padding4B);

  for (const auto &R : Records) {
    switch (R.Type) {
    case RecordTypes::ENTER:
    case RecordTypes::ENTER_ARG:
      Writer.write(R.RecordType);
      Writer.write(static_cast<uint8_t>(R.CPU));
      Writer.write(uint8_t{0});
      break;
    case RecordTypes::EXIT:
      Writer.write(R.RecordType);
      Writer.write(static_cast<uint8_t>(R.CPU));
      Writer.write(uint8_t{1});
      break;
    case RecordTypes::TAIL_EXIT:
      Writer.write(R.RecordType);
      Writer.write(static_cast<uint8_t>(R.CPU));
      Writer.write(uint8_t{2});
      break;
    case RecordTypes::CUSTOM_EVENT:
    case RecordTypes::TYPED_EVENT:
      continue;
    }
    Writer.write(R.FuncId);
    Writer.write(R.TSC);
    Writer.write(R.TId);

    if (FH.Version >= 3)
      Writer.write(R.PId);
    else
      Writer.write(Padding4B);

    Writer.write(Padding4B);
    Writer.write(Padding4B);
  }
}

namespace {

struct StackIdData {
  unsigned id;
  SmallVector<TrieNode<StackIdData> *, 4> siblings;
};
} // namespace

using StackTrieNode = TrieNode<StackIdData>;

static SmallVector<StackTrieNode *, 4>
findSiblings(StackTrieNode *parent, int32_t FnId, uint32_t TId,
             const DenseMap<uint32_t, SmallVector<StackTrieNode *, 4>>
                 &StackRootsByThreadId) {

  SmallVector<StackTrieNode *, 4> Siblings{};

  if (parent == nullptr) {
    for (const auto &map_iter : StackRootsByThreadId) {
      if (map_iter.first != TId)
        for (auto node_iter : map_iter.second) {
          if (node_iter->FuncId == FnId)
            Siblings.push_back(node_iter);
        }
    }
    return Siblings;
  }

  for (auto *ParentSibling : parent->ExtraData.siblings)
    for (auto node_iter : ParentSibling->Callees)
      if (node_iter->FuncId == FnId)
        Siblings.push_back(node_iter);

  return Siblings;
}

static StackTrieNode *findOrCreateStackNode(
    StackTrieNode *Parent, int32_t FuncId, uint32_t TId,
    DenseMap<uint32_t, SmallVector<StackTrieNode *, 4>> &StackRootsByThreadId,
    DenseMap<unsigned, StackTrieNode *> &StacksByStackId, unsigned *id_counter,
    std::forward_list<StackTrieNode> &NodeStore) {
  SmallVector<StackTrieNode *, 4> &ParentCallees =
      Parent == nullptr ? StackRootsByThreadId[TId] : Parent->Callees;
  auto match = find_if(ParentCallees, [FuncId](StackTrieNode *ParentCallee) {
    return FuncId == ParentCallee->FuncId;
  });
  if (match != ParentCallees.end())
    return *match;

  SmallVector<StackTrieNode *, 4> siblings =
      findSiblings(Parent, FuncId, TId, StackRootsByThreadId);
  if (siblings.empty()) {
    NodeStore.push_front({FuncId, Parent, {}, {(*id_counter)++, {}}});
    StackTrieNode *CurrentStack = &NodeStore.front();
    StacksByStackId[*id_counter - 1] = CurrentStack;
    ParentCallees.push_back(CurrentStack);
    return CurrentStack;
  }
  unsigned stack_id = siblings[0]->ExtraData.id;
  NodeStore.push_front({FuncId, Parent, {}, {stack_id, std::move(siblings)}});
  StackTrieNode *CurrentStack = &NodeStore.front();
  for (auto *sibling : CurrentStack->ExtraData.siblings)
    sibling->ExtraData.siblings.push_back(CurrentStack);
  ParentCallees.push_back(CurrentStack);
  return CurrentStack;
}

static void writeTraceViewerRecord(uint16_t Version, raw_ostream &OS,
                                   int32_t FuncId, uint32_t TId, uint32_t PId,
                                   bool Symbolize,
                                   const FuncIdConversionHelper &FuncIdHelper,
                                   double EventTimestampUs,
                                   const StackTrieNode &StackCursor,
                                   StringRef FunctionPhenotype) {
  OS << "    ";
  if (Version >= 3) {
    OS << llvm::formatv(
        R"({ "name" : "{0}", "ph" : "{1}", "tid" : "{2}", "pid" : "{3}", )"
        R"("ts" : "{4:f4}", "sf" : "{5}" })",
        (Symbolize ? FuncIdHelper.SymbolOrNumber(FuncId)
                   : llvm::to_string(FuncId)),
        FunctionPhenotype, TId, PId, EventTimestampUs,
        StackCursor.ExtraData.id);
  } else {
    OS << llvm::formatv(
        R"({ "name" : "{0}", "ph" : "{1}", "tid" : "{2}", "pid" : "1", )"
        R"("ts" : "{3:f3}", "sf" : "{4}" })",
        (Symbolize ? FuncIdHelper.SymbolOrNumber(FuncId)
                   : llvm::to_string(FuncId)),
        FunctionPhenotype, TId, EventTimestampUs, StackCursor.ExtraData.id);
  }
}

void TraceConverter::exportAsChromeTraceEventFormat(const Trace &Records,
                                                    raw_ostream &OS) {
  const auto &FH = Records.getFileHeader();
  auto Version = FH.Version;
  auto CycleFreq = FH.CycleFrequency;

  unsigned id_counter = 0;
  int NumOutputRecords = 0;

  OS << "{\n  \"traceEvents\": [\n";
  DenseMap<uint32_t, StackTrieNode *> StackCursorByThreadId{};
  DenseMap<uint32_t, SmallVector<StackTrieNode *, 4>> StackRootsByThreadId{};
  DenseMap<unsigned, StackTrieNode *> StacksByStackId{};
  std::forward_list<StackTrieNode> NodeStore{};
  for (const auto &R : Records) {
    double EventTimestampUs = double(1000000) / CycleFreq * double(R.TSC);
    StackTrieNode *&StackCursor = StackCursorByThreadId[R.TId];
    switch (R.Type) {
    case RecordTypes::CUSTOM_EVENT:
    case RecordTypes::TYPED_EVENT:
      break;
    case RecordTypes::ENTER:
    case RecordTypes::ENTER_ARG:
      StackCursor = findOrCreateStackNode(StackCursor, R.FuncId, R.TId,
                                          StackRootsByThreadId, StacksByStackId,
                                          &id_counter, NodeStore);
      if (NumOutputRecords++ > 0) {
        OS << ",\n";
      }
      writeTraceViewerRecord(Version, OS, R.FuncId, R.TId, R.PId, Symbolize,
                             FuncIdHelper, EventTimestampUs, *StackCursor, "B");
      break;
    case RecordTypes::EXIT:
    case RecordTypes::TAIL_EXIT:
      if (StackCursor == nullptr)
        break;
      StackTrieNode *PreviousCursor = nullptr;
      do {
        if (NumOutputRecords++ > 0) {
          OS << ",\n";
        }
        writeTraceViewerRecord(Version, OS, StackCursor->FuncId, R.TId, R.PId,
                               Symbolize, FuncIdHelper, EventTimestampUs,
                               *StackCursor, "E");
        PreviousCursor = StackCursor;
        StackCursor = StackCursor->Parent;
      } while (PreviousCursor->FuncId != R.FuncId && StackCursor != nullptr);
      break;
    }
  }
  OS << "\n  ],\n";
  OS << "  "
     << "\"displayTimeUnit\": \"ns\",\n";

  OS << R"(  "stackFrames": {)";
  int stack_frame_count = 0;
  for (auto map_iter : StacksByStackId) {
    if (stack_frame_count++ == 0)
      OS << "\n";
    else
      OS << ",\n";
    OS << "    ";
    OS << llvm::formatv(
        R"("{0}" : { "name" : "{1}")", map_iter.first,
        (Symbolize ? FuncIdHelper.SymbolOrNumber(map_iter.second->FuncId)
                   : llvm::to_string(map_iter.second->FuncId)));
    if (map_iter.second->Parent != nullptr)
      OS << llvm::formatv(R"(, "parent": "{0}")",
                          map_iter.second->Parent->ExtraData.id);
    OS << " }";
  }
  OS << "\n  }\n";
  OS << "}\n";
}

Error tryConvert() {
  // FIXME: Support conversion to BINARY when upgrading XRay trace versions.
  InstrumentationMap Map;
  if (!ConvertInstrMapVal.empty()) {
    auto InstrumentationMapOrError = loadInstrumentationMap(ConvertInstrMapVal);
    if (!InstrumentationMapOrError)
      return joinErrors(make_error<StringError>(
                            Twine("Cannot open instrumentation map '") +
                                ConvertInstrMapVal + "'",
                            std::make_error_code(std::errc::invalid_argument)),
                        InstrumentationMapOrError.takeError());
    Map = std::move(*InstrumentationMapOrError);
  }

  const auto &FunctionAddresses = Map.getFunctionAddresses();
  symbolize::LLVMSymbolizer::Options SymbolizerOpts;
  if (ConvertNoDemangleVal)
    SymbolizerOpts.Demangle = false;
  symbolize::LLVMSymbolizer Symbolizer(SymbolizerOpts);
  FuncIdConversionHelper FuncIdHelper(ConvertInstrMapVal, Symbolizer,
                                      FunctionAddresses);
  TraceConverter TC(FuncIdHelper, ConvertSymbolizeVal);
  std::error_code EC;
  raw_fd_ostream OS(ConvertOutputVal, EC,
                    ConvertOutputFormatVal == ConvertFormats::BINARY
                        ? sys::fs::OpenFlags::OF_None
                        : sys::fs::OpenFlags::OF_TextWithCRLF);
  if (EC)
    return make_error<StringError>(
        Twine("Cannot open file '") + ConvertOutputVal + "' for writing.", EC);

  auto TraceOrErr = loadTraceFile(ConvertInputVal, ConvertSortInputVal);
  if (!TraceOrErr)
    return joinErrors(
        make_error<StringError>(
            Twine("Failed loading input file '") + ConvertInputVal + "'.",
            std::make_error_code(std::errc::executable_format_error)),
        TraceOrErr.takeError());

  auto &T = *TraceOrErr;
  switch (ConvertOutputFormatVal) {
  case ConvertFormats::YAML:
    TC.exportAsYAML(T, OS);
    break;
  case ConvertFormats::BINARY:
    TC.exportAsRAWv1(T, OS);
    break;
  case ConvertFormats::CHROME_TRACE_EVENT:
    TC.exportAsChromeTraceEventFormat(T, OS);
    break;
  }
  return Error::success();
}
