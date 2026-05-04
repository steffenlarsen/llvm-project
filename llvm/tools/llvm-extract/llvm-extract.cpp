//===- llvm-extract.cpp - LLVM function extraction utility ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This utility changes the input module to only contain a single function,
// which is primarily used for debugging transformations.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRPrinter/IRPrintingPasses.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/Regex.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/IPO/BlockExtractor.h"
#include "llvm/Transforms/IPO/ExtractGV.h"
#include "llvm/Transforms/IPO/GlobalDCE.h"
#include "llvm/Transforms/IPO/StripDeadPrototypes.h"
#include "llvm/Transforms/IPO/StripSymbols.h"
#include "llvm/Transforms/Utils/CodeExtractor.h"
#include <memory>
#include <utility>

using namespace llvm;
using namespace llvm::clv2;

static constexpr OptionCategory ExtractCat{"llvm-extract Options"};

static constexpr OptionInfo<std::string> InputFilename{
    "input", "<input bitcode file>", Positional{}, Init{"-"}};

static constexpr OptionInfo<std::string> OutputFilename{
    "o", "Specify output filename", value_desc("filename"), Init{"-"},
    cat(ExtractCat)};

static constexpr OptionInfo<bool> Force{
    "f", "Enable binary output on terminals", cat(ExtractCat)};

static constexpr OptionInfo<bool> DeleteFn{
    "delete", "Delete specified Globals from Module", cat(ExtractCat)};

static constexpr OptionInfo<bool> KeepConstInit{
    "keep-const-init", "Keep initializers of constants", cat(ExtractCat)};

static constexpr OptionInfo<bool> Recursive{
    "recursive", "Recursively extract all called functions", cat(ExtractCat)};

static constexpr ListOptionInfo<std::string> ExtractFuncs{
    "func", "Specify function to extract", value_desc("function"),
    cat(ExtractCat)};

static constexpr ListOptionInfo<std::string> ExtractRegExpFuncs{
    "rfunc", "Specify function(s) to extract using a regular expression",
    value_desc("rfunction"), cat(ExtractCat)};

static constexpr ListOptionInfo<std::string> ExtractBlocks{
    "bb",
    "Specify <function, basic block1[;basic block2...]> pairs to extract.\n"
    "Each pair will create a function.\n"
    "If multiple basic blocks are specified in one pair,\n"
    "the first block in the sequence should dominate the rest.\n"
    "If an unnamed basic block is to be extracted,\n"
    "'%' should be added before the basic block variable names.\n"
    "eg:\n"
    "  --bb=f:bb1;bb2 will extract one function with both bb1 and bb2;\n"
    "  --bb=f:bb1 --bb=f:bb2 will extract two functions, one with bb1, one "
    "with bb2.\n"
    "  --bb=f:%1 will extract one function with basic block 1;",
    value_desc("function:bb1[;bb2...]"), cat(ExtractCat)};

static constexpr ListOptionInfo<std::string> ExtractAliases{
    "alias", "Specify alias to extract", value_desc("alias"), cat(ExtractCat)};

static constexpr ListOptionInfo<std::string> ExtractRegExpAliases{
    "ralias", "Specify alias(es) to extract using a regular expression",
    value_desc("ralias"), cat(ExtractCat)};

static constexpr ListOptionInfo<std::string> ExtractGlobals{
    "glob", "Specify global to extract", value_desc("global"), cat(ExtractCat)};

static constexpr ListOptionInfo<std::string> ExtractRegExpGlobals{
    "rglob", "Specify global(s) to extract using a regular expression",
    value_desc("rglobal"), cat(ExtractCat)};

static constexpr OptionInfo<bool> OutputAssembly{
    "S", "Write output as LLVM assembly", Hidden, cat(ExtractCat)};

// --aggregate-extracted-args comes from TransformUtilsOptsReg, which
// RegisterAllLLVMOptions() already adds; llvm-extract used to declare a second
// option with the same name and mirror it into a global.

