//===- ConditionalRegionRecorder.h - Conditional token ranges ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Records which tokens each preprocessor conditional branch contributed to a
// recorded token stream.
//
// A combined multi-target frontend preprocesses once per target and has to
// splice the results into one stream for a single parse. Splicing on a diff of
// the streams does not work: diff has no knowledge of the conditional structure
// that caused the divergence, so it cuts regions at arbitrary points and leaves
// them unbalanced. The boundaries the preprocessor already knows -- where each
// #if branch started and stopped producing tokens -- are the ones to splice on.
//
// The preprocessor reports those boundaries through PPCallbacks already; this
// only pairs them with positions in the stream being recorded.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LEX_CONDITIONALREGIONRECORDER_H
#define LLVM_CLANG_LEX_CONDITIONALREGIONRECORDER_H

#include "clang/Basic/SourceLocation.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Support/Compiler.h"
#include "llvm/ADT/SmallVector.h"
#include <vector>

namespace clang {

class Token;

/// The tokens one branch of a conditional contributed to the stream.
///
/// A skipped branch produces none, so \c FirstToken == \c EndToken. That is the
/// signal a splice needs: an empty range on one target and a non-empty range on
/// another is exactly a point of target divergence.
struct ConditionalRegion {
  /// The \#if, \#elif or \#else that opened this branch.
  SourceLocation DirectiveLoc;
  /// The \#if that opened the whole conditional, shared by all its branches.
  SourceLocation IfLoc;
  /// Half-open range of indices into the recorded token stream.
  unsigned FirstToken = 0;
  unsigned EndToken = 0;
  /// Nesting depth, 0 for a conditional at file scope.
  unsigned Depth = 0;
  /// Identifies the \#if this branch belongs to. Source location is not enough:
  /// a header included twice yields two conditionals at the same location, and
  /// treating them as one merges everything between into a single span.
  unsigned ConditionalID = 0;

  unsigned size() const { return EndToken - FirstToken; }
  bool empty() const { return FirstToken == EndToken; }
};

/// Pairs the preprocessor's conditional boundaries with positions in a token
/// stream being recorded alongside it.
///
/// The recorder reads \p Toks.size() as the current position, so it must be
/// installed on the same preprocessor that is filling \p Toks, and \p Toks must
/// outlive it.
class ConditionalRegionRecorder : public PPCallbacks {
  const std::vector<Token> &Toks;
  std::vector<ConditionalRegion> Regions;
  /// Indices into Regions of the branches currently open, innermost last.
  SmallVector<unsigned, 8> OpenBranches;
  unsigned NextConditionalID = 0;

  void openBranch(SourceLocation DirectiveLoc, SourceLocation IfLoc,
                  unsigned ID);
  /// Returns the id of the branch that was closed.
  unsigned closeInnermost();

public:
  explicit ConditionalRegionRecorder(const std::vector<Token> &Toks)
      : Toks(Toks) {}

  ArrayRef<ConditionalRegion> regions() const { return Regions; }

  LLVM_ABI void If(SourceLocation Loc, SourceRange, ConditionValueKind) override;
  LLVM_ABI void Ifdef(SourceLocation Loc, const Token &,
                      const MacroDefinition &) override;
  LLVM_ABI void Ifndef(SourceLocation Loc, const Token &,
                       const MacroDefinition &) override;
  LLVM_ABI void Elif(SourceLocation Loc, SourceRange, ConditionValueKind,
                     SourceLocation IfLoc) override;
  LLVM_ABI void Else(SourceLocation Loc, SourceLocation IfLoc) override;
  LLVM_ABI void Endif(SourceLocation Loc, SourceLocation IfLoc) override;
};

} // namespace clang

#endif
