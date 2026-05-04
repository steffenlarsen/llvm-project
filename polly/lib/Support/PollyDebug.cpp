//===-PollyDebug.cpp -Provide support for debugging Polly passes-*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Functions to aid printing Debug Info of all polly passes.
//
//===----------------------------------------------------------------------===//

#include "polly/Support/PollyDebug.h"
#include "polly/PollyOptionsOptInfos.h"
#include "llvm/Support/CommandLineV2.h"

using namespace polly;
using namespace llvm;

bool PollyDebugFlag;
bool polly::getPollyDebugFlag() { return PollyDebugFlag; }

void polly_opts::initPollyDebugOpts(const polly_opts::ParsedOpts &Opts) {
  using namespace llvm::clv2;
  if (Opts.specified<&POLLY_Debug>())
    PollyDebugFlag = Opts.get<&POLLY_Debug>();
}
