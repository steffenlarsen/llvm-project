//===-- llvm-exegesis.cpp ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Measures execution properties (latencies/uops) of an instruction.
///
//===----------------------------------------------------------------------===//

#include "lib/Analysis.h"
#include "lib/BenchmarkResult.h"
#include "lib/BenchmarkRunner.h"
#include "lib/Clustering.h"
#include "lib/CodeTemplate.h"
#include "lib/Error.h"
#include "lib/LlvmState.h"
#include "lib/PerfHelper.h"
#include "lib/ProgressMeter.h"
#include "lib/ResultAggregator.h"
#include "lib/SnippetFile.h"
#include "lib/SnippetRepetitor.h"
#include "lib/Target.h"
#include "lib/TargetSelect.h"
#include "lib/ValidationEvent.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/MC/MCInstBuilder.h"
#include "llvm/MC/MCObjectFileInfo.h"
#include "llvm/MC/MCParser/MCAsmParser.h"
#include "llvm/MC/MCParser/MCTargetAsmParser.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/RegisterLLVMOptions.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/TargetParser/Host.h"
#include <algorithm>
#include <string>

namespace llvm {
namespace exegesis {

struct ExegesisArgs {
  int OpcodeIndex;
  std::string OpcodeNames;
  std::string SnippetsFile;
  std::string BenchmarkFile;
  Benchmark::ModeE BenchmarkMode;
  Benchmark::ResultAggregationModeE ResultAggMode;
  Benchmark::RepetitionModeE RepetitionMode;
  bool BenchmarkMeasurementsPrintProgress;
  BenchmarkPhaseSelectorE BenchmarkPhaseSelector;
  bool UseDummyPerfCounters;
  unsigned MinInstructions;
  unsigned LoopBodySize;
  unsigned MaxConfigsPerOpcode;
  bool IgnoreInvalidSchedClass;
  BenchmarkFilter AnalysisSnippetFilter;
  BenchmarkClustering::ModeE AnalysisClusteringAlgorithm;
  unsigned AnalysisDbscanNumPoints;
  float AnalysisClusteringEpsilon;
  float AnalysisInconsistencyEpsilon;
  std::string AnalysisClustersOutputFile;
  std::string AnalysisInconsistenciesOutputFile;
  bool AnalysisDisplayUnstableOpcodes;
  bool AnalysisOverrideBenchmarksTripleAndCpu;
  std::string TripleName;
  std::string MCPU;
  std::string DumpObjectToDisk;
  unsigned DumpObjectToDiskOccurrences;
  BenchmarkRunner::ExecutionModeE ExecutionMode;
  unsigned BenchmarkRepeatCount;
  std::vector<ValidationEvent> ValidationCounters;
  int BenchmarkProcessCPU;
  std::string MAttr;
};

// Globals defined in lib files, set from main after parsing.
extern unsigned RandomGeneratorSeed;
extern unsigned RandomGeneratorSeedOccurrences;
extern unsigned LbrSamplingPeriod;
extern bool DisableUpperSSERegisters;
extern bool OnlyUsesVLMAXForVL;
extern bool EnumerateRoundingModes;
extern std::string FilterConfig;

static ExitOnError ExitOnErr("llvm-exegesis error: ");

// Helper function that logs the error(s) and exits.
template <typename... ArgTs> static void ExitWithError(ArgTs &&... Args) {
  ExitOnErr(make_error<Failure>(std::forward<ArgTs>(Args)...));
}

// Check Err. If it's in a failure state log the file error(s) and exit.
static void ExitOnFileError(const Twine &FileName, Error Err) {
  if (Err) {
    ExitOnErr(createFileError(FileName, std::move(Err)));
  }
}

// Check E. If it's in a success state then return the contained value.
// If it's in a failure state log the file error(s) and exit.
template <typename T>
T ExitOnFileError(const Twine &FileName, Expected<T> &&E) {
  ExitOnFileError(FileName, E.takeError());
  return std::move(*E);
}

// Checks that only one of OpcodeNames, OpcodeIndex or SnippetsFile is provided,
// and returns the opcode indices or {} if snippets should be read from
// `SnippetsFile`.
static std::vector<unsigned> getOpcodesOrDie(const LLVMState &State,
                                             const ExegesisArgs &Args) {
  const size_t NumSetFlags = (Args.OpcodeNames.empty() ? 0 : 1) +
                             (Args.OpcodeIndex == 0 ? 0 : 1) +
                             (Args.SnippetsFile.empty() ? 0 : 1);
  const auto &ET = State.getExegesisTarget();
  const auto AvailableFeatures = State.getSubtargetInfo().getFeatureBits();

  if (NumSetFlags != 1) {
    ExitOnErr.setBanner("llvm-exegesis: ");
    ExitWithError("please provide one and only one of 'opcode-index', "
                  "'opcode-name' or 'snippets-file'");
  }
  if (!Args.SnippetsFile.empty())
    return {};
  if (Args.OpcodeIndex > 0)
    return {static_cast<unsigned>(Args.OpcodeIndex)};
  if (Args.OpcodeIndex < 0) {
    std::vector<unsigned> Result;
    unsigned NumOpcodes = State.getInstrInfo().getNumOpcodes();
    Result.reserve(NumOpcodes);
    for (unsigned I = 0, E = NumOpcodes; I < E; ++I) {
      if (!ET.isOpcodeAvailable(I, AvailableFeatures))
        continue;
      Result.push_back(I);
    }
    return Result;
  }
  // Resolve opcode name -> opcode.
  const auto ResolveName = [&State](StringRef OpcodeName) -> unsigned {
    const auto &Map = State.getOpcodeNameToOpcodeIdxMapping();
    auto I = Map.find(OpcodeName);
    if (I != Map.end())
      return I->getSecond();
    return 0u;
  };

  SmallVector<StringRef, 2> Pieces;
  StringRef(Args.OpcodeNames)
      .split(Pieces, ",", /* MaxSplit */ -1, /* KeepEmpty */ false);
  std::vector<unsigned> Result;
  Result.reserve(Pieces.size());
  for (const StringRef &OpcodeName : Pieces) {
    if (unsigned Opcode = ResolveName(OpcodeName))
      Result.push_back(Opcode);
    else
      ExitWithError(Twine("unknown opcode ").concat(OpcodeName));
  }
  return Result;
}

// Generates code snippets for opcode `Opcode`.
static Expected<std::vector<BenchmarkCode>>
generateSnippets(const LLVMState &State, unsigned Opcode,
                 const BitVector &ForbiddenRegs, const ExegesisArgs &Args) {
  // Ignore instructions that we cannot run.
  if (const char *Reason =
          State.getExegesisTarget().getIgnoredOpcodeReasonOrNull(State, Opcode))
    return make_error<Failure>(Reason);

  const Instruction &Instr = State.getIC().getInstr(Opcode);
  const std::vector<InstructionTemplate> InstructionVariants =
      State.getExegesisTarget().generateInstructionVariants(
          Instr, Args.MaxConfigsPerOpcode);

  SnippetGenerator::Options SnippetOptions;
  SnippetOptions.MaxConfigsPerOpcode = Args.MaxConfigsPerOpcode;
  const std::unique_ptr<SnippetGenerator> Generator =
      State.getExegesisTarget().createSnippetGenerator(Args.BenchmarkMode,
                                                       State, SnippetOptions);
  if (!Generator)
    ExitWithError("cannot create snippet generator");

  std::vector<BenchmarkCode> Benchmarks;
  for (const InstructionTemplate &Variant : InstructionVariants) {
    if (Benchmarks.size() >= Args.MaxConfigsPerOpcode)
      break;
    if (auto Err = Generator->generateConfigurations(Variant, Benchmarks,
                                                     ForbiddenRegs))
      return std::move(Err);
  }
  return Benchmarks;
}

static void runBenchmarkConfigurations(
    const LLVMState &State, ArrayRef<BenchmarkCode> Configurations,
    ArrayRef<std::unique_ptr<const SnippetRepetitor>> Repetitors,
    const BenchmarkRunner &Runner, ExegesisArgs &Args) {
  assert(!Configurations.empty() && "Don't have any configurations to run.");
  std::optional<raw_fd_ostream> FileOstr;
  if (Args.BenchmarkFile != "-") {
    int ResultFD = 0;
    ExitOnErr(errorCodeToError(openFileForWrite(Args.BenchmarkFile, ResultFD,
                                                sys::fs::CD_CreateAlways,
                                                sys::fs::OF_TextWithCRLF)));
    FileOstr.emplace(ResultFD, true /*shouldClose*/);
  }
  raw_ostream &Ostr = FileOstr ? *FileOstr : outs();

  std::optional<ProgressMeter<>> Meter;
  if (Args.BenchmarkMeasurementsPrintProgress)
    Meter.emplace(Configurations.size());

  SmallVector<unsigned, 2> MinInstructionCounts = {Args.MinInstructions};
  if (Args.RepetitionMode == Benchmark::MiddleHalfDuplicate ||
      Args.RepetitionMode == Benchmark::MiddleHalfLoop)
    MinInstructionCounts.push_back(Args.MinInstructions * 2);

  for (const BenchmarkCode &Conf : Configurations) {
    ProgressMeter<>::ProgressMeterStep MeterStep(Meter ? &*Meter : nullptr);
    SmallVector<Benchmark, 2> AllResults;

    for (const std::unique_ptr<const SnippetRepetitor> &Repetitor :
         Repetitors) {
      for (unsigned IterationRepetitions : MinInstructionCounts) {
        auto RC = ExitOnErr(Runner.getRunnableConfiguration(
            Conf, IterationRepetitions, Args.LoopBodySize, *Repetitor));
        std::optional<StringRef> DumpFile;
        if (Args.DumpObjectToDiskOccurrences)
          DumpFile = Args.DumpObjectToDisk;
        const std::optional<int> BenchmarkCPU =
            Args.BenchmarkProcessCPU == -1
                ? std::nullopt
                : std::optional(Args.BenchmarkProcessCPU);
        auto [Err, BenchmarkResult] =
            Runner.runConfiguration(std::move(RC), DumpFile, BenchmarkCPU);
        if (Err) {
          // Errors from executing the snippets are fine.
          // All other errors are a framework issue and should fail.
          if (!Err.isA<SnippetExecutionFailure>())
            ExitOnErr(std::move(Err));

          BenchmarkResult.Error = toString(std::move(Err));
        }
        AllResults.push_back(std::move(BenchmarkResult));
      }
    }

    Benchmark &Result = AllResults.front();

    // If any of our measurements failed, pretend they all have failed.
    if (AllResults.size() > 1 &&
        any_of(AllResults, [](const Benchmark &R) {
          return R.Measurements.empty();
        }))
      Result.Measurements.clear();

    std::unique_ptr<ResultAggregator> ResultAgg =
        ResultAggregator::CreateAggregator(Args.RepetitionMode);
    ResultAgg->AggregateResults(Result,
                                ArrayRef<Benchmark>(AllResults).drop_front());

    // With dummy counters, measurements are rather meaningless,
    // so drop them altogether.
    if (Args.UseDummyPerfCounters)
      Result.Measurements.clear();

    ExitOnFileError(Args.BenchmarkFile, Result.writeYamlTo(State, Ostr));
  }
}

void benchmarkMain(ExegesisArgs &Args) {
  if (Args.BenchmarkPhaseSelector == BenchmarkPhaseSelectorE::Measure &&
      !Args.UseDummyPerfCounters) {
#ifndef HAVE_LIBPFM
    ExitWithError(
        "benchmarking unavailable, LLVM was built without libpfm. You can "
        "pass --benchmark-phase=... to skip the actual benchmarking or "
        "--use-dummy-perf-counters to not query the kernel for real event "
        "counts.");
#else
    if (pfm::pfmInitialize())
      ExitWithError("cannot initialize libpfm");
#endif
  }

  InitializeAllExegesisTargets();
#define LLVM_EXEGESIS(TargetName)                                              \
  LLVMInitialize##TargetName##AsmPrinter();                                    \
  LLVMInitialize##TargetName##AsmParser();                                     \
  LLVMInitialize##TargetName##Disassembler();
#include "llvm/Config/TargetExegesis.def"

  const LLVMState State = ExitOnErr(LLVMState::Create(
      Args.TripleName, Args.MCPU, Args.MAttr, Args.UseDummyPerfCounters));

  if (Args.BenchmarkPhaseSelector == BenchmarkPhaseSelectorE::Measure)
    ExitOnErr(State.getExegesisTarget().checkFeatureSupport());

  if (Args.ExecutionMode == BenchmarkRunner::ExecutionModeE::SubProcess &&
      Args.UseDummyPerfCounters)
    ExitWithError("Dummy perf counters are not supported in the subprocess "
                  "execution mode.");

  const std::unique_ptr<BenchmarkRunner> Runner =
      ExitOnErr(State.getExegesisTarget().createBenchmarkRunner(
          Args.BenchmarkMode, State, Args.BenchmarkPhaseSelector,
          Args.ExecutionMode, Args.BenchmarkRepeatCount,
          Args.ValidationCounters, Args.ResultAggMode));
  if (!Runner) {
    ExitWithError("cannot create benchmark runner");
  }

  const auto Opcodes = getOpcodesOrDie(State, Args);
  std::vector<BenchmarkCode> Configurations;

  MCRegister LoopRegister =
      State.getExegesisTarget().getDefaultLoopCounterRegister(
          State.getTargetMachine().getTargetTriple());

  if (Opcodes.empty()) {
    Configurations = ExitOnErr(readSnippets(State, Args.SnippetsFile));
    for (const auto &Configuration : Configurations) {
      if (Args.ExecutionMode != BenchmarkRunner::ExecutionModeE::SubProcess &&
          (Configuration.Key.MemoryMappings.size() != 0 ||
           Configuration.Key.MemoryValues.size() != 0 ||
           Configuration.Key.SnippetAddress != 0))
        ExitWithError("Memory and snippet address annotations are only "
                      "supported in subprocess "
                      "execution mode");
    }
    LoopRegister = Configurations[0].Key.LoopRegister;
  }

  SmallVector<std::unique_ptr<const SnippetRepetitor>, 2> Repetitors;
  if (Args.RepetitionMode != Benchmark::RepetitionModeE::AggregateMin)
    Repetitors.emplace_back(
        SnippetRepetitor::Create(Args.RepetitionMode, State, LoopRegister));
  else {
    for (Benchmark::RepetitionModeE RepMode :
         {Benchmark::RepetitionModeE::Duplicate,
          Benchmark::RepetitionModeE::Loop})
      Repetitors.emplace_back(
          SnippetRepetitor::Create(RepMode, State, LoopRegister));
  }

  BitVector AllReservedRegs;
  for (const std::unique_ptr<const SnippetRepetitor> &Repetitor : Repetitors)
    AllReservedRegs |= Repetitor->getReservedRegs();

  if (!Opcodes.empty()) {
    for (const unsigned Opcode : Opcodes) {
      // Ignore instructions without a sched class if
      // -ignore-invalid-sched-class is passed.
      if (Args.IgnoreInvalidSchedClass &&
          State.getInstrInfo().get(Opcode).getSchedClass() == 0) {
        errs() << State.getInstrInfo().getName(Opcode)
               << ": ignoring instruction without sched class\n";
        continue;
      }

      auto ConfigsForInstr =
          generateSnippets(State, Opcode, AllReservedRegs, Args);
      if (!ConfigsForInstr) {
        logAllUnhandledErrors(
            ConfigsForInstr.takeError(), errs(),
            Twine(State.getInstrInfo().getName(Opcode)).concat(": "));
        continue;
      }
      std::move(ConfigsForInstr->begin(), ConfigsForInstr->end(),
                std::back_inserter(Configurations));
    }
  }

  if (Args.MinInstructions == 0) {
    ExitOnErr.setBanner("llvm-exegesis: ");
    ExitWithError("--min-instructions must be greater than zero");
  }

  // Write to standard output if file is not set.
  if (Args.BenchmarkFile.empty())
    Args.BenchmarkFile = "-";

  if (!Configurations.empty())
    runBenchmarkConfigurations(State, Configurations, Repetitors, *Runner,
                               Args);

  pfm::pfmTerminate();
}

// Prints the results of running analysis pass `Pass` to file `OutputFilename`
// if OutputFilename is non-empty.
template <typename Pass>
static void maybeRunAnalysis(const Analysis &Analyzer, const std::string &Name,
                             const std::string &OutputFilename) {
  if (OutputFilename.empty())
    return;
  if (OutputFilename != "-") {
    errs() << "Printing " << Name << " results to file '" << OutputFilename
           << "'\n";
  }
  std::error_code ErrorCode;
  raw_fd_ostream ClustersOS(OutputFilename, ErrorCode,
                            sys::fs::FA_Read | sys::fs::FA_Write);
  if (ErrorCode)
    ExitOnFileError(OutputFilename, errorCodeToError(ErrorCode));
  if (auto Err = Analyzer.run<Pass>(ClustersOS))
    ExitOnFileError(OutputFilename, std::move(Err));
}

static void filterPoints(MutableArrayRef<Benchmark> Points,
                         const MCInstrInfo &MCII, const ExegesisArgs &Args) {
  if (Args.AnalysisSnippetFilter == BenchmarkFilter::All)
    return;

  bool WantPointsWithMemOps =
      Args.AnalysisSnippetFilter == BenchmarkFilter::WithMem;
  for (Benchmark &Point : Points) {
    if (!Point.Error.empty())
      continue;
    if (WantPointsWithMemOps ==
        any_of(Point.Key.Instructions, [&MCII](const MCInst &Inst) {
          const MCInstrDesc &MCDesc = MCII.get(Inst.getOpcode());
          return MCDesc.mayLoad() || MCDesc.mayStore();
        }))
      continue;
    Point.Error = "filtered out by user";
  }
}

static void analysisMain(const ExegesisArgs &Args) {
  ExitOnErr.setBanner("llvm-exegesis: ");
  if (Args.BenchmarkFile.empty())
    ExitWithError("--benchmarks-file must be set");

  if (Args.AnalysisClustersOutputFile.empty() &&
      Args.AnalysisInconsistenciesOutputFile.empty()) {
    ExitWithError(
        "for --mode=analysis: At least one of --analysis-clusters-output-file "
        "and --analysis-inconsistencies-output-file must be specified");
  }

  InitializeAllExegesisTargets();
#define LLVM_EXEGESIS(TargetName)                                              \
  LLVMInitialize##TargetName##AsmPrinter();                                    \
  LLVMInitialize##TargetName##Disassembler();
#include "llvm/Config/TargetExegesis.def"

  auto MemoryBuffer = ExitOnFileError(
      Args.BenchmarkFile, errorOrToExpected(MemoryBuffer::getFile(
                              Args.BenchmarkFile, /*IsText=*/true)));

  const auto TriplesAndCpus =
      ExitOnFileError(Args.BenchmarkFile,
                      Benchmark::readTriplesAndCpusFromYamls(*MemoryBuffer));
  if (TriplesAndCpus.empty()) {
    errs() << "no benchmarks to analyze\n";
    return;
  }
  if (TriplesAndCpus.size() > 1) {
    ExitWithError("analysis file contains benchmarks from several CPUs. This "
                  "is unsupported.");
  }
  auto TripleAndCpu = *TriplesAndCpus.begin();
  if (Args.AnalysisOverrideBenchmarksTripleAndCpu) {
    errs() << "overridding file CPU name (" << TripleAndCpu.CpuName
           << ") with provided tripled (" << Args.TripleName
           << ") and CPU name (" << Args.MCPU << ")\n";
    TripleAndCpu.LLVMTriple = Args.TripleName;
    TripleAndCpu.CpuName = Args.MCPU;
  }
  errs() << "using Triple '" << TripleAndCpu.LLVMTriple << "' and CPU '"
         << TripleAndCpu.CpuName << "'\n";

  // Read benchmarks.
  const LLVMState State = ExitOnErr(
      LLVMState::Create(TripleAndCpu.LLVMTriple, TripleAndCpu.CpuName));
  std::vector<Benchmark> Points = ExitOnFileError(
      Args.BenchmarkFile, Benchmark::readYamls(State, *MemoryBuffer));

  outs() << "Parsed " << Points.size() << " benchmark points\n";
  if (Points.empty()) {
    errs() << "no benchmarks to analyze\n";
    return;
  }
  // FIXME: Merge points from several runs (latency and uops).

  filterPoints(Points, State.getInstrInfo(), Args);

  const auto Clustering = ExitOnErr(BenchmarkClustering::create(
      Points, Args.AnalysisClusteringAlgorithm, Args.AnalysisDbscanNumPoints,
      Args.AnalysisClusteringEpsilon, &State.getSubtargetInfo(),
      &State.getInstrInfo()));

  const Analysis Analyzer(State, Clustering, Args.AnalysisInconsistencyEpsilon,
                          Args.AnalysisDisplayUnstableOpcodes);

  maybeRunAnalysis<Analysis::PrintClusters>(Analyzer, "analysis clusters",
                                            Args.AnalysisClustersOutputFile);
  maybeRunAnalysis<Analysis::PrintSchedClassInconsistencies>(
      Analyzer, "sched class consistency analysis",
      Args.AnalysisInconsistenciesOutputFile);
}

} // namespace exegesis
} // namespace llvm

