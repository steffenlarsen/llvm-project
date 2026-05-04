//===- bolt/tools/driver/llvm-bolt.cpp - Feedback-directed optimizer ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is a binary optimizer that will take 'perf' output and change
// basic block layout for better performance (a.k.a. branch straightening),
// plus some other optimizations that are better performed on a binary.
//
//===----------------------------------------------------------------------===//

#include "bolt/Core/BoltCoreOptionsOptInfos.h"
#include "bolt/Passes/BinaryPasses.h"
#include "bolt/Passes/BoltPassesOptionsOptInfos.h"
#include "bolt/Profile/BoltProfileOptionsOptInfos.h"
#include "bolt/Profile/DataAggregator.h"
#include "bolt/Rewrite/BoltRewriteOptionsOptInfos.h"
#include "bolt/Rewrite/MachORewriteInstance.h"
#include "bolt/Rewrite/RewriteInstance.h"
#include "bolt/RuntimeLibs/BoltRuntimeLibsOptionsOptInfos.h"
#include "bolt/Utils/BoltUtilsOptionsOptInfos.h"
#include "bolt/Utils/CommandLineOpts.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/Binary.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Errc.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"

#define DEBUG_TYPE "bolt"

using namespace llvm;
using namespace object;
using namespace bolt;
using namespace llvm::clv2;

//===----------------------------------------------------------------------===//
// Tool-local option declarations
//===----------------------------------------------------------------------===//

inline constexpr OptionInfo<std::string> BoltInputFile{
    "", "<executable>", Positional{}, Required, cat(BoltCat)};
inline constexpr OptionInfo<std::string> BoltInputFile2{
    "", "<executable>", Positional{}, cat(BoltDiffCat)};
inline constexpr OptionInfo<std::string> BoltData{"data", "<data file>",
                                                  cat(BoltCat)};
inline constexpr AliasInfo BoltDataAlias{"b", "data"};
inline constexpr OptionInfo<std::string> BoltLogFile{
    "log-file", "redirect journaling to a file instead of stdout/stderr",
    Hidden, cat(BoltCat)};
inline constexpr OptionInfo<std::string> BoltData2{"data2", "<data file>",
                                                   cat(BoltCat)};

inline constexpr OptionsRegistry<&BoltInputFile, &BoltInputFile2, &BoltData,
                                 &BoltDataAlias, &BoltLogFile, &BoltData2>
    BoltDriverReg;

