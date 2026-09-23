//===-- llvm-dis.cpp - The low-level LLVM disassembler --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This utility may be invoked in the following manner:
//  llvm-dis [options]      - Read LLVM bitcode from stdin, write asm to stdout
//  llvm-dis [options] x.bc - Read LLVM bitcode from the x.bc file, write asm
//                            to the x.ll file.
//
//===----------------------------------------------------------------------===//

#include "llvm/Bitcode/BitcodeMemProfOptions.h"
#include "llvm/Bitcode/BitcodeOptions.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/AssemblyAnnotationWriter.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/IR/IROptions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/Type.h"
#include "llvm/Option/LibraryOptions.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/PluginLoaderOptions.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/WithColor.h"
#include <system_error>
using namespace llvm;
using namespace llvm::clv2;

static constexpr OptionCategory DisCategory{"Disassembler Options"};

static constexpr ListOptionInfo<std::string> InputFilenames{
    "inputs", "[input bitcode]...", Positional{}, ZeroOrMore, Cat(DisCategory)};

static constexpr OptionInfo<std::string> OutputFilename{
    "o", "Override output filename", value_desc("filename"), Cat(DisCategory)};

static constexpr OptionInfo<bool> Force{
    "f", "Enable binary output on terminals", Cat(DisCategory)};

static constexpr OptionInfo<bool> DontPrint{
    "disable-output", "Don't output the .ll file", Hidden, Cat(DisCategory)};

static constexpr OptionInfo<bool> SetImporting{
    "set-importing", "Set lazy loading to pretend to import a module", Hidden,
    Cat(DisCategory)};

static constexpr OptionInfo<bool> ShowAnnotations{
    "show-annotations", "Add informational comments to the .ll file",
    Cat(DisCategory)};

static constexpr OptionInfo<bool> MaterializeMetadata{
    "materialize-metadata",
    "Load module without materializing metadata, "
    "then materialize only the metadata",
    Cat(DisCategory)};

static constexpr OptionInfo<bool> PrintThinLTOIndexOnly{
    "print-thinlto-index-only",
    "Only read thinlto index and print the index as LLVM assembly.",
    Init{false}, Hidden, Cat(DisCategory)};

// --preserve-ll-uselistorder comes from llvm::IROptions, which has migrated
// off clv2 onto the new per-library OptTable struct design (see
// llvm/include/llvm/Option/LibraryOptions.h); llvm-dis parses it out of argv
// itself in main() below rather than declaring a local duplicate here.
// AsmWriter's AssemblyWriter constructor already consults
// llvm::IROptions::Current directly (see
// isPreserveAssemblyUseListOrderSpecified / getPreserveAssemblyUseListOrder in
// AsmWriter.cpp) whenever the flag was specified, overriding whatever default
// llvm-dis passes to Module::print().

static constexpr OptionsRegistry<&InputFilenames, &OutputFilename, &Force,
                                 &DontPrint, &SetImporting, &ShowAnnotations,
                                 &MaterializeMetadata, &PrintThinLTOIndexOnly>
    DisToolReg;

static void printDebugLoc(const DebugLoc &DL, formatted_raw_ostream &OS) {
  OS << DL.getLine() << ":" << DL.getCol();
  if (DILocation *IDL = DL.getInlinedAt()) {
    OS << "@";
    printDebugLoc(IDL, OS);
  }
}

namespace {
class CommentWriter : public AssemblyAnnotationWriter {
private:
  bool canSafelyAccessUses(const Value &V) {
    const GlobalValue *GV = dyn_cast<GlobalValue>(&V);
    return !GV || (GV->getParent() && GV->getParent()->isMaterialized());
  }

public:
  void emitFunctionAnnot(const Function *F,
                         formatted_raw_ostream &OS) override {
    if (!canSafelyAccessUses(*F))
      return;

    OS << "; [#uses=" << F->getNumUses() << ']';
    OS << '\n';
  }

  void printInfoComment(const Value &V, formatted_raw_ostream &OS) override {
    if (!canSafelyAccessUses(V))
      return;

    bool Padded = false;
    if (!V.getType()->isVoidTy()) {
      OS.PadToColumn(50);
      Padded = true;
      OS << "; [#uses=" << V.getNumUses() << " type=" << *V.getType() << "]";
    }
    if (const Instruction *I = dyn_cast<Instruction>(&V)) {
      if (const DebugLoc &DL = I->getDebugLoc()) {
        if (!Padded) {
          OS.PadToColumn(50);
          Padded = true;
          OS << ";";
        }
        OS << " [debug line = ";
        printDebugLoc(DL, OS);
        OS << "]";
      }
    }
  }
};

struct LLVMDisDiagnosticHandler : public DiagnosticHandler {
  char *Prefix;
  LLVMDisDiagnosticHandler(char *PrefixPtr) : Prefix(PrefixPtr) {}
  bool handleDiagnostics(const DiagnosticInfo &DI) override {
    raw_ostream &OS = errs();
    OS << Prefix << ": ";
    switch (DI.getSeverity()) {
    case DS_Error:
      WithColor::error(OS);
      break;
    case DS_Warning:
      WithColor::warning(OS);
      break;
    case DS_Remark:
      OS << "remark: ";
      break;
    case DS_Note:
      WithColor::note(OS);
      break;
    }

    DiagnosticPrinterRawOStream DP(OS);
    DI.print(DP);
    OS << '\n';

    if (DI.getSeverity() == DS_Error)
      exit(1);
    return true;
  }
};
} // namespace

