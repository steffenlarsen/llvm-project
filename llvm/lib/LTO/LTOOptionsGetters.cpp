//===- LTOOptionsGetters.cpp - clv2 getter definitions
//-----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The single translation unit that defines the generated option getters for
// this registry.  Every other TU sees only their declarations.
// See Registry::OutOfLineGetters in llvm/Support/CLV2Options.td for why.
//
//===----------------------------------------------------------------------===//

#include "llvm/LTO/LTOOptionsOptInfos.h"

#define CLV2_OPTIONS_GETTER_DEFS
#include "llvm/LTO/LTOOptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTER_DEFS
