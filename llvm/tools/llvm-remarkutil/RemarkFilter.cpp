//===- RemarkFilter.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generic tool to filter remarks
//
//===----------------------------------------------------------------------===//

#include "RemarkUtilHelpers.h"

#include "llvm/Support/Error.h"
#include "llvm/Support/Regex.h"
#include <map>

using namespace llvm;
using namespace remarks;
using namespace llvm::remarkutil;

// Note: Avoid using the identifier "filter" in this file, as it is prone to
// namespace collision with headers that might get included e.g.
// curses.h.

std::vector<std::string> FilterInputFileNames;
bool ExcludeOptVal;
bool SortOptVal;
bool DedupeOptVal;

namespace {

class FilterTool {
public:
  Filters Filter;

  bool Sort = false;
  bool Dedupe = false;
  bool Exclude = false;

  FilterTool(Filters Filter) : Filter(std::move(Filter)) {}
  ~FilterTool() { finalize(); }

  Error processInputFile(StringRef InputFileName) {
    auto MaybeBuf = getInputMemoryBuffer(InputFileName);
    if (!MaybeBuf)
      return MaybeBuf.takeError();
    auto MaybeParser =
        createRemarkParser(InputFormat, (*MaybeBuf)->getBuffer());
    if (!MaybeParser)
      return MaybeParser.takeError();
    auto &Parser = **MaybeParser;

    if (Error E = setupSerializer(Parser.ParserFormat))
      return E;

    auto MaybeRemark = Parser.next();
    for (; MaybeRemark; MaybeRemark = Parser.next()) {
      Remark &Remark = **MaybeRemark;
      if (Filter.filterRemark(Remark) == Exclude)
        continue;
      emit(std::move(*MaybeRemark));
    }
    auto E = MaybeRemark.takeError();
    if (!E.isA<EndOfFileError>())
      return E;
    consumeError(std::move(E));
    return Error::success();
  }

  void finalize() {
    if (!Serializer)
      return;
    emitBuffered();
    OF->keep();
    Serializer = nullptr;
  }

private:
  std::unique_ptr<ToolOutputFile> OF;
  std::unique_ptr<RemarkSerializer> Serializer;

  /// Compare Remarks through unique_ptr
  struct RemarkPtrCompare {
    bool operator()(const std::unique_ptr<Remark> &LHS,
                    const std::unique_ptr<Remark> &RHS) const {
      assert(LHS && RHS && "Invalid pointers to compare.");
      return *LHS < *RHS;
    }
  };

  std::map<std::unique_ptr<Remark>, size_t, RemarkPtrCompare> Remarks;
  StringTable StrTab;

  Error setupSerializer(Format DefaultFormat) {
    if (Serializer)
      return Error::success();
    Format SerializerFormat =
        getSerializerFormat(OutputFileName, OutputFormat, DefaultFormat);
    auto MaybeOF = getOutputFileForRemarks(OutputFileName, SerializerFormat);
    if (!MaybeOF)
      return MaybeOF.takeError();
    OF = std::move(*MaybeOF);
    auto MaybeSerializer = createRemarkSerializer(SerializerFormat, OF->os());
    if (!MaybeSerializer)
      return MaybeSerializer.takeError();
    Serializer = std::move(*MaybeSerializer);
    return Error::success();
  }

  void emit(std::unique_ptr<Remark> RPtr) {
    Remark &R = *RPtr;
    if (!Sort && !Dedupe) {
      Serializer->emit(R);
      return;
    }
    StrTab.internalize(R);
    auto [It, Inserted] = Remarks.try_emplace(std::move(RPtr), 1);
    if (!Dedupe && !Inserted)
      ++It->second;
  }

  void emitBuffered() {
    for (auto &[R, Count] : Remarks) {
      for (size_t I = 0; I < Count; ++I)
        Serializer->emit(*R);
    }
    Remarks.clear();
  }
};

} // namespace

Error tryFilter() {
  auto MaybeFilter = getRemarkFilters();
  if (!MaybeFilter)
    return MaybeFilter.takeError();
  FilterTool Tool(std::move(*MaybeFilter));
  Tool.Sort = SortOptVal;
  Tool.Dedupe = DedupeOptVal;
  Tool.Exclude = ExcludeOptVal;

  for (auto &InputFile : FilterInputFileNames) {
    if (Error E = Tool.processInputFile(InputFile))
      return E;
  }
  return Error::success();
}
