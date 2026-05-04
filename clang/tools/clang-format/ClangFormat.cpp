//===-- clang-format/ClangFormat.cpp - Clang format tool ------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a clang-format tool that automatically formats
/// (fragments of) C++ code.
///
//===----------------------------------------------------------------------===//

#include "../../lib/Format/MatchFilePath.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Version.h"
#include "clang/Format/Format.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/VirtualFileSystem.h"
#include <fstream>

using namespace llvm;
using clang::tooling::Replacements;

// Mark all our options with this category, everything else (except for -version
// and -help) will be hidden.
static cl::OptionCategory ClangFormatCategory("Clang-format options");
inline constexpr clv2::OptionCategory
    Clv2ClangFormatCategory("Clang-format options");

// --- constexpr option descriptors ---
inline constexpr clv2::OptionInfo<bool> HelpOpt{
    "h", "Alias for -help", clv2::Hidden, clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::ListOptionInfo<unsigned> OffsetsOpt{
    "offset",
    "Format a range starting at this byte offset.\n"
    "Multiple ranges can be formatted by specifying\n"
    "several -offset and -length pairs.\n"
    "Can only be used with one input file.",
    clv2::value_desc("uint"), clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::ListOptionInfo<unsigned> LengthsOpt{
    "length",
    "Format a range of this length (in bytes).\n"
    "Multiple ranges can be formatted by specifying\n"
    "several -offset and -length pairs.\n"
    "When only a single -offset is specified without\n"
    "-length, clang-format will format up to the end\n"
    "of the file.\n"
    "Can only be used with one input file.",
    clv2::value_desc("uint"), clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::ListOptionInfo<std::string> LineRangesOpt{
    "lines",
    "<start line>:<end line> - format a range of\n"
    "lines (both 1-based).\n"
    "Multiple ranges can be formatted by specifying\n"
    "several -lines arguments.\n"
    "Can't be used with -offset and -length.\n"
    "Can only be used with one input file.",
    clv2::value_desc("string"), clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::OptionInfo<std::string> StyleOpt{
    "style",
    "Set coding style. <string> can be:\n"
    "1. A preset: LLVM, GNU, Google, Chromium, Microsoft,\n"
    "   Mozilla, WebKit.\n"
    "2. 'file' to load style configuration from a\n"
    "   .clang-format file in one of the parent directories\n"
    "   of the source file (for stdin, see --assume-filename).\n"
    "   If no .clang-format file is found, falls back to\n"
    "   --fallback-style.\n"
    "   --style=file is the default.\n"
    "3. 'file:<format_file_path>' to explicitly specify\n"
    "   the configuration file.\n"
    "4. \"{key: value, ...}\" to set specific parameters, e.g.:\n"
    "   --style=\"{BasedOnStyle: llvm, IndentWidth: 8}\"",
    clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::OptionInfo<std::string> FallbackStyleOpt{
    "fallback-style",
    "The name of the predefined style used as a\n"
    "fallback in case clang-format is invoked with\n"
    "-style=file, but can not find the .clang-format\n"
    "file to use. Defaults to 'LLVM'.\n"
    "Use -fallback-style=none to skip formatting.",
    clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::OptionInfo<std::string> AssumeFileNameOpt{
    "assume-filename",
    "Set filename used to determine the language and to find\n"
    ".clang-format file.\n"
    "Only used when reading from stdin.\n"
    "If this is not passed, the .clang-format file is searched\n"
    "relative to the current working directory when reading stdin.\n"
    "Unrecognized filenames are treated as C++.\n"
    "supported:\n"
    "  CSharp: .cs\n"
    "  Java: .java\n"
    "  JavaScript: .js .mjs .cjs .ts\n"
    "  JSON: .json .ipynb\n"
    "  Objective-C: .m .mm\n"
    "  Proto: .proto .protodevel\n"
    "  TableGen: .td\n"
    "  TextProto: .txtpb .textpb .pb.txt .textproto .asciipb\n"
    "  Verilog: .sv .svh .v .vh",
    clv2::Init{"<stdin>"}, clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::OptionInfo<bool> InplaceOpt{
    "i", "Inplace edit <file>s, if specified.",
    clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::OptionInfo<bool> OutputXMLOpt{
    "output-replacements-xml", "Output replacements as XML.",
    clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::OptionInfo<bool> DumpConfigOpt{
    "dump-config",
    "Dump configuration options to stdout and exit.\n"
    "Can be used with -style option.",
    clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::OptionInfo<unsigned> CursorOpt{
    "cursor",
    "The position of the cursor when invoking\n"
    "clang-format from an editor integration",
    clv2::Init{0u}, clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::OptionInfo<bool> SortIncludesOpt{
    "sort-includes",
    "If set, overrides the include sorting behavior\n"
    "determined by the SortIncludes style flag",
    clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::OptionInfo<std::string> QualifierAlignmentOpt{
    "qualifier-alignment",
    "If set, overrides the qualifier alignment style\n"
    "determined by the QualifierAlignment style flag",
    clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::OptionInfo<std::string> FilesOpt{
    "files", "A file containing a list of files to process, one per line.",
    clv2::value_desc("filename"), clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::OptionInfo<bool> VerboseOpt{
    "verbose", "If set, shows the list of processed files",
    clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::OptionInfo<bool> DryRunOpt{
    "dry-run", "If set, do not actually make the formatting changes",
    clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::AliasInfo DryRunAliasOpt{"n", "dry-run",
                                                "Alias for --dry-run"};

inline constexpr clv2::OptionInfo<bool> WarnFormatOpt{
    "Wclang-format-violations",
    "Warnings about individual formatting changes needed. "
    "Used only with --dry-run or -n",
    clv2::Init{true}, clv2::Hidden, clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::OptionInfo<bool> NoWarnFormatOpt{
    "Wno-clang-format-violations",
    "Do not warn about individual formatting changes "
    "needed. Used only with --dry-run or -n",
    clv2::Hidden, clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::OptionInfo<unsigned> ErrorLimitOpt{
    "ferror-limit",
    "Set the maximum number of clang-format errors to emit\n"
    "before stopping (0 = no limit).\n"
    "Used only with --dry-run or -n",
    clv2::Init{0u}, clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::OptionInfo<bool> WarningsAsErrorsOpt{
    "Werror", "If set, changes formatting warnings to errors",
    clv2::cat(Clv2ClangFormatCategory)};

enum class WNoErrorKind { Unknown };

inline constexpr clv2::EnumVal<WNoErrorKind> WNoErrorVals[] = {
    {"unknown", WNoErrorKind::Unknown,
     "If set, unknown format options are only warned about.\n"
     "This can be used to enable formatting, even if the\n"
     "configuration contains unknown (newer) options.\n"
     "Use with caution, as this might lead to dramatically\n"
     "differing format depending on an option being\n"
     "supported or not."},
};

inline constexpr auto WNoErrorOpt = clv2::makeEnumOption<WNoErrorKind>(
    "Wno-error", "If set, don't error out on the specified warning type.",
    WNoErrorVals, clv2::cat(Clv2ClangFormatCategory));

inline constexpr clv2::OptionInfo<bool> ShowColorsOpt{
    "fcolor-diagnostics",
    "If set, and on a color-capable terminal controls "
    "whether or not to print diagnostics in color",
    clv2::Init{true}, clv2::Hidden, clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::OptionInfo<bool> NoShowColorsOpt{
    "fno-color-diagnostics",
    "If set, and on a color-capable terminal controls "
    "whether or not to print diagnostics in color",
    clv2::Hidden, clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::ListOptionInfo<std::string> FileNamesOpt{
    "", "[@<file>] [<file> ...]", clv2::Positional{}, clv2::ZeroOrMore,
    clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::OptionInfo<bool> FailOnIncompleteFormatOpt{
    "fail-on-incomplete-format",
    "If set, fail with exit code 1 on incomplete format.",
    clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::OptionInfo<bool> ListIgnoredOpt{
    "list-ignored", "List ignored files.", clv2::Hidden,
    clv2::cat(Clv2ClangFormatCategory)};

inline constexpr clv2::OptionsRegistry<
    &HelpOpt, &OffsetsOpt, &LengthsOpt, &LineRangesOpt, &StyleOpt,
    &FallbackStyleOpt, &AssumeFileNameOpt, &InplaceOpt, &OutputXMLOpt,
    &DumpConfigOpt, &CursorOpt, &SortIncludesOpt, &QualifierAlignmentOpt,
    &FilesOpt, &VerboseOpt, &DryRunOpt, &DryRunAliasOpt, &WarnFormatOpt,
    &NoWarnFormatOpt, &ErrorLimitOpt, &WarningsAsErrorsOpt, &WNoErrorOpt,
    &ShowColorsOpt, &NoShowColorsOpt, &FileNamesOpt, &FailOnIncompleteFormatOpt,
    &ListIgnoredOpt>
    ClangFormatReg;

namespace {
/// Parsed command-line values for one clang-format run.
///
/// Owned by main() and passed explicitly to the helpers below rather than
/// held at file scope, so the values travel with the invocation instead of
/// living in process-wide state.  A few are not raw option reads -- Style and
/// FallbackStyle get their defaults applied after parsing, and fillRanges
/// treats Offsets/Lengths as scratch -- which is why this is a materialised
/// struct rather than reads against the OptionsContext at each use.
struct FormatOptions {
  bool Help = false;
  std::vector<unsigned> Offsets;
  std::vector<unsigned> Lengths;
  std::vector<std::string> LineRanges;
  std::string Style;
  std::string FallbackStyle;
  std::string AssumeFileName;
  bool Inplace = false;
  bool OutputXML = false;
  bool DumpConfig = false;
  unsigned Cursor = 0;
  bool CursorSpecified = false;
  bool SortIncludes = false;
  bool SortIncludesSpecified = false;
  std::string QualifierAlignment;
  std::string Files;
  bool Verbose = false;
  bool DryRun = false;
  bool WarnFormat = true;
  bool NoWarnFormat = false;
  unsigned ErrorLimit = 0;
  bool WarningsAsErrors = false;
  bool WNoErrorUnknown = false;
  bool ShowColors = true;
  bool NoShowColors = false;
  std::vector<std::string> FileNames;
  bool FailOnIncompleteFormat = false;
  bool ListIgnored = false;
};
} // namespace

namespace clang {
namespace format {

static FileID createInMemoryFile(StringRef FileName, MemoryBufferRef Source,
                                 SourceManager &Sources, FileManager &Files,
                                 llvm::vfs::InMemoryFileSystem *MemFS) {
  MemFS->addFileNoOwn(FileName, 0, Source);
  auto File = Files.getOptionalFileRef(FileName);
  assert(File && "File not added to MemFS?");
  return Sources.createFileID(*File, SourceLocation(), SrcMgr::C_User);
}

// Parses <start line>:<end line> input to a pair of line numbers.
// Returns true on error.
static bool parseLineRange(StringRef Input, unsigned &FromLine,
                           unsigned &ToLine) {
  std::pair<StringRef, StringRef> LineRange = Input.split(':');
  return LineRange.first.getAsInteger(0, FromLine) ||
         LineRange.second.getAsInteger(0, ToLine);
}

static bool fillRanges(const FormatOptions &O, MemoryBuffer *Code,
                       std::vector<tooling::Range> &Ranges) {
  auto InMemoryFileSystem =
      makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
  FileManager Files(FileSystemOptions(), InMemoryFileSystem);
  DiagnosticOptions DiagOpts;
  DiagnosticsEngine Diagnostics(DiagnosticIDs::create(), DiagOpts);
  SourceManager Sources(Diagnostics, Files);
  const auto ID = createInMemoryFile("<irrelevant>", *Code, Sources, Files,
                                     InMemoryFileSystem.get());

  // Offsets is copied, not referenced: the "no -offset given" case appends a
  // synthetic 0 below, which must not escape this call.
  std::vector<unsigned> Offsets = O.Offsets;
  const std::vector<unsigned> &Lengths = O.Lengths;

  if (!O.LineRanges.empty()) {
    if (!Offsets.empty() || !Lengths.empty()) {
      errs() << "error: cannot use -lines with -offset/-length\n";
      return true;
    }

    for (const auto &LineRange : O.LineRanges) {
      unsigned FromLine, ToLine;
      if (parseLineRange(LineRange, FromLine, ToLine)) {
        errs() << "error: invalid <start line>:<end line> pair\n";
        return true;
      }
      if (FromLine < 1) {
        errs() << "error: start line should be at least 1\n";
        return true;
      }
      if (FromLine > ToLine) {
        errs() << "error: start line should not exceed end line\n";
        return true;
      }
      const auto Start = Sources.translateLineCol(ID, FromLine, 1);
      const auto End = Sources.translateLineCol(ID, ToLine, UINT_MAX);
      if (Start.isInvalid() || End.isInvalid())
        return true;
      const auto Offset = Sources.getFileOffset(Start);
      const auto Length = Sources.getFileOffset(End) - Offset;
      Ranges.push_back(tooling::Range(Offset, Length));
    }
    return false;
  }

  if (Offsets.empty())
    Offsets.push_back(0);
  const bool EmptyLengths = Lengths.empty();
  unsigned Length = 0;
  if (Offsets.size() == 1 && EmptyLengths) {
    Length = Sources.getFileOffset(Sources.getLocForEndOfFile(ID)) - Offsets[0];
  } else if (Offsets.size() != Lengths.size()) {
    errs() << "error: number of -offset and -length arguments must match.\n";
    return true;
  }
  for (unsigned I = 0, E = Offsets.size(), CodeSize = Code->getBufferSize();
       I < E; ++I) {
    const auto Offset = Offsets[I];
    if (Offset >= CodeSize) {
      errs() << "error: offset " << Offset << " is outside the file\n";
      return true;
    }
    if (!EmptyLengths)
      Length = Lengths[I];
    if (Offset + Length > CodeSize) {
      errs() << "error: invalid length " << Length << ", offset + length ("
             << Offset + Length << ") is outside the file.\n";
      return true;
    }
    Ranges.push_back(tooling::Range(Offset, Length));
  }
  return false;
}

static void outputReplacementXML(StringRef Text) {
  // FIXME: When we sort includes, we need to make sure the stream is correct
  // utf-8.
  size_t From = 0;
  size_t Index;
  while ((Index = Text.find_first_of("\n\r<&", From)) != StringRef::npos) {
    outs() << Text.substr(From, Index - From);
    switch (Text[Index]) {
    case '\n':
      outs() << "&#10;";
      break;
    case '\r':
      outs() << "&#13;";
      break;
    case '<':
      outs() << "&lt;";
      break;
    case '&':
      outs() << "&amp;";
      break;
    default:
      llvm_unreachable("Unexpected character encountered!");
    }
    From = Index + 1;
  }
  outs() << Text.substr(From);
}

static void outputReplacementsXML(const Replacements &Replaces) {
  for (const auto &R : Replaces) {
    outs() << "<replacement "
           << "offset='" << R.getOffset() << "' "
           << "length='" << R.getLength() << "'>";
    outputReplacementXML(R.getReplacementText());
    outs() << "</replacement>\n";
  }
}

static bool emitReplacementWarnings(const FormatOptions &O,
                                    const Replacements &Replaces,
                                    StringRef AssumedFileName,
                                    std::unique_ptr<llvm::MemoryBuffer> Code) {
  unsigned Errors = 0;
  if (O.WarnFormat && !O.NoWarnFormat) {
    SourceMgr Mgr;
    const char *StartBuf = Code->getBufferStart();

    Mgr.AddNewSourceBuffer(std::move(Code), SMLoc());
    for (const auto &R : Replaces) {
      SMDiagnostic Diag = Mgr.GetMessage(
          SMLoc::getFromPointer(StartBuf + R.getOffset()),
          O.WarningsAsErrors ? SourceMgr::DiagKind::DK_Error
                             : SourceMgr::DiagKind::DK_Warning,
          "code should be clang-formatted [-Wclang-format-violations]");

      Diag.print(nullptr, llvm::errs(), O.ShowColors && !O.NoShowColors);
      if (O.ErrorLimit && ++Errors >= O.ErrorLimit)
        break;
    }
  }
  return O.WarningsAsErrors;
}

static void outputXML(const FormatOptions &O, const Replacements &Replaces,
                      const Replacements &FormatChanges,
                      const FormattingAttemptStatus &Status,
                      unsigned CursorPosition) {
  outs() << "<?xml version='1.0'?>\n<replacements "
            "xml:space='preserve' incomplete_format='"
         << (Status.FormatComplete ? "false" : "true") << "'";
  if (!Status.FormatComplete)
    outs() << " line='" << Status.Line << "'";
  outs() << ">\n";
  if (O.CursorSpecified) {
    outs() << "<cursor>" << FormatChanges.getShiftedCodePosition(CursorPosition)
           << "</cursor>\n";
  }

  outputReplacementsXML(Replaces);
  outs() << "</replacements>\n";
}

class ClangFormatDiagConsumer : public DiagnosticConsumer {
  virtual void anchor() {}

  void HandleDiagnostic(DiagnosticsEngine::Level DiagLevel,
                        const Diagnostic &Info) override {

    SmallVector<char, 16> vec;
    Info.FormatDiagnostic(vec);
    errs() << "clang-format error:" << vec << "\n";
  }
};

// Returns true on error.
static bool format(const FormatOptions &O, StringRef FileName,
                   bool ErrorOnIncompleteFormat = false) {
  const bool IsSTDIN = FileName == "-";
  if (!O.OutputXML && O.Inplace && IsSTDIN) {
    errs() << "error: cannot use -i when reading from stdin.\n";
    return true;
  }
  // On Windows, overwriting a file with an open file mapping doesn't work,
  // so read the whole file into memory when formatting in-place.
  ErrorOr<std::unique_ptr<MemoryBuffer>> CodeOrErr =
      !O.OutputXML && O.Inplace
          ? MemoryBuffer::getFileAsStream(FileName)
          : MemoryBuffer::getFileOrSTDIN(FileName, /*IsText=*/true);
  if (std::error_code EC = CodeOrErr.getError()) {
    errs() << FileName << ": " << EC.message() << "\n";
    return true;
  }
  std::unique_ptr<llvm::MemoryBuffer> Code = std::move(CodeOrErr.get());
  if (Code->getBufferSize() == 0)
    return false; // Empty files are formatted correctly.

  StringRef BufStr = Code->getBuffer();

  const char *InvalidBOM = SrcMgr::ContentCache::getInvalidBOM(BufStr);

  if (InvalidBOM) {
    errs() << "error: encoding with unsupported byte order mark \""
           << InvalidBOM << "\" detected";
    if (!IsSTDIN)
      errs() << " in file '" << FileName << "'";
    errs() << ".\n";
    return true;
  }

  std::vector<tooling::Range> Ranges;
  if (fillRanges(O, Code.get(), Ranges))
    return true;
  StringRef AssumedFileName = IsSTDIN ? O.AssumeFileName : FileName;
  if (AssumedFileName.empty()) {
    llvm::errs() << "error: empty filenames are not allowed\n";
    return true;
  }

  Expected<FormatStyle> FormatStyle =
      getStyle(O.Style, AssumedFileName, O.FallbackStyle, Code->getBuffer(),
               nullptr, O.WNoErrorUnknown);
  if (!FormatStyle) {
    llvm::errs() << toString(FormatStyle.takeError()) << "\n";
    return true;
  }

  StringRef QualifierAlignmentOrder = O.QualifierAlignment;

  FormatStyle->QualifierAlignment =
      StringSwitch<FormatStyle::QualifierAlignmentStyle>(
          QualifierAlignmentOrder.lower())
          .Case("right", FormatStyle::QAS_Right)
          .Case("left", FormatStyle::QAS_Left)
          .Default(FormatStyle->QualifierAlignment);

  if (FormatStyle->QualifierAlignment == FormatStyle::QAS_Left) {
    FormatStyle->QualifierOrder = {"const", "volatile", "type"};
  } else if (FormatStyle->QualifierAlignment == FormatStyle::QAS_Right) {
    FormatStyle->QualifierOrder = {"type", "const", "volatile"};
  } else if (QualifierAlignmentOrder.contains("type")) {
    FormatStyle->QualifierAlignment = FormatStyle::QAS_Custom;
    SmallVector<StringRef> Qualifiers;
    QualifierAlignmentOrder.split(Qualifiers, " ", /*MaxSplit=*/-1,
                                  /*KeepEmpty=*/false);
    FormatStyle->QualifierOrder = {Qualifiers.begin(), Qualifiers.end()};
  }

  if (O.SortIncludesSpecified) {
    FormatStyle->SortIncludes = {};
    if (O.SortIncludes)
      FormatStyle->SortIncludes.Enabled = true;
  }
  unsigned CursorPosition = O.Cursor;
  Replacements Replaces = sortIncludes(*FormatStyle, Code->getBuffer(), Ranges,
                                       AssumedFileName, &CursorPosition);

  const bool IsJson = FormatStyle->isJson();

  // To format JSON insert a variable to trick the code into thinking its
  // JavaScript.
  if (IsJson && !FormatStyle->DisableFormat) {
    auto Err =
        Replaces.add(tooling::Replacement(AssumedFileName, 0, 0, "x = "));
    if (Err)
      llvm::errs() << "Bad JSON variable insertion\n";
  }

  auto ChangedCode = tooling::applyAllReplacements(Code->getBuffer(), Replaces);
  if (!ChangedCode) {
    llvm::errs() << toString(ChangedCode.takeError()) << "\n";
    return true;
  }
  // Get new affected ranges after sorting `#includes`.
  Ranges = tooling::calculateRangesAfterReplacements(Replaces, Ranges);
  FormattingAttemptStatus Status;
  Replacements FormatChanges =
      reformat(*FormatStyle, *ChangedCode, Ranges, AssumedFileName, &Status);
  Replaces = Replaces.merge(FormatChanges);
  if (O.DryRun) {
    return Replaces.size() > (IsJson ? 1u : 0u) &&
           emitReplacementWarnings(O, Replaces, AssumedFileName,
                                   std::move(Code));
  }
  if (O.OutputXML) {
    outputXML(O, Replaces, FormatChanges, Status, CursorPosition);
  } else {
    auto InMemoryFileSystem =
        makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();
    FileManager Files(FileSystemOptions(), InMemoryFileSystem);

    DiagnosticOptions DiagOpts;
    ClangFormatDiagConsumer IgnoreDiagnostics;
    DiagnosticsEngine Diagnostics(DiagnosticIDs::create(), DiagOpts,
                                  &IgnoreDiagnostics, false);
    SourceManager Sources(Diagnostics, Files);
    FileID ID = createInMemoryFile(AssumedFileName, *Code, Sources, Files,
                                   InMemoryFileSystem.get());
    Rewriter Rewrite(Sources, LangOptions());
    tooling::applyAllReplacements(Replaces, Rewrite);
    if (O.Inplace) {
      if (Rewrite.overwriteChangedFiles())
        return true;
    } else {
      if (O.CursorSpecified) {
        outs() << "{ \"Cursor\": "
               << FormatChanges.getShiftedCodePosition(CursorPosition)
               << ", \"IncompleteFormat\": "
               << (Status.FormatComplete ? "false" : "true");
        if (!Status.FormatComplete)
          outs() << ", \"Line\": " << Status.Line;
        outs() << " }\n";
      }
      Rewrite.getEditBuffer(ID).write(outs());
    }
  }
  return ErrorOnIncompleteFormat && !Status.FormatComplete;
}

} // namespace format
} // namespace clang

static void PrintVersion(raw_ostream &OS) {
  OS << clang::getClangToolFullVersion("clang-format") << '\n';
}

// Dump the configuration.
static int dumpConfig(const FormatOptions &O) {
  std::unique_ptr<llvm::MemoryBuffer> Code;
  // We can't read the code to detect the language if there's no file name.
  if (!O.FileNames.empty()) {
    // Read in the code in case the filename alone isn't enough to detect the
    // language.
    ErrorOr<std::unique_ptr<MemoryBuffer>> CodeOrErr =
        MemoryBuffer::getFileOrSTDIN(O.FileNames[0], /*IsText=*/true);
    if (std::error_code EC = CodeOrErr.getError()) {
      llvm::errs() << EC.message() << "\n";
      return 1;
    }
    Code = std::move(CodeOrErr.get());
  }
  Expected<clang::format::FormatStyle> FormatStyle = clang::format::getStyle(
      O.Style,
      O.FileNames.empty() || O.FileNames[0] == "-" ? O.AssumeFileName
                                                   : O.FileNames[0],
      O.FallbackStyle, Code ? Code->getBuffer() : "");
  if (!FormatStyle) {
    llvm::errs() << toString(FormatStyle.takeError()) << "\n";
    return 1;
  }
  std::string Config = clang::format::configurationAsText(*FormatStyle);
  outs() << Config << "\n";
  return 0;
}

using String = SmallString<128>;
static String IgnoreDir;             // Directory of .clang-format-ignore file.
static String PrevDir;               // Directory of previous `FilePath`.
static SmallVector<String> Patterns; // Patterns in .clang-format-ignore file.

// Check whether `FilePath` is ignored according to the nearest
// .clang-format-ignore file based on the rules below:
// - A blank line is skipped.
// - Leading and trailing spaces of a line are trimmed.
// - A line starting with a hash (`#`) is a comment.
// - A non-comment line is a single pattern.
// - The slash (`/`) is used as the directory separator.
// - A pattern is relative to the directory of the .clang-format-ignore file (or
//   the root directory if the pattern starts with a slash).
// - A pattern is negated if it starts with a bang (`!`).
static bool isIgnored(const FormatOptions &O, StringRef FilePath) {
  using namespace llvm::sys::fs;
  if (!is_regular_file(FilePath))
    return false;

  String Path;
  String AbsPath{FilePath};

  using namespace llvm::sys::path;
  make_absolute(AbsPath);
  remove_dots(AbsPath, /*remove_dot_dot=*/true);

  if (StringRef Dir{parent_path(AbsPath)}; PrevDir != Dir) {
    PrevDir = Dir;

    for (;;) {
      Path = Dir;
      append(Path, ".clang-format-ignore");
      if (is_regular_file(Path))
        break;
      Dir = parent_path(Dir);
      if (Dir.empty())
        return false;
    }

    IgnoreDir = convert_to_slash(Dir);

    std::ifstream IgnoreFile{Path.c_str()};
    if (!IgnoreFile.good())
      return false;

    Patterns.clear();

    for (std::string Line; std::getline(IgnoreFile, Line);) {
      if (const auto Pattern{StringRef{Line}.trim()};
          // Skip empty and comment lines.
          !Pattern.empty() && Pattern[0] != '#') {
        Patterns.push_back(Pattern);
      }
    }
  }

  if (IgnoreDir.empty())
    return false;

  bool IsIgnored = false;
  const auto Pathname{convert_to_slash(AbsPath)};
  for (const auto &Pat : Patterns) {
    const bool IsNegated = Pat[0] == '!';
    StringRef Pattern{Pat};
    if (IsNegated)
      Pattern = Pattern.drop_front();

    if (Pattern.empty())
      continue;

    Pattern = Pattern.ltrim();

    // `Pattern` is relative to `IgnoreDir` unless it starts with a slash.
    // This doesn't support patterns containing drive names (e.g. `C:`).
    if (Pattern[0] != '/') {
      Path = IgnoreDir;
      append(Path, Style::posix, Pattern);
      remove_dots(Path, /*remove_dot_dot=*/true, Style::posix);
      Pattern = Path;
    }

    if (clang::format::matchFilePath(Pattern, Pathname))
      IsIgnored = !IsNegated;
  }

  return IsIgnored;
}

int main(int argc, const char **argv) {
  InitLLVM X(argc, argv);

  clv2::OptionParser P;
  P.add<&ClangFormatReg>();
  RegisterAllLLVMOptions(P);
  P.hideUnrelatedOptions({&Clv2ClangFormatCategory});

  static constexpr llvm::StringLiteral Overview =
      "A tool to format C/C++/Java/JavaScript/JSON/Objective-C/Protobuf/C# "
      "code.\n\n"
      "If no arguments are specified, it formats the code from standard input\n"
      "and writes the result to the standard output.\n"
      "If <file>s are given, it reformats the files. If -i is specified\n"
      "together with <file>s, the files are edited in-place. Otherwise, the\n"
      "result is written to the standard output.\n";
  auto OptsCtx = P.parse(argc, argv, Overview,
                         /*Errs=*/nullptr, /*VersionString=*/{},
                         /*HelpOS=*/nullptr, PrintVersion);
  auto *Opts = OptsCtx->getViewPtr<&ClangFormatReg>();

  // Materialise the parsed values into a local this invocation owns.
  FormatOptions O;
  O.Help = Opts->get<&HelpOpt>();
  O.Offsets = Opts->get<&OffsetsOpt>();
  O.Lengths = Opts->get<&LengthsOpt>();
  O.LineRanges = Opts->get<&LineRangesOpt>();
  O.Style = Opts->get<&StyleOpt>();
  if (O.Style.empty())
    O.Style = clang::format::DefaultFormatStyle;
  O.FallbackStyle = Opts->get<&FallbackStyleOpt>();
  if (O.FallbackStyle.empty())
    O.FallbackStyle = clang::format::DefaultFallbackStyle;
  O.AssumeFileName = Opts->get<&AssumeFileNameOpt>();
  O.Inplace = Opts->get<&InplaceOpt>();
  O.OutputXML = Opts->get<&OutputXMLOpt>();
  O.DumpConfig = Opts->get<&DumpConfigOpt>();
  O.Cursor = Opts->get<&CursorOpt>();
  O.SortIncludes = Opts->get<&SortIncludesOpt>();
  O.QualifierAlignment = Opts->get<&QualifierAlignmentOpt>();
  O.Files = Opts->get<&FilesOpt>();
  O.Verbose = Opts->get<&VerboseOpt>();
  O.DryRun = Opts->get<&DryRunOpt>();
  O.WarnFormat = Opts->get<&WarnFormatOpt>();
  O.NoWarnFormat = Opts->get<&NoWarnFormatOpt>();
  O.ErrorLimit = Opts->get<&ErrorLimitOpt>();
  O.WarningsAsErrors = Opts->get<&WarningsAsErrorsOpt>();
  O.WNoErrorUnknown = Opts->specified<&WNoErrorOpt>();
  // Formerly tracked by clv2 Callback modifiers writing file-scope flags;
  // the parse already records whether the option appeared.
  O.CursorSpecified = Opts->specified<&CursorOpt>();
  O.SortIncludesSpecified = Opts->specified<&SortIncludesOpt>();
  O.ShowColors = Opts->get<&ShowColorsOpt>();
  O.NoShowColors = Opts->get<&NoShowColorsOpt>();
  O.FileNames = Opts->get<&FileNamesOpt>();
  O.FailOnIncompleteFormat = Opts->get<&FailOnIncompleteFormatOpt>();
  O.ListIgnored = Opts->get<&ListIgnoredOpt>();

  if (O.Help) {
    P.printHelp(llvm::outs(), Overview, argv[0]);
    return 0;
  }

  if (O.DumpConfig)
    return dumpConfig(O);

  if (!O.Files.empty()) {
    std::ifstream ExternalFileOfFiles{std::string(O.Files)};
    std::string Line;
    unsigned LineNo = 1;
    while (std::getline(ExternalFileOfFiles, Line)) {
      O.FileNames.push_back(Line);
      LineNo++;
    }
    errs() << "Clang-formatting " << LineNo << " files\n";
  }

  if (O.FileNames.empty()) {
    if (isIgnored(O, O.AssumeFileName)) {
      // The user should be able to expect that running
      // `cat foo | clang-format --assume-filename foo` and writing the output
      // to foo will format foo.
      // Thus, we need to just output stdin untouched if it is ignored.
      if (!O.OutputXML)
        outs() << MemoryBuffer::getSTDIN()->get()->getBuffer();
      return 0;
    }
    return clang::format::format(O, "-", O.FailOnIncompleteFormat);
  }

  if (O.FileNames.size() > 1 &&
      (!O.Offsets.empty() || !O.Lengths.empty() || !O.LineRanges.empty())) {
    errs() << "error: -offset, -length and -lines can only be used for "
              "single file.\n";
    return 1;
  }

  unsigned FileNo = 1;
  bool Error = false;
  for (const auto &FileName : O.FileNames) {
    const bool Ignored = isIgnored(O, FileName);
    if (O.ListIgnored) {
      if (Ignored)
        outs() << FileName << '\n';
      continue;
    }
    if (Ignored)
      continue;
    if (O.Verbose) {
      errs() << "Formatting [" << FileNo++ << "/" << O.FileNames.size() << "] "
             << FileName << "\n";
    }
    Error |= clang::format::format(O, FileName, O.FailOnIncompleteFormat);
  }
  return Error ? 1 : 0;
}
