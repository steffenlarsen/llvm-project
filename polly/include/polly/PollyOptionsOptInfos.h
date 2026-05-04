//===- PollyOptionsOptInfos.h - clv2 option decls --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef POLLY_POLLYOPTIONSOPTINFOS_H
#define POLLY_POLLYOPTIONSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"

namespace llvm {
class Function;
}

#define CLV2_OPTIONS_DECL
#include "polly/PollyOptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

namespace polly_opts {
using ParsedOpts = decltype(llvm::clv2::PollyOptsReg)::ParsedOptionsT;
LLVM_ABI void applyPollyOptions(const ParsedOpts &Opts);
LLVM_ABI const ParsedOpts *getPollyOpts(const llvm::clv2::OptionsContext &Ctx);
void initPollyDebugOpts(const ParsedOpts &);
} // namespace polly_opts

#include "llvm/IR/Function.h"
#define CLV2_OPTIONS_GETTERS
#include "polly/PollyOptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTERS

#endif // POLLY_POLLYOPTIONSOPTINFOS_H
