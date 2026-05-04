//===-- llvm-cfi-verify.cpp - CFI Verification tool for LLVM --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This tool verifies Control Flow Integrity (CFI) instrumentation by static
// binary analysis. See the design document in /docs/CFIVerify.md for more
// information.
//
// This tool is currently incomplete. It currently only does disassembly for
// object files, and searches through the code for indirect control flow
// instructions, printing them once found.
//
//===----------------------------------------------------------------------===//

#include "lib/FileAnalysis.h"
#include "lib/GraphBuilder.h"
#include "llvm/Support/WithColor.h"

#include "llvm/BinaryFormat/ELF.h"
#include "llvm/DebugInfo/Symbolize/SymbolizableModule.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SpecialCaseList.h"
#include "llvm/Support/VirtualFileSystem.h"

#include <cstdlib>

using namespace llvm;
using namespace llvm::clv2;
using namespace llvm::object;
using namespace llvm::cfi_verify;

namespace {
static constexpr OptionCategory CFIVerifyCategory{"CFI Verify Options"};

static constexpr OptionInfo<std::string> InputFilenameOpt{
    "input", "<input file>", Positional{}, Required, cat(CFIVerifyCategory)};
static constexpr OptionInfo<std::string> IgnorelistFilenameOpt{
    "ignorelist", "[ignorelist file]", Positional{}, Init{"-"},
    cat(CFIVerifyCategory)};
static constexpr OptionInfo<bool> PrintGraphsOpt{
    "print-graphs",
    "Print graphs around indirect CF instructions in DOT format.", Init{false},
    cat(CFIVerifyCategory)};
static constexpr OptionInfo<unsigned> PrintBlameContextOpt{
    "blame-context",
    "Print the blame context (if possible) for BAD instructions. This "
    "specifies the number of lines of context to include, where zero "
    "disables this feature.",
    Init{0u}, cat(CFIVerifyCategory)};
static constexpr OptionInfo<unsigned> PrintBlameContextAllOpt{
    "blame-context-all",
    "Prints the blame context (if possible) for ALL instructions. "
    "This specifies the number of lines of context for non-BAD "
    "instructions (see --blame-context). If --blame-context is "
    "unspecified, it prints this number of contextual lines for BAD "
    "instructions as well.",
    Init{0u}, cat(CFIVerifyCategory)};
static constexpr OptionInfo<bool> SummarizeOpt{
    "summarize", "Print the summary only.", Init{false},
    cat(CFIVerifyCategory)};
static constexpr OptionInfo<bool> IgnoreDWARFOpt{
    "ignore-dwarf",
    "Ignore all DWARF data. This relaxes the requirements for all "
    "statically linked libraries to have been compiled with '-g', but "
    "will result in false positives for 'CFI unprotected' instructions.",
    Init{false}, Hidden, cat(CFIVerifyCategory)};
static constexpr OptionInfo<uint64_t> SearchLengthUndefOpt{
    "search-length-undef",
    "Specify the maximum amount of instructions to inspect when searching "
    "for an undefined instruction from a conditional branch.",
    Init{uint64_t(2)}, Hidden, cat(CFIVerifyCategory)};
static constexpr OptionInfo<uint64_t> SearchLengthCBOpt{
    "search-length-cb",
    "Specify the maximum amount of instructions to inspect when searching "
    "for a conditional branch from an indirect control flow.",
    Init{uint64_t(20)}, Hidden, cat(CFIVerifyCategory)};

static constexpr OptionsRegistry<
    &InputFilenameOpt, &IgnorelistFilenameOpt, &PrintGraphsOpt,
    &PrintBlameContextOpt, &PrintBlameContextAllOpt, &SummarizeOpt,
    &IgnoreDWARFOpt, &SearchLengthUndefOpt, &SearchLengthCBOpt>
    CFIVerifyToolReg;
} // namespace

struct CmdArgs {
  std::string InputFilename;
  std::string IgnorelistFilename;
  bool PrintGraphs;
  unsigned BlameContext;
  unsigned BlameContextAll;
  bool Summarize;
  uint64_t SearchLengthUndef;
  uint64_t SearchLengthCB;
};

ExitOnError ExitOnErr;

static void printBlameContext(const DILineInfo &LineInfo, unsigned Context) {
  auto FileOrErr = MemoryBuffer::getFile(LineInfo.FileName);
  if (!FileOrErr) {
    errs() << "Could not open file: " << LineInfo.FileName << "\n";
    return;
  }

  std::unique_ptr<MemoryBuffer> File = std::move(FileOrErr.get());
  SmallVector<StringRef, 100> Lines;
  File->getBuffer().split(Lines, '\n');

  for (unsigned i = std::max<size_t>(1, LineInfo.Line - Context);
       i < std::min<size_t>(Lines.size() + 1, LineInfo.Line + Context + 1);
       ++i) {
    if (i == LineInfo.Line)
      outs() << ">";
    else
      outs() << " ";

    outs() << i << ": " << Lines[i - 1] << "\n";
  }
}

