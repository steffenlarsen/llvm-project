//===- SSAFFormat.cpp - SSAF Format Tool ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file implements the SSAF format tool that validates and converts
//  TU and LU summaries between registered serialization formats.
//
//===----------------------------------------------------------------------===//

#include "clang/ScalableStaticAnalysis/Core/EntityLinker/LUSummaryEncoding.h"
#include "clang/ScalableStaticAnalysis/Core/EntityLinker/MultiArchSharedLibrary.h"
#include "clang/ScalableStaticAnalysis/Core/EntityLinker/MultiArchStaticLibrary.h"
#include "clang/ScalableStaticAnalysis/Core/EntityLinker/StaticLibrary.h"
#include "clang/ScalableStaticAnalysis/Core/EntityLinker/TUSummaryEncoding.h"
#include "clang/ScalableStaticAnalysis/Core/Serialization/JSONFormat.h"
#include "clang/ScalableStaticAnalysis/Core/Serialization/SerializationFormatRegistry.h"
#include "clang/ScalableStaticAnalysis/SSAFForceLinker.h" // IWYU pragma: keep
#include "clang/ScalableStaticAnalysis/Tool/Utils.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <optional>
#include <string>

using namespace llvm;
using namespace clang::ssaf;

enum class SummaryType {
  Auto,
  TU,
  LU,
  StaticLibrary,
  MultiArchStaticLibrary,
  MultiArchSharedLibrary,
  WPA
};

llvm::clv2::OptionCategory SsafFormatCategory("clang-ssaf-format options");

namespace {

struct SsafFormatOptions {
  std::vector<std::string> LoadPlugins;
  SummaryType Type = SummaryType::Auto;
  bool TypeSet = false;
  std::string InputPath;
  std::string OutputPath;
  bool UseEncoding = false;
  bool ListFormats = false;
};

} // namespace

// clang-ssaf-format uses initTool() which owns the parse, so we use
// registerAsRuntime with a bridge callback.

inline constexpr llvm::clv2::ListOptionInfo<std::string> SFLoadOpt{
    "load", "Load a plugin shared library", llvm::clv2::value_desc("path"),
    llvm::clv2::cat(SsafFormatCategory)};

inline constexpr llvm::clv2::EnumVal<SummaryType> SFTypeVals[] = {
    {"auto", SummaryType::Auto, "Detect type from the file's 'type' field"},
    {"tu", SummaryType::TU, "Translation unit summary"},
    {"lu", SummaryType::LU, "Link unit summary"},
    {"static-library", SummaryType::StaticLibrary,
     "Static library of translation unit summaries"},
    {"multi-arch-static-library", SummaryType::MultiArchStaticLibrary,
     "Multi-architecture static library"},
    {"multi-arch-shared-library", SummaryType::MultiArchSharedLibrary,
     "Multi-architecture shared library"},
    {"wpa", SummaryType::WPA, "Whole-program analysis suite"},
};

inline constexpr llvm::clv2::OptionInfo<SummaryType> SFTypeOpt{
    "type", "Summary type (required unless --list is given)",
    llvm::clv2::ValuesRef<SummaryType>(SFTypeVals),
    llvm::clv2::cat(SsafFormatCategory)};

inline constexpr llvm::clv2::OptionInfo<std::string> SFInputPathOpt{
    "", "<input file>", llvm::clv2::Positional{},
    llvm::clv2::cat(SsafFormatCategory)};

inline constexpr llvm::clv2::OptionInfo<std::string> SFOutputPathOpt{
    "o", "Output file path", llvm::clv2::value_desc("path"),
    llvm::clv2::cat(SsafFormatCategory)};

inline constexpr llvm::clv2::OptionInfo<bool> SFUseEncodingOpt{
    "encoding",
    "Read and write summary encodings rather "
    "than decoded summaries",
    llvm::clv2::cat(SsafFormatCategory)};

inline constexpr llvm::clv2::OptionInfo<bool> SFListFormatsOpt{
    "list",
    "List registered serialization formats and "
    "analyses, then exit",
    llvm::clv2::cat(SsafFormatCategory)};