static constexpr OptionsRegistry<
    &InputFilename, &OutputFilename, &Force, &DeleteFn, &KeepConstInit,
    &Recursive, &ExtractFuncs, &ExtractRegExpFuncs, &ExtractBlocks,
    &ExtractAliases, &ExtractRegExpAliases, &ExtractGlobals,
    &ExtractRegExpGlobals, &OutputAssembly>
    ExtractToolReg;

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  clv2::OptionParser P;
  P.add<&ExtractToolReg>();
  RegisterAllLLVMOptions(P);
  P.hideUnrelatedOptions({&ExtractCat});
  // Owned by TransformUtilsOptsReg rather than ExtractCat, but llvm-extract
  // has always listed it, so keep it visible.
  P.showOptions({"aggregate-extracted-args"});
  auto OptsCtx = P.parse(argc, argv, "llvm extractor\n");
  auto *Opts = OptsCtx->getViewPtr<&ExtractToolReg>();

  LLVMContext Context(*OptsCtx);

  SMDiagnostic Err;
  std::unique_ptr<Module> M =
      getLazyIRFileModule(Opts->get<&InputFilename>(), Err, Context);

  if (!M) {
    Err.print(argv[0], errs());
    return 1;
  }

  SetVector<GlobalValue *> GVs;

  for (size_t i = 0, e = Opts->get<&ExtractAliases>().size(); i != e; ++i) {
    GlobalAlias *GA = M->getNamedAlias(Opts->get<&ExtractAliases>()[i]);
    if (!GA) {
      errs() << argv[0] << ": program doesn't contain alias named '"
             << Opts->get<&ExtractAliases>()[i] << "'!\n";
      return 1;
    }
    GVs.insert(GA);
  }

  for (size_t i = 0, e = Opts->get<&ExtractRegExpAliases>().size(); i != e;
       ++i) {
    std::string Error;
    Regex RegEx(Opts->get<&ExtractRegExpAliases>()[i]);
    if (!RegEx.isValid(Error)) {
      errs() << argv[0] << ": '" << Opts->get<&ExtractRegExpAliases>()[i]
             << "' invalid regex: " << Error;
    }
    bool match = false;
    for (Module::alias_iterator GA = M->alias_begin(), E = M->alias_end();
         GA != E; GA++) {
      if (RegEx.match(GA->getName())) {
        GVs.insert(&*GA);
        match = true;
      }
    }
    if (!match) {
      errs() << argv[0] << ": program doesn't contain global named '"
             << Opts->get<&ExtractRegExpAliases>()[i] << "'!\n";
      return 1;
    }
  }

  for (size_t i = 0, e = Opts->get<&ExtractGlobals>().size(); i != e; ++i) {
    GlobalValue *GV = M->getNamedGlobal(Opts->get<&ExtractGlobals>()[i]);
    if (!GV) {
      errs() << argv[0] << ": program doesn't contain global named '"
             << Opts->get<&ExtractGlobals>()[i] << "'!\n";
      return 1;
    }
    GVs.insert(GV);
  }

  for (size_t i = 0, e = Opts->get<&ExtractRegExpGlobals>().size(); i != e;
       ++i) {
    std::string Error;
    Regex RegEx(Opts->get<&ExtractRegExpGlobals>()[i]);
    if (!RegEx.isValid(Error)) {
      errs() << argv[0] << ": '" << Opts->get<&ExtractRegExpGlobals>()[i]
             << "' invalid regex: " << Error;
    }
    bool match = false;
    for (auto &GV : M->globals()) {
      if (RegEx.match(GV.getName())) {
        GVs.insert(&GV);
        match = true;
      }
    }
    if (!match) {
      errs() << argv[0] << ": program doesn't contain global named '"
             << Opts->get<&ExtractRegExpGlobals>()[i] << "'!\n";
      return 1;
    }
  }

  for (size_t i = 0, e = Opts->get<&ExtractFuncs>().size(); i != e; ++i) {
    GlobalValue *GV = M->getFunction(Opts->get<&ExtractFuncs>()[i]);
    if (!GV) {
      errs() << argv[0] << ": program doesn't contain function named '"
             << Opts->get<&ExtractFuncs>()[i] << "'!\n";
      return 1;
    }
    GVs.insert(GV);
  }

  for (size_t i = 0, e = Opts->get<&ExtractRegExpFuncs>().size(); i != e; ++i) {
    std::string Error;
    StringRef RegExStr = Opts->get<&ExtractRegExpFuncs>()[i];
    Regex RegEx(RegExStr);
    if (!RegEx.isValid(Error)) {
      errs() << argv[0] << ": '" << Opts->get<&ExtractRegExpFuncs>()[i]
             << "' invalid regex: " << Error;
    }
    bool match = false;
    for (Module::iterator F = M->begin(), E = M->end(); F != E; F++) {
      if (RegEx.match(F->getName())) {
        GVs.insert(&*F);
        match = true;
      }
    }
    if (!match) {
      errs() << argv[0] << ": program doesn't contain global named '"
             << Opts->get<&ExtractRegExpFuncs>()[i] << "'!\n";
      return 1;
    }
  }

  SmallVector<std::pair<Function *, SmallVector<StringRef, 16>>, 2> BBMap;
  for (StringRef StrPair : Opts->get<&ExtractBlocks>()) {
    SmallVector<StringRef, 16> BBNames;
    auto BBInfo = StrPair.split(':');
    Function *F = M->getFunction(BBInfo.first);
    if (!F) {
      errs() << argv[0] << ": program doesn't contain a function named '"
             << BBInfo.first << "'!\n";
      return 1;
    }
    GVs.insert(F);
    BBInfo.second.split(BBNames, ';', /*MaxSplit=*/-1, /*KeepEmpty=*/false);
    BBMap.push_back({F, std::move(BBNames)});
  }

  ExitOnError ExitOnErr(std::string(*argv) + ": error reading input: ");

  if (Opts->get<&Recursive>()) {
    std::vector<llvm::Function *> Workqueue;
    for (GlobalValue *GV : GVs) {
      if (auto *F = dyn_cast<Function>(GV)) {
        Workqueue.push_back(F);
      }
    }
    while (!Workqueue.empty()) {
      Function *F = &*Workqueue.back();
      Workqueue.pop_back();
      ExitOnErr(F->materialize());
      for (auto &BB : *F) {
        for (auto &I : BB) {
          CallBase *CB = dyn_cast<CallBase>(&I);
          if (!CB)
            continue;
          Function *CF = CB->getCalledFunction();
          if (!CF)
            continue;
          if (CF->isDeclaration() || !GVs.insert(CF))
            continue;
          Workqueue.push_back(CF);
        }
      }
    }
  }

  auto Materialize = [&](GlobalValue &GV) { ExitOnErr(GV.materialize()); };

  if (!Opts->get<&DeleteFn>()) {
    for (size_t i = 0, e = GVs.size(); i != e; ++i)
      Materialize(*GVs[i]);
  } else {
    SmallPtrSet<GlobalValue *, 8> GVSet(llvm::from_range, GVs);
    for (auto &F : *M) {
      if (!GVSet.count(&F))
        Materialize(F);
    }
  }

  {
    std::vector<GlobalValue *> Gvs(GVs.begin(), GVs.end());
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;

    PassBuilder PB(llvm::clv2::defaultOptionsContext());

    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    ModulePassManager PM;
    PM.addPass(ExtractGVPass(Gvs, Opts->get<&DeleteFn>(),
                             Opts->get<&KeepConstInit>()));
    PM.run(*M, MAM);

    ExitOnErr(M->materializeAll());
  }

  if (!Opts->get<&ExtractBlocks>().empty()) {
    std::vector<std::vector<BasicBlock *>> GroupOfBBs;
    for (auto &P : BBMap) {
      std::vector<BasicBlock *> BBs;
      for (StringRef BBName : P.second) {
        auto Res = llvm::find_if(*P.first, [&](const BasicBlock &BB) {
          return BB.getNameOrAsOperand() == BBName;
        });
        if (Res == P.first->end()) {
          errs() << argv[0] << ": function " << P.first->getName()
                 << " doesn't contain a basic block named '" << BBName
                 << "'!\n";
          return 1;
        }
        BBs.push_back(&*Res);
      }
      GroupOfBBs.push_back(BBs);
    }

    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;

    PassBuilder PB(llvm::clv2::defaultOptionsContext());

    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    ModulePassManager PM;
    PM.addPass(BlockExtractorPass(std::move(GroupOfBBs), true));
    PM.run(*M, MAM);
  }

  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  PassBuilder PB(llvm::clv2::defaultOptionsContext());

  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  ModulePassManager PM;
  if (!Opts->get<&DeleteFn>())
    PM.addPass(GlobalDCEPass());
  PM.addPass(StripDeadDebugInfoPass());
  PM.addPass(StripDeadPrototypesPass());
  PM.addPass(StripDeadCGProfilePass());

  std::error_code EC;
  ToolOutputFile Out(Opts->get<&OutputFilename>(), EC, sys::fs::OF_None);
  if (EC) {
    errs() << EC.message() << '\n';
    return 1;
  }

  if (Opts->get<&OutputAssembly>())
    PM.addPass(
        PrintModulePass(Out.os(), "", /* ShouldPreserveUseListOrder */ false));
  else if (Opts->get<&Force>() || !CheckBitcodeOutputToConsole(Out.os()))
    PM.addPass(
        BitcodeWriterPass(Out.os(), /* ShouldPreserveUseListOrder */ true));

  PM.run(*M, MAM);

  Out.keep();

  return 0;
}
