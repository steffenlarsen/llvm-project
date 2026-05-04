//===- ProfCorrelatorKind.h - Profile correlation kind ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Split out of InstrProfCorrelator.h so the generated clv2 options header can
// name this enum without including all of InstrProfCorrelator for it alone
// (8.56 G of instructions in every TU that reads an instrumentation option).
//
// It is scoped where the original was unscoped.  The enumerators are NONE,
// DEBUG_INFO and BINARY, which cannot be moved to namespace scope unscoped
// without polluting llvm:: with three very generic names.  InstrProfCorrelator
// keeps a member alias, so InstrProfCorrelator::ProfCorrelatorKind and
// ProfCorrelatorKind::X both still resolve; only the unqualified
// InstrProfCorrelator::NONE spelling changed.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_PROFILEDATA_PROFCORRELATORKIND_H
#define LLVM_PROFILEDATA_PROFCORRELATORKIND_H

namespace llvm {
/// Indicate if we should use the debug info or profile metadata sections to
/// correlate.
enum class ProfCorrelatorKind { NONE, DEBUG_INFO, BINARY };
} // namespace llvm

#endif // LLVM_PROFILEDATA_PROFCORRELATORKIND_H
