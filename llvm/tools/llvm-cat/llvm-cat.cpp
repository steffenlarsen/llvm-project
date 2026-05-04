//===- llvm-cat.cpp - LLVM module concatenation utility -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program is for testing features that rely on multi-module bitcode files.
// It takes a list of input modules and uses them to create a multi-module
// bitcode file.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/SmallVector.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <string>
#include <system_error>
#include <vector>

using namespace llvm;
using namespace llvm::clv2;

static constexpr OptionCategory CatCategory{"llvm-cat Options"};

static constexpr OptionInfo<bool> BinaryCat{
    "b", "Whether to perform binary concatenation", cat(CatCategory)};

static constexpr OptionInfo<std::string> OutputFilename{
    "o", "Output filename", Required, value_desc("filename"), cat(CatCategory)};

static constexpr ListOptionInfo<std::string> InputFilenames{
    "inputs", "<input files>", Positional{}, ZeroOrMore, cat(CatCategory)};

static constexpr OptionsRegistry<&BinaryCat, &OutputFilename, &InputFilenames>
    CatToolReg;
int main(int argc, char **argv) {
  clv2::OptionParser P;
  P.add<&CatToolReg>();
  RegisterAllLLVMOptions(P);
  P.hideUnrelatedOptions({&CatCategory});
  auto OptsCtx = P.parse(argc, argv, "Module concatenation");
  auto *Opts = OptsCtx->getViewPtr<&CatToolReg>();

  ExitOnError ExitOnErr("llvm-cat: ");
  LLVMContext Context(*OptsCtx);

  SmallVector<char, 0> Buffer;
  BitcodeWriter Writer(Buffer);
  if (Opts->get<&BinaryCat>()) {
    for (const auto &InputFilename : Opts->get<&InputFilenames>()) {
      std::unique_ptr<MemoryBuffer> MB = ExitOnErr(
          errorOrToExpected(MemoryBuffer::getFileOrSTDIN(InputFilename)));
      std::vector<BitcodeModule> Mods = ExitOnErr(getBitcodeModuleList(*MB));
      for (auto &BitcodeMod : Mods) {
        llvm::append_range(Buffer, BitcodeMod.getBuffer());
        Writer.copyStrtab(BitcodeMod.getStrtab());
      }
    }
  } else {
    // The string table does not own strings added to it, some of which are
    // owned by the modules; keep them alive until we write the string table.
    std::vector<std::unique_ptr<Module>> OwnedMods;
    for (const auto &InputFilename : Opts->get<&InputFilenames>()) {
      SMDiagnostic Err;
      std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);
      if (!M) {
        Err.print(argv[0], errs());
        return 1;
      }
      Writer.writeModule(*M);
      OwnedMods.push_back(std::move(M));
    }
    Writer.writeStrtab();
  }

  std::error_code EC;
  raw_fd_ostream OS(Opts->get<&OutputFilename>(), EC,
                    sys::fs::OpenFlags::OF_None);
  if (EC) {
    errs() << argv[0] << ": cannot open " << Opts->get<&OutputFilename>()
           << " for writing: " << EC.message();
    return 1;
  }

  OS.write(Buffer.data(), Buffer.size());
  return 0;
}
