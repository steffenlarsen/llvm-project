//===- DebugCounter.cpp - Debug Counter Facilities ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "mlir/Debug/Counter.h"
#include "mlir/IR/MLIROptionsOptInfos.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"

using namespace mlir;
using namespace mlir::tracing;


//===----------------------------------------------------------------------===//
// DebugCounter
//===----------------------------------------------------------------------===//

DebugCounter::DebugCounter(const llvm::clv2::OptionsContext &ctx)
    : optsCtx(&ctx) {
  applyCLOptions(*optsCtx);
}

DebugCounter::~DebugCounter() {
  using namespace llvm::clv2;
  auto *O = mlir_opts::getMLIROptsReg(*optsCtx);
  if (O && O->get<&MLIR_PrintDebugCounter>())
    print(llvm::dbgs());
}

/// Add a counter for the given debug action tag. `countToSkip` is the number
/// of counter executions to skip before enabling execution of the action.
/// `countToStopAfter` is the number of executions of the counter to allow
/// before preventing the action from executing any more.
void DebugCounter::addCounter(StringRef actionTag, int64_t countToSkip,
                              int64_t countToStopAfter) {
  assert(!counters.count(actionTag) &&
         "a counter for the given action was already registered");
  counters.try_emplace(actionTag, countToSkip, countToStopAfter);
}

void DebugCounter::operator()(llvm::function_ref<void()> transform,
                              const Action &action) {
  if (shouldExecute(action.getTag()))
    transform();
}

bool DebugCounter::shouldExecute(StringRef tag) {
  auto counterIt = counters.find(tag);
  if (counterIt == counters.end())
    return true;

  ++counterIt->second.count;

  // We only execute while the `countToSkip` is not smaller than `count`, and
  // `countToStopAfter + countToSkip` is larger than `count`. Negative counters
  // always execute.
  if (counterIt->second.countToSkip < 0)
    return true;
  if (counterIt->second.countToSkip >= counterIt->second.count)
    return false;
  if (counterIt->second.countToStopAfter < 0)
    return true;
  return counterIt->second.countToStopAfter + counterIt->second.countToSkip >=
         counterIt->second.count;
}

void DebugCounter::print(raw_ostream &os) const {
  // Order the registered counters by name.
  SmallVector<const llvm::StringMapEntry<Counter> *, 16> sortedCounters(
      llvm::make_pointer_range(counters));
  llvm::array_pod_sort(sortedCounters.begin(), sortedCounters.end(),
                       [](const decltype(sortedCounters)::value_type *lhs,
                          const decltype(sortedCounters)::value_type *rhs) {
                         return (*lhs)->getKey().compare((*rhs)->getKey());
                       });

  os << "DebugCounter counters:\n";
  for (const llvm::StringMapEntry<Counter> *counter : sortedCounters) {
    os << llvm::left_justify(counter->getKey(), 32) << ": {"
       << counter->second.count << "," << counter->second.countToSkip << ","
       << counter->second.countToStopAfter << "}\n";
  }
}

/// Register a set of useful command-line options that can be used to configure
/// various flags within the DebugCounter. These flags are used when
/// constructing a DebugCounter for initialization.
void DebugCounter::registerCLOptions() {}

bool DebugCounter::isActivated(const llvm::clv2::OptionsContext &optsCtx) {
  using namespace llvm::clv2;
  auto *O = mlir_opts::getMLIROptsReg(optsCtx);
  if (!O)
    return false;
  return !O->get<&MLIR_DebugCounter>().empty() ||
         O->get<&MLIR_PrintDebugCounter>();
}

// This is called by the command line parser when it sees a value for the
// debug-counter option defined above.
void DebugCounter::applyCLOptions(const llvm::clv2::OptionsContext &optsCtx) {
  using namespace llvm::clv2;
  auto *O = mlir_opts::getMLIROptsReg(optsCtx);
  if (!O)
    return;
  const auto &counterArgs = O->get<&MLIR_DebugCounter>();
  if (counterArgs.empty())
    return;

  for (StringRef arg : counterArgs) {
    if (arg.empty())
      continue;

    // Debug counter arguments are expected to be in the form: `counter=value`.
    auto [counterName, counterValueStr] = arg.split('=');
    if (counterValueStr.empty()) {
      llvm::errs()
          << "error: for the --mlir-debug-counter option: "
          << "expected DebugCounter argument to have an `=` separating "
             "the counter name and value, but the provided argument "
             "was: `"
          << arg << "`\n";
      exit(1);
    }

    // Extract the counter value.
    int64_t counterValue;
    if (counterValueStr.getAsInteger(0, counterValue)) {
      llvm::errs() << "error: for the --mlir-debug-counter option: "
                   << "expected DebugCounter counter value to be numeric, but "
                      "got `"
                   << counterValueStr << "`\n";
      exit(1);
    }

    // Now we need to see if this is the skip or the count, remove the suffix,
    // and add it to the counter values.
    if (counterName.consume_back("-skip")) {
      counters[counterName].countToSkip = counterValue;

    } else if (counterName.consume_back("-count")) {
      counters[counterName].countToStopAfter = counterValue;

    } else {
      llvm::errs() << "error: for the --mlir-debug-counter option: "
                   << "expected DebugCounter counter name to end with either "
                      "`-skip` or `-count`, but got `"
                   << counterName << "`\n";
      exit(1);
    }
  }
}
