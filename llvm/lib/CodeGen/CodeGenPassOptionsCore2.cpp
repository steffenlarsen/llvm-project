//===- CodeGenPassOptionsCore2.cpp - Core2 command-line options ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/CodeGenPassOptionsCore2.h"
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
#include "llvm/CodeGen/CodeGenPassOptionsCore2.inc"
#undef OPTION
};

#define OPTTABLE_STR_TABLE_CODE
#include "llvm/CodeGen/CodeGenPassOptionsCore2.inc"
#undef OPTTABLE_STR_TABLE_CODE

#define OPTTABLE_PREFIXES_TABLE_CODE
#include "llvm/CodeGen/CodeGenPassOptionsCore2.inc"
#undef OPTTABLE_PREFIXES_TABLE_CODE

static constexpr OptTable::Info InfoTable[] = {
#define OPTION(...) LLVM_CONSTRUCT_OPT_INFO(__VA_ARGS__),
#include "llvm/CodeGen/CodeGenPassOptionsCore2.inc"
#undef OPTION
};

class CodeGenCore2OptTable : public GenericOptTable {
public:
  CodeGenCore2OptTable()
      : GenericOptTable(OptionStrTable, OptionPrefixesTable, InfoTable) {}
};
} // namespace

llvm::opt::OptTable &llvm::CodeGenCore2Options::table() {
  static CodeGenCore2OptTable Table;
  return Table;
}

#define OPTIONS_STRUCT_DEFS
#include "llvm/CodeGen/CodeGenPassOptionsCore2.inc"
#undef OPTIONS_STRUCT_DEFS

static llvm::opt::RegisterLibraryOptions<llvm::CodeGenCore2Options>
    Registration;
