//===- AggressiveInstCombineOptions.cpp - AggressiveInstCombine options -===//
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
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombineOptions.h"

using namespace llvm;
using namespace llvm::opt;

namespace {
enum ID {
  OPT_INVALID = 0,
#define OPTION(...) LLVM_MAKE_OPT_ID(__VA_ARGS__),
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombineOptions.inc"
#undef OPTION
};

#define OPTTABLE_STR_TABLE_CODE
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombineOptions.inc"
#undef OPTTABLE_STR_TABLE_CODE

#define OPTTABLE_PREFIXES_TABLE_CODE
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombineOptions.inc"
#undef OPTTABLE_PREFIXES_TABLE_CODE

static constexpr OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombineOptions.inc"
#undef OPTION
};

class AggressiveInstCombineOptTable : public GenericOptTable {
public:
  AggressiveInstCombineOptTable()
      : GenericOptTable(OptionStrTable, OptionPrefixesTable, InfoTable) {}
};
} // namespace

llvm::opt::OptTable &llvm::AggressiveInstCombineOptions::table() {
  static AggressiveInstCombineOptTable Table;
  return Table;
}

#define OPTIONS_STRUCT_DEFS
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombineOptions.inc"
#undef OPTIONS_STRUCT_DEFS

static llvm::opt::RegisterLibraryOptions<llvm::AggressiveInstCombineOptions>
    Registration;