inline constexpr llvm::clv2::OptionsRegistry<
    &SFLoadOpt, &SFTypeOpt, &SFInputPathOpt, &SFOutputPathOpt,
    &SFUseEncodingOpt, &SFListFormatsOpt>
    SsafFormatOptsReg;

static void
applySsafFormatOpts(const decltype(SsafFormatOptsReg)::ParsedOptionsT &Opts,
                    SsafFormatOptions &ToolOpts) {
  ToolOpts.LoadPlugins = Opts.get<&SFLoadOpt>();
  if (Opts.occurrences<&SFTypeOpt>() > 0) {
    ToolOpts.Type = Opts.get<&SFTypeOpt>();
    ToolOpts.TypeSet = true;
  }
  ToolOpts.InputPath = Opts.get<&SFInputPathOpt>();
  ToolOpts.OutputPath = Opts.get<&SFOutputPathOpt>();
  ToolOpts.UseEncoding = Opts.get<&SFUseEncodingOpt>();
  ToolOpts.ListFormats = Opts.get<&SFListFormatsOpt>();
}

static void configureParser(clv2::OptionParser &P,
                            SsafFormatOptions &ToolOpts) {
  using ParsedT = decltype(SsafFormatOptsReg)::ParsedOptionsT;
  auto *Storage = new ParsedT();
  decltype(SsafFormatOptsReg)::applyDefaultsTo(*Storage);
  std::vector<clv2::detail::OptionEntry> Entries;
  std::vector<clv2::detail::AliasEntry> Aliases;
  std::vector<clv2::detail::SubCommandSpec> SubSpecs;
  decltype(SsafFormatOptsReg)::staticBuildInto(*Storage, Entries, Aliases,
                                               SubSpecs);
  for (auto &E : Entries)
    P.addDynamicEntry(std::move(E));
  clv2::registerDynamicPostParseCallback(
      [Storage, &ToolOpts]() { applySsafFormatOpts(*Storage, ToolOpts); });
}

