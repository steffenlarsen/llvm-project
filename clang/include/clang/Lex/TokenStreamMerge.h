//===- TokenStreamMerge.h - Merge per-target token streams ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Aligns the token streams several targets produce for the same source, and
// splices them into the single stream a combined frontend parses.
//
// The streams differ only where a preprocessor conditional resolves
// differently -- measured on ggml-cuda, 96-99% of tokens are common -- so the
// merged stream is almost all shared, with small alternative regions bracketed
// by annot_target_alt_{begin,sep,end}.
//
// This lives in Lex because it needs nothing but tokens and the conditional
// structure the preprocessor reported. Producing the recordings needs a
// CompilerInstance and lives in Frontend.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_TOKENSTREAMMERGE_H
#define LLVM_CLANG_LEX_TOKENSTREAMMERGE_H

#include "clang/Lex/ConditionalRegionRecorder.h"
#include "clang/Lex/Token.h"
#include "clang/Support/Compiler.h"
#include "llvm/ADT/ArrayRef.h"
#include <string>
#include <vector>

namespace clang {

class Preprocessor;
class SourceManager;
class raw_ostream;

/// One target's view of the translation unit.
///
/// A recorded Token carries no storage: its identifier belongs to the
/// preprocessor that lexed it and its literal data points into that
/// preprocessor's SourceManager. \c SM identifies which, so tokens can be
/// translated into another compilation; only \c Hashes is self-contained.
struct TargetRecording {
  std::string Triple;
  std::vector<Token> Tokens;
  /// Content hash per token, comparable across passes. Identifier and literal
  /// *text* is hashed; the pointers in a Token are not comparable.
  std::vector<uint64_t> Hashes;
  std::vector<ConditionalRegion> Regions;
  /// "file:line:col" of each region's opening \#if, parallel to \c Regions.
  /// Presumed locations anchor the passes to each other, because their raw
  /// SourceLocations belong to different SourceManagers.
  std::vector<std::string> RegionKeys;
  /// Where this recording's tokens live. Null for the primary recording, which
  /// is already in the compilation being parsed.
  const SourceManager *SM = nullptr;
};

/// A run of the merged stream: either common to every target, or one range per
/// target where they disagree.
struct MergedSegment {
  bool Shared = false;
  /// Half-open token range in each recording, indexed the same way.
  std::vector<std::pair<unsigned, unsigned>> Ranges;
};

/// Content hash of a token, comparable between preprocessors.
LLVM_ABI uint64_t hashToken(const Token &T);

/// Align \p Recordings into shared and alternative segments.
///
/// Anchors on the conditional structure the preprocessor reported, recursively:
/// a conditional whose contents differ is descended into rather than marked
/// divergent whole, which matters because the outermost conditionals in real
/// headers span hundreds of thousands of tokens. Adjacent alternative segments
/// are then coalesced until their bracket deltas agree, so a parser leaves an
/// alternative region in the same state whichever branch it takes.
///
/// \param WidenToDeclarations grow each divergent region until it starts and
/// ends between top-level declarations, so each alternative is a sequence of
/// complete declarations that a recursive-descent parser can parse on its own.
/// Costs the shared text swept up along the way.
LLVM_ABI std::vector<MergedSegment>
alignRecordings(ArrayRef<TargetRecording> Recordings,
                bool WidenToDeclarations = false);

/// Build the single stream a combined frontend parses.
///
/// Shared segments contribute \c Recordings[0]'s tokens once; divergent regions
/// carry each target's tokens in turn, bracketed by the annot_target_alt_*
/// annotations.
///
/// Tokens from a recording with a \c SM are rewritten to belong to \p PP's
/// compilation: identifiers are re-interned in its table and locations mapped
/// into its SourceManager. Only the divergent regions need this, so the cost is
/// proportional to how much the targets disagree rather than to the stream.
LLVM_ABI std::vector<Token>
buildMergedStream(Preprocessor &PP, ArrayRef<TargetRecording> Recordings,
                  ArrayRef<MergedSegment> Segments);

/// Extract the stream target \p Which would have seen, by taking its
/// alternative from every divergent region.
LLVM_ABI std::vector<Token> selectTarget(ArrayRef<Token> Merged, unsigned Which);

/// Check that every target reads back out of \p Merged exactly, and report.
/// Content is compared, not source location: a shared token is stored once and
/// necessarily carries one target's location.
LLVM_ABI bool verifyRoundTrip(raw_ostream &OS,
                              ArrayRef<TargetRecording> Recordings,
                              ArrayRef<Token> Merged);

/// Report what the merge would cost if divergent regions had to be widened to
/// whole top-level declarations.
///
/// A recursive-descent parser cannot fork mid-construct, and divergent regions
/// do not respect declaration boundaries -- the common
/// \code
///   #if AMD_MFMA_AVAILABLE
///           if (i < I) {
///   #else
///           {
///   #endif
/// \endcode
/// idiom puts one inside a function body. Parsing each alternative separately
/// therefore means widening it until it starts and ends between declarations,
/// which duplicates everything in between. This measures how much.
LLVM_ABI void reportDeclarationGranularity(raw_ostream &OS,
                                           ArrayRef<TargetRecording> Recordings,
                                           ArrayRef<MergedSegment> Segments);

/// Report how far divergence would spread if shared code that references a
/// divergent entity had to be parsed per target too.
///
/// Shared code is parsed once, so a reference in it to an entity that differs
/// between targets can only mean one of them. Making that correct means
/// treating such code as divergent as well -- and then the code referencing
/// *it* becomes divergent, and so on. Whether that closure stays small or
/// swallows the translation unit decides the design, so it is measured before
/// anything is built on it.
LLVM_ABI void reportDivergenceClosure(raw_ostream &OS,
                                      ArrayRef<TargetRecording> Recordings,
                                      ArrayRef<MergedSegment> Segments);

/// Summarise an alignment: segment counts, shared fraction, and whether the
/// splice invariant holds.
LLVM_ABI void reportAlignment(raw_ostream &OS,
                              ArrayRef<TargetRecording> Recordings,
                              ArrayRef<MergedSegment> Segments);

} // namespace clang

#endif
