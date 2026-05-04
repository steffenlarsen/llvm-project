//===- ObjCARCOptionsOptInfos.h - clv2 option decls --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_OBJCARC_OBJCARCOPTIONSOPTINFOS_H
#define LLVM_TRANSFORMS_OBJCARC_OBJCARCOPTIONSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"
#include <optional>

namespace llvm {
class Function;
}

#define CLV2_OPTIONS_DECL
#include "llvm/Transforms/ObjCARC/ObjCARCOptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

#include "llvm/IR/Function.h"
#define CLV2_OPTIONS_GETTERS
#include "llvm/Transforms/ObjCARC/ObjCARCOptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTERS

#endif // LLVM_TRANSFORMS_OBJCARC_OBJCARCOPTIONSOPTINFOS_H
