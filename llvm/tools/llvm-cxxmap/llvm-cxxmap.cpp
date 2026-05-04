//===- llvm-cxxmap.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// llvm-cxxmap computes a correspondence between old symbol names and new
// symbol names based on a symbol equivalence file.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ProfileData/SymbolRemappingReader.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/LineIterator.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using namespace llvm::clv2;

static constexpr OptionCategory CXXMapCategory{"CXX Map Options"};

static constexpr OptionInfo<std::string> OldSymbolFile{
    "old-symbols", "<symbol-file>", Positional{}, Required,
    cat(CXXMapCategory)};
static constexpr OptionInfo<std::string> NewSymbolFile{
    "new-symbols", "<symbol-file>", Positional{}, Required,
    cat(CXXMapCategory)};
static constexpr OptionInfo<std::string> RemappingFile{
    "remapping-file", "Remapping file", Required, cat(CXXMapCategory)};
static constexpr AliasInfo RemappingFileA{"r", "remapping-file"};
static constexpr OptionInfo<std::string> OutputFilename{
    "output", "Output file", value_desc("output"), Init{"-"},
    cat(CXXMapCategory)};
static constexpr AliasInfo OutputFilenameA{"o", "output"};

static constexpr OptionInfo<bool> WarnAmbiguous{
    "Wambiguous", "Warn on equivalent symbols in the output symbol list",
    cat(CXXMapCategory)};
static constexpr OptionInfo<bool> WarnIncomplete{
    "Wincomplete", "Warn on input symbols missing from output symbol list",
    cat(CXXMapCategory)};

static constexpr OptionsRegistry<
    &OldSymbolFile, &NewSymbolFile, &RemappingFile, &RemappingFileA,
    &OutputFilename, &OutputFilenameA, &WarnAmbiguous, &WarnIncomplete>
    CXXMapToolReg;
static void warn(Twine Message, Twine Whence = "",
                 std::string Hint = "") {
  WithColor::warning();
  std::string WhenceStr = Whence.str();
  if (!WhenceStr.empty())
    errs() << WhenceStr << ": ";
  errs() << Message << "\n";
  if (!Hint.empty())
    WithColor::note() << Hint << "\n";
}

static void exitWithError(Twine Message, Twine Whence = "",
                          std::string Hint = "") {
  WithColor::error();
  std::string WhenceStr = Whence.str();
  if (!WhenceStr.empty())
    errs() << WhenceStr << ": ";
  errs() << Message << "\n";
  if (!Hint.empty())
    WithColor::note() << Hint << "\n";
  ::exit(1);
}

static void exitWithError(Error E, StringRef Whence = "") {
  exitWithError(toString(std::move(E)), Whence);
}

static void exitWithErrorCode(std::error_code EC, StringRef Whence = "") {
  exitWithError(EC.message(), Whence);
}

static void remapSymbols(MemoryBuffer &OldSymBuf, MemoryBuffer &NewSymBuf,
                         MemoryBuffer &RemapBuf, raw_ostream &Out,
                         bool DoWarnAmbiguous, bool DoWarnIncomplete) {
  // Load the remapping file and prepare to canonicalize symbols.
  SymbolRemappingReader Reader;
  if (Error E = Reader.read(RemapBuf))
    exitWithError(std::move(E));

  // Canonicalize the new symbols.
  DenseMap<SymbolRemappingReader::Key, StringRef> MappedNames;
  DenseSet<StringRef> UnparseableSymbols;
  for (line_iterator LineIt(NewSymBuf, /*SkipBlanks=*/true, '#');
       !LineIt.is_at_eof(); ++LineIt) {
    StringRef Symbol = *LineIt;

    auto K = Reader.insert(Symbol);
    if (!K) {
      UnparseableSymbols.insert(Symbol);
      continue;
    }

    auto ItAndIsNew = MappedNames.insert({K, Symbol});
    if (DoWarnAmbiguous && !ItAndIsNew.second &&
        ItAndIsNew.first->second != Symbol) {
      warn("symbol " + Symbol + " is equivalent to earlier symbol " +
               ItAndIsNew.first->second,
           NewSymBuf.getBufferIdentifier() + ":" + Twine(LineIt.line_number()),
           "later symbol will not be the target of any remappings");
    }
  }

  // Figure out which new symbol each old symbol is equivalent to.
  for (line_iterator LineIt(OldSymBuf, /*SkipBlanks=*/true, '#');
       !LineIt.is_at_eof(); ++LineIt) {
    StringRef Symbol = *LineIt;

    auto K = Reader.lookup(Symbol);
    StringRef NewSymbol = MappedNames.lookup(K);

    if (NewSymbol.empty()) {
      if (DoWarnIncomplete && !UnparseableSymbols.count(Symbol)) {
        warn("no new symbol matches old symbol " + Symbol,
             OldSymBuf.getBufferIdentifier() + ":" +
                 Twine(LineIt.line_number()));
      }
      continue;
    }

    Out << Symbol << " " << NewSymbol << "\n";
  }
}

int main(int argc, const char *argv[]) {
  InitLLVM X(argc, argv);

  clv2::OptionParser P;
  P.add<&CXXMapToolReg>();
  RegisterAllLLVMOptions(P);
  P.hideUnrelatedOptions({&CXXMapCategory, &getColorCategory()});
  auto OptsCtx = P.parse(argc, argv, "LLVM C++ mangled name remapper\n");
  auto *Opts = OptsCtx->getViewPtr<&CXXMapToolReg>();

  auto OldSymbolBufOrError = MemoryBuffer::getFileOrSTDIN(
      Opts->get<&OldSymbolFile>(), /*IsText=*/true);
  if (!OldSymbolBufOrError)
    exitWithErrorCode(OldSymbolBufOrError.getError(),
                      Opts->get<&OldSymbolFile>());

  auto NewSymbolBufOrError = MemoryBuffer::getFileOrSTDIN(
      Opts->get<&NewSymbolFile>(), /*IsText=*/true);
  if (!NewSymbolBufOrError)
    exitWithErrorCode(NewSymbolBufOrError.getError(),
                      Opts->get<&NewSymbolFile>());

  auto RemappingBufOrError = MemoryBuffer::getFileOrSTDIN(
      Opts->get<&RemappingFile>(), /*IsText=*/true);
  if (!RemappingBufOrError)
    exitWithErrorCode(RemappingBufOrError.getError(),
                      Opts->get<&RemappingFile>());

  std::error_code EC;
  raw_fd_ostream OS(Opts->get<&OutputFilename>(), EC, sys::fs::OF_TextWithCRLF);
  if (EC)
    exitWithErrorCode(EC, Opts->get<&OutputFilename>());

  remapSymbols(*OldSymbolBufOrError.get(), *NewSymbolBufOrError.get(),
               *RemappingBufOrError.get(), OS, Opts->get<&WarnAmbiguous>(),
               Opts->get<&WarnIncomplete>());
}
