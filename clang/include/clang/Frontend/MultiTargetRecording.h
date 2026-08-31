//===- MultiTargetRecording.h - Per-target token streams --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Records one token stream per target and aligns them into a single sequence of
// shared and alternative segments.
//
// A combined multi-target frontend parses once. To do that it needs one token
// stream, and the streams the targets produce are not the same: they differ
// wherever a preprocessor conditional resolves differently. They differ very
// little -- measured on ggml-cuda, 98.8-99.5% of tokens are common -- so the
// merged stream is almost all shared, with small alternative regions where the
// targets disagree.
//
// Preprocessing runs once per target rather than once with both macro tables,
// because 33% of target-divergent regions in the ROCm headers contain #define:
// retaining both arms of those in a single pass defines each macro twice, and
// every later use sees whichever won. Separate passes make that problem vanish
// instead of solving it, and preprocessing is only ~7% of a frontend run.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_FRONTEND_MULTITARGETRECORDING_H
#define LLVM_CLANG_FRONTEND_MULTITARGETRECORDING_H

#include "clang/Lex/TokenStreamMerge.h"
#include "clang/Support/Compiler.h"
#include "llvm/ADT/ArrayRef.h"
#include <memory>
#include <string>
#include <vector>

namespace clang {

class CompilerInstance;
class raw_ostream;

/// The recordings, plus the compilations that produced them.
///
/// A recorded Token points at an IdentifierInfo in its preprocessor's
/// identifier table and, for literals, at a buffer in its SourceManager. Both
/// belong to the nested compilation, so it has to outlive any use of the tokens
/// beyond their kind. Only \c Hashes is self-contained.
///
/// Stage 3.4 removes this by running the passes against the primary
/// compilation's SourceManager and re-interning identifiers, which is what a
/// merged stream fed to the parser needs anyway.
struct MultiTargetRecordings {
  std::vector<TargetRecording> Targets;
  std::vector<std::unique_ptr<CompilerInstance>> Owners;

  bool empty() const { return Targets.empty(); }
  size_t size() const { return Targets.size(); }
};

/// Preprocess \p CI's input once per configured target.
///
/// Each pass runs in a nested CompilerInstance whose diagnostics are buffered:
/// the primary compilation reports for the primary target, and repeating every
/// diagnostic once per target would say everything N times.
LLVM_ABI MultiTargetRecordings recordTargetStreams(CompilerInstance &CI);

/// Align \p Recordings into shared and alternative segments.
///
/// Anchors on the conditional structure the preprocessor reported, recursively:
/// a conditional whose contents differ is descended into rather than marked
/// divergent whole, which matters because the outermost conditionals in real
/// headers span hundreds of thousands of tokens. Adjacent alternative segments
/// are then coalesced until their bracket deltas agree, so that a parser
/// leaves an alternative region in the same state whichever branch it takes.
LLVM_ABI std::vector<MergedSegment>
alignRecordings(ArrayRef<TargetRecording> Recordings);

} // namespace clang

#endif
