//===- bolt/tools/binary-analysis/binary-analysis.cpp ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a generic binary analysis tool, where multiple different specific
// binary analyses can be plugged in to. The binary analyses are mostly built
// on top of BOLT components.
//
//===----------------------------------------------------------------------===//

#include "bolt/Core/BoltCoreOptionsOptInfos.h"
#include "bolt/Passes/BoltPassesOptionsOptInfos.h"
#include "bolt/Profile/BoltProfileOptionsOptInfos.h"
#include "bolt/Rewrite/BoltRewriteOptionsOptInfos.h"
#include "bolt/Rewrite/RewriteInstance.h"
#include "bolt/RuntimeLibs/BoltRuntimeLibsOptionsOptInfos.h"
#include "bolt/Utils/BoltUtilsOptionsOptInfos.h"
#include "bolt/Utils/CommandLineOpts.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ELFObjectFile.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/VirtualFileSystem.h"

#define DEBUG_TYPE "bolt"

using namespace llvm;
using namespace object;
using namespace bolt;
using namespace llvm::clv2;

namespace opts {

inline constexpr OptionInfo<std::string> BAInputFile{
    "", "<executable>", Positional{}, Required, cat(BinaryAnalysisCat)};
inline constexpr OptionsRegistry<&BAInputFile> BADriverReg;

} // namespace opts

static StringRef ToolName = "llvm-bolt-binary-analysis";

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

std::unique_ptr<clv2::OptionsContext>
ParseCommandLine(int argc, char **argv, std::string &InputFilename) {
  // Register the target printer for --version.
  cl::AddExtraVersionPrinter(TargetRegistry::printRegisteredTargetsForVersion);

  clv2::OptionParser P;
  P.add<&opts::BADriverReg>();
  P.add<&clv2::BoltUtilsOptsReg>();
  P.add<&clv2::BoltCoreOptsReg>();
  P.add<&clv2::BoltProfileOptsReg>();
  P.add<&clv2::BoltPassesOptsReg>();
  P.add<&clv2::BoltRewriteOptsReg>();
  P.add<&clv2::BoltRuntimeLibsOptsReg>();
  RegisterCoreLLVMOptions(P);
  P.hideUnrelatedOptions({&clv2::BinaryAnalysisCat});
  auto OptsCtx = P.parse(argc, argv, "BinaryAnalysis\n");
  auto *Opts = OptsCtx->getViewPtr<&opts::BADriverReg>();
  InputFilename = Opts->get<&opts::BAInputFile>();
  return OptsCtx;
}

static std::string GetExecutablePath(const char *Argv0) {
  SmallString<256> ExecutablePath(Argv0);
  // Do a PATH lookup if Argv0 isn't a valid path.
  if (!llvm::sys::fs::exists(ExecutablePath))
    if (llvm::ErrorOr<std::string> P =
            llvm::sys::findProgramByName(ExecutablePath))
      ExecutablePath = *P;
  return std::string(ExecutablePath.str());
}

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  PrettyStackTraceProgram X(argc, argv);

  std::string ToolPath = GetExecutablePath(argv[0]);

  llvm_shutdown_obj Y; // Call llvm_shutdown() on exit.

  // Initialize targets and assembly printers/parsers.
#define BOLT_TARGET(target)                                                    \
  LLVMInitialize##target##TargetInfo();                                        \
  LLVMInitialize##target##TargetMC();                                          \
  LLVMInitialize##target##AsmParser();                                         \
  LLVMInitialize##target##Disassembler();                                      \
  LLVMInitialize##target##Target();                                            \
  LLVMInitialize##target##AsmPrinter();

#include "bolt/Core/TargetConfig.def"

  std::string InputFilename;
  std::unique_ptr<clv2::OptionsContext> ToolOptsCtx =
      ParseCommandLine(argc, argv, InputFilename);

  if (auto *V = ToolOptsCtx->getViewPtr<&clv2::BoltUtilsOptsReg>())
    V->get<&clv2::BOLT_BinaryAnalysisMode>() = true;

  if (!sys::fs::exists(InputFilename))
    report_error(InputFilename, errc::no_such_file_or_directory);

  Expected<OwningBinary<Binary>> BinaryOrErr = createBinary(InputFilename);
  if (Error E = BinaryOrErr.takeError())
    report_error(InputFilename, std::move(E));
  Binary &Binary = *BinaryOrErr.get().getBinary();

  if (auto *e = dyn_cast<ELFObjectFileBase>(&Binary)) {
    auto RIOrErr = RewriteInstance::create(
        e, argc, argv, ToolPath, llvm::outs(), llvm::errs(), ToolOptsCtx.get());
    if (Error E = RIOrErr.takeError())
      report_error(InputFilename, std::move(E));
    RewriteInstance &RI = *RIOrErr.get();
    if (Error E = RI.run())
      report_error(InputFilename, std::move(E));
  }

  return EXIT_SUCCESS;
}
