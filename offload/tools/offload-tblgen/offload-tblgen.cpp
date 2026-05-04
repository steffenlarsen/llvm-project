//===- offload-tblgen/offload-tblgen.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a Tablegen tool that produces source files for the Offload project.
// See offload/API/README.md for more information.
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/TableGen/Main.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"

#include "Generators.hpp"

namespace llvm {
namespace offload {
namespace tblgen {

enum ActionType {
  PrintRecords,
  DumpJSON,
  GenAPI,
  GenDoc,
  GenFuncNames,
  GenImplFuncDecls,
  GenEntryPoints,
  GenPrintHeader,
  GenExports,
  GenErrcodes,
  GenInfo,
};

static constexpr clv2::EnumVal<ActionType> ActionVals[] = {
    {"print-records", PrintRecords, "Print all records to stdout (default)"},
    {"dump-json", DumpJSON, "Dump all records as machine-readable JSON"},
    {"gen-api", GenAPI, "Generate Offload API header contents"},
    {"gen-doc", GenDoc, "Generate Offload API documentation contents"},
    {"gen-func-names", GenFuncNames,
     "Generate a list of all Offload API function names"},
    {"gen-impl-func-decls", GenImplFuncDecls,
     "Generate declarations for Offload API implementation functions"},
    {"gen-entry-points", GenEntryPoints,
     "Generate Offload API wrapper function definitions"},
    {"gen-print-header", GenPrintHeader, "Generate Offload API print header"},
    {"gen-exports", GenExports, "Generate export file for the Offload library"},
    {"gen-errcodes", GenErrcodes, "Generate Offload Error Code enum"},
    {"gen-info", GenInfo, "Generate Offload Info enum"},
};

static constexpr auto ActionOpt = clv2::makeEnumOption<ActionType>(
    "", "Action to perform:", ActionVals, clv2::Init{PrintRecords});

// File-scope variable populated from parsed options.
static ActionType Action;

static bool OffloadTableGenMain(raw_ostream &OS, const RecordKeeper &Records) {
  switch (Action) {
  case PrintRecords:
    OS << Records;
    break;
  case DumpJSON:
    EmitJSON(Records, OS);
    break;
  case GenAPI:
    EmitOffloadAPI(Records, OS);
    break;
  case GenDoc:
    EmitOffloadDoc(Records, OS);
    break;
  case GenFuncNames:
    EmitOffloadFuncNames(Records, OS);
    break;
  case GenImplFuncDecls:
    EmitOffloadImplFuncDecls(Records, OS);
    break;
  case GenEntryPoints:
    EmitOffloadEntryPoints(Records, OS);
    break;
  case GenPrintHeader:
    EmitOffloadPrintHeader(Records, OS);
    break;
  case GenExports:
    EmitOffloadExports(Records, OS);
    break;
  case GenErrcodes:
    EmitOffloadErrcodes(Records, OS);
    break;
  case GenInfo:
    EmitOffloadInfo(Records, OS);
    break;
  }

  return false;
}

static constexpr clv2::OptionsRegistry<&ActionOpt> OffloadTblgenReg;

int OffloadTblgenMain(int argc, char **argv) {
  InitLLVM y(argc, argv);
  clv2::OptionParser P;
  registerTableGenMainOptions(P);
  TableGen::Emitter::registerBackendOptions(P);
  P.add<&OffloadTblgenReg>();
  auto OptsCtx = P.parse(argc, argv, "Offload TableGen\n");
  auto *Opts = OptsCtx->getViewPtr<&OffloadTblgenReg>();
  Action = Opts->get<&ActionOpt>();
  return TableGenMain(argv[0], &OffloadTableGenMain);
}
} // namespace tblgen
} // namespace offload
} // namespace llvm

using namespace llvm;
using namespace offload::tblgen;

int main(int argc, char **argv) { return OffloadTblgenMain(argc, argv); }
