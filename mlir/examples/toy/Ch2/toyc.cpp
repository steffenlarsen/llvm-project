//===- toyc.cpp - The Toy Compiler ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the entry point for the Toy compiler.
//
//===----------------------------------------------------------------------===//

#include "toy/AST.h"
#include "toy/Dialect.h"
#include "toy/Lexer.h"
#include "toy/MLIRGen.h"
#include "toy/Parser.h"
#include <memory>
#include <string>
#include <system_error>
#include <utility>

#include "mlir/IR/AsmState.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/Parser/Parser.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

using namespace toy;
using namespace llvm;

namespace {
enum InputType { Toy, MLIR };
enum Action { None, DumpAST, DumpMLIR };
} // namespace

static constexpr clv2::OptionInfo<std::string> inputFilenameOpt{
    "", "<input toy file>", clv2::Positional{}, clv2::Init{"-"}};

static constexpr clv2::EnumVal<InputType> inputTypeVals[] = {
    {"toy", Toy, "load the input file as a Toy source."},
    {"mlir", MLIR, "load the input file as an MLIR file"},
};
static constexpr auto inputTypeOpt = clv2::makeEnumOption<InputType>(
    "x", "Decided the kind of output desired", inputTypeVals);

static constexpr clv2::EnumVal<Action> emitActionVals[] = {
    {"ast", DumpAST, "output the AST dump"},
    {"mlir", DumpMLIR, "output the MLIR dump"},
};
static constexpr auto emitActionOpt = clv2::makeEnumOption<Action>(
    "emit", "Select the kind of output desired", emitActionVals);

static constexpr clv2::OptionsRegistry<&inputFilenameOpt, &inputTypeOpt,
                                       &emitActionOpt>
    ToyReg;

// These are populated from parsed options in main().
static std::string inputFilename;
static InputType inputType;
static Action emitAction;

/// Returns a Toy AST resulting from parsing the file or a nullptr on error.
static std::unique_ptr<toy::ModuleAST>
parseInputFile(llvm::StringRef filename) {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> fileOrErr =
      llvm::MemoryBuffer::getFileOrSTDIN(filename);
  if (std::error_code ec = fileOrErr.getError()) {
    llvm::errs() << "Could not open input file: " << ec.message() << "\n";
    return nullptr;
  }
  auto buffer = fileOrErr.get()->getBuffer();
  LexerBuffer lexer(buffer.begin(), buffer.end(), std::string(filename));
  Parser parser(lexer);
  return parser.parseModule();
}

static int dumpMLIR() {
  mlir::MLIRContext context;
  // Load our Dialect in this MLIR Context.
  context.getOrLoadDialect<mlir::toy::ToyDialect>();

  // Handle '.toy' input to the compiler.
  if (inputType != InputType::MLIR &&
      !llvm::StringRef(inputFilename).ends_with(".mlir")) {
    auto moduleAST = parseInputFile(inputFilename);
    if (!moduleAST)
      return 6;
    mlir::OwningOpRef<mlir::ModuleOp> module = mlirGen(context, *moduleAST);
    if (!module)
      return 1;

    module->dump();
    return 0;
  }

  // Otherwise, the input is '.mlir'.
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> fileOrErr =
      llvm::MemoryBuffer::getFileOrSTDIN(inputFilename);
  if (std::error_code ec = fileOrErr.getError()) {
    llvm::errs() << "Could not open input file: " << ec.message() << "\n";
    return -1;
  }

  // Parse the input mlir.
  llvm::SourceMgr sourceMgr;
  sourceMgr.AddNewSourceBuffer(std::move(*fileOrErr), llvm::SMLoc());
  mlir::OwningOpRef<mlir::ModuleOp> module =
      mlir::parseSourceFile<mlir::ModuleOp>(sourceMgr, &context);
  if (!module) {
    llvm::errs() << "Error can't load file " << inputFilename << "\n";
    return 3;
  }

  module->dump();
  return 0;
}

static int dumpAST() {
  if (inputType == InputType::MLIR) {
    llvm::errs() << "Can't dump a Toy AST when the input is MLIR\n";
    return 5;
  }

  auto moduleAST = parseInputFile(inputFilename);
  if (!moduleAST)
    return 1;

  dump(*moduleAST);
  return 0;
}

int main(int argc, char **argv) {
  // Register any command line options.
  mlir::registerAsmPrinterCLOptions();
  mlir::registerMLIRContextCLOptions();
  llvm::clv2::OptionParser P;
  P.add<&ToyReg>();
  RegisterAllLLVMOptions(P);
  auto OptsCtx = P.parse(argc, argv, "toy compiler\n");
  auto *Opts = OptsCtx->getViewPtr<&ToyReg>();

  inputFilename = std::string(Opts->get<&inputFilenameOpt>());
  inputType = Opts->get<&inputTypeOpt>();
  emitAction = Opts->get<&emitActionOpt>();

  switch (emitAction) {
  case Action::DumpAST:
    return dumpAST();
  case Action::DumpMLIR:
    return dumpMLIR();
  default:
    llvm::errs() << "No action specified (parsing only?), use -emit=<action>\n";
  }

  return 0;
}
