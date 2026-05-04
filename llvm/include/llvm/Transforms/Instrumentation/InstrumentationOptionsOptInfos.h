//===- InstrumentationOptionsOptInfos.h - clv2 OptionInfo decls -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_INSTRUMENTATIONOPTIONSOPTINFOS_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_INSTRUMENTATIONOPTIONSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"

namespace llvm {
class Function;
}

#include "llvm/Analysis/GVDAGType.h"
#include "llvm/ProfileData/ProfCorrelatorKind.h"
#include "llvm/Transforms/Instrumentation/AddressSanitizerOptions.h"

#define CLV2_OPTIONS_DECL
#include "llvm/Transforms/Instrumentation/InstrumentationOptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

namespace llvm::inst_opts {
using ParsedOpts = decltype(clv2::InstrumentationOptsReg)::ParsedOptionsT;
} // namespace llvm::inst_opts

#include "llvm/IR/Function.h"
#define CLV2_OPTIONS_GETTERS
#include "llvm/Transforms/Instrumentation/InstrumentationOptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTERS

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_INSTRUMENTATIONOPTIONSOPTINFOS_H
