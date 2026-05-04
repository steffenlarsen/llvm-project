//===- BoltRewriteOptions.cpp - BOLT Rewrite option bridge ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "bolt/Rewrite/BoltRewriteOptionsOptInfos.h"
#include "llvm/Support/OptionsContext.h"
using namespace llvm;
using namespace llvm::clv2;

const bolt::bolt_rewrite_opts::ParsedOpts *
bolt::bolt_rewrite_opts::getBoltRewriteOpts(const OptionsContext &Ctx) {
  if (auto *V = Ctx.getViewPtr<&BoltRewriteOptsReg>())
    return V;
  return nullptr;
}
