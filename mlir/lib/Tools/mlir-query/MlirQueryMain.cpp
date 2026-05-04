//===- MlirQueryMain.cpp - MLIR Query main --------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the general framework of the MLIR query tool. It
// parses the command line arguments, parses the MLIR file and outputs the query
// results.
//
//===----------------------------------------------------------------------===//

#include "mlir/Tools/mlir-query/MlirQueryMain.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/Parser/Parser.h"
#include "mlir/Query/Query.h"
#include "mlir/Query/QuerySession.h"
#include "mlir/Support/FileUtilities.h"
#include "llvm/LineEditor/LineEditor.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"

using namespace llvm::clv2;

static constexpr OptionCategory MlirQueryCategory{"mlir-query options"};

static constexpr OptionInfo<std::string> queryInputOpt{"", "<input file>",
                                                       Positional{}, Init{"-"}};
static constexpr ListOptionInfo<std::string> queryCommandsOpt{
    "c", "Specify command to run", value_desc("command"),
    cat(MlirQueryCategory)};
static constexpr OptionInfo<bool> queryNoImplicitModuleOpt{
    "no-implicit-module",
    "Disable implicit addition of a top-level module op during parsing", Hidden,
    cat(MlirQueryCategory)};
static constexpr OptionInfo<bool> queryAllowUnregisteredOpt{
    "allow-unregistered-dialect", "Allow operation with no registered dialects",
    Hidden, cat(MlirQueryCategory)};

static constexpr OptionsRegistry<&queryInputOpt, &queryCommandsOpt,
                                 &queryNoImplicitModuleOpt,
                                 &queryAllowUnregisteredOpt>
    MlirQueryReg;

llvm::LogicalResult
mlir::mlirQueryMain(int argc, char **argv, mlir::MLIRContext &context,
                    const mlir::query::matcher::Registry &matcherRegistry) {

  llvm::InitLLVM y(argc, argv);

  llvm::clv2::OptionParser P;
  P.add<&MlirQueryReg>();
  llvm::RegisterCoreLLVMOptions(P);
  P.hideUnrelatedOptions({&MlirQueryCategory});
  auto OptsCtx = P.parse(argc, argv, "MLIR test case query tool.\n");
  auto *Opts = OptsCtx->getViewPtr<&MlirQueryReg>();

  // When reading from stdin and the input is a tty, it is often a user mistake
  // and the process "appears to be stuck". Print a message to let the user
  // know!
  auto &inputFilename = Opts->get<&queryInputOpt>();
  bool noImplicitModule = Opts->get<&queryNoImplicitModuleOpt>();
  bool allowUnregisteredDialects = Opts->get<&queryAllowUnregisteredOpt>();
  auto &commands = Opts->get<&queryCommandsOpt>();

  if (inputFilename == "-" &&
      llvm::sys::Process::FileDescriptorIsDisplayed(fileno(stdin)))
    llvm::errs() << "(processing input from stdin now, hit ctrl-c/ctrl-d to "
                    "interrupt)\n";

  // Set up the input file.
  std::string errorMessage;
  auto file = openInputFile(inputFilename, &errorMessage);
  if (!file) {
    llvm::errs() << errorMessage << "\n";
    return mlir::failure();
  }

  auto sourceMgr = llvm::SourceMgr();
  auto bufferId = sourceMgr.AddNewSourceBuffer(std::move(file), SMLoc());

  context.allowUnregisteredDialects(allowUnregisteredDialects);

  // Parse the input MLIR file.
  OwningOpRef<Operation *> opRef =
      noImplicitModule ? parseSourceFile(sourceMgr, &context)
                       : parseSourceFile<mlir::ModuleOp>(sourceMgr, &context);
  if (!opRef)
    return mlir::failure();

  mlir::query::QuerySession qs(opRef.get(), sourceMgr, bufferId,
                               matcherRegistry);
  if (!commands.empty()) {
    for (auto &command : commands) {
      mlir::query::QueryRef queryRef = mlir::query::parse(command, qs);
      if (mlir::failed(queryRef->run(llvm::outs(), qs)))
        return mlir::failure();
    }
  } else {
    llvm::LineEditor le("mlir-query");
    le.setListCompleter([&qs](llvm::StringRef line, size_t pos) {
      return mlir::query::complete(line, pos, qs);
    });
    while (std::optional<std::string> line = le.readLine()) {
      mlir::query::QueryRef queryRef = mlir::query::parse(*line, qs);
      (void)queryRef->run(llvm::outs(), qs);
      llvm::outs().flush();
      if (qs.terminate)
        break;
    }
  }

  return mlir::success();
}
