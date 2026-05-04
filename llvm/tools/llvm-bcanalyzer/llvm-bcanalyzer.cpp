//===-- llvm-bcanalyzer.cpp - Bitcode Analyzer --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tool may be invoked in the following manner:
//  llvm-bcanalyzer [options]      - Read LLVM bitcode from stdin
//  llvm-bcanalyzer [options] x.bc - Read LLVM bitcode from the x.bc file
//
//  Options:
//      --help            - Output information about command line switches
//      --dump            - Dump low-level bitcode structure in readable format
//      --dump-blockinfo  - Dump the BLOCKINFO_BLOCK, when used with --dump
//
// This tool provides analytical information about a bitcode file. It is
// intended as an aid to developers of bitcode reading and writing software. It
// produces on std::out a summary of the bitcode file that shows various
// statistics about the contents of the file. By default this information is
// detailed and contains information about individual bitcode blocks and the
// functions in the module.
// The tool is also able to print a bitcode file in a straight forward text
// format that shows the containment and relationships of the information in
// the bitcode file (-dump option).
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/BitcodeAnalyzer.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <optional>
using namespace llvm;
using namespace llvm::clv2;

static constexpr OptionCategory BCAnalyzerCategory{"BC Analyzer Options"};

static constexpr OptionInfo<std::string> InputFilename{
    "input", "<input bitcode>", Positional{}, Init{"-"},
    cat(BCAnalyzerCategory)};

static constexpr OptionInfo<bool> Dump{"dump", "Dump low level bitcode trace",
                                       cat(BCAnalyzerCategory)};

static constexpr OptionInfo<bool> DumpBlockinfo{
    "dump-blockinfo", "Include BLOCKINFO details in low level dump",
    cat(BCAnalyzerCategory)};

static constexpr OptionInfo<bool> NoHistogram{"disable-histogram",
                                              "Do not print per-code histogram",
                                              cat(BCAnalyzerCategory)};

static constexpr OptionInfo<bool> NonSymbolic{
    "non-symbolic",
    "Emit numeric info in dump even if symbolic info is available",
    cat(BCAnalyzerCategory)};

static constexpr OptionInfo<std::string> BlockInfoFilename{
    "block-info", "Use the BLOCK_INFO from the given file",
    cat(BCAnalyzerCategory)};

static constexpr OptionInfo<bool> ShowBinaryBlobs{
    "show-binary-blobs", "Print binary blobs using hex escapes",
    cat(BCAnalyzerCategory)};

static constexpr OptionInfo<std::string> CheckHash{
    "check-hash", "Check module hash using the argument as a string table",
    cat(BCAnalyzerCategory)};

static constexpr OptionsRegistry<&InputFilename, &Dump, &DumpBlockinfo,
                                 &NoHistogram, &NonSymbolic, &BlockInfoFilename,
                                 &ShowBinaryBlobs, &CheckHash>
    BCAToolReg;

static Error reportError(StringRef Message) {
  return createStringError(std::errc::illegal_byte_sequence, Message.data());
}

static Expected<std::unique_ptr<MemoryBuffer>> openBitcodeFile(StringRef Path) {
  Expected<std::unique_ptr<MemoryBuffer>> MemBufOrErr =
      errorOrToExpected(MemoryBuffer::getFileOrSTDIN(Path));
  if (Error E = MemBufOrErr.takeError())
    return std::move(E);

  std::unique_ptr<MemoryBuffer> MemBuf = std::move(*MemBufOrErr);

  if (MemBuf->getBufferSize() & 3)
    return reportError(
        "Bitcode stream should be a multiple of 4 bytes in length");
  return std::move(MemBuf);
}

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  clv2::OptionParser P;
  P.add<&BCAToolReg>();
  RegisterAllLLVMOptions(P);
  P.hideUnrelatedOptions({&BCAnalyzerCategory, &getColorCategory()});
  auto OptsCtx = P.parse(argc, argv, "llvm-bcanalyzer file analyzer\n");
  auto *Opts = OptsCtx->getViewPtr<&BCAToolReg>();
  ExitOnError ExitOnErr("llvm-bcanalyzer: ");

  std::unique_ptr<MemoryBuffer> MB =
      ExitOnErr(openBitcodeFile(Opts->get<&InputFilename>()));
  std::unique_ptr<MemoryBuffer> BlockInfoMB = nullptr;
  if (!Opts->get<&BlockInfoFilename>().empty())
    BlockInfoMB = ExitOnErr(openBitcodeFile(Opts->get<&BlockInfoFilename>()));

  BitcodeAnalyzer BA(MB->getBuffer(),
                     BlockInfoMB
                         ? std::optional<StringRef>(BlockInfoMB->getBuffer())
                         : std::nullopt);

  BCDumpOptions O(outs());
  O.Histogram = !Opts->get<&NoHistogram>();
  O.Symbolic = !Opts->get<&NonSymbolic>();
  O.ShowBinaryBlobs = Opts->get<&ShowBinaryBlobs>();
  O.DumpBlockinfo = Opts->get<&DumpBlockinfo>();

  const auto &Hash = Opts->get<&CheckHash>();
  ExitOnErr(
      BA.analyze(Opts->get<&Dump>() ? std::optional<BCDumpOptions>(O)
                                    : std::optional<BCDumpOptions>(),
                 Hash.empty() ? std::nullopt : std::optional<StringRef>(Hash)));

  if (Opts->get<&Dump>())
    outs() << "\n\n";

  BA.printStats(O, StringRef(Opts->get<&InputFilename>()));
  return 0;
}
