//===- IROptionsOptInfos.h - clv2 option decls --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_IROPTIONSOPTINFOS_H
#define LLVM_IR_IROPTIONSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"

namespace llvm {
class Function;
}

#include "llvm/IR/PrintPasses.h"

#define CLV2_OPTIONS_DECL
#include "llvm/IR/IROptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

namespace llvm::ir_opts {
using ParsedOpts = decltype(clv2::IROptsReg)::ParsedOptionsT;
LLVM_ABI void applyIROptions(const ParsedOpts &Opts);
} // namespace llvm::ir_opts

namespace llvm {
LLVM_ABI void initOptBisectFromOptions(const ir_opts::ParsedOpts &Opts);
} // namespace llvm

#include "llvm/IR/Function.h"
#define CLV2_OPTIONS_GETTERS
#include "llvm/IR/IROptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTERS

#endif // LLVM_IR_IROPTIONSOPTINFOS_H
