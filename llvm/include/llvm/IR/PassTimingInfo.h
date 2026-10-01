//===- PassTimingInfo.h - pass execution timing -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This header defines classes/functions to handle pass execution timing
/// information with interfaces for both pass managers.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_PASSTIMINGINFO_H
#define LLVM_IR_PASSTIMINGINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Timer.h"
#include <memory>
#include <optional>
#include <utility>

namespace llvm {

class LLVMContext;
class Pass;
class PassInstrumentationCallbacks;
class raw_ostream;

/// If the user specifies the -time-passes argument on an LLVM tool command line
/// then the value of this boolean will be true, otherwise false.
/// This is the storage for the -time-passes option. Compilation never reads
/// it: code that parses a command line uses it to decide whether to give the
/// compilation's LLVMContext a PassTimingState.
LLVM_ABI extern bool TimePassesIsEnabled;
/// If TimePassesPerRun is true, there would be one line of report for
/// each pass invocation.
/// If TimePassesPerRun is false, there would be only one line of
/// report for each pass (even there are more than one pass objects).
/// (For new pass manager only)
/// This is the storage for the -time-passes-per-run option, see
/// TimePassesIsEnabled.
LLVM_ABI extern bool TimePassesPerRun;

/// The timers of one compilation: those of the passes run by either pass
/// manager, and the named region timers of code that runs on the
/// compilation's LLVMContext. Compilations that run concurrently in one process
/// are timed independently by giving each its own state.
///
/// A state is attached with LLVMContext::setPassTimingState and must only be
/// used by one thread at a time. Its timers only count the CPU time of the
/// thread running them.
class PassTimingState : public TimerRegistry {
  bool PerRun;

  /// Map that counts instances of legacy passes.
  StringMap<unsigned> PassIDCountMap;
  /// Timers for legacy pass instances.
  DenseMap<Pass *, std::unique_ptr<Timer>> LegacyPassTimers;

  Timer *newLegacyPassTimer(StringRef PassID, StringRef PassDesc);

public:
  /// See TimePassesPerRun for \p PerRun.
  LLVM_ABI explicit PassTimingState(bool PerRun = false);
  LLVM_ABI ~PassTimingState();

  bool isPerRun() const { return PerRun; }

  /// Get the timer for this legacy-pass-manager's pass instance.
  LLVM_ABI Timer *getLegacyPassTimer(Pass *P);

  /// Report the pass timings immediately and then reset the timers to zero. By
  /// default it uses the stream created by CreateInfoOutputFile().
  LLVM_ABI void reportAndResetTimings(raw_ostream *OutStream = nullptr);
};

/// Request the timer for this legacy-pass-manager's pass instance, or null if
/// \p State is null.
LLVM_ABI Timer *getPassTimer(Pass *, PassTimingState *State);

/// For library entry points that compile on a context they are given but have
/// no command line of their own: while in scope, gives \p Context a
/// PassTimingState if -time-passes is set and \p Context has none, and reports
/// the timings when going out of scope.
class CommandLineTimePassesScope {
  LLVMContext &Context;
  std::optional<PassTimingState> State;

public:
  LLVM_ABI explicit CommandLineTimePassesScope(LLVMContext &Context);
  LLVM_ABI ~CommandLineTimePassesScope();
};

/// This class implements -time-passes functionality for new pass manager.
/// It provides the pass-instrumentation callbacks that measure the pass
/// execution time. They collect timing info into individual timers as
/// passes are being run.
class TimePassesHandler {
  /// Value of this type is capable of uniquely identifying pass invocations.
  /// It is a pair of string Pass-Identifier (which for now is common
  /// to all the instance of a given pass) + sequential invocation counter.
  using PassInvocationID = std::pair<StringRef, unsigned>;

  /// The state that owns the timer groups, or null if timing is disabled.
  PassTimingState *State;

  /// Groups of timers for passes and analyses.
  TimerGroup *PassTG = nullptr;
  TimerGroup *AnalysisTG = nullptr;

  using TimerVector = llvm::SmallVector<std::unique_ptr<Timer>, 4>;
  /// Map of timers for pass invocations
  StringMap<TimerVector> TimingData;

  /// Stack of currently active pass timers. Passes can run other
  /// passes.
  SmallVector<Timer *, 8> PassActiveTimerStack;
  /// Stack of currently active analysis timers. Analyses can request other
  /// analyses.
  SmallVector<Timer *, 8> AnalysisActiveTimerStack;

  /// Custom output stream to print timing information into.
  /// By default (== nullptr) we emit time report into the stream created by
  /// CreateInfoOutputFile().
  raw_ostream *OutStream = nullptr;

public:
  static constexpr StringRef PassGroupName = "pass";
  static constexpr StringRef AnalysisGroupName = "analysis";
  static constexpr StringRef PassGroupDesc = "Pass execution timing report";
  static constexpr StringRef AnalysisGroupDesc =
      "Analysis execution timing report";

  /// Time passes into the timer groups of \p State, which must outlive the
  /// handler. Timing is disabled if \p State is null.
  LLVM_ABI explicit TimePassesHandler(PassTimingState *State);

  /// Prints out timing information and then resets the timers.
  LLVM_ABI void print();

  // We intend this to be unique per-compilation, thus no copies.
  TimePassesHandler(const TimePassesHandler &) = delete;
  void operator=(const TimePassesHandler &) = delete;

  LLVM_ABI void registerCallbacks(PassInstrumentationCallbacks &PIC);

  /// Set a custom output stream for subsequent reporting.
  LLVM_ABI void setOutStream(raw_ostream &OutStream);

private:
  /// Dumps information for running/triggered timers, useful for debugging
  LLVM_DUMP_METHOD void dump() const;

  /// Returns the new timer for each new run of the pass.
  Timer &getPassTimer(StringRef PassID, bool IsPass);

  void startAnalysisTimer(StringRef PassID);
  void stopAnalysisTimer(StringRef PassID);
  void startPassTimer(StringRef PassID);
  void stopPassTimer(StringRef PassID);
};

} // namespace llvm

#endif