static constexpr llvm::clv2::OptionCategory ExegesisOptionsCat{
    "llvm-exegesis options"};
static constexpr llvm::clv2::OptionCategory ExegesisBenchmarkOptionsCat{
    "llvm-exegesis benchmark options"};
static constexpr llvm::clv2::OptionCategory ExegesisAnalysisOptionsCat{
    "llvm-exegesis analysis options"};

// clv2 OptionInfo descriptors for llvm-exegesis
namespace {
using namespace llvm;
using namespace llvm::clv2;
using namespace llvm::exegesis;

// Shorthand aliases for categories
constexpr auto &OptsCat = ExegesisOptionsCat;
constexpr auto &BenchCat = ExegesisBenchmarkOptionsCat;
constexpr auto &AnalCat = ExegesisAnalysisOptionsCat;

inline constexpr OptionInfo<int> OpcodeIndexOpt{
    "opcode-index", "opcode to measure, by index, or -1 to measure all opcodes",
    Init{0}, cat(BenchCat)};

inline constexpr OptionInfo<std::string> OpcodeNamesOpt{
    "opcode-name", "comma-separated list of opcodes to measure, by name",
    Init{""}, cat(BenchCat)};

inline constexpr OptionInfo<std::string> SnippetsFileOpt{
    "snippets-file", "code snippets to measure", Init{""}, cat(BenchCat)};

inline constexpr OptionInfo<std::string> BenchmarkFileOpt{
    "benchmarks-file",
    "File to read (analysis mode) or write "
    "(latency/uops/inverse_throughput modes) benchmark "
    "results. \xe2\x80\x9c-\xe2\x80\x9d uses stdin/stdout.",
    Init{""}, cat(OptsCat)};

inline constexpr EnumVal<Benchmark::ModeE> BenchmarkModeVals[] = {
    {"latency", Benchmark::Latency, "Instruction Latency"},
    {"inverse_throughput", Benchmark::InverseThroughput,
     "Instruction Inverse Throughput"},
    {"uops", Benchmark::Uops, "Uop Decomposition"},
    {"analysis", Benchmark::Unknown, "Analysis"},
};
inline constexpr auto BenchmarkModeOpt = makeEnumOption<Benchmark::ModeE>(
    "mode", "the mode to run", BenchmarkModeVals, cat(OptsCat));

inline constexpr EnumVal<Benchmark::ResultAggregationModeE>
    ResultAggModeVals[] = {
        {"min", Benchmark::Min, "Keep min reading"},
        {"max", Benchmark::Max, "Keep max reading"},
        {"mean", Benchmark::Mean, "Compute mean of all readings"},
        {"min-variance", Benchmark::MinVariance,
         "Keep readings set with min-variance"},
};
inline constexpr auto ResultAggModeOpt =
    makeEnumOption<Benchmark::ResultAggregationModeE>(
        "result-aggregation-mode", "How to aggregate multi-values result",
        ResultAggModeVals, Init{Benchmark::Min}, cat(BenchCat));

inline constexpr EnumVal<Benchmark::RepetitionModeE> RepetitionModeVals[] = {
    {"duplicate", Benchmark::Duplicate, "Duplicate the snippet"},
    {"loop", Benchmark::Loop, "Loop over the snippet"},
    {"min", Benchmark::AggregateMin,
     "All of the above and take the minimum of measurements"},
    {"middle-half-duplicate", Benchmark::MiddleHalfDuplicate,
     "Middle half duplicate mode"},
    {"middle-half-loop", Benchmark::MiddleHalfLoop, "Middle half loop mode"},
};
inline constexpr auto RepetitionModeOpt =
    makeEnumOption<Benchmark::RepetitionModeE>(
        "repetition-mode", "how to repeat the instruction snippet",
        RepetitionModeVals, Init{Benchmark::Duplicate}, cat(BenchCat));

inline constexpr OptionInfo<bool> BenchmarkMeasurementsPrintProgressOpt{
    "measurements-print-progress",
    "Produce progress indicator when performing measurements", Init{false},
    cat(BenchCat)};

inline constexpr EnumVal<BenchmarkPhaseSelectorE> BenchmarkPhaseSelectorVals[] =
    {
        {"prepare-snippet", BenchmarkPhaseSelectorE::PrepareSnippet,
         "Only generate the minimal instruction sequence"},
        {"prepare-and-assemble-snippet",
         BenchmarkPhaseSelectorE::PrepareAndAssembleSnippet,
         "Same as prepare-snippet, but also dumps an excerpt of the "
         "sequence (hex encoded)"},
        {"assemble-measured-code",
         BenchmarkPhaseSelectorE::AssembleMeasuredCode,
         "Same as prepare-and-assemble-snippet, but also creates the "
         "full sequence that can be dumped to a file using "
         "--dump-object-to-disk"},
        {"measure", BenchmarkPhaseSelectorE::Measure,
         "Same as prepare-measured-code, but also runs the measurement "
         "(default)"},
};
inline constexpr auto BenchmarkPhaseSelectorOpt =
    makeEnumOption<BenchmarkPhaseSelectorE>(
        "benchmark-phase",
        "it is possible to stop the benchmarking process after some phase",
        BenchmarkPhaseSelectorVals, Init{BenchmarkPhaseSelectorE::Measure},
        cat(BenchCat));

inline constexpr OptionInfo<bool> UseDummyPerfCountersOpt{
    "use-dummy-perf-counters",
    "Do not read real performance counters, use dummy values (for testing)",
    Init{false}, cat(BenchCat)};

inline constexpr OptionInfo<unsigned> MinInstructionsOpt{
    "min-instructions",
    "The minimum number of instructions that should be included in the snippet",
    Init{10000u}, cat(BenchCat)};

inline constexpr OptionInfo<unsigned> LoopBodySizeOpt{
    "loop-body-size",
    "when repeating the instruction snippet by looping "
    "over it, duplicate the snippet until the loop body "
    "contains at least this many instruction",
    Init{0u}, cat(BenchCat)};

inline constexpr OptionInfo<unsigned> MaxConfigsPerOpcodeOpt{
    "max-configs-per-opcode",
    "allow to snippet generator to generate at most that many configs",
    Init{1u}, cat(BenchCat)};

inline constexpr OptionInfo<bool> IgnoreInvalidSchedClassOpt{
    "ignore-invalid-sched-class",
    "ignore instructions that do not define a sched class", Init{false},
    cat(BenchCat)};

inline constexpr EnumVal<BenchmarkFilter> AnalysisSnippetFilterVals[] = {
    {"all", BenchmarkFilter::All, "Keep all benchmarks (default)"},
    {"reg-only", BenchmarkFilter::RegOnly,
     "Keep only those benchmarks that do *NOT* involve memory"},
    {"mem-only", BenchmarkFilter::WithMem,
     "Keep only the benchmarks that *DO* involve memory"},
};
inline constexpr auto AnalysisSnippetFilterOpt =
    makeEnumOption<BenchmarkFilter>(
        "analysis-filter", "Filter the benchmarks before analysing them",
        AnalysisSnippetFilterVals, Init{BenchmarkFilter::All}, cat(BenchCat));

inline constexpr EnumVal<BenchmarkClustering::ModeE>
    AnalysisClusteringAlgorithmVals[] = {
        {"dbscan", BenchmarkClustering::Dbscan, "use DBSCAN/OPTICS algorithm"},
        {"naive", BenchmarkClustering::Naive, "one cluster per opcode"},
};
inline constexpr auto AnalysisClusteringAlgorithmOpt =
    makeEnumOption<BenchmarkClustering::ModeE>(
        "analysis-clustering", "the clustering algorithm to use",
        AnalysisClusteringAlgorithmVals, Init{BenchmarkClustering::Dbscan},
        cat(AnalCat));

inline constexpr OptionInfo<unsigned> AnalysisDbscanNumPointsOpt{
    "analysis-numpoints",
    "minimum number of points in an analysis cluster (dbscan only)", Init{3u},
    cat(AnalCat)};

inline constexpr OptionInfo<float> AnalysisClusteringEpsilonOpt{
    "analysis-clustering-epsilon", "epsilon for benchmark point clustering",
    Init{0.1f}, cat(AnalCat)};

inline constexpr OptionInfo<float> AnalysisInconsistencyEpsilonOpt{
    "analysis-inconsistency-epsilon",
    "epsilon for detection of when the cluster is different from the "
    "LLVM schedule profile values",
    Init{0.1f}, cat(AnalCat)};

inline constexpr OptionInfo<std::string> AnalysisClustersOutputFileOpt{
    "analysis-clusters-output-file", "", Init{""}, cat(AnalCat)};

inline constexpr OptionInfo<std::string> AnalysisInconsistenciesOutputFileOpt{
    "analysis-inconsistencies-output-file", "", Init{""}, cat(AnalCat)};

inline constexpr OptionInfo<bool> AnalysisDisplayUnstableOpcodesOpt{
    "analysis-display-unstable-clusters",
    "if there is more than one benchmark for an opcode, said "
    "benchmarks may end up not being clustered into the same cluster "
    "if the measured performance characteristics are different. by "
    "default all such opcodes are filtered out. this flag will "
    "instead show only such unstable opcodes",
    Init{false}, cat(AnalCat)};

inline constexpr OptionInfo<bool> AnalysisOverrideBenchmarksTripleAndCpuOpt{
    "analysis-override-benchmark-triple-and-cpu",
    "By default, we analyze the benchmarks for the triple/CPU they "
    "were measured for, but if you want to analyze them for some "
    "other combination (specified via -mtriple/-mcpu), you can "
    "pass this flag.",
    Init{false}, cat(AnalCat)};

inline constexpr OptionInfo<std::string> TripleNameOpt{
    "mtriple", "Target triple. See -version for available targets",
    cat(OptsCat)};

inline constexpr OptionInfo<std::string> MCPUOpt{
    "mcpu", "Target a specific cpu type (-mcpu=help for details)",
    Init{"native"}, clv2::value_desc("cpu-name"), cat(OptsCat)};

inline constexpr OptionInfo<std::string> DumpObjectToDiskOpt{
    "dump-object-to-disk",
    "dumps the generated benchmark object to disk "
    "and prints a message to access it",
    clv2::ValueOptional, cat(BenchCat)};

inline constexpr EnumVal<BenchmarkRunner::ExecutionModeE> ExecutionModeVals[] =
    {
        {"inprocess", BenchmarkRunner::ExecutionModeE::InProcess,
         "Executes the snippets within the same process"},
        {"subprocess", BenchmarkRunner::ExecutionModeE::SubProcess,
         "Spawns a subprocess for each snippet execution, "
         "allows for the use of memory annotations"},
};
inline constexpr auto ExecutionModeOpt =
    makeEnumOption<BenchmarkRunner::ExecutionModeE>(
        "execution-mode",
        "Selects the execution mode to use for running snippets",
        ExecutionModeVals, Init{BenchmarkRunner::ExecutionModeE::InProcess},
        cat(BenchCat));

inline constexpr OptionInfo<unsigned> BenchmarkRepeatCountOpt{
    "benchmark-repeat-count",
    "The number of times to repeat measurements on the benchmark k "
    "before aggregating the results",
    Init{30u}, cat(BenchCat)};

inline constexpr EnumVal<ValidationEvent> ValidationCounterVals[] = {
    {"instructions-retired", InstructionRetired, "Count retired instructions"},
    {"l1d-cache-load-misses", L1DCacheLoadMiss, "Count L1D load cache misses"},
    {"l1d-cache-store-misses", L1DCacheStoreMiss,
     "Count L1D store cache misses"},
    {"l1i-cache-load-misses", L1ICacheLoadMiss, "Count L1I load cache misses"},
    {"data-tlb-load-misses", DataTLBLoadMiss, "Count DTLB load misses"},
    {"data-tlb-store-misses", DataTLBStoreMiss, "Count DTLB store misses"},
    {"instruction-tlb-load-misses", InstructionTLBLoadMiss,
     "Count ITLB load misses"},
    {"branch-prediction-misses", BranchPredictionMiss,
     "Branch prediction misses"},
};
inline constexpr auto ValidationCountersOpt =
    makeEnumListOption<ValidationEvent>(
        "validation-counter",
        "The name of a validation counter to run concurrently with the main "
        "counter to validate benchmarking assumptions",
        ValidationCounterVals, clv2::CommaSeparated, cat(BenchCat));

inline constexpr OptionInfo<int> BenchmarkProcessCPUOpt{
    "benchmark-process-cpu",
    "The CPU number that the benchmarking process should executon on", Init{-1},
    cat(BenchCat)};

inline constexpr OptionInfo<std::string> MAttrOpt{
    "mattr", "comma-separated list of target architecture features", Init{""},
    clv2::value_desc("+feature1,-feature2,..."), cat(OptsCat)};

inline constexpr OptionInfo<unsigned> RandomGeneratorSeedOpt{
    "random-generator-seed",
    "The seed value to use for the random number "
    "generator when generating snippets.",
    Init{0u}, Hidden, cat(BenchCat)};

inline constexpr OptionInfo<unsigned> LbrSamplingPeriodOpt{
    "x86-lbr-sample-period",
    "The sample period (nbranches/sample), used for LBR sampling", Init{0u},
    cat(BenchCat)};

inline constexpr OptionInfo<bool> DisableUpperSSERegistersOpt{
    "x86-disable-upper-sse-registers", "Disable XMM8-XMM15 register usage",
    Init{false}, cat(BenchCat)};

inline constexpr OptionInfo<bool> OnlyUsesVLMAXForVLOpt{
    "riscv-vlmax-for-vl", "Only enumerate VLMAX for VL operand", Init{false},
    clv2::Hidden};

inline constexpr OptionInfo<bool> EnumerateRoundingModesOpt{
    "riscv-enumerate-rounding-modes", "Enumerate different FRM and VXRM",
    Init{true}, clv2::Hidden};

inline constexpr OptionInfo<std::string> FilterConfigOpt{
    "riscv-filter-config", "Show only the configs matching this regex",
    Init{""}, clv2::Hidden};

} // anonymous namespace

