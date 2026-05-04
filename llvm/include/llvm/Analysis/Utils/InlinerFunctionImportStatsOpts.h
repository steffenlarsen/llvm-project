//===- InlinerFunctionImportStatsOpts.h - Inliner import-stats verbosity -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Split out so that the generated clv2 options header can name this enum
// without pulling in ImportedFunctionsInliningStatistics.h for this alone. Same
// rationale as GVDAGType.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_UTILS_INLINERFUNCTIONIMPORTSTATSOPTS_H
#define LLVM_ANALYSIS_UTILS_INLINERFUNCTIONIMPORTSTATSOPTS_H

namespace llvm {
enum class InlinerFunctionImportStatsOpts {
  No = 0,
  Basic = 1,
  Verbose = 2,
};
} // namespace llvm

#endif // LLVM_ANALYSIS_UTILS_INLINERFUNCTIONIMPORTSTATSOPTS_H
