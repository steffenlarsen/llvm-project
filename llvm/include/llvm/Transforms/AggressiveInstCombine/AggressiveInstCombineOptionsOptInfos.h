//===- AggressiveInstCombineOptionsOptInfos.h - clv2 option decls --*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Generated from AggressiveInstCombineOptions.td by llvm-tblgen.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_AGGRESSIVEINSTCOMBINE_AGGRESSIVEINSTCOMBINEOPTIONSOPTINFOS_H
#define LLVM_TRANSFORMS_AGGRESSIVEINSTCOMBINE_AGGRESSIVEINSTCOMBINEOPTIONSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"

namespace llvm {
class Function;
}

// Option declarations, registries, and OptionsContext*-only getters.
#define CLV2_OPTIONS_DECL
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombineOptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

// Context-type getter overloads (Function, MachineFunction, etc.)
// These need the full type definition, so include them after the
// relevant headers are available. Consumers who only need the
// OptionsContext* overload can skip this section.
#include "llvm/IR/Function.h"
#define CLV2_OPTIONS_GETTERS
#include "llvm/Transforms/AggressiveInstCombine/AggressiveInstCombineOptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTERS

#endif
