//===- RemarkConvert.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Convert remarks from bitstream to yaml and the other way around.
//
//===----------------------------------------------------------------------===//

#include "RemarkUtilHelpers.h"

using namespace llvm;
using namespace remarks;
using namespace llvm::remarkutil;

extern ExitOnError ExitOnErr;

namespace yaml2bitstream {
static constexpr Format InputFmt = Format::YAML;
static constexpr Format OutputFmt = Format::Bitstream;

/// Parses all remarks in the input YAML file.
/// \p [out] ParsedRemarks - Filled with remarks parsed from the input file.
/// \p [out] StrTab - A string table populated for later remark serialization.
/// \returns Error::success() if all remarks were successfully parsed, and an
/// Error otherwise.
static Error
tryParseRemarksFromYAMLFile(std::vector<std::unique_ptr<Remark>> &ParsedRemarks,
                            StringTable &StrTab) {
  auto MaybeBuf = getInputMemoryBuffer(InputFileName);
  if (!MaybeBuf)
    return MaybeBuf.takeError();
  auto MaybeParser = createRemarkParser(InputFmt, (*MaybeBuf)->getBuffer());
  if (!MaybeParser)
    return MaybeParser.takeError();
  auto &Parser = **MaybeParser;
  auto MaybeRemark = Parser.next();
  for (; MaybeRemark; MaybeRemark = Parser.next()) {
    StrTab.internalize(**MaybeRemark);
    ParsedRemarks.push_back(std::move(*MaybeRemark));
  }
  auto E = MaybeRemark.takeError();
  if (!E.isA<EndOfFileError>())
    return E;
  consumeError(std::move(E));
  return Error::success();
}

/// Reserialize a list of parsed YAML remarks into bitstream remarks.
/// \p ParsedRemarks - A list of remarks.
/// \p StrTab - The string table for the remarks.
/// \returns Error::success() on success.
static Error tryReserializeYAML2Bitstream(
    const std::vector<std::unique_ptr<Remark>> &ParsedRemarks,
    StringTable &StrTab) {
  auto MaybeOF = getOutputFileForRemarks(OutputFileName, OutputFmt);
  if (!MaybeOF)
    return MaybeOF.takeError();
  auto OF = std::move(*MaybeOF);
  auto MaybeSerializer =
      createRemarkSerializer(OutputFmt, OF->os(), std::move(StrTab));
  if (!MaybeSerializer)
    return MaybeSerializer.takeError();
  auto Serializer = std::move(*MaybeSerializer);
  for (const auto &Remark : ParsedRemarks)
    Serializer->emit(*Remark);
  OF->keep();
  return Error::success();
}

/// Parse YAML remarks and reserialize as bitstream remarks.
/// \returns Error::success() on success, and an Error otherwise.
Error tryYAML2Bitstream() {
  StringTable StrTab;
  std::vector<std::unique_ptr<Remark>> ParsedRemarks;
  ExitOnErr(tryParseRemarksFromYAMLFile(ParsedRemarks, StrTab));
  return tryReserializeYAML2Bitstream(ParsedRemarks, StrTab);
}
} // namespace yaml2bitstream

namespace bitstream2yaml {
static constexpr Format InputFmt = Format::Bitstream;
static constexpr Format OutputFmt = Format::YAML;

/// Parse bitstream remarks and reserialize as YAML remarks.
/// \returns An Error if reserialization fails, or Error::success() on success.
Error tryBitstream2YAML() {
  auto MaybeOF = getOutputFileForRemarks(OutputFileName, OutputFmt);
  if (!MaybeOF)
    return MaybeOF.takeError();
  auto OF = std::move(*MaybeOF);
  auto MaybeSerializer = createRemarkSerializer(OutputFmt, OF->os());
  if (!MaybeSerializer)
    return MaybeSerializer.takeError();

  auto MaybeBuf = getInputMemoryBuffer(InputFileName);
  if (!MaybeBuf)
    return MaybeBuf.takeError();
  auto Serializer = std::move(*MaybeSerializer);
  auto MaybeParser = createRemarkParser(InputFmt, (*MaybeBuf)->getBuffer());
  if (!MaybeParser)
    return MaybeParser.takeError();
  auto &Parser = **MaybeParser;

  auto MaybeRemark = Parser.next();
  for (; MaybeRemark; MaybeRemark = Parser.next())
    Serializer->emit(**MaybeRemark);
  auto E = MaybeRemark.takeError();
  if (!E.isA<EndOfFileError>())
    return E;
  consumeError(std::move(E));
  OF->keep();
  return Error::success();
}
} // namespace bitstream2yaml
