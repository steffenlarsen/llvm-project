//===- IROptions.cpp - LLVMCore option bridge -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Function.h"
#include "llvm/IR/IROptionsOptInfos.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/OptBisect.h"
#include "llvm/IR/PassTimingInfo.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Support/OptionsContext.h"
#include "llvm/Support/Regex.h"

using namespace llvm;
using namespace llvm::clv2;

void ir_opts::applyIROptions(const ir_opts::ParsedOpts &Opts) {
  // --- PassTimingInfo.cpp externs ---
  if (Opts.specified<&IR_TimePasses>())
    TimePassesIsEnabled = Opts.get<&IR_TimePasses>();
  if (Opts.specified<&IR_TimePassesPerRun>()) {
    TimePassesPerRun = Opts.get<&IR_TimePassesPerRun>();
    if (TimePassesPerRun)
      TimePassesIsEnabled = true;
  }

  // --- OptBisect.cpp: configure the global OptBisect singleton ---
  initOptBisectFromOptions(Opts);
}
