//===- ConditionalRegionRecorder.cpp - Conditional token ranges -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/ConditionalRegionRecorder.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/Token.h"
#include "clang/Lex/TokenStreamMerge.h"
#include "llvm/ADT/Hashing.h"

using namespace clang;

void ConditionalRegionRecorder::fingerprintPragmas(Preprocessor &P) {
  PP = &P;
  PrevWatcher = P.takeTokenWatcher();
  PrevPreprocessToken = P.getPreprocessToken();
  P.setTokenWatcher([this](const Token &T) {
    // The tokens a pragma handler lexes reach the previous watcher only if it
    // asked for them.
    if (PrevWatcher && (PrevPreprocessToken || !PP->isLexingNestedToken()))
      PrevWatcher(T);
    watchToken(T);
  });
}

void ConditionalRegionRecorder::stopFingerprintingPragmas() {
  if (!PP)
    return;
  if (InPragma)
    closePragma();
  PP->setTokenWatcher(std::move(PrevWatcher));
  PP = nullptr;
}

void ConditionalRegionRecorder::PragmaDirective(SourceLocation,
                                                PragmaIntroducerKind) {
  // A _Pragma a handler expands is part of the pragma being hashed.
  if (!PP || InPragma)
    return;
  InPragma = true;
  OpenPragmaAt = Toks.size();
  OpenFingerprint = 0;
  // The watcher only sees the tokens a handler lexes with this set.
  PP->setPreprocessToken(true);
}

void ConditionalRegionRecorder::watchToken(const Token &T) {
  if (!InPragma)
    return;
  // A handler that stops early leaves no eod to end on. Its pragma ends at
  // the next recorded token at the latest.
  if (Toks.size() != OpenPragmaAt) {
    closePragma();
    return;
  }
  // Some handlers lex identifiers raw, before any lookup gives them an
  // IdentifierInfo.
  OpenFingerprint = llvm::hash_combine(
      OpenFingerprint, T.is(tok::raw_identifier)
                           ? llvm::hash_value(T.getRawIdentifier())
                           : llvm::hash_code(hashToken(T)));
  if (T.is(tok::eod))
    closePragma();
}

void ConditionalRegionRecorder::closePragma() {
  uint64_t &F = PragmaFingerprints[OpenPragmaAt];
  F = llvm::hash_combine(F, OpenFingerprint);
  InPragma = false;
  PP->setPreprocessToken(PrevPreprocessToken);
}

void ConditionalRegionRecorder::openBranch(SourceLocation DirectiveLoc,
                                           SourceLocation IfLoc, unsigned ID) {
  ConditionalRegion R;
  R.DirectiveLoc = DirectiveLoc;
  R.IfLoc = IfLoc;
  R.FirstToken = R.EndToken = Toks.size();
  R.Depth = OpenBranches.size();
  R.ConditionalID = ID;
  OpenBranches.push_back(Regions.size());
  Regions.push_back(R);
}

unsigned ConditionalRegionRecorder::closeInnermost() {
  // A conditional opened inside a skipped block is never reported, so an
  // unmatched close is possible in malformed input. Ignore it rather than
  // corrupt the stack.
  if (OpenBranches.empty())
    return 0;
  ConditionalRegion &R = Regions[OpenBranches.pop_back_val()];
  R.EndToken = Toks.size();
  return R.ConditionalID;
}

void ConditionalRegionRecorder::If(SourceLocation Loc, SourceRange,
                                   ConditionValueKind) {
  openBranch(Loc, Loc, ++NextConditionalID);
}

void ConditionalRegionRecorder::Ifdef(SourceLocation Loc, const Token &,
                                      const MacroDefinition &) {
  openBranch(Loc, Loc, ++NextConditionalID);
}

void ConditionalRegionRecorder::Ifndef(SourceLocation Loc, const Token &,
                                       const MacroDefinition &) {
  openBranch(Loc, Loc, ++NextConditionalID);
}

void ConditionalRegionRecorder::Elif(SourceLocation Loc, SourceRange,
                                     ConditionValueKind, SourceLocation IfLoc) {
  unsigned ID = closeInnermost();
  openBranch(Loc, IfLoc, ID);
}

void ConditionalRegionRecorder::Else(SourceLocation Loc, SourceLocation IfLoc) {
  unsigned ID = closeInnermost();
  openBranch(Loc, IfLoc, ID);
}

void ConditionalRegionRecorder::Endif(SourceLocation, SourceLocation) {
  closeInnermost();
}
