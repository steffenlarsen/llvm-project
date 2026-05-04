//===- GVDAGType.h - Graph-viewer DAG type enum -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_GVDAGTYPE_H
#define LLVM_ANALYSIS_GVDAGTYPE_H

namespace llvm {
enum GVDAGType { GVDT_None, GVDT_Fraction, GVDT_Integer, GVDT_Count };

enum PGOViewCountsType { PGOVCT_None, PGOVCT_Graph, PGOVCT_Text };
} // namespace llvm

#endif // LLVM_ANALYSIS_GVDAGTYPE_H
