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

static constexpr clv2::OptionInfo<bool> PrintRecordsOpt{
    "print-records", "Print all records to stdout (default)",
    clv2::ValueDisallowed, clv2::EnumGroup{"Action to perform:"}};
static constexpr clv2::OptionInfo<bool> DumpJSONOpt{
    "dump-json", "Dump all records as machine-readable JSON",
    clv2::ValueDisallowed, clv2::EnumGroup{}};
static constexpr clv2::OptionInfo<bool> GenAPIOpt{
    "gen-api", "Generate Offload API header contents", clv2::ValueDisallowed,
    clv2::EnumGroup{}};
static constexpr clv2::OptionInfo<bool> GenDocOpt{
    "gen-doc", "Generate Offload API documentation contents",
    clv2::ValueDisallowed, clv2::EnumGroup{}};
static constexpr clv2::OptionInfo<bool> GenFuncNamesOpt{
    "gen-func-names", "Generate a list of all Offload API function names",
    clv2::ValueDisallowed, clv2::EnumGroup{}};
static constexpr clv2::OptionInfo<bool> GenImplFuncDeclsOpt{
    "gen-impl-func-decls",
    "Generate declarations for Offload API implementation functions",
    clv2::ValueDisallowed, clv2::EnumGroup{}};
static constexpr clv2::OptionInfo<bool> GenEntryPointsOpt{
    "gen-entry-points", "Generate Offload API wrapper function definitions",
    clv2::ValueDisallowed, clv2::EnumGroup{}};
static constexpr clv2::OptionInfo<bool> GenPrintHeaderOpt{
    "gen-print-header", "Generate Offload API print header",
    clv2::ValueDisallowed, clv2::EnumGroup{}};
static constexpr clv2::OptionInfo<bool> GenExportsOpt{
    "gen-exports", "Generate export file for the Offload library",
    clv2::ValueDisallowed, clv2::EnumGroup{}};
static constexpr clv2::OptionInfo<bool> GenErrcodesOpt{
    "gen-errcodes", "Generate Offload Error Code enum", clv2::ValueDisallowed,
    clv2::EnumGroup{}};
static constexpr clv2::OptionInfo<bool> GenInfoOpt{
    "gen-info", "Generate Offload Info enum", clv2::ValueDisallowed,
    clv2::EnumGroup{}};

static bool OffloadTableGenMain(raw_ostream &OS, const RecordKeeper &Records,
                                ActionType Action) {
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

static constexpr clv2::OptionsRegistry<
    &PrintRecordsOpt, &DumpJSONOpt, &GenAPIOpt, &GenDocOpt, &GenFuncNamesOpt,
    &GenImplFuncDeclsOpt, &GenEntryPointsOpt, &GenPrintHeaderOpt,
    &GenExportsOpt, &GenErrcodesOpt, &GenInfoOpt>
    OffloadTblgenReg;

int OffloadTblgenMain(int argc, char **argv) {
  InitLLVM y(argc, argv);
  clv2::OptionParser P;
  registerTableGenMainOptions(P);
  TableGen::Emitter::registerBackendOptions(P);
  P.add<&OffloadTblgenReg>();
  auto OptsCtx = P.parse(argc, argv, "Offload TableGen\n");
  auto *Opts = OptsCtx->getViewPtr<&OffloadTblgenReg>();

  // Each flag below writes into the same logical Action choice; when more
  // than one is given, the one that appears latest on the command line wins.
  ActionType Action = PrintRecords;
  unsigned ActionPos = 0;
  auto ConsiderAction = [&](bool Specified, unsigned Position, ActionType Val) {
    if (Specified && Position > ActionPos) {
      Action = Val;
      ActionPos = Position;
    }
  };
  ConsiderAction(Opts->specified<&PrintRecordsOpt>(),
                 Opts->position<&PrintRecordsOpt>(), PrintRecords);
  ConsiderAction(Opts->specified<&DumpJSONOpt>(),
                 Opts->position<&DumpJSONOpt>(), DumpJSON);
  ConsiderAction(Opts->specified<&GenAPIOpt>(), Opts->position<&GenAPIOpt>(),
                 GenAPI);
  ConsiderAction(Opts->specified<&GenDocOpt>(), Opts->position<&GenDocOpt>(),
                 GenDoc);
  ConsiderAction(Opts->specified<&GenFuncNamesOpt>(),
                 Opts->position<&GenFuncNamesOpt>(), GenFuncNames);
  ConsiderAction(Opts->specified<&GenImplFuncDeclsOpt>(),
                 Opts->position<&GenImplFuncDeclsOpt>(), GenImplFuncDecls);
  ConsiderAction(Opts->specified<&GenEntryPointsOpt>(),
                 Opts->position<&GenEntryPointsOpt>(), GenEntryPoints);
  ConsiderAction(Opts->specified<&GenPrintHeaderOpt>(),
                 Opts->position<&GenPrintHeaderOpt>(), GenPrintHeader);
  ConsiderAction(Opts->specified<&GenExportsOpt>(),
                 Opts->position<&GenExportsOpt>(), GenExports);
  ConsiderAction(Opts->specified<&GenErrcodesOpt>(),
                 Opts->position<&GenErrcodesOpt>(), GenErrcodes);
  ConsiderAction(Opts->specified<&GenInfoOpt>(), Opts->position<&GenInfoOpt>(),
                 GenInfo);

  return TableGenMain(argv[0],
                      [Action](raw_ostream &OS, const RecordKeeper &Records) {
                        return OffloadTableGenMain(OS, Records, Action);
                      });
}
} // namespace tblgen
} // namespace offload
} // namespace llvm

using namespace llvm;
using namespace offload::tblgen;

int main(int argc, char **argv) { return OffloadTblgenMain(argc, argv); }
