//===- BoltUtilsOptionsOptInfos.h - clv2 OptionInfo decls -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef BOLT_UTILS_BOLTUTILSOPTIONSOPTINFOS_H
#define BOLT_UTILS_BOLTUTILSOPTIONSOPTINFOS_H

#include "bolt/Utils/CommandLineOpts.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"

namespace llvm {
class Function;
}

#define CLV2_OPTIONS_DECL
#include "bolt/Utils/BoltUtilsOptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

namespace llvm::clv2 {
class OptionsContext;
}

namespace llvm::bolt::bolt_utils_opts {
using ParsedOpts = decltype(clv2::BoltUtilsOptsReg)::ParsedOptionsT;
LLVM_ABI const ParsedOpts *getBoltUtilsOpts(const clv2::OptionsContext &Ctx);
} // namespace llvm::bolt::bolt_utils_opts

#include "bolt/Core/BinaryContext.h"
#define CLV2_OPTIONS_GETTERS
#include "bolt/Utils/BoltUtilsOptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTERS

#endif // BOLT_UTILS_BOLTUTILSOPTIONSOPTINFOS_H
