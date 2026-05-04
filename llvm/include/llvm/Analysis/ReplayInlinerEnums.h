//===- ReplayInlinerEnums.h - Replay-inliner option enums -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Split out so the generated clv2 options header can name these without
// including ReplayInlineAdvisor.h for them alone.  CallSiteFormat and
// ReplayInlinerSettings keep member aliases; all three are scoped enums, so
// the qualified spellings and Enum::Value access still work.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_REPLAYINLINERENUMS_H
#define LLVM_ANALYSIS_REPLAYINLINERENUMS_H

namespace llvm {
enum class CallSiteFormatKind : int {
  Line,
  LineColumn,
  LineDiscriminator,
  LineColumnDiscriminator
};
enum class ReplayInlinerScope : int { Function, Module };
enum class ReplayInlinerFallback : int { Original, AlwaysInline, NeverInline };
} // namespace llvm

#endif // LLVM_ANALYSIS_REPLAYINLINERENUMS_H
