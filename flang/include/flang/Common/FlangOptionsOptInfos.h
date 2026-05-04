//===- FlangOptionsOptInfos.h - clv2 OptionInfo decls for Flang -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// clv2 OptionInfo declarations for Flang library command-line flags.
//
//===----------------------------------------------------------------------===//

#ifndef FLANG_COMMON_FLANGOPTIONSOPTINFOS_H
#define FLANG_COMMON_FLANGOPTIONSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"
#include <cstddef>
#include <limits>
#include <string>

#define CLV2_OPTIONS_DECL
#include "flang/Common/FlangOptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

namespace llvm::flang_opts {
using ParsedOpts = decltype(clv2::FlangOptsReg)::ParsedOptionsT;
} // namespace llvm::flang_opts

#endif // FLANG_COMMON_FLANGOPTIONSOPTINFOS_H