static constexpr llvm::clv2::OptionsRegistry<
    &OpcodeIndexOpt, &OpcodeNamesOpt, &SnippetsFileOpt, &BenchmarkFileOpt,
    &BenchmarkModeOpt, &ResultAggModeOpt, &RepetitionModeOpt,
    &BenchmarkMeasurementsPrintProgressOpt, &BenchmarkPhaseSelectorOpt,
    &UseDummyPerfCountersOpt, &MinInstructionsOpt, &LoopBodySizeOpt,
    &MaxConfigsPerOpcodeOpt, &IgnoreInvalidSchedClassOpt,
    &AnalysisSnippetFilterOpt, &AnalysisClusteringAlgorithmOpt,
    &AnalysisDbscanNumPointsOpt, &AnalysisClusteringEpsilonOpt,
    &AnalysisInconsistencyEpsilonOpt, &AnalysisClustersOutputFileOpt,
    &AnalysisInconsistenciesOutputFileOpt, &AnalysisDisplayUnstableOpcodesOpt,
    &AnalysisOverrideBenchmarksTripleAndCpuOpt, &TripleNameOpt, &MCPUOpt,
    &DumpObjectToDiskOpt, &ExecutionModeOpt, &BenchmarkRepeatCountOpt,
    &ValidationCountersOpt, &BenchmarkProcessCPUOpt, &MAttrOpt,
    &RandomGeneratorSeedOpt, &LbrSamplingPeriodOpt,
    &DisableUpperSSERegistersOpt, &OnlyUsesVLMAXForVLOpt,
    &EnumerateRoundingModesOpt, &FilterConfigOpt>
    ExegesisToolReg;
