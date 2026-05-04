//===- ForceSummaryHotnessType.h - Summary hotness forcing ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Split out so the generated clv2 options header can name this enum without
// pulling in ModuleSummaryIndex.h for it alone.
//
// This enum was a member of FunctionSummary.  It is unscoped, so a member type
// alias would not carry the enumerators -- FunctionSummary::FSHT_None would
// stop resolving.  It is therefore moved to namespace scope outright and the
// three qualified uses in ModuleSummaryAnalysis.cpp updated.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_FORCESUMMARYHOTNESSTYPE_H
#define LLVM_IR_FORCESUMMARYHOTNESSTYPE_H

namespace llvm {
enum ForceSummaryHotnessType : unsigned {
  FSHT_None,
  FSHT_AllNonCritical,
  FSHT_All
};
} // namespace llvm

#endif // LLVM_IR_FORCESUMMARYHOTNESSTYPE_H
