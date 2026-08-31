//===--- MultiTargetDebugOptions.h - Multi-target debugging aids -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The -mllvm options that dump, trace or check what a compilation for several
// targets at once does. None of them changes the output.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_BASIC_MULTITARGETDEBUGOPTIONS_H
#define LLVM_CLANG_BASIC_MULTITARGETDEBUGOPTIONS_H

#include "llvm/Support/CommandLine.h"
#include <string>

namespace clang::multitarget {
extern llvm::cl::opt<std::string> MultiTargetCodeGenDumpDir;
extern llvm::cl::opt<bool> RecordReplayTokens;
extern llvm::cl::opt<bool> ReportVariantEquivalence;
extern llvm::cl::opt<int> DumpDeclFingerprints;
extern llvm::cl::opt<bool> DumpTokenStream;
extern llvm::cl::opt<bool> VerifyIdenticalAlternatives;
extern llvm::cl::opt<std::string> TraceTaggedDecl;
extern llvm::cl::opt<bool> DumpConditionalRegions;
extern llvm::cl::opt<bool> TraceReparse;
extern llvm::cl::opt<std::string> MultiTargetAudit;
extern llvm::cl::opt<std::string> MultiTargetAuditJoins;
} // namespace clang::multitarget

#endif // LLVM_CLANG_BASIC_MULTITARGETDEBUGOPTIONS_H
