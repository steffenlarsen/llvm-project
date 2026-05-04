//===- MlirPdllLspServerMain.cpp - MLIR PDLL Language Server main ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Tools/mlir-pdll-lsp-server/MlirPdllLspServerMain.h"
#include "LSPServer.h"
#include "PDLLServer.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/LSP/Logging.h"
#include "llvm/Support/LSP/Transport.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/RegisterLLVMOptions.h"

using namespace mlir;
using namespace mlir::lsp;

using llvm::lsp::JSONStreamStyle;
using llvm::lsp::Logger;

//===----------------------------------------------------------------------===//
// Command-line options
//===----------------------------------------------------------------------===//

using namespace llvm::clv2;

static constexpr OptionCategory PdllLspCategory{"mlir-pdll-lsp-server Options"};

enum class LspInputStyle : int { Standard = 0, Delimited };
static constexpr EnumVal<LspInputStyle> pdllInputStyleVals[] = {
    {"standard", LspInputStyle::Standard, "usual LSP protocol"},
    {"delimited", LspInputStyle::Delimited,
     "messages delimited by `// -----` lines, with // comment support"},
};
static constexpr auto pdllInputStyleOpt = makeEnumOption<LspInputStyle>(
    "input-style", "Input JSON stream encoding", pdllInputStyleVals,
    Init{LspInputStyle::Standard}, Hidden);
static constexpr OptionInfo<bool> pdllLitTestOpt{
    "lit-test", "Abbreviation for -input-style=delimited -pretty -log=verbose. "
                "Intended to simplify lit tests"};
enum class LspLogLevel : int { Error = 0, Info, Verbose };
static constexpr EnumVal<LspLogLevel> pdllLogLevelVals[] = {
    {"error", LspLogLevel::Error, "Error messages only"},
    {"info", LspLogLevel::Info, "High level execution tracing"},
    {"verbose", LspLogLevel::Verbose, "Low level details"},
};
static constexpr auto pdllLogOpt = makeEnumOption<LspLogLevel>(
    "log", "Verbosity of log messages written to stderr", pdllLogLevelVals,
    Init{LspLogLevel::Info});
static constexpr OptionInfo<bool> pdllPrettyOpt{"pretty",
                                                "Pretty-print JSON output"};
static constexpr ListOptionInfo<std::string> pdllExtraDirOpt{
    "pdll-extra-dir", "Extra directory of include files",
    value_desc("directory"), CommaSeparated};
static constexpr ListOptionInfo<std::string> pdllCompDbOpt{
    "pdll-compilation-database",
    "Compilation YAML databases containing additional "
    "compilation information for .pdll files",
    CommaSeparated};

static constexpr OptionsRegistry<&pdllInputStyleOpt, &pdllLitTestOpt,
                                 &pdllLogOpt, &pdllPrettyOpt, &pdllExtraDirOpt,
                                 &pdllCompDbOpt>
    PdllLspReg;

LogicalResult mlir::MlirPdllLspServerMain(int argc, char **argv) {
  llvm::clv2::OptionParser P;
  P.add<&PdllLspReg>();
  llvm::RegisterCoreLLVMOptions(P);
  auto OptsCtx = P.parse(argc, argv, "PDLL LSP Language Server");
  auto *Opts = OptsCtx->getViewPtr<&PdllLspReg>();

  auto inputStyle =
      static_cast<JSONStreamStyle>(Opts->get<&pdllInputStyleOpt>());
  bool litTest = Opts->get<&pdllLitTestOpt>();
  auto logLevel = static_cast<Logger::Level>(Opts->get<&pdllLogOpt>());
  bool prettyPrint = Opts->get<&pdllPrettyOpt>();

  if (litTest) {
    inputStyle = JSONStreamStyle::Delimited;
    logLevel = Logger::Level::Debug;
    prettyPrint = true;
  }

  Logger::setLogLevel(logLevel);

  llvm::sys::ChangeStdinToBinary();
  llvm::lsp::JSONTransport transport(stdin, llvm::outs(), inputStyle,
                                     prettyPrint);

  PDLLServer::Options options(Opts->get<&pdllCompDbOpt>(),
                              Opts->get<&pdllExtraDirOpt>());
  PDLLServer server(options);
  return runPdllLSPServer(server, transport);
}