static void printInstructionInformation(const FileAnalysis &Analysis,
                                        const Instr &InstrMeta,
                                        const GraphResult &Graph,
                                        CFIProtectionStatus ProtectionStatus,
                                        bool PrintGraphs) {
  outs() << "Instruction: " << format_hex(InstrMeta.VMAddress, 2) << " ("
         << stringCFIProtectionStatus(ProtectionStatus) << "): ";
  Analysis.printInstruction(InstrMeta, outs());
  outs() << " \n";

  if (PrintGraphs)
    Graph.printToDOT(Analysis, outs());
}

static void printInstructionStatus(unsigned BlameLine, bool CFIProtected,
                                   const DILineInfo &LineInfo,
                                   const CmdArgs &Args) {
  if (BlameLine) {
    outs() << "Ignorelist Match: " << Args.IgnorelistFilename << ":"
           << BlameLine << "\n";
    if (CFIProtected)
      outs() << "====> Unexpected Protected\n";
    else
      outs() << "====> Expected Unprotected\n";

    if (Args.BlameContextAll)
      printBlameContext(LineInfo, Args.BlameContextAll);
  } else {
    if (CFIProtected) {
      outs() << "====> Expected Protected\n";
      if (Args.BlameContextAll)
        printBlameContext(LineInfo, Args.BlameContextAll);
    } else {
      outs() << "====> Unexpected Unprotected (BAD)\n";
      if (Args.BlameContext)
        printBlameContext(LineInfo, Args.BlameContext);
    }
  }
}

static void printIndirectCFInstructions(FileAnalysis &Analysis,
                                        const SpecialCaseList *SpecialCaseList,
                                        const CmdArgs &Args) {
  uint64_t ExpectedProtected = 0;
  uint64_t UnexpectedProtected = 0;
  uint64_t ExpectedUnprotected = 0;
  uint64_t UnexpectedUnprotected = 0;

  std::map<unsigned, uint64_t> BlameCounter;

  for (object::SectionedAddress Address : Analysis.getIndirectInstructions()) {
    const auto &InstrMeta = Analysis.getInstructionOrDie(Address.Address);
    GraphResult Graph = GraphBuilder::buildFlowGraph(
        Analysis, Address, Args.SearchLengthUndef, Args.SearchLengthCB);

    CFIProtectionStatus ProtectionStatus =
        Analysis.validateCFIProtection(Graph);
    bool CFIProtected = (ProtectionStatus == CFIProtectionStatus::PROTECTED);

    if (!Args.Summarize) {
      outs() << "-----------------------------------------------------\n";
      printInstructionInformation(Analysis, InstrMeta, Graph, ProtectionStatus,
                                  Args.PrintGraphs);
    }

    // When IgnoreDWARF is set, FileAnalysis::Create() already skipped the
    // DWARF validation; here we just skip per-instruction symbolization.
    if (Analysis.ignoreDWARF()) {
      if (CFIProtected)
        ExpectedProtected++;
      else
        UnexpectedUnprotected++;
      continue;
    }

    auto InliningInfo = Analysis.symbolizeInlinedCode(Address);
    if (!InliningInfo || InliningInfo->getNumberOfFrames() == 0) {
      errs() << "Failed to symbolise " << format_hex(Address.Address, 2)
             << " with line tables from " << Args.InputFilename << "\n";
      exit(EXIT_FAILURE);
    }

    const auto &LineInfo = InliningInfo->getFrame(0);

    // Print the inlining symbolisation of this instruction.
    if (!Args.Summarize) {
      for (uint32_t i = 0; i < InliningInfo->getNumberOfFrames(); ++i) {
        const auto &Line = InliningInfo->getFrame(i);
        outs() << "  " << format_hex(Address.Address, 2) << " = "
               << Line.FileName << ":" << Line.Line << ":" << Line.Column
               << " (" << Line.FunctionName << ")\n";
      }
    }

    if (!SpecialCaseList) {
      if (CFIProtected) {
        if (Args.BlameContextAll && !Args.Summarize)
          printBlameContext(LineInfo, Args.BlameContextAll);
        ExpectedProtected++;
      } else {
        if (Args.BlameContext && !Args.Summarize)
          printBlameContext(LineInfo, Args.BlameContext);
        UnexpectedUnprotected++;
      }
      continue;
    }

    unsigned BlameLine = 0;
    for (auto &K : {"cfi-icall", "cfi-vcall"}) {
      if (!BlameLine) {
        auto [FileIdx, Line] =
            SpecialCaseList->inSectionBlame(K, "src", LineInfo.FileName);
        BlameLine = Line;
      }
      if (!BlameLine) {
        auto [FileIdx, Line] =
            SpecialCaseList->inSectionBlame(K, "fun", LineInfo.FunctionName);
        BlameLine = Line;
      }
    }

    if (BlameLine) {
      BlameCounter[BlameLine]++;
      if (CFIProtected)
        UnexpectedProtected++;
      else
        ExpectedUnprotected++;
    } else {
      if (CFIProtected)
        ExpectedProtected++;
      else
        UnexpectedUnprotected++;
    }

    if (!Args.Summarize)
      printInstructionStatus(BlameLine, CFIProtected, LineInfo, Args);
  }

  uint64_t IndirectCFInstructions = ExpectedProtected + UnexpectedProtected +
                                    ExpectedUnprotected + UnexpectedUnprotected;

  if (IndirectCFInstructions == 0) {
    outs() << "No indirect CF instructions found.\n";
    return;
  }

  outs() << formatv("\nTotal Indirect CF Instructions: {0}\n"
                    "Expected Protected: {1} ({2:P})\n"
                    "Unexpected Protected: {3} ({4:P})\n"
                    "Expected Unprotected: {5} ({6:P})\n"
                    "Unexpected Unprotected (BAD): {7} ({8:P})\n",
                    IndirectCFInstructions, ExpectedProtected,
                    ((double)ExpectedProtected) / IndirectCFInstructions,
                    UnexpectedProtected,
                    ((double)UnexpectedProtected) / IndirectCFInstructions,
                    ExpectedUnprotected,
                    ((double)ExpectedUnprotected) / IndirectCFInstructions,
                    UnexpectedUnprotected,
                    ((double)UnexpectedUnprotected) / IndirectCFInstructions);

  if (!SpecialCaseList)
    return;

  outs() << "\nIgnorelist Results:\n";
  for (const auto &KV : BlameCounter) {
    outs() << "  " << Args.IgnorelistFilename << ":" << KV.first << " affects "
           << KV.second << " indirect CF instructions.\n";
  }
}

