//===--- llvm-as.cpp - The low-level LLVM assembler -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This utility may be invoked in the following manner:
//   llvm-as --help         - Output information about command line switches
//   llvm-as [options]      - Read LLVM asm from stdin, write bitcode to stdout
//   llvm-as [options] x.ll - Read LLVM asm from the x.ll file, write bitcode
//                            to the x.bc file.
//
//===----------------------------------------------------------------------===//

#include "llvm/AsmParser/Parser.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/PassTimingInfo.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include <memory>
#include <optional>
using namespace llvm;
using namespace llvm::clv2;

static constexpr OptionCategory AsCat{"llvm-as Options"};

static constexpr OptionInfo<std::string> InputFilename{
    "input", "<input .ll file>", Positional{}, Init{"-"}, cat(AsCat)};

static constexpr OptionInfo<std::string> OutputFilename{
    "o", "Override output filename", value_desc("filename"), cat(AsCat)};

static constexpr OptionInfo<bool> Force{
    "f", "Enable binary output on terminals", cat(AsCat)};

static constexpr OptionInfo<bool> DisableOutput{
    "disable-output", "Disable output", Init{false}, cat(AsCat)};

static constexpr OptionInfo<bool> EmitModuleHash{
    "module-hash", "Emit module hash", Init{false}, cat(AsCat)};

static constexpr OptionInfo<bool> DumpAsm{"d", "Print assembly as parsed",
                                          Hidden, cat(AsCat)};

static constexpr OptionInfo<bool> DisableVerify{
    "disable-verify", "Do not run verifier on input LLVM (dangerous!)", Hidden,
    cat(AsCat)};

static constexpr OptionInfo<std::string> ClDataLayout{
    "data-layout", "data layout string to use", value_desc("layout-string"),
    Init{""}, cat(AsCat)};

static constexpr OptionInfo<unsigned> BitcodeMDIndexThreshold{
    "bitcode-mdindex-threshold",
    "Number of metadatas above which we emit an index to enable lazy-loading",
    Hidden, Init{25u}, cat(AsCat)};

static constexpr OptionInfo<bool> TimePassesOpt{
    "time-passes", "Time each pass, printing elapsed time for each on exit",
    Hidden, cat(AsCat)};

static constexpr OptionInfo<bool> TimePassesPerRunOpt{
    "time-passes-per-run",
    "Time each pass run, printing elapsed time for each run on exit", Hidden,
    cat(AsCat)};

static constexpr OptionInfo<bool> AllowIncompleteIROpt{
    "allow-incomplete-ir",
    "Allow incomplete IR on a best effort basis (references to unknown "
    "metadata will be dropped)",
    Hidden, cat(AsCat)};

// --combined-index-memprof-context comes from BitcodeOptsReg, which
// RegisterAllLLVMOptions() already adds; llvm-as used to declare a second
// option with the same name and mirror it into a global.

static constexpr OptionsRegistry<
    &InputFilename, &OutputFilename, &Force, &DisableOutput, &EmitModuleHash,
    &DumpAsm, &DisableVerify, &ClDataLayout, &BitcodeMDIndexThreshold,
    &TimePassesOpt, &TimePassesPerRunOpt, &AllowIncompleteIROpt>
    AsToolReg;

