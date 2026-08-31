//===- unittests/Lex/TokenStreamMergeTest.cpp ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// N-way (N=3) coverage for TokenStreamMerge's alignRecordings/Aligner, added
// alongside its generalization from a pairwise-only (N==2) implementation.
// alignRecordings operates purely on TargetRecording data (Tokens/Hashes/
// Regions/RegionKeys) with no live Preprocessor dependency, so every
// TargetRecording below is hand-built with TargetRecording::SM left null --
// buildMergedStream only calls translateToken when a recording's SM is set,
// so a Preprocessor constructed from an empty buffer (never touched for
// content) is enough to satisfy its signature.
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/TokenStreamMerge.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Lex/HeaderSearch.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/ModuleLoader.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/PreprocessorOptions.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"

using namespace clang;

namespace {

/// Appends one "numeric_constant ;" chunk (2 tokens) to \p R, using \p Lit as
/// the numeric literal's text. \p Lit must outlive \p R (test-owned string
/// storage keeps every literal alive for the whole test).
void appendChunk(TargetRecording &R, StringRef Lit) {
  Token Num;
  Num.startToken();
  Num.setKind(tok::numeric_constant);
  Num.setLiteralData(Lit.data());
  Num.setLength(Lit.size());
  R.Tokens.push_back(Num);
  R.Hashes.push_back(hashToken(Num));

  Token Semi;
  Semi.startToken();
  Semi.setKind(tok::semi);
  Semi.setLength(1);
  R.Tokens.push_back(Semi);
  R.Hashes.push_back(hashToken(Semi));
}

/// Appends a single bare punctuator token (no literal data), for building
/// bracket-delta content (l_brace/r_brace/...).
void appendTok(TargetRecording &R, tok::TokenKind K) {
  Token T;
  T.startToken();
  T.setKind(K);
  T.setLength(1);
  R.Tokens.push_back(T);
  R.Hashes.push_back(hashToken(T));
}

/// Appends an annotation token carrying \p Value, for exercising the
/// PragmaScopeClassifier (push/pop pairs) without depending on Sema's
/// concrete pragma-payload types. hashToken ignores an annotation's payload
/// (only its Kind), so a push and a pop of the same TokenKind hash identically
/// -- precisely what makes chunk()'s content-hash resync search blind to
/// them without the classifier.
void appendAnnot(TargetRecording &R, tok::TokenKind K, intptr_t Value) {
  Token T;
  T.startToken();
  T.setKind(K);
  T.setAnnotationEndLoc(SourceLocation());
  T.setAnnotationValue(reinterpret_cast<void *>(Value));
  R.Tokens.push_back(T);
  R.Hashes.push_back(hashToken(T));
}

/// Registers one conditional branch: the tokens \p R already holds in
/// [First,End) belong to the conditional instance named \p Key (identical
/// spelling across recordings ties them together -- see
/// TokenStreamMerge.cpp's collapse(), which keys instances by
/// "file:line:col" + occurrence count). \p ID must be unique within \p R.
void addRegion(TargetRecording &R, StringRef Key, unsigned ID, unsigned First,
              unsigned End) {
  ConditionalRegion Reg;
  Reg.FirstToken = First;
  Reg.EndToken = End;
  Reg.ConditionalID = ID;
  R.Regions.push_back(Reg);
  R.RegionKeys.push_back(Key.str());
}

/// A recording with no conditionals at all: align() can never find a
/// registered split point, so any content difference becomes one single
/// non-shared segment spanning the whole stream -- the simplest way to feed
/// trimAlternatives/balanceAlternatives a real divergent block without
/// engineering #if-driven recursion for tests that don't need it.
TargetRecording freeform(StringRef Triple) {
  TargetRecording R;
  R.Triple = Triple.str();
  return R;
}

Preprocessor &getDummyPP() {
  static DiagnosticOptions DiagOpts;
  static FileSystemOptions FileMgrOpts;
  static FileManager FileMgr(FileMgrOpts);
  static IntrusiveRefCntPtr<DiagnosticIDs> DiagIDs(DiagnosticIDs::create());
  static DiagnosticsEngine Diags(DiagIDs, DiagOpts, new IgnoringDiagConsumer());
  static SourceManager SourceMgr(Diags, FileMgr);
  static LangOptions LangOpts;
  static std::shared_ptr<TargetOptions> TargetOpts(new TargetOptions);
  static IntrusiveRefCntPtr<TargetInfo> Target;
  static HeaderSearchOptions HSOpts;
  static std::unique_ptr<HeaderSearch> HeaderInfo;
  static TrivialModuleLoader ModLoader;
  static PreprocessorOptions PPOpts;
  static std::unique_ptr<Preprocessor> PP;
  if (!PP) {
    TargetOpts->Triple = "x86_64-unknown-linux-gnu";
    Target = TargetInfo::CreateTargetInfo(Diags, *TargetOpts);
    HeaderInfo = std::make_unique<HeaderSearch>(HSOpts, SourceMgr, Diags,
                                                LangOpts, Target.get());
    SourceMgr.setMainFileID(
        SourceMgr.createFileID(llvm::MemoryBuffer::getMemBuffer("")));
    PP = std::make_unique<Preprocessor>(PPOpts, Diags, LangOpts, SourceMgr,
                                        *HeaderInfo, ModLoader,
                                        /*IILookup=*/nullptr,
                                        /*OwnsHeaderSearch=*/false);
    PP->Initialize(*Target);
  }
  return *PP;
}

unsigned totalShared(ArrayRef<MergedSegment> Segments) {
  return llvm::count_if(Segments, [](const MergedSegment &S) { return S.Shared; });
}
unsigned totalAlternative(ArrayRef<MergedSegment> Segments) {
  return llvm::count_if(Segments, [](const MergedSegment &S) { return !S.Shared; });
}

// N=3, everything shared: three identical streams should align to one Shared
// segment and round-trip exactly.
TEST(TokenStreamMergeN3, AllShared) {
  TargetRecording A = freeform("a"), B = freeform("b"), C = freeform("c");
  for (TargetRecording *R : {&A, &B, &C}) {
    appendChunk(*R, "1");
    appendChunk(*R, "2");
  }
  std::vector<TargetRecording> Recs{A, B, C};
  std::vector<MergedSegment> Segments = alignRecordings(Recs);
  ASSERT_EQ(totalShared(Segments), 1u);
  EXPECT_EQ(totalAlternative(Segments), 0u);

  std::vector<Token> Merged = buildMergedStream(getDummyPP(), Recs, Segments);
  std::string Report;
  llvm::raw_string_ostream OS(Report);
  EXPECT_TRUE(verifyRoundTrip(OS, Recs, Merged)) << Report;
}

// N=3, a conditional key present in all three recordings, each taking a
// different branch: clean recursive sync (Stage 3's ordinary path, no
// inactive recordings involved).
TEST(TokenStreamMergeN3, KeyPresentInAllThree) {
  TargetRecording A = freeform("a"), B = freeform("b"), C = freeform("c");
  appendChunk(A, "0"); // shared prefix
  appendChunk(B, "0");
  appendChunk(C, "0");

  unsigned FirstA = A.Tokens.size(), FirstB = B.Tokens.size(),
           FirstC = C.Tokens.size();
  appendChunk(A, "100");
  appendChunk(B, "200");
  appendChunk(C, "300");
  addRegion(A, "cond:1:1", 1, FirstA, A.Tokens.size());
  addRegion(B, "cond:1:1", 1, FirstB, B.Tokens.size());
  addRegion(C, "cond:1:1", 1, FirstC, C.Tokens.size());

  appendChunk(A, "9"); // shared suffix
  appendChunk(B, "9");
  appendChunk(C, "9");

  std::vector<TargetRecording> Recs{A, B, C};
  std::vector<MergedSegment> Segments = alignRecordings(Recs);
  ASSERT_EQ(totalShared(Segments), 2u);
  ASSERT_EQ(totalAlternative(Segments), 1u);
  for (const MergedSegment &S : Segments)
    if (!S.Shared)
      for (const auto &R : S.Ranges)
        EXPECT_EQ(R.second - R.first, 2u);

  std::vector<Token> Merged = buildMergedStream(getDummyPP(), Recs, Segments);
  std::string Report;
  llvm::raw_string_ostream OS(Report);
  EXPECT_TRUE(verifyRoundTrip(OS, Recs, Merged)) << Report;
}

// N=3, a conditional key present in exactly two of three recordings: the
// third (the "host") never enters the outer conditional's body at all, so
// the inner conditional's key is structurally absent from its Conds -- the
// Active-subset scenario Stage 3 exists to handle. Requiring the inner key
// in literally every recording (the pre-Stage-3 rule) would force the whole
// outer span to flatten into one divergent block the moment host's empty
// branch is considered; the fix keeps the divergence scoped to the outer
// conditional's own (small) content.
TEST(TokenStreamMergeN3, KeyPresentInTwoOfThree) {
  TargetRecording Host = freeform("host"), Dev1 = freeform("dev1"),
                  Dev2 = freeform("dev2");
  appendChunk(Host, "0"); // shared prefix
  appendChunk(Dev1, "0");
  appendChunk(Dev2, "0");

  // Host's outer branch is empty: zero tokens, but the conditional itself is
  // still recorded (a skipped branch produces FirstToken == EndToken).
  unsigned OuterHost = Host.Tokens.size();
  addRegion(Host, "outer:1:1", 1, OuterHost, OuterHost);

  // dev1/dev2 both enter the outer conditional's body: a shared wrapper
  // l_paren/r_paren around an inner conditional whose branches differ.
  unsigned OuterDev1 = Dev1.Tokens.size();
  appendTok(Dev1, tok::l_paren);
  unsigned InnerDev1 = Dev1.Tokens.size();
  appendChunk(Dev1, "111");
  addRegion(Dev1, "inner:1:1", 2, InnerDev1, Dev1.Tokens.size());
  appendTok(Dev1, tok::r_paren);
  addRegion(Dev1, "outer:1:1", 1, OuterDev1, Dev1.Tokens.size());

  unsigned OuterDev2 = Dev2.Tokens.size();
  appendTok(Dev2, tok::l_paren);
  unsigned InnerDev2 = Dev2.Tokens.size();
  appendChunk(Dev2, "222");
  addRegion(Dev2, "inner:1:1", 2, InnerDev2, Dev2.Tokens.size());
  appendTok(Dev2, tok::r_paren);
  addRegion(Dev2, "outer:1:1", 1, OuterDev2, Dev2.Tokens.size());

  appendChunk(Host, "9"); // shared suffix
  appendChunk(Dev1, "9");
  appendChunk(Dev2, "9");

  std::vector<TargetRecording> Recs{Host, Dev1, Dev2};
  std::vector<MergedSegment> Segments = alignRecordings(Recs);

  // Prefix and suffix must stay shared: over-flattening would swallow them
  // into one all-divergent block instead.
  ASSERT_EQ(totalShared(Segments), 2u);
  // Divergence must stay scoped to the outer conditional's own content
  // (l_paren + inner-chunk + r_paren = 4 tokens for dev1/dev2, 0 for host),
  // not explode to cover the whole stream.
  uint64_t AltTokensDev1 = 0;
  for (const MergedSegment &S : Segments)
    if (!S.Shared)
      AltTokensDev1 += S.Ranges[1].second - S.Ranges[1].first;
  EXPECT_EQ(AltTokensDev1, 4u);

  std::vector<Token> Merged = buildMergedStream(getDummyPP(), Recs, Segments);
  std::string Report;
  llvm::raw_string_ostream OS(Report);
  EXPECT_TRUE(verifyRoundTrip(OS, Recs, Merged)) << Report;
}

// N=3, trimAlternatives resync: two valid full (N-way) resync candidates
// exist at different total skew. The one reachable at the smallest offset
// in recording 0 (a naive "anchor on stream 0" search would commit to it
// first) has total skew 1+5+5=11; a better one exists at a larger stream-0
// offset with total skew 3+0+1=4. Confirms the implemented symmetric
// content-hash search picks the globally smallest total, not whichever
// stream 0 reaches first.
TEST(TokenStreamMergeN3, TrimAlternativesPicksGlobalMinimum) {
  TargetRecording A = freeform("a"), B = freeform("b"), C = freeform("c");
  for (StringRef Lit : {"n0", "500", "n1", "777", "n2"})
    appendChunk(A, Lit);
  for (StringRef Lit : {"777", "m0", "m1", "m2", "m3", "500"})
    appendChunk(B, Lit);
  for (StringRef Lit : {"p0", "777", "p1", "p2", "p3", "500"})
    appendChunk(C, Lit);
  // No conditionals registered anywhere: align() cannot find a split point,
  // so the whole (differing) stream becomes one non-shared segment, and
  // trimAlternatives is what has to find structure inside it.
  std::vector<TargetRecording> Recs{A, B, C};
  std::vector<MergedSegment> Segments =
      alignRecordings(Recs, /*WidenToDeclarations=*/true);

  bool FoundSharedSeven = false;
  for (const MergedSegment &S : Segments) {
    if (!S.Shared || S.Ranges[0].second - S.Ranges[0].first != 2)
      continue;
    if (StringRef(A.Tokens[S.Ranges[0].first].getLiteralData(),
                 A.Tokens[S.Ranges[0].first].getLength()) == "777") {
      FoundSharedSeven = true;
      // Must also be "777" in B and C at this same shared segment.
      EXPECT_EQ(StringRef(B.Tokens[S.Ranges[1].first].getLiteralData(),
                          B.Tokens[S.Ranges[1].first].getLength()),
               "777");
      EXPECT_EQ(StringRef(C.Tokens[S.Ranges[2].first].getLiteralData(),
                          C.Tokens[S.Ranges[2].first].getLength()),
               "777");
    }
  }
  EXPECT_TRUE(FoundSharedSeven)
      << "expected the lower-total-skew resync point (\"777\") to be found "
        "as a shared segment, not the smaller-stream-0-offset one (\"500\")";

  std::vector<Token> Merged = buildMergedStream(getDummyPP(), Recs, Segments);
  std::string Report;
  llvm::raw_string_ostream OS(Report);
  EXPECT_TRUE(verifyRoundTrip(OS, Recs, Merged)) << Report;
}

// N=3, balanceAlternatives must independently verify every recording's own
// bracket delta reaches {0,0,0}, not just recording 0's -- the pre-existing
// bug this project's N-way rewrite fixed alongside generalizing it. Uses
// real l_brace/r_brace content (unlike the other tests here) so a version
// that only checks recording 0 could plausibly stop too early for the
// others.
TEST(TokenStreamMergeN3, BalanceAlternativesChecksEveryRecording) {
  TargetRecording A = freeform("a"), B = freeform("b"), C = freeform("c");
  for (StringRef Lit : {"1", "2", "3"})
    appendChunk(A, Lit);
  appendTok(A, tok::l_brace);
  for (StringRef Lit : {"4", "5"})
    appendChunk(A, Lit);
  appendTok(A, tok::r_brace);

  for (StringRef Lit : {"10", "20"})
    appendChunk(B, Lit);
  appendTok(B, tok::l_brace);
  appendTok(B, tok::l_brace);
  for (StringRef Lit : {"30", "40", "50"})
    appendChunk(B, Lit);
  appendTok(B, tok::r_brace);
  appendTok(B, tok::r_brace);

  for (StringRef Lit : {"100"})
    appendChunk(C, Lit);
  appendTok(C, tok::l_brace);
  for (StringRef Lit : {"200", "300", "400", "500"})
    appendChunk(C, Lit);
  appendTok(C, tok::r_brace);

  std::vector<TargetRecording> Recs{A, B, C};
  std::vector<MergedSegment> Segments =
      alignRecordings(Recs, /*WidenToDeclarations=*/true);

  // Whatever alignRecordings settled on, every non-shared segment's own
  // bracket delta must be balanced for every recording -- the invariant
  // balanceAlternatives (and trimAlternatives's own Balanced check) exist to
  // guarantee. A regression that only checks recording 0 would leave some
  // other recording's delta nonzero here undetected by that check, but not
  // by this direct per-recording verification.
  for (const MergedSegment &S : Segments) {
    if (S.Shared)
      continue;
    for (unsigned K = 0; K != Recs.size(); ++K) {
      int Depth = 0;
      for (unsigned I = S.Ranges[K].first; I != S.Ranges[K].second; ++I) {
        if (Recs[K].Tokens[I].is(tok::l_brace))
          ++Depth;
        else if (Recs[K].Tokens[I].is(tok::r_brace))
          --Depth;
      }
      EXPECT_EQ(Depth, 0) << "recording " << K << " unbalanced in segment ["
                         << S.Ranges[K].first << "," << S.Ranges[K].second
                         << ")";
    }
  }

  std::vector<Token> Merged = buildMergedStream(getDummyPP(), Recs, Segments);
  std::string Report;
  llvm::raw_string_ostream OS(Report);
  EXPECT_TRUE(verifyRoundTrip(OS, Recs, Merged)) << Report;
}

// N=3, a paired pragma-scope push/pop (modelled on `#pragma pack(push)`/
// `pop`) straddles content that differs per target, with identical content
// following the pop. Without a PragmaScopeClassifier, trimAlternatives's
// content-hash resync search cannot tell push from pop (hashToken ignores an
// annotation's payload) and would happily carve the identical "pop + tail"
// off into its own Shared segment, leaving the push (with the differing
// content before it) in a separate Alt segment -- exactly the shape of the
// real force_cuda_host_device bug this classifier fixes generally: a Shared
// segment fires once and an Alt segment fires once per target arm, so
// splitting a push from its pop across that boundary desyncs whichever
// counter a Parser/Sema uses to track the pragma's depth.
TEST(TokenStreamMergeN3, PragmaScopeNotSplitAcrossBoundary) {
  TargetRecording A = freeform("a"), B = freeform("b"), C = freeform("c");
  for (TargetRecording *R : {&A, &B, &C})
    appendAnnot(*R, tok::annot_pragma_pack, /*Push=*/1);
  unsigned PushIdxA = A.Tokens.size() - 1;

  appendChunk(A, "100");
  appendChunk(B, "200");
  appendChunk(C, "300");

  for (TargetRecording *R : {&A, &B, &C})
    appendAnnot(*R, tok::annot_pragma_pack, /*Pop=*/-1);
  unsigned PopIdxA = A.Tokens.size() - 1;

  for (TargetRecording *R : {&A, &B, &C})
    appendChunk(*R, "9"); // identical tail: what a naive resync would carve out

  auto ClassifyPack = [](const Token &T) -> PragmaScopeEvent {
    if (!T.is(tok::annot_pragma_pack))
      return {};
    return {0,
            static_cast<int>(reinterpret_cast<intptr_t>(T.getAnnotationValue()))};
  };

  std::vector<TargetRecording> Recs{A, B, C};
  std::vector<MergedSegment> Segments =
      alignRecordings(Recs, /*WidenToDeclarations=*/true, ClassifyPack);

  // No segment boundary may fall strictly between the push and its matching
  // pop in recording 0 -- that is the invariant the classifier exists to
  // guarantee, whatever else alignRecordings decides to do with the rest of
  // the stream.
  for (const MergedSegment &S : Segments) {
    for (unsigned Edge : {S.Ranges[0].first, S.Ranges[0].second})
      EXPECT_FALSE(Edge > PushIdxA && Edge <= PopIdxA)
          << "boundary at " << Edge << " splits push (" << PushIdxA
          << ") from pop (" << PopIdxA << ")";
  }

  // Concretely, with the differing "100"/"200"/"300" pinned inside the
  // protected span, push and the divergent content collapse into one Alt
  // segment together with the pop that closes the span (no interior boundary
  // exists between an open push and its own pop). The identical "9" tail is
  // a different matter: the pop token itself closes the last open pragma
  // scope, which is a valid declaration boundary, so the tail is not dragged
  // into the Alt segment with it and instead resyncs into its own trailing
  // Shared segment.
  EXPECT_EQ(totalShared(Segments), 1u);
  EXPECT_EQ(totalAlternative(Segments), 1u);

  std::vector<Token> Merged = buildMergedStream(getDummyPP(), Recs, Segments);
  std::string Report;
  llvm::raw_string_ostream OS(Report);
  EXPECT_TRUE(verifyRoundTrip(OS, Recs, Merged)) << Report;
}

} // namespace
