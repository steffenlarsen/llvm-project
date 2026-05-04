//===-- llvm-modextract.cpp - LLVM module extractor utility ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program is for testing features that rely on multi-module bitcode files.
// It takes a multi-module bitcode file, extracts one of the modules and writes
// it to the output file.
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"

using namespace llvm;
using namespace llvm::clv2;

static constexpr OptionCategory ModextractCategory{"Modextract Options"};

static constexpr OptionInfo<bool> BinaryExtract{
    "b", "Whether to perform binary extraction", cat(ModextractCategory)};

static constexpr OptionInfo<std::string> OutputFilename{
    "o", "Output filename", value_desc("filename"), Required,
    cat(ModextractCategory)};

static constexpr OptionInfo<std::string> InputFilename{
    "input", "<input bitcode>", Positional{}, Init{"-"},
    cat(ModextractCategory)};

static constexpr OptionInfo<unsigned> ModuleIndex{
    "n", "Index of module to extract", value_desc("index"), Required,
    cat(ModextractCategory)};

static constexpr OptionsRegistry<&BinaryExtract, &OutputFilename,
                                 &InputFilename, &ModuleIndex>
    ModextractToolReg;

int main(int argc, char **argv) {
  clv2::OptionParser P;
  P.add<&ModextractToolReg>();
  RegisterAllLLVMOptions(P);
  P.hideUnrelatedOptions({&ModextractCategory, &getColorCategory()});
  auto OptsCtx = P.parse(argc, argv, "Module extractor");
  auto *Opts = OptsCtx->getViewPtr<&ModextractToolReg>();

  ExitOnError ExitOnErr("llvm-modextract: error: ");

  std::unique_ptr<MemoryBuffer> MB = ExitOnErr(errorOrToExpected(
      MemoryBuffer::getFileOrSTDIN(Opts->get<&InputFilename>())));
  std::vector<BitcodeModule> Ms = ExitOnErr(getBitcodeModuleList(*MB));

  LLVMContext Context(*OptsCtx);
  if (Opts->get<&ModuleIndex>() >= Ms.size()) {
    errs() << "llvm-modextract: error: module index out of range; bitcode file "
              "contains "
           << Ms.size() << " module(s)\n";
    return 1;
  }

  std::error_code EC;
  std::unique_ptr<ToolOutputFile> Out(
      new ToolOutputFile(Opts->get<&OutputFilename>(), EC, sys::fs::OF_None));
  ExitOnErr(errorCodeToError(EC));

  if (Opts->get<&BinaryExtract>()) {
    SmallVector<char, 0> Result;
    BitcodeWriter Writer(Result);
    Result.append(Ms[Opts->get<&ModuleIndex>()].getBuffer().begin(),
                  Ms[Opts->get<&ModuleIndex>()].getBuffer().end());
    Writer.copyStrtab(Ms[Opts->get<&ModuleIndex>()].getStrtab());
    Out->os() << Result;
    Out->keep();
    return 0;
  }

  std::unique_ptr<Module> M =
      ExitOnErr(Ms[Opts->get<&ModuleIndex>()].parseModule(Context));
  WriteBitcodeToFile(*M, Out->os());

  Out->keep();
  return 0;
}
