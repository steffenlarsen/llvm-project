//===- BoltProfileOptions.cpp - BOLT Profile option bridge ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "bolt/Profile/BoltProfileOptionsOptInfos.h"
#include "llvm/Support/OptionsContext.h"
using namespace llvm;
using namespace llvm::clv2;

const bolt::bolt_profile_opts::ParsedOpts *
bolt::bolt_profile_opts::getBoltProfileOpts(const OptionsContext &Ctx) {
  if (auto *V = Ctx.getViewPtr<&BoltProfileOptsReg>())
    return V;
  return nullptr;
}
