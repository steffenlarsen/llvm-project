//===- PassesOptions.cpp - Passes library command-line options ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Option/Arg.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/LibraryOptions.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Option.h"
#include "llvm/Passes/PassesOptions.h"

using namespace llvm;
using namespace llvm::opt;

namespace {
enum ID {
  OPT_INVALID = 0,
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "llvm/Passes/PassesOptions.inc"
#undef OPTION
};

#define OPTTABLE_STR_TABLE_CODE
#include "llvm/Passes/PassesOptions.inc"
#undef OPTTABLE_STR_TABLE_CODE

#define OPTTABLE_PREFIXES_TABLE_CODE
#include "llvm/Passes/PassesOptions.inc"
#undef OPTTABLE_PREFIXES_TABLE_CODE

static constexpr OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "llvm/Passes/PassesOptions.inc"
#undef OPTION
};

class PassesOptTable : public GenericOptTable {
public:
  PassesOptTable()
      : GenericOptTable(OptionStrTable, OptionPrefixesTable, InfoTable) {}
};
} // namespace

llvm::opt::OptTable &llvm::PassesOptions::table() {
  static PassesOptTable Table;
  return Table;
}

#define OPTIONS_STRUCT_DEFS
#include "llvm/Passes/PassesOptions.inc"
#undef OPTIONS_STRUCT_DEFS

static llvm::opt::RegisterLibraryOptions<llvm::PassesOptions> Registration;
