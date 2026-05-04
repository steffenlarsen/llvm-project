//===- BoltPassesOptions.cpp - BOLT Passes option bridge
//-------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "bolt/Passes/BoltPassesOptionsOptInfos.h"
#include "llvm/Support/OptionsContext.h"
using namespace llvm;
using namespace llvm::clv2;

const bolt::bolt_passes_opts::ParsedOpts *
bolt::bolt_passes_opts::getBoltPassesOpts(const OptionsContext &Ctx) {
  if (auto *V = Ctx.getViewPtr<&BoltPassesOptsReg>())
    return V;
  return nullptr;
}
