//===- ConditionalRegionRecorder.cpp - Conditional token ranges -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/ConditionalRegionRecorder.h"
#include "clang/Lex/Token.h"

using namespace clang;

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
  // unmatched close is possible in malformed input; ignore it rather than
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
