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
#include "toy/Lexer.h"
#include "toy/Parser.h"

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <string>
#include <system_error>

using namespace toy;
using namespace llvm;

namespace {
enum Action { None, DumpAST };
} // namespace

static constexpr clv2::OptionInfo<std::string> inputFilenameOpt{
    "", "<input toy file>", clv2::Positional{}, clv2::Init{"-"}};

static constexpr clv2::EnumVal<Action> emitActionVals[] = {
    {"ast", DumpAST, "output the AST dump"},
};
static constexpr auto emitActionOpt = clv2::makeEnumOption<Action>(
    "emit", "Select the kind of output desired", emitActionVals);

static constexpr clv2::OptionsRegistry<&inputFilenameOpt, &emitActionOpt>
    ToyReg;

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

int main(int argc, char **argv) {
  llvm::clv2::OptionParser P;
  P.add<&ToyReg>();
  RegisterAllLLVMOptions(P);
  auto OptsCtx = P.parse(argc, argv, "toy compiler\n");
  auto *Opts = OptsCtx->getViewPtr<&ToyReg>();

  auto inputFilename = std::string(Opts->get<&inputFilenameOpt>());
  Action emitAction = Opts->get<&emitActionOpt>();

  auto moduleAST = parseInputFile(inputFilename);
  if (!moduleAST)
    return 1;

  switch (emitAction) {
  case Action::DumpAST:
    dump(*moduleAST);
    return 0;
  default:
    llvm::errs() << "No action specified (parsing only?), use -emit=<action>\n";
  }

  return 0;
}