int main(int Argc, char **Argv) {
  using namespace llvm;

  InitLLVM X(Argc, Argv);

  // Initialize targets so we can print them when flag --version is specified.
#define LLVM_EXEGESIS(TargetName)                                              \
  LLVMInitialize##TargetName##Target();                                        \
  LLVMInitialize##TargetName##TargetInfo();                                    \
  LLVMInitialize##TargetName##TargetMC();
#include "llvm/Config/TargetExegesis.def"

  // Register the Target and CPU printer for --version.
  cl::AddExtraVersionPrinter(sys::printDefaultTargetAndDetectedCPU);

  // Enable printing of available targets when flag --version is specified.
  cl::AddExtraVersionPrinter(TargetRegistry::printRegisteredTargetsForVersion);

  clv2::OptionParser P;
  P.add<&ExegesisToolReg>();
  RegisterCoreLLVMOptions(P);
  P.hideUnrelatedOptions({&ExegesisOptionsCat, &ExegesisBenchmarkOptionsCat,
                          &ExegesisAnalysisOptionsCat});
  auto OptsCtx = P.parse(Argc, Argv,
                         "llvm host machine instruction characteristics "
                         "measurment and analysis.\n");
  auto *Opts = OptsCtx->getViewPtr<&ExegesisToolReg>();

  exegesis::ExegesisArgs Args;
  Args.OpcodeIndex = Opts->get<&OpcodeIndexOpt>();
  Args.OpcodeNames = Opts->get<&OpcodeNamesOpt>();
  Args.SnippetsFile = Opts->get<&SnippetsFileOpt>();
  Args.BenchmarkFile = Opts->get<&BenchmarkFileOpt>();
  Args.BenchmarkMode = Opts->get<&BenchmarkModeOpt>();
  Args.ResultAggMode = Opts->get<&ResultAggModeOpt>();
  Args.RepetitionMode = Opts->get<&RepetitionModeOpt>();
  Args.BenchmarkMeasurementsPrintProgress =
      Opts->get<&BenchmarkMeasurementsPrintProgressOpt>();
  Args.BenchmarkPhaseSelector = Opts->get<&BenchmarkPhaseSelectorOpt>();
  Args.UseDummyPerfCounters = Opts->get<&UseDummyPerfCountersOpt>();
  Args.MinInstructions = Opts->get<&MinInstructionsOpt>();
  Args.LoopBodySize = Opts->get<&LoopBodySizeOpt>();
  Args.MaxConfigsPerOpcode = Opts->get<&MaxConfigsPerOpcodeOpt>();
  Args.IgnoreInvalidSchedClass = Opts->get<&IgnoreInvalidSchedClassOpt>();
  Args.AnalysisSnippetFilter = Opts->get<&AnalysisSnippetFilterOpt>();
  Args.AnalysisClusteringAlgorithm =
      Opts->get<&AnalysisClusteringAlgorithmOpt>();
  Args.AnalysisDbscanNumPoints = Opts->get<&AnalysisDbscanNumPointsOpt>();
  Args.AnalysisClusteringEpsilon = Opts->get<&AnalysisClusteringEpsilonOpt>();
  Args.AnalysisInconsistencyEpsilon =
      Opts->get<&AnalysisInconsistencyEpsilonOpt>();
  Args.AnalysisClustersOutputFile = Opts->get<&AnalysisClustersOutputFileOpt>();
  Args.AnalysisInconsistenciesOutputFile =
      Opts->get<&AnalysisInconsistenciesOutputFileOpt>();
  Args.AnalysisDisplayUnstableOpcodes =
      Opts->get<&AnalysisDisplayUnstableOpcodesOpt>();
  Args.AnalysisOverrideBenchmarksTripleAndCpu =
      Opts->get<&AnalysisOverrideBenchmarksTripleAndCpuOpt>();
  Args.TripleName = Opts->get<&TripleNameOpt>();
  Args.MCPU = Opts->get<&MCPUOpt>();
  Args.DumpObjectToDisk = Opts->get<&DumpObjectToDiskOpt>();
  Args.DumpObjectToDiskOccurrences = Opts->occurrences<&DumpObjectToDiskOpt>();
  Args.ExecutionMode = Opts->get<&ExecutionModeOpt>();
  Args.BenchmarkRepeatCount = Opts->get<&BenchmarkRepeatCountOpt>();
  Args.ValidationCounters = Opts->get<&ValidationCountersOpt>();
  Args.BenchmarkProcessCPU = Opts->get<&BenchmarkProcessCPUOpt>();
  Args.MAttr = Opts->get<&MAttrOpt>();

  exegesis::RandomGeneratorSeed = Opts->get<&RandomGeneratorSeedOpt>();
  exegesis::RandomGeneratorSeedOccurrences =
      Opts->occurrences<&RandomGeneratorSeedOpt>();
  exegesis::LbrSamplingPeriod = Opts->get<&LbrSamplingPeriodOpt>();
  exegesis::DisableUpperSSERegisters =
      Opts->get<&DisableUpperSSERegistersOpt>();
  exegesis::OnlyUsesVLMAXForVL = Opts->get<&OnlyUsesVLMAXForVLOpt>();
  exegesis::EnumerateRoundingModes = Opts->get<&EnumerateRoundingModesOpt>();
  exegesis::FilterConfig = Opts->get<&FilterConfigOpt>();

  exegesis::ExitOnErr.setExitCodeMapper([](const Error &Err) {
    if (Err.isA<exegesis::ClusteringError>())
      return EXIT_SUCCESS;
    return EXIT_FAILURE;
  });

  if (Args.BenchmarkMode == exegesis::Benchmark::Unknown) {
    exegesis::analysisMain(Args);
  } else {
    exegesis::benchmarkMain(Args);
  }
  return EXIT_SUCCESS;
}
