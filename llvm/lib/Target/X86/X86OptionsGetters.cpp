//===- X86OptionsGetters.cpp - clv2 getter definitions -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The single translation unit that defines the generated option getters for
// the X86 registry.  Every other TU sees only their declarations.
// See Registry::OutOfLineGetters in llvm/Support/CLV2Options.td for why.
//
//===----------------------------------------------------------------------===//

#include "llvm/Target/X86/X86OptionsOptInfos.h"

#define CLV2_OPTIONS_GETTER_DEFS
#include "llvm/Target/X86/X86OptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTER_DEFS
