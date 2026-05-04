//===-- Benchmark ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "JSON.h"
#include "LibcBenchmark.h"
#include "LibcMemoryBenchmark.h"
#include "MemorySizeDistributions.h"
#include "src/__support/macros/config.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/raw_ostream.h"

#include <cstring>
#include <unistd.h>

namespace LIBC_NAMESPACE_DECL {

extern void *memcpy(void *__restrict, const void *__restrict, size_t);
extern void *memmove(void *, const void *, size_t);
extern void *memset(void *, int, size_t);
extern void bzero(void *, size_t);
extern int memcmp(const void *, const void *, size_t);
extern int bcmp(const void *, const void *, size_t);

} // namespace LIBC_NAMESPACE_DECL

namespace llvm {
namespace libc_benchmarks {

static constexpr clv2::OptionInfo<std::string> StudyNameOpt{
    "study-name", "The name for this study", clv2::Required};

static constexpr clv2::OptionInfo<std::string> SizeDistributionNameOpt{
    "size-distribution-name", "The name of the distribution to use"};

static constexpr clv2::OptionInfo<bool> SweepModeOpt{
    "sweep-mode",
    "If set, benchmark all sizes from sweep-min-size to sweep-max-size"};

static constexpr clv2::OptionInfo<unsigned> SweepMinSizeOpt{
    "sweep-min-size", "The minimum size to use in sweep-mode", clv2::Init{0u}};

static constexpr clv2::OptionInfo<unsigned> SweepMaxSizeOpt{
    "sweep-max-size", "The maximum size to use in sweep-mode",
    clv2::Init{256u}};

static constexpr clv2::OptionInfo<unsigned> AlignedAccessOpt{
    "aligned-access",
    "The alignment to use when accessing the buffers\n"
    "Default is unaligned\n"
    "Use 0 to disable address randomization",
    clv2::Init{1u}};

static constexpr clv2::OptionInfo<std::string> OutputOpt{
    "output", "Specify output filename", clv2::value_desc("filename"),
    clv2::Init{"-"}};

static constexpr clv2::OptionInfo<unsigned> NumTrialsOpt{
    "num-trials", "The number of benchmarks run to perform", clv2::Init{1u}};

static constexpr clv2::OptionsRegistry<
    &StudyNameOpt, &SizeDistributionNameOpt, &SweepModeOpt, &SweepMinSizeOpt,
    &SweepMaxSizeOpt, &AlignedAccessOpt, &OutputOpt, &NumTrialsOpt>
    BenchmarkReg;

// File-scope variables populated from parsed options in main().
static std::string StudyName;
static std::string SizeDistributionName;
static bool SweepMode;
static uint32_t SweepMinSize;
static uint32_t SweepMaxSize;
static uint32_t AlignedAccess;
static std::string Output;
static uint32_t NumTrials;

#if defined(LIBC_BENCHMARK_FUNCTION_MEMCPY)
#define LIBC_BENCHMARK_FUNCTION LIBC_BENCHMARK_FUNCTION_MEMCPY
using BenchmarkSetup = CopySetup;
#elif defined(LIBC_BENCHMARK_FUNCTION_MEMMOVE)
#define LIBC_BENCHMARK_FUNCTION LIBC_BENCHMARK_FUNCTION_MEMMOVE
using BenchmarkSetup = MoveSetup;
#elif defined(LIBC_BENCHMARK_FUNCTION_MEMSET)
#define LIBC_BENCHMARK_FUNCTION LIBC_BENCHMARK_FUNCTION_MEMSET
using BenchmarkSetup = SetSetup;
#elif defined(LIBC_BENCHMARK_FUNCTION_BZERO)
#define LIBC_BENCHMARK_FUNCTION LIBC_BENCHMARK_FUNCTION_BZERO
using BenchmarkSetup = SetSetup;
#elif defined(LIBC_BENCHMARK_FUNCTION_MEMCMP)
#define LIBC_BENCHMARK_FUNCTION LIBC_BENCHMARK_FUNCTION_MEMCMP
using BenchmarkSetup = ComparisonSetup;
#elif defined(LIBC_BENCHMARK_FUNCTION_BCMP)
#define LIBC_BENCHMARK_FUNCTION LIBC_BENCHMARK_FUNCTION_BCMP
using BenchmarkSetup = ComparisonSetup;
#else
#error "Missing LIBC_BENCHMARK_FUNCTION_XXX definition"
#endif

struct MemfunctionBenchmarkBase : public BenchmarkSetup {
  MemfunctionBenchmarkBase() : ReportProgress(isatty(fileno(stdout))) {}
  virtual ~MemfunctionBenchmarkBase() {}

