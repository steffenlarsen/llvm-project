//===- AnalysisOptions.h - Analysis options ---------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// llvm::AnalysisOptions: the plain C++ struct generated from
// AnalysisOptions.td by -gen-opt-parser-defs. See
// llvm/include/llvm/Option/OptParser.td and
// llvm/include/llvm/Option/LibraryOptions.h for the generation and runtime
// mechanism this relies on.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_ANALYSISOPTIONS_H
#define LLVM_ANALYSIS_ANALYSISOPTIONS_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/CtxProfPrintMode.h"
#include "llvm/Analysis/GVDAGType.h"
#include "llvm/Analysis/IR2VecKind.h"
#include "llvm/Analysis/Utils/InlinerFunctionImportStatsOpts.h"
#include "llvm/IR/ForceSummaryHotnessType.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/TypeSize.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>
#include <string>
#include <vector>

namespace llvm {
namespace opt {
class Arg;
class OptTable;
} // namespace opt
} // namespace llvm

#define OPTIONS_STRUCT_DECL
#include "llvm/Analysis/AnalysisOptions.inc"
#undef OPTIONS_STRUCT_DECL

#endif // LLVM_ANALYSIS_ANALYSISOPTIONS_H
