//===- llvm/IR/OptBisect/Bisect.cpp - LLVM Bisect support -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// This file implements support for a bisecting optimizations based on a
/// command line option.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/OptBisect.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/IR/IROptionsOptInfos.h"
#include "llvm/Pass.h"
#include "llvm/Support/IntegerInclusiveInterval.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>
#include <cstdlib>
#include <limits>

using namespace llvm;

static OptBisect &getOptBisector() {
  static OptBisect OptBisector;
  return OptBisector;
}

static void printPassMessage(StringRef Name, int PassNum, StringRef TargetDesc,
                             bool Running) {
  StringRef Status = Running ? "" : "NOT ";
  errs() << "BISECT: " << Status << "running pass (" << PassNum << ") " << Name
         << " on " << TargetDesc << '\n';
}

bool OptBisect::shouldRunPass(StringRef PassName,
                              StringRef IRDescription) const {
  assert(isEnabled());

  int CurBisectNum = ++LastBisectNum;

  // Check if current pass number falls within any of the specified intervals.
  // Since the bisector may be enabled by opt-disable, we also need to check if
  // the BisectIntervals are empty.
  bool ShouldRun =
      BisectIntervals.empty() ||
      IntegerInclusiveIntervalUtils::contains(BisectIntervals, CurBisectNum);

  // Also check if the pass is disabled via -opt-disable.
  ShouldRun = ShouldRun && !DisabledPasses.contains(PassName);

  if (isVerbose())
    printPassMessage(PassName, CurBisectNum, IRDescription, ShouldRun);
  return ShouldRun;
}

OptPassGate &llvm::getGlobalPassGate() { return getOptBisector(); }

void llvm::initOptBisectFromOptions(const ir_opts::ParsedOpts &Opts) {
  getOptBisector().setVerbose(Opts.get<&clv2::IR_OptBisectVerbose>());

  // Handle -opt-bisect-limit=N (legacy single-limit form).
  // -1 means run all passes but still enable verbose output.
  // 0 means run no passes (but still enable verbose output).
  // N>0 means run passes 1..N.
  if (Opts.specified<&clv2::IR_OptBisectLimit>()) {
    int Limit = Opts.get<&clv2::IR_OptBisectLimit>();
    std::string RangeStr;
    if (Limit == -1) {
      // Run all passes — use a very large upper bound so isEnabled() is true.
      RangeStr = "1-" + std::to_string(std::numeric_limits<int>::max());
    } else if (Limit == 0) {
      // Run no passes — use "0" which matches nothing since passes start at 1.
      RangeStr = "0";
    } else if (Limit > 0) {
      RangeStr = "1-" + std::to_string(Limit);
    }
    if (!RangeStr.empty()) {
      auto Intervals = IntegerInclusiveIntervalUtils::parseIntervals(RangeStr);
      if (Intervals)
        getOptBisector().setIntervals(std::move(*Intervals));
      else
        consumeError(Intervals.takeError());
    }
  }

  // Handle -opt-bisect=<intervals> (e.g. "1-10,20-30,45").
  // Special values: -1 means run all passes, 0 means run no passes.
  if (Opts.specified<&clv2::IR_OptBisectIntervals>()) {
    std::string IntervalsStr = Opts.get<&clv2::IR_OptBisectIntervals>();
    if (!IntervalsStr.empty()) {
      if (IntervalsStr == "-1") {
        // Run all passes — use a very large upper bound.
        std::string AllStr =
            "1-" + std::to_string(std::numeric_limits<int>::max());
        auto Intervals = IntegerInclusiveIntervalUtils::parseIntervals(AllStr);
        if (Intervals)
          getOptBisector().setIntervals(std::move(*Intervals));
        else
          consumeError(Intervals.takeError());
      } else {
        auto Intervals =
            IntegerInclusiveIntervalUtils::parseIntervals(IntervalsStr);
        if (Intervals)
          getOptBisector().setIntervals(std::move(*Intervals));
        else
          consumeError(Intervals.takeError());
      }
    }
  }

  // Handle -opt-disable=pass1,pass2,...
  if (Opts.specified<&clv2::IR_OptDisablePasses>()) {
    for (const auto &PassName : Opts.get<&clv2::IR_OptDisablePasses>())
      getOptBisector().setDisabled(PassName);
  }
}
