//===- VectorizeOptionsOptInfos.h - clv2 OptionInfo decls -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_VECTORIZEOPTIONSOPTINFOS_H
#define LLVM_TRANSFORMS_VECTORIZE_VECTORIZEOPTIONSOPTINFOS_H

#include "llvm/Analysis/TailFoldingStyle.h"
#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"

namespace llvm {
class Function;
}

#include "llvm/Support/CommandLineCompat.h"
#include "llvm/Transforms/Vectorize/LoopIdiomVectorizeStyle.h"
#include "llvm/Transforms/Vectorize/ScalableForceKind.h"

#define CLV2_OPTIONS_DECL
#include "llvm/Transforms/Vectorize/VectorizeOptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

#ifdef EXPENSIVE_CHECKS
namespace llvm::clv2 {
inline constexpr OptionInfo<bool> VEC_VPlanVerifyDom{
    "vplan-verify-dom", "verify VPlan dominance", Init{true}};
} // namespace llvm::clv2
#endif

namespace llvm::vec_opts {
using ParsedOpts = decltype(clv2::VectorizeOptsReg)::ParsedOptionsT;
} // namespace llvm::vec_opts

#include "llvm/IR/Function.h"
#define CLV2_OPTIONS_GETTERS
#include "llvm/Transforms/Vectorize/VectorizeOptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTERS

#endif // LLVM_TRANSFORMS_VECTORIZE_VECTORIZEOPTIONSOPTINFOS_H
