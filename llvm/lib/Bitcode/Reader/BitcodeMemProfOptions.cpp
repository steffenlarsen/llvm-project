//===- BitcodeMemProfOptions.cpp - Bitcode memprof command-line options --===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/BitcodeMemProfOptions.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/LibraryOptions.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"

using namespace llvm;
using namespace llvm::opt;

namespace {
enum ID {
  OPT_INVALID = 0,
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "llvm/Bitcode/BitcodeMemProfOptions.inc"
#undef OPTION
};

#define OPTTABLE_STR_TABLE_CODE
#include "llvm/Bitcode/BitcodeMemProfOptions.inc"
#undef OPTTABLE_STR_TABLE_CODE

#define OPTTABLE_PREFIXES_TABLE_CODE
#include "llvm/Bitcode/BitcodeMemProfOptions.inc"
#undef OPTTABLE_PREFIXES_TABLE_CODE

static constexpr OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "llvm/Bitcode/BitcodeMemProfOptions.inc"
#undef OPTION
};

class BitcodeMemProfOptTable : public GenericOptTable {
public:
  BitcodeMemProfOptTable()
      : GenericOptTable(OptionStrTable, OptionPrefixesTable, InfoTable) {}
};
} // namespace

llvm::opt::OptTable &llvm::BitcodeMemProfOptions::table() {
  static BitcodeMemProfOptTable Table;
  return Table;
}

#define OPTIONS_STRUCT_DEFS
#include "llvm/Bitcode/BitcodeMemProfOptions.inc"
#undef OPTIONS_STRUCT_DEFS

static llvm::opt::RegisterLibraryOptions<llvm::BitcodeMemProfOptions>
    Registration;

// This getter is called from combined/summary-index-only writer paths (see
// llvm::writeIndexToFile()'s callers) that have no Module or LLVMContext in
// scope, so it cannot use Ctx.getContext().getOptions<BitcodeMemProfOptions>().
// It reads the process-wide BitcodeMemProfOptions::Current default instead,
// which every top-level parseLibraryOptionsChain<BitcodeMemProfOptions> call
// keeps up to date.
bool llvm::getCombinedIndexMemProfContextEnabled(
    const clv2::OptionsContext &Ctx) {
  return BitcodeMemProfOptions::Current.BC_CombinedIndexMemProfContext;
}
