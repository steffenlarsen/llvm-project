//===- llvm-reduce.cpp - The LLVM Delta Reduction utility -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This program tries to reduce an IR test case for a given interesting-ness
// test. It runs multiple delta debugging passes in order to minimize the input
// file.
//
//===----------------------------------------------------------------------===//

#include "DeltaManager.h"
#include "ReduceConfig.h"
#include "ReducerWorkItem.h"
#include "TestRunner.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/MC/MCTargetOptionsCommandFlags.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SupportOptionsOptInfos.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"

#ifdef _WIN32
#include <windows.h>
#endif

using namespace llvm;
using namespace clv2;

//===----------------------------------------------------------------------===//
// Option definitions
//===----------------------------------------------------------------------===//

inline constexpr OptionCategory LLVMReduceCategory{"llvm-reduce options"};

inline constexpr AliasInfo AliasH{"h", "help"};
inline constexpr AliasInfo AliasV{"v", "version"};

inline constexpr OptionInfo<bool> PreserveDebugEnvironmentOpt{
    "preserve-debug-environment",
    "Don't disable features used for crash "
    "debugging (crash reports, llvm-symbolizer and core dumps)",
    cat(LLVMReduceCategory)};

inline constexpr OptionInfo<bool> PrintDeltaPassesOpt{
    "print-delta-passes",
    "Print list of delta passes, passable to "
    "--delta-passes as a comma separated list",
    cat(LLVMReduceCategory)};

inline constexpr OptionInfo<std::string> InputFilenameOpt{
    "input", "<input llvm ll/bc file>", Positional{}, cat(LLVMReduceCategory)};

inline constexpr OptionInfo<std::string> TestFilenameOpt{
    "test", "Name of the interesting-ness test to be run",
    cat(LLVMReduceCategory)};

inline constexpr ListOptionInfo<std::string> TestArgumentsOpt{
    "test-arg", "Arguments passed onto the interesting-ness test",
    cat(LLVMReduceCategory)};

inline constexpr OptionInfo<std::string> OutputFilenameOpt{
    "output", "Specify the output file. default: reduced.ll|.bc|.mir"};
inline constexpr AliasInfo AliasO{"o", "output"};

inline constexpr OptionInfo<bool> ReplaceInputOpt{
    "in-place",
    "WARNING: This option will replace your input file with the reduced "
    "version!",
    cat(LLVMReduceCategory)};

enum class InputLanguages { None, IR, MIR };

inline constexpr EnumVal<InputLanguages> InputLanguageVals[] = {
    {"ir", InputLanguages::IR, ""},
    {"mir", InputLanguages::MIR, ""},
};
inline constexpr auto InputLanguageOpt = makeEnumOption<InputLanguages>(
    "x", "Input language ('ir' or 'mir')", InputLanguageVals,
    Init{InputLanguages::None}, ValueOptional, cat(LLVMReduceCategory));

inline constexpr OptionInfo<bool> ForceOutputBitcodeOpt{
    "output-bitcode", "Emit final result as bitcode instead of text IR",
    Hidden};

inline constexpr OptionInfo<int> MaxPassIterationsOpt{
    "max-pass-iterations",
    "Maximum number of times to run the full set of delta passes (default=5)",
    Init{5}, cat(LLVMReduceCategory)};

// Delta.cpp options
inline constexpr OptionInfo<bool> AbortOnInvalidReductionOpt{
    "abort-on-invalid-reduction",
    "Abort if any reduction results in invalid IR", cat(LLVMReduceCategory)};

inline constexpr OptionInfo<bool> SkipVerifyAfterCountingChunksOpt{
    "skip-verify-interesting-after-counting-chunks",
    "Do not validate testcase is interesting after counting chunks "
    "(may speed up reduction)",
    cat(LLVMReduceCategory)};

inline constexpr OptionInfo<unsigned> StartingGranularityLevelOpt{
    "starting-granularity-level",
    "Number of times to divide chunks prior to first test",
    cat(LLVMReduceCategory)};

#ifdef LLVM_ENABLE_THREADS
inline constexpr OptionInfo<unsigned> NumJobsOpt{
    "j",
    "Maximum number of threads to use to process chunks. Set to 1 to "
    "disable parallelism.",
    Init{1u}, cat(LLVMReduceCategory)};
#endif

// DeltaManager.cpp options
inline constexpr ListOptionInfo<std::string> DeltaPassesOpt{
    "delta-passes",
    "Delta passes to run, separated by commas. By default, run all delta "
    "passes.",
    CommaSeparated, cat(LLVMReduceCategory)};

inline constexpr ListOptionInfo<std::string> SkipDeltaPassesOpt{
    "skip-delta-passes",
    "Delta passes to not run, separated by commas. By default, run all delta "
    "passes.",
    CommaSeparated, cat(LLVMReduceCategory)};

// ReducerWorkItem.cpp options
inline constexpr OptionInfo<std::string> TargetTripleOpt{
    "mtriple", "Set the target triple", cat(LLVMReduceCategory)};

