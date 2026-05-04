//===- CtxProfPrintMode.h - Contextual-profile print mode -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Split out so the generated clv2 options header can name this enum without
// pulling in CtxProfAnalysis.h for it alone.  CtxProfAnalysisPrinterPass keeps
// a member alias, so the qualified spelling still resolves.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CTXPROFPRINTMODE_H
#define LLVM_ANALYSIS_CTXPROFPRINTMODE_H

namespace llvm {
enum class CtxProfPrintMode { Everything, YAML };
} // namespace llvm

#endif // LLVM_ANALYSIS_CTXPROFPRINTMODE_H
