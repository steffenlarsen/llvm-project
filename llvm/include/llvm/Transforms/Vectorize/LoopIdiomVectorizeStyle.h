//===- LoopIdiomVectorizeStyle.h - Loop-idiom vectorize style ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Split out so the generated clv2 options header can name this enum without
// including LoopIdiomVectorize.h for it alone.  Same rationale as GVDAGType.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_LOOPIDIOMVECTORIZESTYLE_H
#define LLVM_TRANSFORMS_VECTORIZE_LOOPIDIOMVECTORIZESTYLE_H

namespace llvm {
enum class LoopIdiomVectorizeStyle { Masked, Predicated };
} // namespace llvm

#endif // LLVM_TRANSFORMS_VECTORIZE_LOOPIDIOMVECTORIZESTYLE_H
