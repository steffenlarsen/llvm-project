//===- PollyOptions.cpp - Polly option bridge -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "polly/PollyOptionsOptInfos.h"
#include "llvm/Support/OptionsContext.h"
using namespace llvm;
using namespace llvm::clv2;

void polly_opts::applyPollyOptions(const polly_opts::ParsedOpts &Opts) {
  initPollyDebugOpts(Opts);
}

const polly_opts::ParsedOpts *
polly_opts::getPollyOpts(const OptionsContext &Ctx) {
  if (auto *V = Ctx.getViewPtr<&PollyOptsReg>())
    return V;
  return nullptr;
}
