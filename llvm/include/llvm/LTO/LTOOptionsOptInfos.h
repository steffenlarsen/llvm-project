//===- LTOOptionsOptInfos.h - clv2 OptionInfo decls for LTO flags -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LTO_LTOOPTIONSOPTINFOS_H
#define LLVM_LTO_LTOOPTIONSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"

namespace llvm {
class Function;
}

#define CLV2_OPTIONS_DECL
#include "llvm/LTO/LTOOptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

#include "llvm/IR/Function.h"
#define CLV2_OPTIONS_GETTERS
#include "llvm/LTO/LTOOptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTERS

#endif // LLVM_LTO_LTOOPTIONSOPTINFOS_H