inline constexpr OptionInfo<bool> PrintInvalidMachineReductionsOpt{
    "print-invalid-reduction-machine-verifier-errors",
    "Print machine verifier errors on invalid reduction attempts triple",
    cat(LLVMReduceCategory)};

inline constexpr OptionInfo<bool> TmpFilesAsBitcodeOpt{
    "write-tmp-files-as-bitcode",
    "Always write temporary files as bitcode instead of textual IR",
    Init{false}, cat(LLVMReduceCategory)};

// deltas/ReduceInlineCallSites.cpp option
inline constexpr OptionInfo<int> CallsiteInlineThresholdOpt{
    "reduce-callsite-inline-threshold",
    "Number of instructions in a function to unconditionally inline "
    "(-1 for inline all)",
    Init{5}, cat(LLVMReduceCategory)};

// deltas/ReduceMetadata.cpp option
inline constexpr OptionInfo<bool> AggressiveMetadataReductionOpt{
    "aggressive-named-md-reduction",
    "Reduce named metadata without taking its type into account",
    cat(LLVMReduceCategory)};

// deltas/RunIRPasses.cpp option
inline constexpr OptionInfo<std::string> IRPassPipelineOpt{
    "ir-passes",
    "A textual description of the pass pipeline, same as "
    "what's passed to `opt -passes`.",
    Init{"function(sroa,instcombine<no-verify-fixpoint>,gvn,"
         "simplifycfg,infer-address-spaces)"},
    cat(LLVMReduceCategory)};

// Verbose flag (used via llvm::Verbose extern in Utils.h)
inline constexpr OptionInfo<bool> VerboseOpt{
    "verbose", "Print extra debugging information", Init{false},
    cat(LLVMReduceCategory)};

//===----------------------------------------------------------------------===//
// CodeGen option infos (mirrors opt's CG_* declarations)
//===----------------------------------------------------------------------===//

// CG_* option descriptors are provided by CommandFlagsOptInfos.h (CGOptsReg).

// Registry
//===----------------------------------------------------------------------===//

inline constexpr OptionsRegistry<
    &AliasH, &AliasV, &PreserveDebugEnvironmentOpt, &PrintDeltaPassesOpt,
    &InputFilenameOpt, &TestFilenameOpt, &TestArgumentsOpt, &OutputFilenameOpt,
    &AliasO, &ReplaceInputOpt, &InputLanguageOpt, &ForceOutputBitcodeOpt,
    &MaxPassIterationsOpt,
    // Delta.cpp options
    &AbortOnInvalidReductionOpt, &SkipVerifyAfterCountingChunksOpt,
    &StartingGranularityLevelOpt,
#ifdef LLVM_ENABLE_THREADS
    &NumJobsOpt,
#endif
    // DeltaManager.cpp options
    &DeltaPassesOpt, &SkipDeltaPassesOpt,
    // ReducerWorkItem.cpp options
    &TargetTripleOpt, &PrintInvalidMachineReductionsOpt, &TmpFilesAsBitcodeOpt,
    // Verbose
    &VerboseOpt,
    // deltas/ options
    &CallsiteInlineThresholdOpt, &AggressiveMetadataReductionOpt,
    &IRPassPipelineOpt>
    ReduceToolReg;

//===----------------------------------------------------------------------===//
// Helpers
//===----------------------------------------------------------------------===//

static void disableEnvironmentDebugFeatures() {
  sys::Process::PreventCoreFiles();

#ifdef _WIN32
  SetEnvironmentVariableA("LLVM_DISABLE_CRASH_REPORT", "1");
  SetEnvironmentVariableA("LLVM_DISABLE_SYMBOLIZATION", "1");
#else
  setenv("LLVM_DISABLE_CRASH_REPORT", "1", /*overwrite=*/1);
  setenv("LLVM_DISABLE_SYMBOLIZATION", "1", /*overwrite=*/1);
#endif
}

static std::pair<StringRef, bool>
determineOutputType(bool IsMIR, bool InputIsBitcode, std::string &OutputFile,
                    StringRef InputFile, bool ReplaceInput,
                    bool ForceOutputBitcode) {
  bool OutputBitcode = ForceOutputBitcode || InputIsBitcode;

  if (ReplaceInput) {
    OutputFile = InputFile.str();
  } else if (OutputFile.empty()) {
    OutputFile =
        IsMIR ? "reduced.mir" : (OutputBitcode ? "reduced.bc" : "reduced.ll");
  }

  return {OutputFile, OutputBitcode};
}

