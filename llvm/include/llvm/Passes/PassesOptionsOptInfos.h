//===- PassesOptionsOptInfos.h - clv2 option decls --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PASSES_PASSESOPTIONSOPTINFOS_H
#define LLVM_PASSES_PASSESOPTIONSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"

namespace llvm {
class Function;
}

#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Transforms/IPO/Attributor.h"

#define CLV2_OPTIONS_DECL
#include "llvm/Passes/PassesOptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

namespace llvm::passes {
using ParsedOpts = decltype(clv2::PassesOptsReg)::ParsedOptionsT;
} // namespace llvm::passes

#include "llvm/IR/Function.h"
#define CLV2_OPTIONS_GETTERS
#include "llvm/Passes/PassesOptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTERS

#endif // LLVM_PASSES_PASSESOPTIONSOPTINFOS_H