namespace {

//===----------------------------------------------------------------------===//
// Format Listing
//===----------------------------------------------------------------------===//

constexpr size_t FormatIndent = 4;
constexpr size_t AnalysisIndent = 4;

struct AnalysisData {
  std::string Name;
  std::string Desc;
};

struct FormatData {
  std::string Name;
  std::string Desc;
  llvm::SmallVector<AnalysisData> Analyses;
};

struct PrintLayout {
  size_t FormatNumWidth;
  size_t MaxFormatNameWidth;
  size_t FormatNameCol;
  size_t AnalysisCol;
  size_t AnalysisNumWidth;
  size_t MaxAnalysisNameWidth;
};

llvm::SmallVector<FormatData> collectFormats() {
  llvm::SmallVector<FormatData> Formats;
  for (const auto &Entry : SerializationFormatRegistry::entries()) {
    FormatData FD;
    FD.Name = Entry.getName().str();
    FD.Desc = Entry.getDesc().str();
    auto Format = Entry.instantiate();
    Format->forEachRegisteredAnalysis(
        [&](llvm::StringRef Name, llvm::StringRef Desc) {
          FD.Analyses.push_back({Name.str(), Desc.str()});
        });
    Formats.push_back(std::move(FD));
  }
  return Formats;
}

void printAnalysis(const AnalysisData &AD, size_t AnalysisIndex,
                   size_t FormatIndex, const PrintLayout &Layout) {
  std::string AnalysisNum = std::to_string(FormatIndex + 1) + "." +
                            std::to_string(AnalysisIndex + 1) + ".";
  llvm::outs().indent(Layout.AnalysisCol)
      << llvm::right_justify(AnalysisNum, Layout.AnalysisNumWidth) << " "
      << llvm::left_justify(AD.Name, Layout.MaxAnalysisNameWidth) << " - "
      << AD.Desc << "\n";
}

void printAnalyses(const llvm::SmallVector<AnalysisData> &Analyses,
                   size_t FormatIndex, const PrintLayout &Layout) {
  if (Analyses.empty()) {
    llvm::outs().indent(Layout.FormatNameCol) << "Analyses: (none)\n";
    return;
  }

  llvm::outs().indent(Layout.FormatNameCol) << "Analyses:\n";

  for (size_t AnalysisIndex = 0; AnalysisIndex < Analyses.size();
       ++AnalysisIndex) {
    printAnalysis(Analyses[AnalysisIndex], AnalysisIndex, FormatIndex, Layout);
  }
}

void printFormat(const FormatData &FD, size_t FormatIndex,
                 const PrintLayout &Layout) {
  // Blank line before each format entry for readability.
  llvm::outs() << "\n";

  std::string FormatNum = std::to_string(FormatIndex + 1) + ".";
  llvm::outs().indent(FormatIndent)
      << llvm::right_justify(FormatNum, Layout.FormatNumWidth) << " "
      << llvm::left_justify(FD.Name, Layout.MaxFormatNameWidth) << " - "
      << FD.Desc << "\n";

  printAnalyses(FD.Analyses, FormatIndex, Layout);
}

void printFormats(const llvm::SmallVector<FormatData> &Formats,
                  const PrintLayout &Layout) {
  llvm::outs() << "Registered serialization formats:\n";
  for (size_t FormatIndex = 0; FormatIndex < Formats.size(); ++FormatIndex) {
    printFormat(Formats[FormatIndex], FormatIndex, Layout);
  }
}

PrintLayout computePrintLayout(const llvm::SmallVector<FormatData> &Formats) {
  size_t MaxFormatNameWidth = 0;
  size_t MaxAnalysisCount = 0;
  size_t MaxAnalysisNameWidth = 0;
  for (const auto &FD : Formats) {
    MaxFormatNameWidth = std::max(MaxFormatNameWidth, FD.Name.size());
    MaxAnalysisCount = std::max(MaxAnalysisCount, FD.Analyses.size());
    for (const auto &AD : FD.Analyses) {
      MaxAnalysisNameWidth = std::max(MaxAnalysisNameWidth, AD.Name.size());
    }
  }

  // Width of the widest format number string, e.g. "10." -> 3.
  size_t FormatNumWidth =
      std::to_string(Formats.size()).size() + 1; // +1 for '.'
  // Width of the widest analysis number string, e.g. "10.10." -> 6.
  size_t AnalysisNumWidth = std::to_string(Formats.size()).size() + 1 +
                            std::to_string(MaxAnalysisCount).size() + 1;

  // Where the format name starts (also where "Analyses:" is indented to).
  size_t FormatNameCol = FormatIndent + FormatNumWidth + 1;
  // Where the analysis number starts.
  size_t AnalysisCol = FormatNameCol + AnalysisIndent;

  return {
      FormatNumWidth, MaxFormatNameWidth, FormatNameCol,
      AnalysisCol,    AnalysisNumWidth,   MaxAnalysisNameWidth,
  };
}

void listFormats() {
  llvm::SmallVector<FormatData> Formats = collectFormats();
  if (Formats.empty()) {
    llvm::outs() << "No serialization formats registered.\n";
    return;
  }
  printFormats(Formats, computePrintLayout(Formats));
}

//===----------------------------------------------------------------------===//
// Input Validation
//===----------------------------------------------------------------------===//

struct FormatInput {
  FormatFile InputFile;
  std::optional<FormatFile> OutputFile;
};

FormatInput validateInput(const SsafFormatOptions &ToolOpts) {
  assert(!ToolOpts.ListFormats);

  FormatInput FI;

  // Validate the input path.
  {
    if (ToolOpts.InputPath.empty()) {
      fail("no input file specified");
    }

    FI.InputFile = FormatFile::fromInputPath(ToolOpts.InputPath);
  }

  // Validate the output path.
  if (!ToolOpts.OutputPath.empty()) {
    FI.OutputFile = FormatFile::fromOutputPath(ToolOpts.OutputPath);
  }

  return FI;
}

//===----------------------------------------------------------------------===//
// Format Conversion
//===----------------------------------------------------------------------===//

template <typename ReadFn, typename WriteFn>
void run(const FormatInput &FI, ReadFn Read, WriteFn Write) {
  auto ExpectedResult = (FI.InputFile.Format->*Read)(FI.InputFile.Path);
  if (!ExpectedResult) {
    fail(ExpectedResult.takeError());
  }

  if (!FI.OutputFile) {
    return;
  }

  auto Err =
      (FI.OutputFile->Format->*Write)(*ExpectedResult, FI.OutputFile->Path);
  if (Err) {
    fail(std::move(Err));
  }
}

void convert(const FormatInput &FI, const SsafFormatOptions &ToolOpts) {
  switch (ToolOpts.Type) {
  case SummaryType::Auto:
    if (ToolOpts.UseEncoding) {
      run(FI, &SerializationFormat::readArtifactEncoding,
          &SerializationFormat::writeArtifactEncoding);
    } else {
      run(FI, &SerializationFormat::readArtifact,
          &SerializationFormat::writeArtifact);
    }
    return;
  case SummaryType::TU:
    if (ToolOpts.UseEncoding) {
      run(FI, &SerializationFormat::readTUSummaryEncoding,
          &SerializationFormat::writeTUSummaryEncoding);
    } else {
      run(FI, &SerializationFormat::readTUSummary,
          &SerializationFormat::writeTUSummary);
    }
    return;
  case SummaryType::LU:
    if (ToolOpts.UseEncoding) {
      run(FI, &SerializationFormat::readLUSummaryEncoding,
          &SerializationFormat::writeLUSummaryEncoding);
    } else {
      run(FI, &SerializationFormat::readLUSummary,
          &SerializationFormat::writeLUSummary);
    }
    return;
  case SummaryType::StaticLibrary:
    // StaticLibrary has only an encoded representation, so --encoding is a
    // no-op here: both paths route to readStaticLibrary / writeStaticLibrary.
    run(FI, &SerializationFormat::readStaticLibrary,
        &SerializationFormat::writeStaticLibrary);
    return;
  case SummaryType::MultiArchStaticLibrary:
    // MultiArchStaticLibrary has only an encoded representation, so
    // --encoding is a no-op here: both paths route to
    // readMultiArchStaticLibrary / writeMultiArchStaticLibrary.
    run(FI, &SerializationFormat::readMultiArchStaticLibrary,
        &SerializationFormat::writeMultiArchStaticLibrary);
    return;
  case SummaryType::MultiArchSharedLibrary:
    // MultiArchSharedLibrary has only an encoded representation, so
    // --encoding is a no-op here: both paths route to
    // readMultiArchSharedLibrary / writeMultiArchSharedLibrary.
    run(FI, &SerializationFormat::readMultiArchSharedLibrary,
        &SerializationFormat::writeMultiArchSharedLibrary);
    return;
  case SummaryType::WPA:
    run(FI, &SerializationFormat::readWPASuite,
        &SerializationFormat::writeWPASuite);
    return;
  }

  llvm_unreachable("Unhandled SummaryType variant");
}

} // namespace

//===----------------------------------------------------------------------===//
// Driver
//===----------------------------------------------------------------------===//

int main(int argc, const char **argv) {
  llvm::StringRef ToolHeading = "SSAF Format";

  InitLLVM X(argc, argv);
  SsafFormatOptions ToolOpts;
  initTool(
      argc, argv, "0.1", SsafFormatCategory, ToolHeading,
      [&ToolOpts](clv2::OptionParser &P) { configureParser(P, ToolOpts); });

  loadPlugins(ToolOpts.LoadPlugins);

  if (ToolOpts.ListFormats) {
    listFormats();
  } else {
    FormatInput FI = validateInput(ToolOpts);
    convert(FI, ToolOpts);
  }

  return 0;
}
