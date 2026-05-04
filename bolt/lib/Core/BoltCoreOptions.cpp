//===- BoltCoreOptions.cpp - BOLT Core option bridge ----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "bolt/Core/BoltCoreOptionsOptInfos.h"
#include "llvm/Support/OptionsContext.h"
using namespace llvm;
using namespace llvm::clv2;

const bolt::bolt_core_opts::ParsedOpts *
bolt::bolt_core_opts::getBoltCoreOpts(const OptionsContext &Ctx) {
  if (auto *V = Ctx.getViewPtr<&BoltCoreOptsReg>())
    return V;
  return nullptr;
}
