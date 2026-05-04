//===-- llvm-diff.cpp - Module comparator command-line driver ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the command-line driver for the difference engine.
//
//===----------------------------------------------------------------------===//

#include "lib/DiffLog.h"
#include "lib/DifferenceEngine.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace llvm;
using namespace llvm::clv2;

/// Reads a module from a file.  On error, messages are written to stderr
/// and null is returned.
static std::unique_ptr<Module> readModule(LLVMContext &Context,
                                          StringRef Name) {
  SMDiagnostic Diag;
  std::unique_ptr<Module> M = parseIRFile(Name, Diag, Context);
  if (!M)
    Diag.print("llvm-diff", errs());
  return M;
}

static void diffGlobal(DifferenceEngine &Engine, Module &L, Module &R,
                       StringRef Name) {
  Name.consume_front("@");

  Function *LFn = L.getFunction(Name);
  Function *RFn = R.getFunction(Name);
  if (LFn && RFn)
    Engine.diff(LFn, RFn);
  else if (!LFn && !RFn)
    errs() << "No function named @" << Name << " in either module\n";
  else if (!LFn)
    errs() << "No function named @" << Name << " in left module\n";
  else
    errs() << "No function named @" << Name << " in right module\n";
}

static constexpr OptionCategory DiffCategory{"Diff Options"};

static constexpr OptionInfo<std::string> LeftFilename{
    "left", "<first file>", Positional{}, Required, cat(DiffCategory)};
static constexpr OptionInfo<std::string> RightFilename{
    "right", "<second file>", Positional{}, Required, cat(DiffCategory)};
static constexpr ListOptionInfo<std::string> GlobalsToCompare{
    "globals", "<globals to compare>", Positional{}, ZeroOrMore,
    cat(DiffCategory)};

static constexpr OptionsRegistry<&LeftFilename, &RightFilename,
                                 &GlobalsToCompare>
    DiffToolReg;
int main(int argc, char **argv) {
  clv2::OptionParser P;
  P.add<&DiffToolReg>();
  RegisterAllLLVMOptions(P);
  P.hideUnrelatedOptions({&DiffCategory, &getColorCategory()});
  auto OptsCtx = P.parse(argc, argv);
  auto *Opts = OptsCtx->getViewPtr<&DiffToolReg>();

  LLVMContext Context(*OptsCtx);

  std::unique_ptr<Module> LModule =
      readModule(Context, Opts->get<&LeftFilename>());
  std::unique_ptr<Module> RModule =
      readModule(Context, Opts->get<&RightFilename>());
  if (!LModule || !RModule)
    return 1;

  DiffConsumer Consumer;
  DifferenceEngine Engine(Consumer);

  const auto &Globals = Opts->get<&GlobalsToCompare>();
  if (!Globals.empty()) {
    for (unsigned I = 0, E = Globals.size(); I != E; ++I)
      diffGlobal(Engine, *LModule, *RModule, Globals[I]);
  } else {
    Engine.diff(LModule.get(), RModule.get());
  }

  return Consumer.hadDifferences();
}