  virtual Study run() = 0;

  CircularArrayRef<ParameterBatch::ParameterType>
  generate_batch(size_t iterations) {
    randomize();
    return cycle(ArrayRef(parameters), iterations);
  }

protected:
  Study createStudy() {
    Study study;
    // Setup study.
    study.study_name = StudyName;
    Runtime &ri = study.runtime;
    ri.host = HostState::get();
    ri.buffer_size = buffer_size;
    ri.batch_parameter_count = batch_size;

    BenchmarkOptions &bo = ri.benchmark_options;
    bo.min_duration = std::chrono::milliseconds(1);
    bo.max_duration = std::chrono::seconds(1);
    bo.max_iterations = 10'000'000U;
    bo.min_samples = 4;
    bo.max_samples = 1000;
    bo.epsilon = 0.01; // 1%
    bo.scaling_factor = 1.4;

    StudyConfiguration &sc = study.configuration;
    sc.num_trials = NumTrials;
    sc.is_sweep_mode = SweepMode;
    sc.access_alignment = MaybeAlign(AlignedAccess);
    sc.function = LIBC_BENCHMARK_FUNCTION_NAME;
    return study;
  }

  void runTrials(const BenchmarkOptions &options,
                 std::vector<Duration> &measurements) {
    for (size_t i = 0; i < NumTrials; ++i) {
      const BenchmarkResult result = benchmark(
          options, *this, [this](ParameterBatch::ParameterType parameter) {
            return call(parameter, LIBC_BENCHMARK_FUNCTION);
          });
      measurements.push_back(result.best_guess);
      reportProgress(measurements);
    }
  }

  virtual void randomize() = 0;

private:
  bool ReportProgress;

  void reportProgress(const std::vector<Duration> &Measurements) {
    if (!ReportProgress)
      return;
    static size_t LastPercent = -1;
    const size_t TotalSteps = Measurements.capacity();
    const size_t Steps = Measurements.size();
    const size_t Percent = 100 * Steps / TotalSteps;
    if (Percent == LastPercent)
      return;
    LastPercent = Percent;
    size_t i = 0;
    errs() << '[';
    for (; i <= Percent; ++i)
      errs() << '#';
    for (; i <= 100; ++i)
      errs() << '_';
    errs() << "] " << Percent << '%' << '\r';
  }
};

struct MemfunctionBenchmarkSweep final : public MemfunctionBenchmarkBase {
  MemfunctionBenchmarkSweep()
      : offset_sampler(MemfunctionBenchmarkBase::buffer_size, SweepMaxSize,
                       MaybeAlign(AlignedAccess)) {}

  virtual void randomize() override {
    for (auto &p : parameters) {
      p.offset_bytes = offset_sampler(gen);
      p.size_bytes = current_sweep_size;
      check_valid(p);
    }
  }

