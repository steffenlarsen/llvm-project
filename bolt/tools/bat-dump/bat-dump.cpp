//===- bolt/tools/bat-dump/bat-dump.cpp - BAT dumper utility --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "bolt/Profile/BoltAddressTranslation.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/ErrorOr.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/raw_ostream.h"
#include <assert.h>
#include <cstdint>
#include <map>
#include <stdlib.h>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>

using namespace llvm;
using namespace bolt;

namespace opts {

using namespace llvm::clv2;

inline constexpr OptionCategory BatDumpCat{"BAT dump options"};
inline constexpr OptionInfo<std::string> BatInputFile{
    "", "<executable>", Positional{}, Required, cat(BatDumpCat)};
inline constexpr OptionInfo<bool> BatDumpAll{"dump-all", "dump all BAT tables",
                                             Init{false}, cat(BatDumpCat)};
inline constexpr ListOptionInfo<uint64_t> BatTranslate{
    "translate", "translate addresses using BAT", ZeroOrMore,
    value_desc("addr"), cat(BatDumpCat)};

inline constexpr OptionsRegistry<&BatInputFile, &BatDumpAll, &BatTranslate>
    BatDumpReg;

} // namespace opts

namespace {
struct BatDumpOptions {
  std::string InputFilename;
  std::vector<uint64_t> Translate;
  bool DumpAll = false;
};
} // namespace

static StringRef ToolName;

static void report_error(StringRef Message, std::error_code EC) {
  assert(EC);
  errs() << ToolName << ": '" << Message << "': " << EC.message() << ".\n";
  exit(1);
}

static void report_error(StringRef Message, Error E) {
  assert(E);
  errs() << ToolName << ": '" << Message << "': " << toString(std::move(E))
         << ".\n";
  exit(1);
}

void dumpBATFor(llvm::object::ELFObjectFileBase *InputFile,
                const BatDumpOptions &Opts) {
  BoltAddressTranslation BAT;
  if (!BAT.enabledFor(InputFile)) {
    errs() << "error: no BAT table found.\n";
    exit(1);
  }

  // Look for BAT section
  bool Found = false;
  StringRef SectionContents;
  for (const llvm::object::SectionRef &Section : InputFile->sections()) {
    Expected<StringRef> SectionNameOrErr = Section.getName();
    if (Error E = SectionNameOrErr.takeError())
      continue;

    if (SectionNameOrErr.get() != BoltAddressTranslation::SECTION_NAME)
      continue;

    Found = true;
    Expected<StringRef> ContentsOrErr = Section.getContents();
    if (Error E = ContentsOrErr.takeError())
      continue;
    SectionContents = ContentsOrErr.get();
  }

  if (!Found) {
    errs() << "BOLT-ERROR: failed to parse BOLT address translation "
              "table. No BAT section found\n";
    exit(1);
  }

  if (std::error_code EC = BAT.parse(outs(), SectionContents)) {
    errs() << "BOLT-ERROR: failed to parse BOLT address translation "
              "table. Malformed BAT section\n";
    exit(1);
  }

  if (Opts.DumpAll)
    BAT.dump(outs());

  if (!Opts.Translate.empty()) {
    // Build map of <Address, SymbolName> for InputFile
    std::map<uint64_t, StringRef> FunctionsMap;
    for (const llvm::object::ELFSymbolRef &Symbol : InputFile->symbols()) {
      Expected<StringRef> NameOrError = Symbol.getName();
      if (NameOrError.takeError())
        continue;
      if (cantFail(Symbol.getType()) != llvm::object::SymbolRef::ST_Function)
        continue;
      const StringRef Name = *NameOrError;
      const uint64_t Address = cantFail(Symbol.getAddress());
      FunctionsMap[Address] = Name;
    }

    outs() << "Translating addresses according to parsed BAT tables:\n";
    for (uint64_t Address : Opts.Translate) {
      auto FI = FunctionsMap.upper_bound(Address);
      if (FI == FunctionsMap.begin()) {
        outs() << "No function symbol found for 0x" << Twine::utohexstr(Address)
               << "\n";
        continue;
      }
      --FI;
      outs() << "0x" << Twine::utohexstr(Address) << " -> " << FI->second
             << " + 0x"
             << Twine::utohexstr(
                    BAT.translate(FI->first, Address - FI->first, false))
             << "\n";
    }
  }
}

int main(int argc, char **argv) {
  clv2::OptionParser P;
  P.add<&opts::BatDumpReg>();
  RegisterCoreLLVMOptions(P);
  P.hideUnrelatedOptions({&opts::BatDumpCat});
  auto OptsCtx = P.parse(argc, argv, "");
  auto *Opts = OptsCtx->getViewPtr<&opts::BatDumpReg>();
  BatDumpOptions ToolOpts;
  ToolOpts.InputFilename = Opts->get<&opts::BatInputFile>();
  ToolOpts.DumpAll = Opts->get<&opts::BatDumpAll>();
  auto &TranslateAddrs = Opts->get<&opts::BatTranslate>();
  ToolOpts.Translate.assign(TranslateAddrs.begin(), TranslateAddrs.end());

  if (!sys::fs::exists(ToolOpts.InputFilename))
    report_error(ToolOpts.InputFilename, errc::no_such_file_or_directory);

  ToolName = argv[0];
  Expected<llvm::object::OwningBinary<llvm::object::Binary>> BinaryOrErr =
      llvm::object::createBinary(ToolOpts.InputFilename);
  if (Error E = BinaryOrErr.takeError())
    report_error(ToolOpts.InputFilename, std::move(E));
  llvm::object::Binary &Binary = *BinaryOrErr.get().getBinary();

  if (auto *InputFile = dyn_cast<llvm::object::ELFObjectFileBase>(&Binary))
    dumpBATFor(InputFile, ToolOpts);
  else
    report_error(ToolOpts.InputFilename,
                 llvm::object::object_error::invalid_file_type);

  return EXIT_SUCCESS;
}