int main(int Argc, const char **Argv) {
  InitLLVM X(Argc, Argv);
  const StringRef ToolName(Argv[0]);

  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();

  // When invoked with no arguments, synthesize --help so the built-in help
  // handler runs (which also correctly resolves aliases before printing).
  SmallVector<const char *, 4> SyntheticArgv;
  if (Argc == 1) {
    SyntheticArgv.assign(Argv, Argv + Argc);
    SyntheticArgv.push_back("--help");
    Argc = static_cast<int>(SyntheticArgv.size());
    Argv = SyntheticArgv.data();
  }

  clv2::OptionParser P;
  P.add<&ReduceToolReg>();
  RegisterAllLLVMOptions(P);
  P.hideUnrelatedOptions({&LLVMReduceCategory, &ColorOptionsCategory});
  auto OptsCtx = P.parse(
      Argc, Argv,
      "LLVM automatic testcase reducer.\n"
      "See https://llvm.org/docs/CommandGuide/llvm-reduce.html for more "
      "information.\n");
  auto *Opts = OptsCtx->getViewPtr<&ReduceToolReg>();

  // Collect parsed option values into the ReduceConfig struct.
  ReduceConfig Config;
  Config.Verbose = Opts->get<&VerboseOpt>();
  Config.AbortOnInvalidReduction = Opts->get<&AbortOnInvalidReductionOpt>();
  Config.SkipVerifyAfterCountingChunks =
      Opts->get<&SkipVerifyAfterCountingChunksOpt>();
  Config.StartingGranularityLevel = Opts->get<&StartingGranularityLevelOpt>();
#ifdef LLVM_ENABLE_THREADS
  Config.NumJobs = Opts->get<&NumJobsOpt>();
#endif
  Config.DeltaPassList = Opts->get<&DeltaPassesOpt>();
  Config.SkipDeltaPassList = Opts->get<&SkipDeltaPassesOpt>();
  Config.ReduceTargetTriple = Opts->get<&TargetTripleOpt>();
  Config.PrintInvalidMachineReductions =
      Opts->get<&PrintInvalidMachineReductionsOpt>();
  Config.TmpFilesAsBitcode = Opts->get<&TmpFilesAsBitcodeOpt>();
  Config.CallsiteInlineThreshold = Opts->get<&CallsiteInlineThresholdOpt>();
  Config.AggressiveMetadataReduction =
      Opts->get<&AggressiveMetadataReductionOpt>();
  Config.IRPassPipeline = Opts->get<&IRPassPipelineOpt>();

  bool PrintDeltaPasses = Opts->get<&PrintDeltaPassesOpt>();
  std::string InputFile = Opts->get<&InputFilenameOpt>();
  std::string TestFile = Opts->get<&TestFilenameOpt>();
  auto TestArgs = Opts->get<&TestArgumentsOpt>();
  std::string OutputFile = Opts->get<&OutputFilenameOpt>();
  bool ReplaceInput = Opts->get<&ReplaceInputOpt>();
  InputLanguages InputLang = Opts->get<&InputLanguageOpt>();
  bool ForceOutputBitcode = Opts->get<&ForceOutputBitcodeOpt>();
  int MaxPassIter = Opts->get<&MaxPassIterationsOpt>();
  bool PreserveDbgEnv = Opts->get<&PreserveDebugEnvironmentOpt>();

  if (PrintDeltaPasses) {
    printDeltaPasses(outs());
    return 0;
  }

  bool ReduceModeMIR = false;
  if (InputLang != InputLanguages::None) {
    if (InputLang == InputLanguages::MIR)
      ReduceModeMIR = true;
  } else if (StringRef(InputFile).ends_with(".mir")) {
    ReduceModeMIR = true;
  }

  if (InputFile.empty()) {
    WithColor::error(errs(), ToolName)
        << "reduction testcase positional argument must be specified\n";
    return 1;
  }

  if (TestFile.empty()) {
    WithColor::error(errs(), ToolName) << "--test option must be specified\n";
    return 1;
  }

  if (!PreserveDbgEnv)
    disableEnvironmentDebugFeatures();

  LLVMContext Context(*OptsCtx);
  std::unique_ptr<TargetMachine> TM;

  auto [OriginalProgram, InputIsBitcode] =
      parseReducerWorkItem(ToolName, InputFile, Context, TM, ReduceModeMIR,
                           Config.ReduceTargetTriple);
  if (!OriginalProgram) {
    return 1;
  }

  StringRef OutputFilenameRef;
  bool OutputBitcode;
  std::tie(OutputFilenameRef, OutputBitcode) =
      determineOutputType(ReduceModeMIR, InputIsBitcode, OutputFile, InputFile,
                          ReplaceInput, ForceOutputBitcode);

  TestRunner Tester(TestFile, TestArgs, std::move(OriginalProgram),
                    std::move(TM), ToolName, OutputFilenameRef, InputIsBitcode,
                    OutputBitcode, std::move(Config));
  Tester.setOptionsContext(*OptsCtx);
  Tester.getProgram().Config = &Tester.getConfig();

  if (!Tester.getProgram().isReduced(Tester)) {
    errs() << "\nInput isn't interesting! Verify interesting-ness test\n";
    return 2;
  }

  runDeltaPasses(Tester, MaxPassIter);

  if (OutputFilenameRef == "-")
    Tester.getProgram().print(outs(), nullptr);
  else
    Tester.writeOutput("Done reducing! Reduced testcase: ");

  return 0;
}
