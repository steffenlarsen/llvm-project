//===- TailFoldingStyle.h - Tail-folding style enum -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Split out of TargetTransformInfo.h so the generated clv2 options header can
// name this enum without including all of TTI for it alone.  TTI.h includes
// this file, so every existing user is unaffected.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_TAILFOLDINGSTYLE_H
#define LLVM_ANALYSIS_TAILFOLDINGSTYLE_H

namespace llvm {
enum class TailFoldingStyle {
  /// Don't use tail folding
  None,
  /// Use predicate only to mask operations on data in the loop.
  /// When the VL is not known to be a power-of-2, this method requires a
  /// runtime overflow check for the i + VL in the loop because it compares the
  /// scalar induction variable against the tripcount rounded up by VL which may
  /// overflow. When the VL is a power-of-2, both the increment and uprounded
  /// tripcount will overflow to 0, which does not require a runtime check
  /// since the loop is exited when the loop induction variable equals the
  /// uprounded trip-count, which are both 0.
  Data,
  /// Same as Data, but avoids using the get.active.lane.mask intrinsic to
  /// calculate the mask and instead implements this with a
  /// splat/stepvector/cmp.
  /// FIXME: Can this kind be removed now that SelectionDAGBuilder expands the
  /// active.lane.mask intrinsic when it is not natively supported?
  DataWithoutLaneMask,
  /// Use predicate to control both data and control flow.
  /// This method always requires a runtime overflow check for the i + VL
  /// increment inside the loop, because it uses the result direclty in the
  /// active.lane.mask to calculate the mask for the next iteration. If the
  /// increment overflows, the mask is no longer correct.
  DataAndControlFlow,
  /// Use predicated EVL instructions for tail-folding.
  /// Indicates that VP intrinsics should be used.
  DataWithEVL,
};
} // namespace llvm

#endif // LLVM_ANALYSIS_TAILFOLDINGSTYLE_H
