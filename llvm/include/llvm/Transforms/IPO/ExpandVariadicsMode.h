//===- ExpandVariadicsMode.h - Variadic expansion mode ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Split out so the generated clv2 options header can name this enum without
// including ExpandVariadics.h for it alone.  Same rationale as GVDAGType.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_EXPANDVARIADICSMODE_H
#define LLVM_TRANSFORMS_IPO_EXPANDVARIADICSMODE_H

namespace llvm {
enum class ExpandVariadicsMode {
  Unspecified, // Use the implementation defaults
  Disable,     // Disable the pass entirely
  Optimize,    // Optimise without changing ABI
  Lowering,    // Change variadic calling convention
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_IPO_EXPANDVARIADICSMODE_H