  virtual Study run() override {
    Study study = createStudy();
    study.configuration.sweep_mode_max_size = SweepMaxSize;
    BenchmarkOptions &bo = study.runtime.benchmark_options;
    bo.min_duration = std::chrono::milliseconds(1);
    bo.initial_iterations = 100;
    auto &measurements = study.measurements;
    measurements.reserve(NumTrials * SweepMaxSize);
    for (size_t Size = SweepMinSize; Size <= SweepMaxSize; ++Size) {
      current_sweep_size = Size;
      runTrials(bo, measurements);
    }
    return study;
  }

private:
  size_t current_sweep_size = 0;
  OffsetDistribution offset_sampler;
  std::mt19937_64 gen;
};

struct MemfunctionBenchmarkDistribution final
    : public MemfunctionBenchmarkBase {
  MemfunctionBenchmarkDistribution(MemorySizeDistribution distribution_arg)
      : distribution(distribution_arg),
        probabilities(distribution_arg.probabilities),
        size_sampler(probabilities.begin(), probabilities.end()),
        offset_sampler(MemfunctionBenchmarkBase::buffer_size,
                       probabilities.size() - 1, MaybeAlign(AlignedAccess)) {}

  virtual void randomize() override {
    for (auto &p : parameters) {
      p.offset_bytes = offset_sampler(gen);
      p.size_bytes = size_sampler(gen);
      check_valid(p);
    }
  }

  virtual Study run() override {
    Study study = createStudy();
    study.configuration.size_distribution_name = distribution.name.str();
    BenchmarkOptions &bo = study.runtime.benchmark_options;
    bo.min_duration = std::chrono::milliseconds(10);
    bo.initial_iterations = batch_size * 10;
    auto &measurements = study.measurements;
    measurements.reserve(NumTrials);
    runTrials(bo, measurements);
    return study;
  }

private:
  MemorySizeDistribution distribution;
  ArrayRef<double> probabilities;
  std::discrete_distribution<unsigned> size_sampler;
  OffsetDistribution offset_sampler;
  std::mt19937_64 gen;
};

void writeStudy(const Study &S) {
  std::error_code EC;
  raw_fd_ostream FOS(Output, EC);
  if (EC)
    report_fatal_error(Twine("Could not open file: ")
                           .concat(EC.message())
                           .concat(", ")
                           .concat(Output));
  json::OStream JOS(FOS);
  serializeToJson(S, JOS);
  FOS << "\n";
}

void main() {
  checkRequirements();
  if (!isPowerOf2_32(AlignedAccess))
    report_fatal_error(Twine("aligned-access") +
                       Twine(" must be a power of two or zero"));

  const bool HasDistributionName = !SizeDistributionName.empty();
  if (SweepMode && HasDistributionName)
    report_fatal_error("Select only one of `--" + Twine("sweep-mode") +
                       "` or `--" + Twine("size-distribution-name") + "`");

  std::unique_ptr<MemfunctionBenchmarkBase> Benchmark;
  if (SweepMode)
    Benchmark.reset(new MemfunctionBenchmarkSweep());
  else
    Benchmark.reset(new MemfunctionBenchmarkDistribution(getDistributionOrDie(
        BenchmarkSetup::get_distributions(), SizeDistributionName)));
  writeStudy(Benchmark->run());
}

} // namespace libc_benchmarks
} // namespace llvm

#ifndef NDEBUG
#error For reproducibility benchmarks should not be compiled in DEBUG mode.
#endif

int main(int argc, char **argv) {
  llvm::clv2::OptionParser P;
  P.add<&llvm::libc_benchmarks::BenchmarkReg>();
  auto OptsCtx = P.parse(argc, argv);
  auto *Opts = OptsCtx->getViewPtr<&llvm::libc_benchmarks::BenchmarkReg>();
  llvm::libc_benchmarks::StudyName =
      Opts->get<&llvm::libc_benchmarks::StudyNameOpt>();
  llvm::libc_benchmarks::SizeDistributionName =
      Opts->get<&llvm::libc_benchmarks::SizeDistributionNameOpt>();
  llvm::libc_benchmarks::SweepMode =
      Opts->get<&llvm::libc_benchmarks::SweepModeOpt>();
  llvm::libc_benchmarks::SweepMinSize =
      Opts->get<&llvm::libc_benchmarks::SweepMinSizeOpt>();
  llvm::libc_benchmarks::SweepMaxSize =
      Opts->get<&llvm::libc_benchmarks::SweepMaxSizeOpt>();
  llvm::libc_benchmarks::AlignedAccess =
      Opts->get<&llvm::libc_benchmarks::AlignedAccessOpt>();
  llvm::libc_benchmarks::Output =
      Opts->get<&llvm::libc_benchmarks::OutputOpt>();
  llvm::libc_benchmarks::NumTrials =
      Opts->get<&llvm::libc_benchmarks::NumTrialsOpt>();
  llvm::libc_benchmarks::main();
  return EXIT_SUCCESS;
}
