//===- ScalableForceKind.h - Scalable-vectorization hint --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Split out so the generated clv2 options header can name this enum without
// including LoopVectorizationLegality.h for it alone.
//
// This was a member of LoopVectorizeHints.  It is unscoped, so a member alias
// would not carry the enumerators; it is moved to namespace scope outright.
// The SK_ prefix keeps that safe -- these four names appear nowhere else.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_SCALABLEFORCEKIND_H
#define LLVM_TRANSFORMS_VECTORIZE_SCALABLEFORCEKIND_H

namespace llvm {
enum ScalableForceKind {
  /// Not selected.
  SK_Unspecified = -1,
  /// Disables vectorization with scalable vectors.
  SK_FixedWidthOnly = 0,
  /// Vectorize loops using scalable vectors or fixed-width vectors, but favor
  /// scalable vectors when the cost-model is inconclusive. This is the
  /// default when the scalable.enable hint is enabled through a pragma.
  SK_PreferScalable = 1,
  /// Always vectorize loops using scalable vectors if feasible (i.e. the plan
  /// has a valid cost and is not restricted by fixed-length dependence
  /// distances).
  SK_AlwaysScalable = 2
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_VECTORIZE_SCALABLEFORCEKIND_H
