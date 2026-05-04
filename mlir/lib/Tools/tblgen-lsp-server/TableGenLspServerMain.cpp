//===- TableGenLspServerMain.cpp - TableGen Language Server main ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Tools/tblgen-lsp-server/TableGenLspServerMain.h"
#include "LSPServer.h"
#include "TableGenServer.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/LSP/Logging.h"
#include "llvm/Support/LSP/Transport.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/RegisterLLVMOptions.h"

using namespace mlir;
using namespace mlir::lsp;

using llvm::lsp::JSONStreamStyle;
using llvm::lsp::JSONTransport;
using llvm::lsp::Logger;

//===----------------------------------------------------------------------===//
// Command-line options
//===----------------------------------------------------------------------===//

using namespace llvm::clv2;

static constexpr OptionCategory TblgenLspCategory{"tblgen-lsp-server Options"};

enum class LspInputStyle : int { Standard = 0, Delimited };
static constexpr EnumVal<LspInputStyle> tblgenInputStyleVals[] = {
    {"standard", LspInputStyle::Standard, "usual LSP protocol"},
    {"delimited", LspInputStyle::Delimited,
     "messages delimited by `// -----` lines, with // comment support"},
};
static constexpr auto tblgenInputStyleOpt = makeEnumOption<LspInputStyle>(
    "input-style", "Input JSON stream encoding", tblgenInputStyleVals,
    Init{LspInputStyle::Standard}, Hidden);
static constexpr OptionInfo<bool> tblgenLitTestOpt{
    "lit-test", "Abbreviation for -input-style=delimited -pretty -log=verbose. "
                "Intended to simplify lit tests"};
enum class LspLogLevel : int { Error = 0, Info, Verbose };
static constexpr EnumVal<LspLogLevel> tblgenLogLevelVals[] = {
    {"error", LspLogLevel::Error, "Error messages only"},
    {"info", LspLogLevel::Info, "High level execution tracing"},
    {"verbose", LspLogLevel::Verbose, "Low level details"},
};
static constexpr auto tblgenLogOpt = makeEnumOption<LspLogLevel>(
    "log", "Verbosity of log messages written to stderr", tblgenLogLevelVals,
    Init{LspLogLevel::Info});
static constexpr OptionInfo<bool> tblgenPrettyOpt{"pretty",
                                                  "Pretty-print JSON output"};
static constexpr ListOptionInfo<std::string> tblgenExtraDirOpt{
    "tablegen-extra-dir", "Extra directory of include files",
    value_desc("directory"), CommaSeparated};
static constexpr ListOptionInfo<std::string> tblgenCompDbOpt{
    "tablegen-compilation-database",
    "Compilation YAML databases containing additional "
    "compilation information for .td files",
    CommaSeparated};

static constexpr OptionsRegistry<&tblgenInputStyleOpt, &tblgenLitTestOpt,
                                 &tblgenLogOpt, &tblgenPrettyOpt,
                                 &tblgenExtraDirOpt, &tblgenCompDbOpt>
    TblgenLspReg;

LogicalResult mlir::TableGenLspServerMain(int argc, char **argv) {
  llvm::clv2::OptionParser P;
  P.add<&TblgenLspReg>();
  llvm::RegisterCoreLLVMOptions(P);
  auto OptsCtx = P.parse(argc, argv, "TableGen LSP Language Server");
  auto *Opts = OptsCtx->getViewPtr<&TblgenLspReg>();

  auto inputStyle =
      static_cast<JSONStreamStyle>(Opts->get<&tblgenInputStyleOpt>());
  bool litTest = Opts->get<&tblgenLitTestOpt>();
  auto logLevel = static_cast<Logger::Level>(Opts->get<&tblgenLogOpt>());
  bool prettyPrint = Opts->get<&tblgenPrettyOpt>();

  if (litTest) {
    inputStyle = JSONStreamStyle::Delimited;
    logLevel = Logger::Level::Debug;
    prettyPrint = true;
  }

  Logger::setLogLevel(logLevel);

  llvm::sys::ChangeStdinToBinary();
  JSONTransport transport(stdin, llvm::outs(), inputStyle, prettyPrint);

  TableGenServer::Options options(Opts->get<&tblgenCompDbOpt>(),
                                  Opts->get<&tblgenExtraDirOpt>());
  TableGenServer server(options);
  return runTableGenLSPServer(server, transport);
}
