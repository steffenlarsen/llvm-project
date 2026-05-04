//===- BoltPassesOptionsOptInfos.h - clv2 OptionInfo decls -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef BOLT_PASSES_BOLTPASSESOPTIONSOPTINFOS_H
#define BOLT_PASSES_BOLTPASSESOPTIONSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"
#include <limits>

namespace llvm {
class Function;
}

#include "bolt/Passes/BinaryPasses.h"
#include "bolt/Passes/FrameOptimizer.h"
#include "bolt/Passes/IdenticalCodeFolding.h"
#include "bolt/Passes/PLTCall.h"
#include "bolt/Passes/ReorderFunctions.h"
#include "bolt/Passes/RetpolineInsertion.h"
#include "bolt/Passes/TailDuplication.h"
#include "bolt/Utils/BoltUtilsOptionsOptInfos.h"

#define CLV2_OPTIONS_DECL
#include "bolt/Passes/BoltPassesOptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

namespace llvm::clv2 {
class OptionsContext;
}

namespace llvm::bolt::bolt_passes_opts {
using ParsedOpts = decltype(clv2::BoltPassesOptsReg)::ParsedOptionsT;
LLVM_ABI const ParsedOpts *getBoltPassesOpts(const clv2::OptionsContext &Ctx);
} // namespace llvm::bolt::bolt_passes_opts

#include "bolt/Core/BinaryContext.h"
#define CLV2_OPTIONS_GETTERS
#include "bolt/Passes/BoltPassesOptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTERS

#endif // BOLT_PASSES_BOLTPASSESOPTIONSOPTINFOS_H