namespace opts {

// Globals written from parsed results and read throughout the driver.
static std::string InputFilename;
static std::string InputFilename2;
static std::string InputDataFilename;
static std::string LogFile;
static std::string InputDataFilename2;

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

static void printBoltRevision(llvm::raw_ostream &OS) {
  OS << "BOLT revision " << BoltRevision << "\n";
}

static std::unique_ptr<clv2::OptionsContext> ToolOptsCtx;

/// The tool's parsed options, or the shared empty default.  Only one of the
/// three mode entry points assigns ToolOptsCtx, so the others would leave it
/// null here.
static const clv2::OptionsContext &toolOptsCtx() {
  return ToolOptsCtx ? *ToolOptsCtx : clv2::defaultOptionsContext();
}

void perf2boltMode(int argc, char **argv) {
  static const clv2::OptionCategory *Perf2BoltCats[] = {&clv2::AggregatorCat,
                                                        &clv2::BoltOutputCat};
  clv2::OptionParser P;
  P.add<&BoltDriverReg>();
  P.add<&clv2::BoltUtilsOptsReg>();
  P.add<&clv2::BoltCoreOptsReg>();
  P.add<&clv2::BoltProfileOptsReg>();
  P.add<&clv2::BoltPassesOptsReg>();
  P.add<&clv2::BoltRewriteOptsReg>();
  P.add<&clv2::BoltRuntimeLibsOptsReg>();
  RegisterAllLLVMOptions(P);
  // bolt::registerPrintSortedByOption(P); // Now handled by
  // BOLTPASS_PrintSortedBy OptionInfo
  P.hideUnrelatedOptions(ArrayRef(Perf2BoltCats));
  cl::AddExtraVersionPrinter(printBoltRevision);
  auto OptsCtx =
      P.parse(argc, argv,
              "perf2bolt - BOLT data aggregator\n"
              "\nEXAMPLE: perf2bolt -p=perf.data executable -o data.fdata\n");
  auto *Opts = OptsCtx->getViewPtr<&BoltDriverReg>();
  opts::InputFilename = Opts->get<&BoltInputFile>();
  opts::InputFilename2 = Opts->get<&BoltInputFile2>();
  opts::InputDataFilename = Opts->get<&BoltData>();
  opts::LogFile = Opts->get<&BoltLogFile>();
  opts::InputDataFilename2 = Opts->get<&BoltData2>();
  auto *UtilOpts = bolt_utils_opts::getBoltUtilsOpts(*OptsCtx);
  const auto &PerfData = UtilOpts->get<&clv2::BOLT_PerfData>();
  if (PerfData.empty()) {
    errs() << ToolName << ": expected -perfdata=<filename> option.\n";
    exit(1);
  }
  if (!opts::InputDataFilename.empty()) {
    errs() << ToolName << ": unknown -data option.\n";
    exit(1);
  }
  for (const std::string &PD : PerfData) {
    if (!sys::fs::exists(PD))
      report_error(PD, errc::no_such_file_or_directory);
    if (!DataAggregator::checkPerfDataMagic(PD, *OptsCtx)) {
      errs() << ToolName << ": '" << PD
             << "': expected valid perf.data file.\n";
      exit(1);
    }
  }
  if (UtilOpts->get<&clv2::BOLT_OutputFilename>().empty()) {
    errs() << ToolName << ": expected -o=<output file> option.\n";
    exit(1);
  }
  ToolOptsCtx = std::move(OptsCtx);
  if (auto *V = ToolOptsCtx->getViewPtr<&clv2::BoltUtilsOptsReg>()) {
    V->get<&clv2::BOLT_AggregateOnly>() = true;
    V->get<&clv2::BOLT_ShowDensity>() = true;
  }
}

void boltDiffMode(int argc, char **argv) {
  static const clv2::OptionCategory *BoltDiffCats[] = {&clv2::BoltDiffCat};
  clv2::OptionParser P;
  P.add<&BoltDriverReg>();
  P.add<&clv2::BoltUtilsOptsReg>();
  P.add<&clv2::BoltCoreOptsReg>();
  P.add<&clv2::BoltProfileOptsReg>();
  P.add<&clv2::BoltPassesOptsReg>();
  P.add<&clv2::BoltRewriteOptsReg>();
  P.add<&clv2::BoltRuntimeLibsOptsReg>();
  RegisterAllLLVMOptions(P);
  // bolt::registerPrintSortedByOption(P); // Now handled by
  // BOLTPASS_PrintSortedBy OptionInfo
  P.hideUnrelatedOptions(ArrayRef(BoltDiffCats));
  cl::AddExtraVersionPrinter(printBoltRevision);
  auto OptsCtx = P.parse(
      argc, argv,
      "llvm-boltdiff - BOLT binary diff tool\n"
      "\nEXAMPLE: llvm-boltdiff -data=a.fdata -data2=b.fdata exec1 exec2\n");
  auto *Opts = OptsCtx->getViewPtr<&BoltDriverReg>();
  opts::InputFilename = Opts->get<&BoltInputFile>();
  opts::InputFilename2 = Opts->get<&BoltInputFile2>();
  opts::InputDataFilename = Opts->get<&BoltData>();
  opts::LogFile = Opts->get<&BoltLogFile>();
  opts::InputDataFilename2 = Opts->get<&BoltData2>();
  if (opts::InputDataFilename2.empty()) {
    errs() << ToolName << ": expected -data2=<filename> option.\n";
    exit(1);
  }
  if (opts::InputDataFilename.empty()) {
    errs() << ToolName << ": expected -data=<filename> option.\n";
    exit(1);
  }
  if (opts::InputFilename2.empty()) {
    errs() << ToolName << ": expected second binary name.\n";
    exit(1);
  }
  if (opts::InputFilename.empty()) {
    errs() << ToolName << ": expected binary.\n";
    exit(1);
  }
  ToolOptsCtx = std::move(OptsCtx);
  if (auto *V = ToolOptsCtx->getViewPtr<&clv2::BoltUtilsOptsReg>())
    V->get<&clv2::BOLT_DiffOnly>() = true;
}

void boltMode(int argc, char **argv) {
  static const clv2::OptionCategory *BoltCats[] = {
      &clv2::BoltCat, &clv2::BoltOptCat, &clv2::BoltRelocCat,
      &clv2::BoltInstrCat, &clv2::BoltOutputCat};
  clv2::OptionParser P;
  P.add<&BoltDriverReg>();
  P.add<&clv2::BoltUtilsOptsReg>();
  P.add<&clv2::BoltCoreOptsReg>();
  P.add<&clv2::BoltProfileOptsReg>();
  P.add<&clv2::BoltPassesOptsReg>();
  P.add<&clv2::BoltRewriteOptsReg>();
  P.add<&clv2::BoltRuntimeLibsOptsReg>();
  RegisterAllLLVMOptions(P);
  // bolt::registerPrintSortedByOption(P); // Now handled by
  // BOLTPASS_PrintSortedBy OptionInfo
  P.hideUnrelatedOptions(ArrayRef(BoltCats));
  // Register the target printer for --version.
  cl::AddExtraVersionPrinter(printBoltRevision);
  cl::AddExtraVersionPrinter(TargetRegistry::printRegisteredTargetsForVersion);

  auto OptsCtx =
      P.parse(argc, argv, "BOLT - Binary Optimization and Layout Tool\n");
  auto *Opts = OptsCtx->getViewPtr<&BoltDriverReg>();
  opts::InputFilename = Opts->get<&BoltInputFile>();
  opts::InputFilename2 = Opts->get<&BoltInputFile2>();
  opts::InputDataFilename = Opts->get<&BoltData>();
  opts::LogFile = Opts->get<&BoltLogFile>();
  opts::InputDataFilename2 = Opts->get<&BoltData2>();

  auto *BoltUtilOpts = bolt_utils_opts::getBoltUtilsOpts(*OptsCtx);
  if (BoltUtilOpts->get<&clv2::BOLT_OutputFilename>().empty()) {
    errs() << ToolName << ": expected -o=<output file> option.\n";
    exit(1);
  }
  ToolOptsCtx = std::move(OptsCtx);
}

int main(int argc, char **argv) {
  // Print a stack trace if we signal out.
  sys::PrintStackTraceOnErrorSignal(argv[0]);
  PrettyStackTraceProgram X(argc, argv);

  llvm_shutdown_obj Y; // Call llvm_shutdown() on exit.

  std::string ToolPath = llvm::sys::fs::getMainExecutable(argv[0], nullptr);

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

  if (llvm::sys::path::filename(ToolName).starts_with("perf2bolt"))
    perf2boltMode(argc, argv);
  else if (llvm::sys::path::filename(ToolName).starts_with("llvm-boltdiff"))
    boltDiffMode(argc, argv);
  else
    boltMode(argc, argv);

  if (!sys::fs::exists(opts::InputFilename))
    report_error(opts::InputFilename, errc::no_such_file_or_directory);

  // Initialize journaling streams
  raw_ostream *BOLTJournalOut = &outs();
  raw_ostream *BOLTJournalErr = &errs();
  // RAII obj to keep log file open throughout execution
  std::unique_ptr<raw_fd_ostream> LogFileStream;
  if (!opts::LogFile.empty()) {
    std::error_code LogEC;
    LogFileStream = std::make_unique<raw_fd_ostream>(
        opts::LogFile, LogEC, sys::fs::OpenFlags::OF_None);
    if (LogEC) {
      errs() << "BOLT-ERROR: cannot open requested log file for writing: "
             << LogEC.message() << "\n";
      exit(1);
    }
    BOLTJournalOut = LogFileStream.get();
    BOLTJournalErr = LogFileStream.get();
  }

  // Attempt to open the binary.
  auto *MainUtilOpts = bolt_utils_opts::getBoltUtilsOpts(toolOptsCtx());
  bool DiffOnly =
      MainUtilOpts ? MainUtilOpts->get<&clv2::BOLT_DiffOnly>() : false;
  if (!DiffOnly) {
    Expected<OwningBinary<Binary>> BinaryOrErr =
        createBinary(opts::InputFilename);
    if (Error E = BinaryOrErr.takeError())
      report_error(opts::InputFilename, std::move(E));
    Binary &Binary = *BinaryOrErr.get().getBinary();

    if (auto *e = dyn_cast<ELFObjectFileBase>(&Binary)) {
      auto RIOrErr =
          RewriteInstance::create(e, argc, argv, ToolPath, *BOLTJournalOut,
                                  *BOLTJournalErr, ToolOptsCtx.get());
      if (Error E = RIOrErr.takeError())
        report_error(opts::InputFilename, std::move(E));
      RewriteInstance &RI = *RIOrErr.get();

      auto *ProfOpts =
          bolt::bolt_profile_opts::getBoltProfileOpts(toolOptsCtx());
      bool ArmSPE = ProfOpts ? ProfOpts->get<&clv2::BOLTPROF_ArmSPE>() : false;
      auto *UtilOpts = bolt_utils_opts::getBoltUtilsOpts(toolOptsCtx());
      bool AggregateOnly =
          UtilOpts ? UtilOpts->get<&clv2::BOLT_AggregateOnly>() : false;
      if (AggregateOnly && !RI.getBinaryContext().isAArch64() && ArmSPE) {
        errs() << ToolName << ": -spe is available only on AArch64.\n";
        exit(1);
      }

      const auto &PerfData = UtilOpts->get<&clv2::BOLT_PerfData>();
      if (!PerfData.empty()) {
        if (!AggregateOnly) {
          errs() << ToolName
                 << ": WARNING: reading perf data directly is unsupported, "
                    "please use "
                    "-aggregate-only or perf2bolt.\n!!! Proceed on your own "
                    "risk. !!!\n";
        }
        for (StringRef Filename : PerfData)
          if (Error E = RI.setProfile(Filename))
            report_error(Filename, std::move(E));
      }
      if (!opts::InputDataFilename.empty()) {
        if (Error E = RI.setProfile(opts::InputDataFilename))
          report_error(opts::InputDataFilename, std::move(E));
      }
      if (AggregateOnly && PerfData.empty()) {
        errs() << ToolName << ": missing required -perfdata option.\n";
        exit(1);
      }

      if (Error E = RI.run())
        report_error(opts::InputFilename, std::move(E));
    } else if (auto *O = dyn_cast<MachOObjectFile>(&Binary)) {
      auto MachORIOrErr =
          MachORewriteInstance::create(O, ToolPath, ToolOptsCtx.get());
      if (Error E = MachORIOrErr.takeError())
        report_error(opts::InputFilename, std::move(E));
      MachORewriteInstance &MachORI = *MachORIOrErr.get();

      if (!opts::InputDataFilename.empty())
        if (Error E = MachORI.setProfile(opts::InputDataFilename))
          report_error(opts::InputDataFilename, std::move(E));

      MachORI.run();
    } else {
      report_error(opts::InputFilename, object_error::invalid_file_type);
    }

    return EXIT_SUCCESS;
  }

  // Bolt-diff
  Expected<OwningBinary<Binary>> BinaryOrErr1 =
      createBinary(opts::InputFilename);
  Expected<OwningBinary<Binary>> BinaryOrErr2 =
      createBinary(opts::InputFilename2);
  if (Error E = BinaryOrErr1.takeError())
    report_error(opts::InputFilename, std::move(E));
  if (Error E = BinaryOrErr2.takeError())
    report_error(opts::InputFilename2, std::move(E));
  Binary &Binary1 = *BinaryOrErr1.get().getBinary();
  Binary &Binary2 = *BinaryOrErr2.get().getBinary();
  if (auto *ELFObj1 = dyn_cast<ELFObjectFileBase>(&Binary1)) {
    if (auto *ELFObj2 = dyn_cast<ELFObjectFileBase>(&Binary2)) {
      auto RI1OrErr =
          RewriteInstance::create(ELFObj1, argc, argv, ToolPath, llvm::outs(),
                                  llvm::errs(), ToolOptsCtx.get());
      if (Error E = RI1OrErr.takeError())
        report_error(opts::InputFilename, std::move(E));
      RewriteInstance &RI1 = *RI1OrErr.get();
      if (Error E = RI1.setProfile(opts::InputDataFilename))
        report_error(opts::InputDataFilename, std::move(E));
      auto RI2OrErr =
          RewriteInstance::create(ELFObj2, argc, argv, ToolPath, llvm::outs(),
                                  llvm::errs(), ToolOptsCtx.get());
      if (Error E = RI2OrErr.takeError())
        report_error(opts::InputFilename2, std::move(E));
      RewriteInstance &RI2 = *RI2OrErr.get();
      if (Error E = RI2.setProfile(opts::InputDataFilename2))
        report_error(opts::InputDataFilename2, std::move(E));
      outs() << "BOLT-DIFF: *** Analyzing binary 1: " << opts::InputFilename
             << "\n";
      outs() << "BOLT-DIFF: *** Binary 1 fdata:     " << opts::InputDataFilename
             << "\n";
      if (Error E = RI1.run())
        report_error(opts::InputFilename, std::move(E));
      outs() << "BOLT-DIFF: *** Analyzing binary 2: " << opts::InputFilename2
             << "\n";
      outs() << "BOLT-DIFF: *** Binary 2 fdata:     "
             << opts::InputDataFilename2 << "\n";
      if (Error E = RI2.run())
        report_error(opts::InputFilename2, std::move(E));
      RI1.compare(RI2);
    } else {
      report_error(opts::InputFilename2, object_error::invalid_file_type);
    }
  } else {
    report_error(opts::InputFilename, object_error::invalid_file_type);
  }

  return EXIT_SUCCESS;
}
