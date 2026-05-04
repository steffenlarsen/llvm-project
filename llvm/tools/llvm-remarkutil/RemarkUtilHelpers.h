//===- RemarkUtilHelpers.h ------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Helpers for remark utilites
//
//===----------------------------------------------------------------------===//
#ifndef TOOLS_LLVM_REMARKUTIL_HELPERS_H
#define TOOLS_LLVM_REMARKUTIL_HELPERS_H

#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Remarks/Remark.h"
#include "llvm/Remarks/RemarkFormat.h"
#include "llvm/Remarks/RemarkParser.h"
#include "llvm/Remarks/RemarkSerializer.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/StringSaver.h"
#include "llvm/Support/ToolOutputFile.h"
#include <optional>
#include <string>
#include <vector>

//===----------------------------------------------------------------------===//
// Enum types for subcommand options
//===----------------------------------------------------------------------===//

namespace instructionmix {
enum ReportStyleOptions { human_output, csv_output };
} // namespace instructionmix

namespace sizediff {
enum ReportStyleOptions { human_output, json_output };
} // namespace sizediff

namespace summary {
enum class KeepMode { None, Used, All };
} // namespace summary

namespace llvm {
namespace remarkutil {

// Shared option globals (set by main after parsing, used by sub-file handlers)
extern std::string InputFileName;
extern std::string OutputFileName;
extern remarks::Format InputFormat;
extern remarks::Format OutputFormat;
extern bool UseDebugLoc;

// Shared filter globals (used by count and filter subcommands)
extern std::string FunctionOpt;
extern std::string FunctionOptRE;
extern std::string RemarkNameOpt;
extern std::string RemarkNameOptRE;
extern std::string PassNameOpt;
extern std::string PassNameOptRE;
extern std::optional<remarks::Type> RemarkTypeFilter;
extern std::string RemarkFilterArgByOpt;
extern std::string RemarkArgFilterOptRE;

} // namespace remarkutil

namespace remarks {
Expected<std::unique_ptr<MemoryBuffer>>
getInputMemoryBuffer(StringRef InputFileName);
Expected<std::unique_ptr<ToolOutputFile>>
getOutputFileWithFlags(StringRef OutputFileName, sys::fs::OpenFlags Flags);
Expected<std::unique_ptr<ToolOutputFile>>
getOutputFileForRemarks(StringRef OutputFileName, Format OutputFormat);

/// Choose the serializer format. If \p SelectedFormat is Format::Auto, try to
/// detect the format based on the extension of \p OutputFileName or fall back
/// to \p DefaultFormat.
Format getSerializerFormat(StringRef OutputFileName, Format SelectedFormat,
                           Format DefaultFormat);

/// Filter object which can be either a string or a regex to match with the
/// remark properties.
class FilterMatcher {
  Regex FilterRE;
  std::string FilterStr;
  bool IsRegex;

  FilterMatcher(StringRef Filter, bool IsRegex)
      : FilterRE(Filter), FilterStr(Filter), IsRegex(IsRegex) {}

public:
  static FilterMatcher createExact(StringRef Filter) { return {Filter, false}; }

  static Expected<FilterMatcher> createRE(StringRef ArgName, StringRef Value);

  static Expected<std::optional<FilterMatcher>>
  createExactOrRE(StringRef ExactValue, StringRef REValue,
                  StringRef ExactArgName, StringRef REArgName);

  static FilterMatcher createAny() { return {".*", true}; }

  bool match(StringRef StringToMatch) const {
    if (IsRegex)
      return FilterRE.match(StringToMatch);
    return FilterStr == StringToMatch.trim().str();
  }
};

/// Filter out remarks based on remark properties (function, remark name, pass
/// name, argument values and type).
struct Filters {
  std::optional<FilterMatcher> FunctionFilter;
  std::optional<FilterMatcher> RemarkNameFilter;
  std::optional<FilterMatcher> PassNameFilter;
  std::optional<FilterMatcher> ArgFilter;
  std::optional<Type> RemarkTypeFilter;

  /// Returns true if \p Remark satisfies all the provided filters.
  bool filterRemark(const Remark &Remark);
};

/// Construct filter objects from the shared filter globals.
Expected<Filters> getRemarkFilters();

/// Helper to construct Remarks using an API similar to DiagnosticInfo.
/// Once this is more fully featured, consider implementing DiagnosticInfo using
/// RemarkBuilder.
class RemarkBuilder {
  BumpPtrAllocator Alloc;
  UniqueStringSaver Strs;

public:
  Remark R;
  struct Argument {
    std::string Key;
    std::string Val;
    std::optional<RemarkLocation> Loc;
    Argument(StringRef Key, StringRef Val,
             std::optional<RemarkLocation> Loc = std::nullopt)
        : Key(Key), Val(Val), Loc(Loc) {}
    Argument(StringRef Key, int Val,
             std::optional<RemarkLocation> Loc = std::nullopt)
        : Key(Key), Val(itostr(Val)), Loc(Loc) {}
  };

  RemarkBuilder(Type RemarkType, StringRef PassName, StringRef RemarkName,
                StringRef FunctionName)
      : Strs(Alloc) {
    R.RemarkType = RemarkType;
    R.PassName = Strs.save(PassName);
    R.RemarkName = Strs.save(RemarkName);
    R.FunctionName = Strs.save(FunctionName);
  }

  RemarkBuilder &operator<<(Argument &&Arg) {
    auto &RArg = R.Args.emplace_back(Strs.save(Arg.Key), Strs.save(Arg.Val));
    RArg.Loc = Arg.Loc;
    return *this;
  }

  RemarkBuilder &operator<<(const char *Str) {
    R.Args.emplace_back("String", Str);
    return *this;
  }

  RemarkBuilder &operator<<(StringRef Str) {
    R.Args.emplace_back("String", Strs.save(Str));
    return *this;
  }
};

using NV = RemarkBuilder::Argument;

} // namespace remarks
} // namespace llvm

#endif // TOOLS_LLVM_REMARKUTIL_HELPERS_H
