//===- IR2VecKind.h - IR2Vec embedding kind -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Split out so that the generated clv2 options header can name this enum
// without pulling in IR2Vec.h for this alone.  Same rationale as GVDAGType.h.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_IR2VECKIND_H
#define LLVM_ANALYSIS_IR2VECKIND_H

namespace llvm {
enum class IR2VecKind { Symbolic, FlowAware };
} // namespace llvm

#endif // LLVM_ANALYSIS_IR2VECKIND_H
