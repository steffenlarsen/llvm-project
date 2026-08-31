//===- TargetDivergence.h - Multi-target divergence analysis ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Computes, before parsing, which target-dependent properties actually differ
// between the targets of a combined multi-target compilation.
//
// The point is to be able to prove a negative cheaply. A combined frontend has
// to decide, for every cached target-dependent answer, whether it needs one
// entry per target or can share one. Almost everything can be shared: measured
// on x86_64 host vs gfx90a device, exactly one primitive type differs
// (long double). Knowing that up front keeps the shared path free.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_TARGETDIVERGENCE_H
#define LLVM_CLANG_AST_TARGETDIVERGENCE_H

#include "clang/AST/Type.h"
#include "clang/Basic/LLVM.h"
#include "clang/Support/Compiler.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"

namespace clang {
class RecordDecl;
class TargetInfo;
class raw_ostream;

/// Which target-dependent properties differ across a set of targets.
class TargetDivergence {
  /// Indexed by BuiltinType::Kind.
  llvm::BitVector DivergentBuiltins;
  bool AnyDivergence = false;

public:
  /// Compare \p Targets and record what differs. With fewer than two targets
  /// nothing can diverge.
  LLVM_ABI explicit TargetDivergence(ArrayRef<const TargetInfo *> Targets);

  /// Does anything at all differ? When false the frontend can treat every
  /// target-dependent answer as shared, and pay nothing for multi-target
  /// support.
  bool any() const { return AnyDivergence; }

  /// Can this builtin type's layout differ between targets?
  LLVM_ABI bool isDivergent(BuiltinType::Kind K) const;

  /// Can this type's layout differ between targets? Conservative: a type is
  /// divergent if it is, or transitively contains, a divergent builtin.
  LLVM_ABI bool isLayoutDivergent(QualType T) const;

  /// Can this record's layout differ between targets? True if any field is,
  /// or transitively contains, a divergent builtin.
  LLVM_ABI bool isLayoutDivergent(const RecordDecl *RD) const;

  /// Human-readable summary, for -Rtarget-divergence and for header authors
  /// asking "where does my code depend on the target?".
  LLVM_ABI void print(raw_ostream &OS) const;
};

} // namespace clang

#endif