int main(int argc, char **argv) {
  clv2::OptionParser P;
  P.add<&CFIVerifyToolReg>();
  RegisterCoreLLVMOptions(P);
  P.hideUnrelatedOptions({&CFIVerifyCategory, &getColorCategory()});
  auto OptsCtx = P.parse(
      argc, argv,
      "Identifies whether Control Flow Integrity protects all indirect control "
      "flow instructions in the provided object file, DSO or binary.\nNote: "
      "Anything statically linked into the provided file *must* be compiled "
      "with '-g'. This can be relaxed through the '--ignore-dwarf' flag.");
  auto *ParsedOpts = OptsCtx->getViewPtr<&CFIVerifyToolReg>();

  CmdArgs Args;
  Args.InputFilename = ParsedOpts->get<&InputFilenameOpt>();
  Args.IgnorelistFilename = ParsedOpts->get<&IgnorelistFilenameOpt>();
  Args.PrintGraphs = ParsedOpts->get<&PrintGraphsOpt>();
  Args.BlameContext = ParsedOpts->get<&PrintBlameContextOpt>();
  Args.BlameContextAll = ParsedOpts->get<&PrintBlameContextAllOpt>();
  Args.Summarize = ParsedOpts->get<&SummarizeOpt>();
  Args.SearchLengthUndef = ParsedOpts->get<&SearchLengthUndefOpt>();
  Args.SearchLengthCB = ParsedOpts->get<&SearchLengthCBOpt>();
  bool IgnoreDWARF = ParsedOpts->get<&IgnoreDWARFOpt>();

  InitializeAllTargetInfos();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllDisassemblers();

  if (Args.BlameContextAll && !Args.BlameContext)
    Args.BlameContext = Args.BlameContextAll;

  std::unique_ptr<SpecialCaseList> SpecialCaseList;
  if (Args.IgnorelistFilename != "-") {
    std::string Error;
    SpecialCaseList = SpecialCaseList::create({Args.IgnorelistFilename},
                                              *vfs::getRealFileSystem(), Error);
    if (!SpecialCaseList) {
      errs() << "Failed to get ignorelist: " << Error << "\n";
      exit(EXIT_FAILURE);
    }
  }

  FileAnalysis Analysis =
      ExitOnErr(FileAnalysis::Create(Args.InputFilename, IgnoreDWARF));
  printIndirectCFInstructions(Analysis, SpecialCaseList.get(), Args);

  return EXIT_SUCCESS;
}