static void WriteOutputFile(const Module *M, const ModuleSummaryIndex *Index,
                            const std::string &InFile,
                            const std::string &OutFile, bool DoForce,
                            bool DoEmitModuleHash, unsigned DoMDIndexThreshold,
                            const clv2::OptionsContext &OptsCtx) {
  std::string FinalOut = OutFile;
  if (FinalOut.empty()) {
    if (InFile == "-") {
      FinalOut = "-";
    } else {
      StringRef IFN = InFile;
      FinalOut = (IFN.ends_with(".ll") ? IFN.drop_back(3) : IFN).str();
      FinalOut += ".bc";
    }
  }

  std::error_code EC;
  std::unique_ptr<ToolOutputFile> Out(
      new ToolOutputFile(FinalOut, EC, sys::fs::OF_None));
  if (EC) {
    errs() << EC.message() << '\n';
    exit(1);
  }

  if (DoForce || !CheckBitcodeOutputToConsole(Out->os())) {
    const ModuleSummaryIndex *IndexToWrite = nullptr;
    if (Index && (Index->begin() != Index->end() || Index->getFlags()))
      IndexToWrite = Index;
    if (!IndexToWrite || (M && (!M->empty() || !M->global_empty())))
      WriteBitcodeToFile(*M, Out->os(), /* ShouldPreserveUseListOrder */ true,
                         IndexToWrite, DoEmitModuleHash, nullptr,
                         DoMDIndexThreshold);
    else
      writeIndexToFile(*IndexToWrite, Out->os(), OptsCtx,
                       /*ModuleToSummaries=*/nullptr,
                       /*DecSummaries=*/nullptr);
  }

  Out->keep();
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);
  clv2::OptionParser P;
  P.add<&AsToolReg>();
  RegisterAllLLVMOptions(P);
  P.hideUnrelatedOptions({&AsCat});
  // Owned by BitcodeOptsReg rather than AsCat, but llvm-as has always
  // listed it, so keep it visible.
  P.showOptions({"combined-index-memprof-context"});
  auto OptsCtx = P.parse(argc, argv, "llvm .ll -> .bc assembler\n");
  auto *Opts = OptsCtx->getViewPtr<&AsToolReg>();

  if (Opts->get<&TimePassesPerRunOpt>()) {
    llvm::TimePassesIsEnabled = true;
    llvm::TimePassesPerRun = true;
  } else {
    llvm::TimePassesIsEnabled = Opts->get<&TimePassesOpt>();
  }

  llvm::setAllowIncompleteIRParsing(Opts->get<&AllowIncompleteIROpt>());

  LLVMContext Context(*OptsCtx);

  SMDiagnostic Err;
  auto SetDataLayout = [Opts](StringRef,
                              StringRef) -> std::optional<std::string> {
    if (Opts->get<&ClDataLayout>().empty())
      return std::nullopt;
    return Opts->get<&ClDataLayout>();
  };
  ParsedModuleAndIndex ModuleAndIndex;
  if (Opts->get<&DisableVerify>()) {
    ModuleAndIndex = parseAssemblyFileWithIndexNoUpgradeDebugInfo(
        Opts->get<&InputFilename>(), Err, Context, nullptr, SetDataLayout);
  } else {
    ModuleAndIndex = parseAssemblyFileWithIndex(
        Opts->get<&InputFilename>(), Err, Context, nullptr, SetDataLayout);
  }
  std::unique_ptr<Module> M = std::move(ModuleAndIndex.Mod);
  if (!M) {
    Err.print(argv[0], errs());
    return 1;
  }

  std::unique_ptr<ModuleSummaryIndex> Index = std::move(ModuleAndIndex.Index);

  if (!Opts->get<&DisableVerify>()) {
    std::string ErrorStr;
    raw_string_ostream OS(ErrorStr);
    if (verifyModule(*M, &OS)) {
      errs() << argv[0]
             << ": assembly parsed, but does not verify as correct!\n";
      errs() << OS.str();
      return 1;
    }
  }

  if (Opts->get<&DumpAsm>()) {
    errs() << "Here's the assembly:\n" << *M;
    if (Index.get() && Index->begin() != Index->end())
      Index->print(errs());
  }

  if (!Opts->get<&DisableOutput>())
    WriteOutputFile(M.get(), Index.get(), Opts->get<&InputFilename>(),
                    Opts->get<&OutputFilename>(), Opts->get<&Force>(),
                    Opts->get<&EmitModuleHash>(),
                    Opts->get<&BitcodeMDIndexThreshold>(), *OptsCtx);

  return 0;
}
