//===- bolt/tools/heatmap/heatmap.cpp - Profile heatmap visualization tool ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
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
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/TargetSelect.h"

using namespace llvm;
using namespace bolt;
using namespace llvm::clv2;

namespace opts {

inline constexpr OptionInfo<std::string> HeatmapInputFile{
    "", "<executable>", Positional{}, Required, cat(HeatmapCat)};
inline constexpr OptionsRegistry<&HeatmapInputFile> HeatmapDriverReg;

} // namespace opts

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

static std::string GetExecutablePath(const char *Argv0) {
  SmallString<256> ExecutablePath(Argv0);
  // Do a PATH lookup if Argv0 isn't a valid path.
  if (!llvm::sys::fs::exists(ExecutablePath))
    if (llvm::ErrorOr<std::string> P =
            llvm::sys::findProgramByName(ExecutablePath))
      ExecutablePath = *P;
  return std::string(ExecutablePath);
}

int main(int argc, char **argv) {
  clv2::OptionParser P;
  P.add<&opts::HeatmapDriverReg>();
  P.add<&clv2::BoltUtilsOptsReg>();
  P.add<&clv2::BoltCoreOptsReg>();
  P.add<&clv2::BoltProfileOptsReg>();
  P.add<&clv2::BoltPassesOptsReg>();
  P.add<&clv2::BoltRewriteOptsReg>();
  P.add<&clv2::BoltRuntimeLibsOptsReg>();
  RegisterCoreLLVMOptions(P);
  P.hideUnrelatedOptions({&clv2::HeatmapCat, &clv2::BoltOutputCat});
  auto OptsCtx = P.parse(
      argc, argv,
      " BOLT Code Heatmap tool\n\n"
      "  Produces code heatmaps using sampled profile\n\n"

      "  Inputs:\n"
      "  - Binary (supports BOLT-optimized binaries),\n"
      "  - Sampled profile collected from the binary:\n"
      "    - perf data or pre-aggregated profile data (instrumentation profile "
      "not supported)\n"
      "    - perf data can have basic (IP) or branch-stack (brstack) "
      "samples\n\n"

      "  Outputs:\n"
      "  - Heatmaps: colored ASCII (requires a color-capable terminal or a"
      " conversion tool like `aha`)\n"
      "    Multiple heatmaps are produced by default with different "
      "granularities (set by `block-size` option)\n"
      "  - Section hotness: per-section samples% and utilization%\n"
      "  - Cumulative distribution: working set size corresponding to a "
      "given percentile of samples\n");
  auto *Opts = OptsCtx->getViewPtr<&opts::HeatmapDriverReg>();
  std::string InputFilename = Opts->get<&opts::HeatmapInputFile>();

  auto *UtilOpts = bolt_utils_opts::getBoltUtilsOpts(*OptsCtx);
  const auto &PerfData = UtilOpts->get<&clv2::BOLT_PerfData>();
  if (PerfData.empty()) {
    errs() << ToolName << ": expected -perfdata=<filename> option.\n";
    exit(1);
  }

  if (auto *V = OptsCtx->getViewPtr<&clv2::BoltUtilsOptsReg>()) {
    V->get<&clv2::BOLT_HeatmapMode>() = opts::HM_Exclusive;
    V->get<&clv2::BOLT_AggregateOnly>() = true;
  }
  if (!sys::fs::exists(InputFilename))
    report_error(InputFilename, errc::no_such_file_or_directory);

  // Output to stdout by default
  if (auto *V = OptsCtx->getViewPtr<&clv2::BoltUtilsOptsReg>()) {
    if (V->get<&clv2::BOLT_OutputFilename>().empty())
      V->get<&clv2::BOLT_OutputFilename>() = "-";
    V->get<&clv2::BOLT_HeatmapOutput>() = V->get<&clv2::BOLT_OutputFilename>();
  }

  // Initialize targets and assembly printers/parsers.
#define BOLT_TARGET(target)                                                    \
  LLVMInitialize##target##TargetInfo();                                        \
  LLVMInitialize##target##TargetMC();                                          \
  LLVMInitialize##target##AsmParser();                                         \
  LLVMInitialize##target##Disassembler();                                      \
  LLVMInitialize##target##Target();                                            \
  LLVMInitialize##target##AsmPrinter();

#include "bolt/Core/TargetConfig.def"

  ToolName = argv[0];
  std::string ToolPath = GetExecutablePath(argv[0]);
  Expected<OwningBinary<Binary>> BinaryOrErr = createBinary(InputFilename);
  if (Error E = BinaryOrErr.takeError())
    report_error(InputFilename, std::move(E));
  Binary &Binary = *BinaryOrErr.get().getBinary();

  if (auto *e = dyn_cast<ELFObjectFileBase>(&Binary)) {
    auto RIOrErr = RewriteInstance::create(
        e, argc, argv, ToolPath, llvm::outs(), llvm::errs(), OptsCtx.get());
    if (Error E = RIOrErr.takeError())
      report_error("RewriteInstance", std::move(E));

    RewriteInstance &RI = *RIOrErr.get();
    for (StringRef Filename : PerfData)
      if (Error E = RI.setProfile(Filename))
        report_error(Filename, std::move(E));

    if (Error E = RI.run())
      report_error(InputFilename, std::move(E));
  } else {
    report_error(InputFilename, object_error::invalid_file_type);
  }

  return EXIT_SUCCESS;
}
