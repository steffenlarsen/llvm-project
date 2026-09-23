//===- IROptions.cpp - IR options -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/IROptions.h"
#include "llvm/IR/OptBisect.h"
#include "llvm/IR/PassTimingInfo.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/LibraryOptions.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/Regex.h"

using namespace llvm;
using namespace llvm::opt;

namespace {
enum ID {
  OPT_INVALID = 0,
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "llvm/IR/IROptions.inc"
#undef OPTION
};

#define OPTTABLE_STR_TABLE_CODE
#include "llvm/IR/IROptions.inc"
#undef OPTTABLE_STR_TABLE_CODE

#define OPTTABLE_PREFIXES_TABLE_CODE
#include "llvm/IR/IROptions.inc"
#undef OPTTABLE_PREFIXES_TABLE_CODE

static constexpr OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "llvm/IR/IROptions.inc"
#undef OPTION
};

class IROptTable : public GenericOptTable {
public:
  IROptTable()
      : GenericOptTable(OptionStrTable, OptionPrefixesTable, InfoTable) {}
};
} // namespace

llvm::opt::OptTable &llvm::IROptions::table() {
  static IROptTable Table;
  return Table;
}

#define OPTIONS_STRUCT_DEFS
#include "llvm/IR/IROptions.inc"
#undef OPTIONS_STRUCT_DEFS

static llvm::opt::RegisterLibraryOptions<llvm::IROptions> Registration;

// DiagnosticHandler.cpp's isPassedOptRemarkEnabled() et al. lazily (re-)build
// their Regex from the last -pass-remarks*= value on every check, which only
// matches the original cl::opt<PassRemarksOpt> behavior (repeated flags: last
// wins) if that value is actually valid; the original custom parser validated
// eagerly at command-line-parse time via report_fatal_error, before any pass
// ever ran (see e.g. `not opt -pass-remarks='(' ...` in
// llvm/test/Other/optimization-remarks-inline.ll, which never runs a pass
// pipeline and so would never reach a lazy check). Replicate that eager
// validation here, including the original's message text unconditionally
// naming "-pass-remarks" even for the -missed/-analysis variants.
static void validatePassRemarksPattern(const std::vector<std::string> &Vals) {
  if (Vals.empty())
    return;
  const std::string &Val = Vals.back();
  if (Val.empty())
    return;
  Regex Pat(Val);
  std::string RegexError;
  if (!Pat.isValid(RegexError))
    report_fatal_error(Twine("Invalid regular expression '") + Val +
                           "' in -pass-remarks: " + RegexError,
                       false);
}

void llvm::ir_opts::applyIROptions() {
  // --- PassTimingInfo.cpp externs ---
  TimePassesIsEnabled = IROptions::Current.IR_TimePasses;
  TimePassesPerRun = IROptions::Current.IR_TimePassesPerRun;
  if (TimePassesPerRun)
    TimePassesIsEnabled = true;

  // --- OptBisect.cpp: configure the global OptBisect singleton ---
  initOptBisectFromOptions();

  // --- DiagnosticHandler.cpp: eager -pass-remarks* regex validation ---
  validatePassRemarksPattern(IROptions::Current.IR_PassRemarks);
  validatePassRemarksPattern(IROptions::Current.IR_PassRemarksMissed);
  validatePassRemarksPattern(IROptions::Current.IR_PassRemarksAnalysis);
}
