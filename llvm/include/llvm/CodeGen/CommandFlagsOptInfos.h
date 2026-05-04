//===- CommandFlagsOptInfos.h - clv2 OptionInfo decls for CG flags -------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_COMMANDFLAGSOPTINFOS_H
#define LLVM_CODEGEN_COMMANDFLAGSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"

namespace llvm {
class Function;
}

#include "llvm/CodeGen/SaveStatsMode.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Target/TargetOptions.h"

#define CLV2_OPTIONS_DECL
#include "llvm/CodeGen/CommandFlagsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

#include "llvm/IR/Function.h"
#define CLV2_OPTIONS_GETTERS
#include "llvm/CodeGen/CommandFlagsOptInfos.inc"
#undef CLV2_OPTIONS_GETTERS

#endif // LLVM_CODEGEN_COMMANDFLAGSOPTINFOS_H
