//===- AnalysisOptionsOptInfos.h - clv2 OptionInfo decls for Analysis --*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// clv2 OptionInfo declarations for the Analysis library command-line flags.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_ANALYSISOPTIONSOPTINFOS_H
#define LLVM_ANALYSIS_ANALYSISOPTIONSOPTINFOS_H

#include "llvm/Support/CommandLineV2.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/OptionsContext.h"

namespace llvm {
class Function;
}

// Enum-only headers: each holds just the enum an option is typed on, so this
// header does not drag in BlockFrequencyInfo.h, CtxProfAnalysis.h, IR2Vec.h,
// ImportedFunctionsInliningStatistics.h or ModuleSummaryIndex.h.
#include "llvm/Analysis/CtxProfPrintMode.h"
#include "llvm/Analysis/GVDAGType.h"
#include "llvm/Analysis/IR2VecKind.h"
#include "llvm/Analysis/Utils/InlinerFunctionImportStatsOpts.h"
#include "llvm/IR/ForceSummaryHotnessType.h"

#define CLV2_OPTIONS_DECL
#include "llvm/Analysis/AnalysisOptionsOptInfos.inc"
#undef CLV2_OPTIONS_DECL

namespace llvm::an_opts {
using ParsedOpts = decltype(clv2::AnalysisOptsReg)::ParsedOptionsT;
} // namespace llvm::an_opts

#include "llvm/IR/Function.h"
#define CLV2_OPTIONS_GETTERS
#include "llvm/Analysis/AnalysisOptionsOptInfos.inc"
#undef CLV2_OPTIONS_GETTERS

#endif // LLVM_ANALYSIS_ANALYSISOPTIONSOPTINFOS_H