static ExitOnError ExitOnErr;

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  ExitOnErr.setBanner(std::string(argv[0]) + ": error: ");

  clv2::OptionParser P;
  P.add<&DisToolReg>();
  RegisterAllLLVMOptions(P);
  P.hideUnrelatedOptions({&DisCategory, &getColorCategory()});
  std::vector<const char *> ArgsAfterPlugins =
      loadPluginsAndStripArgs(argc, argv);

  // llvm::BitcodeOptions, llvm::BitcodeMemProfOptions, and llvm::IROptions
  // have migrated off clv2 onto the new per-library OptTable struct design
  // (see llvm/include/llvm/Option/LibraryOptions.h) and are no longer among
  // the clv2::OptionParser registries configured above. Parse them out of
  // argv first, forwarding whatever none of them recognizes to the legacy
  // clv2 parser unchanged.
  SmallVector<const char *, 32> BitcodeOptsRest;
  {
    std::string BitcodeOptsErrs;
    raw_string_ostream BitcodeOptsErrsOS(BitcodeOptsErrs);
    if (Error Err =
            opt::parseLibraryOptionsChain<BitcodeOptions, BitcodeMemProfOptions,
                                          IROptions>(
                ArrayRef<const char *>(ArgsAfterPlugins).drop_front(),
                BitcodeOptsRest, BitcodeOptsErrsOS)) {
      errs() << "llvm-dis: " << toString(std::move(Err)) << "\n";
      return 1;
    }
    errs() << BitcodeOptsErrs;
  }
  // IROptions has no automatic apply step (unlike BitcodeOptions/
  // BitcodeMemProfOptions, which are read on demand via Ctx.getOptions<T>());
  // it must sync a couple of legacy globals (TimePassesIsEnabled/
  // TimePassesPerRun and the OptBisect singleton) explicitly. See
  // llvm/lib/IR/IROptions.cpp.
  llvm::ir_opts::applyIROptions();
  SmallVector<const char *, 32> ArgvAfterBitcodeOpts;
  ArgvAfterBitcodeOpts.push_back(argv[0]);
  ArgvAfterBitcodeOpts.append(BitcodeOptsRest.begin(), BitcodeOptsRest.end());

  auto OptsCtx =
      P.parse(static_cast<int>(ArgvAfterBitcodeOpts.size()),
              ArgvAfterBitcodeOpts.data(), "llvm .bc -> .ll disassembler\n");
  auto *Opts = OptsCtx->getViewPtr<&DisToolReg>();

  auto Inputs = Opts->get<&InputFilenames>();
  if (Inputs.size() < 1) {
    Inputs.push_back("-");
  } else if (Inputs.size() > 1 && !Opts->get<&OutputFilename>().empty()) {
    errs()
        << "error: output file name cannot be set for multiple input files\n";
    return 1;
  }

  for (const auto &InputFilename : Inputs) {
    LLVMContext Context(*OptsCtx);
    Context.setDiagnosticHandler(
        std::make_unique<LLVMDisDiagnosticHandler>(argv[0]));

    ErrorOr<std::unique_ptr<MemoryBuffer>> BufferOrErr =
        MemoryBuffer::getFileOrSTDIN(InputFilename);
    if (std::error_code EC = BufferOrErr.getError()) {
      WithColor::error() << InputFilename << ": " << EC.message() << '\n';
      return 1;
    }
    std::unique_ptr<MemoryBuffer> MB = std::move(BufferOrErr.get());

    BitcodeFileContents IF = ExitOnErr(llvm::getBitcodeFileContents(*MB));

    const size_t N = IF.Mods.size();

    if (Opts->get<&OutputFilename>() == "-" && N > 1)
      errs() << "only single module bitcode files can be written to stdout\n";

    for (size_t I = 0; I < N; ++I) {
      BitcodeModule BM = IF.Mods[I];

      std::unique_ptr<Module> M;

      if (!Opts->get<&PrintThinLTOIndexOnly>()) {
        M = ExitOnErr(BM.getLazyModule(Context,
                                       Opts->get<&MaterializeMetadata>(),
                                       Opts->get<&SetImporting>()));
        if (Opts->get<&MaterializeMetadata>())
          ExitOnErr(M->materializeMetadata());
        else
          ExitOnErr(M->materializeAll());
      }

      BitcodeLTOInfo LTOInfo = ExitOnErr(BM.getLTOInfo());
      std::unique_ptr<ModuleSummaryIndex> Index;
      if (LTOInfo.HasSummary)
        Index = ExitOnErr(BM.getSummary());

      std::string FinalFilename(Opts->get<&OutputFilename>());

      if (Opts->get<&DontPrint>())
        FinalFilename = "-";

      if (FinalFilename.empty()) {
        if (InputFilename == "-") {
          FinalFilename = "-";
        } else {
          StringRef IFN = InputFilename;
          FinalFilename = (IFN.ends_with(".bc") ? IFN.drop_back(3) : IFN).str();
          if (N > 1)
            FinalFilename += std::string(".") + std::to_string(I);
          FinalFilename += ".ll";
        }
      } else {
        if (N > 1)
          FinalFilename += std::string(".") + std::to_string(I);
      }

      std::error_code EC;
      std::unique_ptr<ToolOutputFile> Out(
          new ToolOutputFile(FinalFilename, EC, sys::fs::OF_TextWithCRLF));
      if (EC) {
        errs() << EC.message() << '\n';
        return 1;
      }

      std::unique_ptr<AssemblyAnnotationWriter> Annotator;
      if (Opts->get<&ShowAnnotations>())
        Annotator.reset(new CommentWriter());

      if (!Opts->get<&DontPrint>()) {
        if (M) {
          M->renumberMetadataForAssembly();
          M->print(Out->os(), Annotator.get());
        }
        if (Index)
          Index->print(Out->os());
      }

      Out->keep();
    }
  }

  return 0;
}
