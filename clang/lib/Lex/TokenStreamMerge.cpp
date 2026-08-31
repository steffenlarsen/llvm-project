//===- TokenStreamMerge.cpp - Merge per-target token streams --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Lex/TokenStreamMerge.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

uint64_t clang::hashToken(const Token &T) {
  uint64_t H = llvm::hash_value(static_cast<unsigned>(T.getKind()));
  // An annotation token's PtrData is its payload; the identifier and literal
  // accessors only assert on that, and asserts are off in release builds.
  if (T.isAnnotation())
    return H;
  if (T.isLiteral()) {
    if (const char *Data = T.getLiteralData())
      H = llvm::hash_combine(H, StringRef(Data, T.getLength()));
  } else if (const IdentifierInfo *II = T.getIdentifierInfo()) {
    H = llvm::hash_combine(H, II->getName());
  }
  return H;
}

//===----------------------------------------------------------------------===//
// Alignment
//===----------------------------------------------------------------------===//

namespace {

/// A conditional as a single span, collapsing its branches.
struct Conditional {
  unsigned Key;
  unsigned First;
  unsigned End;
};

/// Collapse branches into one span per conditional *instance*, sorted by start,
/// widest first.
///
/// Instances are matched across passes by (source location, how many times that
/// location has been seen so far). Location alone is not a key: a header
/// included twice contributes two conditionals at the same line, and merging
/// them spans everything in between -- observed as three bogus 100-200 K token
/// "divergent" regions before this was keyed properly.
static std::vector<Conditional>
collapse(const TargetRecording &R, llvm::StringMap<unsigned> &Keys) {
  llvm::DenseMap<unsigned, Conditional> ByInstance;
  llvm::DenseMap<unsigned, std::string> InstanceKey;
  llvm::StringMap<unsigned> Occurrences;
  SmallVector<unsigned, 64> Order;

  for (auto [I, Region] : llvm::enumerate(R.Regions)) {
    if (R.RegionKeys[I].empty() || Region.ConditionalID == 0)
      continue;
    auto It = ByInstance.find(Region.ConditionalID);
    if (It == ByInstance.end()) {
      unsigned N = Occurrences[R.RegionKeys[I]]++;
      InstanceKey[Region.ConditionalID] =
          (R.RegionKeys[I] + "#" + llvm::Twine(N)).str();
      ByInstance[Region.ConditionalID] = {0, Region.FirstToken,
                                          Region.EndToken};
      Order.push_back(Region.ConditionalID);
    } else {
      It->second.First = std::min(It->second.First, Region.FirstToken);
      It->second.End = std::max(It->second.End, Region.EndToken);
    }
  }

  std::vector<Conditional> Out;
  Out.reserve(Order.size());
  for (unsigned ID : Order) {
    Conditional C = ByInstance[ID];
    C.Key = Keys.try_emplace(InstanceKey[ID], Keys.size()).first->second;
    Out.push_back(C);
  }
  llvm::sort(Out, [](const Conditional &A, const Conditional &B) {
    if (A.First != B.First)
      return A.First < B.First;
    return A.End > B.End;
  });
  return Out;
}

class Aligner {
  ArrayRef<TargetRecording> Recs;
  bool WidenToDeclarations;
  PragmaScopeClassifier Classify;
  std::vector<std::vector<Conditional>> Conds;
  std::vector<MergedSegment> Segments;

  bool
  equalRange(ArrayRef<std::pair<unsigned, unsigned>> Windows) const {
    unsigned Len = Windows[0].second - Windows[0].first;
    for (unsigned K = 1; K != Windows.size(); ++K)
      if (Windows[K].second - Windows[K].first != Len)
        return false;
    for (unsigned K = 1; K != Windows.size(); ++K)
      if (!std::equal(Recs[0].Hashes.begin() + Windows[0].first,
                      Recs[0].Hashes.begin() + Windows[0].second,
                      Recs[K].Hashes.begin() + Windows[K].first))
        return false;
    return true;
  }

  /// Conditionals lying immediately inside [Lo,Hi): contained, not nested in
  /// another, and not already entered on this path. Nested conditionals can
  /// share a span, and without the visited set they alternate as each other's
  /// child forever.
  std::vector<Conditional> childrenOf(unsigned Which, unsigned Lo, unsigned Hi,
                                      const llvm::DenseSet<unsigned> &Seen) const {
    std::vector<Conditional> Out;
    unsigned End = Lo;
    for (const Conditional &C : Conds[Which]) {
      if (Seen.count(C.Key) || C.First < Lo || C.End > Hi || C.First < End)
        continue;
      Out.push_back(C);
      End = C.End;
    }
    return Out;
  }

  void emit(bool Shared, ArrayRef<std::pair<unsigned, unsigned>> Windows) {
    if (llvm::all_of(Windows, [](const std::pair<unsigned, unsigned> &W) {
          return W.first == W.second;
        }))
      return;
    Segments.push_back({Shared, std::vector<std::pair<unsigned, unsigned>>(
                                     Windows.begin(), Windows.end())});
  }

  /// \p Windows holds one [Lo,Hi) window per recording. A recording whose
  /// window is empty is inactive for this call -- it contributes a frozen
  /// empty range and is excluded from key-agreement, rather than forcing
  /// every divergent region to flatten whenever a nested conditional is
  /// structurally absent from one recording's own conditional list (the
  /// common `#if HOST ... #if DEVICE_ONLY_NESTED ... #endif ... #endif`
  /// shape, where the host's Conds simply never records the inner key).
  /// `Shared` segments are unaffected by this: equalRange still requires
  /// every window -- inactive ones included -- to agree, so an inactive
  /// recording can never be silently folded into a Shared span.
  void align(SmallVector<std::pair<unsigned, unsigned>, 8> Windows,
             llvm::DenseSet<unsigned> Seen, unsigned Depth = 0) {
    unsigned N = Windows.size();
    // Conditional nesting is shallow in practice (deepest observed: 11), so a
    // runaway here is a bug in the child selection rather than real input.
    if (Depth > 256 ||
        llvm::any_of(Windows, [](const std::pair<unsigned, unsigned> &W) {
          return W.second < W.first;
        })) {
      emit(false, Windows);
      return;
    }
    if (equalRange(Windows)) {
      emit(true, Windows);
      return;
    }

    SmallVector<unsigned, 8> Active;
    for (unsigned K = 0; K != N; ++K)
      if (Windows[K].second > Windows[K].first)
        Active.push_back(K);
    // equalRange above already covers "every window is empty" (equal, all-
    // zero lengths), so at least one recording here has real tokens left.
    unsigned Driver = Active.front();

    std::vector<Conditional> DriverChildren =
        childrenOf(Driver, Windows[Driver].first, Windows[Driver].second, Seen);
    SmallVector<llvm::DenseMap<unsigned, Conditional>, 8> ChildMaps(N);
    for (unsigned K : Active) {
      if (K == Driver)
        continue;
      for (const Conditional &C :
           childrenOf(K, Windows[K].first, Windows[K].second, Seen))
        ChildMaps[K][C.Key] = C;
    }

    SmallVector<unsigned, 8> Pos(N);
    for (unsigned K = 0; K != N; ++K)
      Pos[K] = Windows[K].first;

    bool Matched = false;
    for (const Conditional &C : DriverChildren) {
      if (C.First < Pos[Driver])
        continue;
      SmallVector<std::pair<unsigned, unsigned>, 8> Spans(N);
      Spans[Driver] = {C.First, C.End};
      bool AllMatch = true;
      for (unsigned K : Active) {
        if (K == Driver)
          continue;
        auto It = ChildMaps[K].find(C.Key);
        if (It == ChildMaps[K].end() || It->second.First < Pos[K]) {
          AllMatch = false;
          break;
        }
        Spans[K] = {It->second.First, It->second.End};
      }
      if (!AllMatch)
        continue;
      for (unsigned K = 0; K != N; ++K)
        if (!llvm::is_contained(Active, K))
          Spans[K] = {Pos[K], Pos[K]}; // frozen, inactive pass-through

      Matched = true;
      SmallVector<std::pair<unsigned, unsigned>, 8> Gap(N);
      for (unsigned K = 0; K != N; ++K)
        Gap[K] = {Pos[K], Spans[K].first};
      align(Gap, Seen, Depth + 1);

      if (equalRange(Spans)) {
        emit(true, Spans);
      } else {
        llvm::DenseSet<unsigned> Inner = Seen;
        Inner.insert(C.Key);
        align(Spans, std::move(Inner), Depth + 1);
      }
      for (unsigned K = 0; K != N; ++K)
        Pos[K] = Spans[K].second;
    }
    if (!Matched) {
      emit(false, Windows);
      return;
    }
    SmallVector<std::pair<unsigned, unsigned>, 8> Tail(N);
    for (unsigned K = 0; K != N; ++K)
      Tail[K] = {Pos[K], Windows[K].second};
    align(Tail, std::move(Seen), Depth + 1);
  }

  /// Bracket delta over a token range.
  std::array<int, 3> delta(unsigned Which, unsigned Lo, unsigned Hi) const {
    std::array<int, 3> D{0, 0, 0};
    // Coalescing spans from one segment's start to another's end; if the two
    // ever arrive out of order the loop would run off the buffer.
    if (Hi <= Lo || Hi > Recs[Which].Tokens.size())
      return D;
    for (unsigned I = Lo; I != Hi; ++I) {
      switch (Recs[Which].Tokens[I].getKind()) {
      case tok::l_brace:  ++D[0]; break;
      case tok::r_brace:  --D[0]; break;
      case tok::l_paren:  ++D[1]; break;
      case tok::r_paren:  --D[1]; break;
      case tok::l_square: ++D[2]; break;
      case tok::r_square: --D[2]; break;
      default: break;
      }
    }
    return D;
  }

  /// The recursion can split one logical divergence into neighbouring segments
  /// that are each unbalanced but agree once combined. Absorb forward until
  /// they do.
  void coalesce() {
    unsigned N = Recs.size();
    std::vector<MergedSegment> Out;
    for (unsigned I = 0; I != Segments.size();) {
      if (Segments[I].Shared) {
        Out.push_back(Segments[I++]);
        continue;
      }
      unsigned J = I;
      auto agrees = [&] {
        std::array<int, 3> D0 = delta(0, Segments[I].Ranges[0].first,
                                      Segments[J].Ranges[0].second);
        for (unsigned K = 1; K != N; ++K)
          if (delta(K, Segments[I].Ranges[K].first,
                    Segments[J].Ranges[K].second) != D0)
            return false;
        return true;
      };
      while (J + 1 < Segments.size() && !agrees())
        ++J;
      std::vector<std::pair<unsigned, unsigned>> Ranges(N);
      for (unsigned K = 0; K != N; ++K)
        Ranges[K] = {Segments[I].Ranges[K].first, Segments[J].Ranges[K].second};
      Out.push_back({false, std::move(Ranges)});
      I = J + 1;
    }
    Segments = std::move(Out);
  }

  /// Whether a top-level declaration can end just before \p P in stream
  /// \p Which.
  ///
  /// Only ever called with \p Which == 0. That is sufficient even though
  /// widening also moves the other streams' boundaries in lockstep: it only
  /// ever cuts inside a *Shared* run, where every stream's tokens equal
  /// stream 0's by construction (that is what made the run Shared), so
  /// stream 0's pragma-scope depth there is every stream's.
  std::vector<bool> declarationBoundaries(unsigned Which = 0) const {
    ArrayRef<Token> Toks = Recs[Which].Tokens;
    std::vector<bool> B(Toks.size() + 1, false);
    B.front() = B.back() = true;
    unsigned D = 0;
    std::array<int, NumPragmaScopeKinds> PragmaDepth{};
    for (auto [I, T] : llvm::enumerate(Toks)) {
      if (T.is(tok::l_brace))
        ++D;
      else if (T.is(tok::r_brace) && D)
        --D;
      PragmaScopeEvent Ev = Classify(T);
      bool ClosedPragmaScope = false;
      if (Ev.Kind >= 0) {
        PragmaDepth[Ev.Kind] += Ev.Delta;
        if (Ev.Delta < 0 && PragmaDepth[Ev.Kind] == 0)
          ClosedPragmaScope = true;
      }
      // A `}` that a `;` follows does not end the declaration; the `;` does.
      // Widening a region inside `struct X { ... };` to the gap between them
      // left the `;` outside the alternative, and every arm then ended with
      // "expected ';' after struct". A cut point also cannot fall inside an
      // open pragma scope (e.g. `pack(push)`/`pack(pop)`): a Shared run fires
      // once and an Alt run fires once per target arm, so splitting a push
      // from its pop across that boundary desyncs the counter Sema tracks it
      // with. The token that closes the *last* open pragma scope is itself a
      // valid boundary -- without this, a divergent region with nothing
      // cuttable between its closing pragma pop and true end of file (no
      // trailing `;`/`}`) swept that stream's own end-of-file token into the
      // alternative, and replaying it fed the parser a real `tok::eof` in the
      // middle of the merged stream: TokenLexer::Lex delivers whatever token
      // is at that array index verbatim, so the parser silently stopped
      // there and every later alternative's declarations were dropped with
      // no diagnostic.
      if (D == 0 && PragmaDepth == std::array<int, NumPragmaScopeKinds>{} &&
          (T.is(tok::semi) ||
           (T.is(tok::r_brace) &&
            (I + 1 == Toks.size() || !Toks[I + 1].is(tok::semi))) ||
           ClosedPragmaScope))
        B[I + 1] = true;
    }
    return B;
  }

  /// Grow every divergent region until it starts and ends between top-level
  /// declarations.
  ///
  /// A recursive-descent parser cannot fork mid-construct, and divergent
  /// regions routinely start inside a function body -- the ggml idiom is
  /// `#if ... if (i < I) { #else { #endif`. Widening lets each alternative be
  /// parsed as a sequence of complete declarations, at the cost of duplicating
  /// the shared text swept up along the way.
  void widenToDeclarations() {
    if (Segments.empty())
      return;
    std::vector<bool> Boundary = declarationBoundaries();
    unsigned N = Recs.size();

    std::vector<MergedSegment> Out;
    for (unsigned I = 0; I != Segments.size();) {
      if (Segments[I].Shared) {
        Out.push_back(Segments[I++]);
        continue;
      }
      std::vector<unsigned> Lo(N), Hi(N);
      for (unsigned K = 0; K != N; ++K) {
        Lo[K] = Segments[I].Ranges[K].first;
        Hi[K] = Segments[I].Ranges[K].second;
      }

      // Absorb backwards. A preceding shared run maps one-to-one between the
      // streams, so a boundary inside it moves every recording's end by the
      // same amount (measured against recording 0, which is what `Boundary`
      // was built from).
      while (!Boundary[Lo[0]] && !Out.empty()) {
        MergedSegment &Prev = Out.back();
        if (!Prev.Shared) {
          // Two divergent regions in one declaration: they become one.
          for (unsigned K = 0; K != N; ++K)
            Lo[K] = Prev.Ranges[K].first;
          Out.pop_back();
          continue;
        }
        unsigned A = Prev.Ranges[0].first;
        unsigned P = Lo[0];
        while (P > A && !Boundary[P])
          --P;
        for (unsigned K = 1; K != N; ++K)
          Lo[K] -= Lo[0] - P;
        Lo[0] = P;
        if (P > A) {
          Prev.Ranges[0].second = P;
          for (unsigned K = 1; K != N; ++K)
            Prev.Ranges[K].second = Prev.Ranges[K].first + (P - A);
          break;
        }
        for (unsigned K = 1; K != N; ++K)
          Lo[K] = Prev.Ranges[K].first;
        Out.pop_back();
      }

      // Absorb forwards, the same way.
      unsigned J = I + 1;
      bool Advanced = false;
      while (!Boundary[Hi[0]] && J != Segments.size()) {
        const MergedSegment &Next = Segments[J];
        if (!Next.Shared) {
          for (unsigned K = 0; K != N; ++K)
            Hi[K] = Next.Ranges[K].second;
          ++J;
          continue;
        }
        unsigned B = Next.Ranges[0].second;
        unsigned P = Hi[0];
        while (P < B && !Boundary[P])
          ++P;
        for (unsigned K = 1; K != N; ++K)
          Hi[K] += P - Hi[0];
        Hi[0] = P;
        ++J;
        if (P < B) {
          // The rest of that shared run survives.
          MergedSegment Rest = Next;
          Rest.Ranges[0].first = P;
          for (unsigned K = 1; K != N; ++K)
            Rest.Ranges[K].first = Next.Ranges[K].first + (P - Next.Ranges[0].first);
          std::vector<std::pair<unsigned, unsigned>> Ranges(N);
          for (unsigned K = 0; K != N; ++K)
            Ranges[K] = {Lo[K], Hi[K]};
          Out.push_back({false, std::move(Ranges)});
          Out.push_back(Rest);
          Lo[0] = Hi[0]; // consumed
          Advanced = true;
          break;
        }
        for (unsigned K = 1; K != N; ++K)
          Hi[K] = Next.Ranges[K].second;
      }
      if (!Advanced) {
        std::vector<std::pair<unsigned, unsigned>> Ranges(N);
        for (unsigned K = 0; K != N; ++K)
          Ranges[K] = {Lo[K], Hi[K]};
        Out.push_back({false, std::move(Ranges)});
      }
      I = J;
    }
    Segments = std::move(Out);
  }


  /// Does the brace at \p P open a declaration container rather than a body?
  ///
  /// Only a container's interior can be spliced: its contents are declarations,
  /// each self-contained. A function body's contents are statements, and cutting
  /// between them produces "function definition is not allowed here".
  ///
  /// Namespaces and linkage specifications only. Class bodies are containers too
  /// but the parser handles member declarations elsewhere, so allowing them here
  /// would emit markers where nothing consumes them.
  static bool isContainerBrace(ArrayRef<Token> Toks, unsigned P) {
    if (P >= 2 && Toks[P - 1].is(tok::string_literal) &&
        Toks[P - 2].is(tok::kw_extern))
      return true;
    unsigned Q = P;
    while (Q && (Toks[Q - 1].is(tok::identifier) ||
                 Toks[Q - 1].is(tok::coloncolon)))
      --Q;
    return Q && Toks[Q - 1].is(tok::kw_namespace);
  }

  /// Does the brace at \p P open a class, struct, union or enum body?
  ///
  /// Such a brace does not end the declaration: `typedef struct { ... } fd_set;`
  /// and `struct X { ... } x;` both continue past it, and only the `;` ends
  /// them. A function body's brace does end one. Walking back over the name and
  /// any base-clause reaches the tag keyword; a `)` on the way means a parameter
  /// list, so it is a function.
  static bool isTagBrace(ArrayRef<Token> Toks, unsigned P) {
    for (unsigned Q = P; Q; --Q) {
      const Token &T = Toks[Q - 1];
      switch (T.getKind()) {
      case tok::kw_class:
      case tok::kw_struct:
      case tok::kw_union:
      case tok::kw_enum:
        return true;
      case tok::identifier:
      case tok::coloncolon:
      case tok::colon:
      case tok::comma:
      case tok::kw_public:
      case tok::kw_protected:
      case tok::kw_private:
      case tok::kw_virtual:
      case tok::kw_int:
      case tok::kw_unsigned:
      case tok::kw_signed:
      case tok::kw_char:
      case tok::kw_short:
      case tok::kw_long:
      case tok::kw_bool:
      case tok::less:
      case tok::greater:
        continue;
      default:
        return false;
      }
    }
    return false;
  }

  /// Split [Lo,Hi) where a declaration can end.
  ///
  /// Depth is measured *relative to the start of the region*, not absolutely: a
  /// widened region can be a whole `namespace std { ... }`, and every
  /// declaration inside it sits at depth 1. Requiring absolute depth 0 makes
  /// such a region a single 73,000-token chunk that nothing can be trimmed
  /// from, which is exactly what it did.
public:
  std::vector<unsigned> chunkFor(unsigned Which, unsigned Lo, unsigned Hi) const {
    return chunk(Which, Lo, Hi);
  }

private:
  std::vector<unsigned> chunk(unsigned Which, unsigned Lo, unsigned Hi) const {
    ArrayRef<Token> Toks = Recs[Which].Tokens;
    std::vector<unsigned> Cuts{Lo};
    // Open braces within the region, container or body. A cut is legal wherever
    // no *body* brace is open: inside `namespace std { }` that is every member
    // declaration, which is the whole point -- one divergence in <cmath> was
    // otherwise duplicating the entire namespace, 73,698 tokens.
    // 0 = function body, 1 = namespace or linkage spec, 2 = tag body.
    SmallVector<char, 8> Open;
    unsigned BodyOpen = 0;
    // Region-relative, per stream (\p Which varies across calls, unlike
    // declarationBoundaries' fixed stream 0): protects trimAlternatives' own
    // re-splitting from cutting between a pragma-scope push and its pop,
    // same as declarationBoundaries does for widenToDeclarations.
    std::array<int, NumPragmaScopeKinds> PragmaDepth{};
    for (unsigned P = Lo; P != Hi; ++P) {
      tok::TokenKind K = Toks[P].getKind();
      bool ClosedTag = false;
      if (K == tok::l_brace) {
        char Kind = isContainerBrace(Toks, P) ? 1 : (isTagBrace(Toks, P) ? 2 : 0);
        Open.push_back(Kind);
        BodyOpen += Kind != 1;
      } else if (K == tok::r_brace && !Open.empty()) {
        char Kind = Open.pop_back_val();
        BodyOpen -= Kind != 1;
        ClosedTag = Kind == 2;
      }
      PragmaScopeEvent Ev = Classify(Toks[P]);
      bool ClosedPragmaScope = false;
      if (Ev.Kind >= 0) {
        PragmaDepth[Ev.Kind] += Ev.Delta;
        if (Ev.Delta < 0 && PragmaDepth[Ev.Kind] == 0)
          ClosedPragmaScope = true;
      }
      // A `}` ends a declaration only when it closes a function body.
      // `struct X { ... };` and `typedef struct { ... } fd_set;` both run on to
      // the `;`, and cutting at the `}` let trimming take the tail out of the
      // alternative as a shared run of its own -- so every arm ended at the
      // `}` and the tail was orphaned. The token that closes the last open
      // pragma scope is a cut point for the same reason declarationBoundaries
      // treats it as one -- see the comment there.
      if (!BodyOpen && PragmaDepth == std::array<int, NumPragmaScopeKinds>{} &&
          (K == tok::semi || (K == tok::r_brace && !ClosedTag) ||
           ClosedPragmaScope) &&
          P + 1 < Hi)
        Cuts.push_back(P + 1);
    }
    Cuts.push_back(Hi);
    Cuts.erase(std::unique(Cuts.begin(), Cuts.end()), Cuts.end());
    return Cuts;
  }

  /// Give back the declarations widening swept up that did not need to be
  /// duplicated.
  ///
  /// Widening grows a region until it starts and ends between declarations,
  /// which is what lets each alternative be parsed on its own -- but it drags in
  /// whole declarations that are identical in every target. Parsing those once
  /// per target declares them once per target, which is a redefinition, and it
  /// is also wasted work: on ggml the widened regions are 11-26% of the stream
  /// and almost all of it is shared text.
  ///
  /// So each alternative is re-split at declaration boundaries and the two
  /// sequences are diffed. Diffing *tokens* was rejected for this earlier -- it
  /// has no idea where a construct starts and cuts regions in half. Diffing
  /// *declarations* does not have that problem: a declaration is self-contained,
  /// so any run of them can be spliced.
  void trimAlternatives() {
    unsigned N = Recs.size();
    std::vector<MergedSegment> Out;
    for (const MergedSegment &S : Segments) {
      if (S.Shared) {
        Out.push_back(S);
        continue;
      }
      std::vector<MergedSegment> Split;
      std::vector<std::vector<unsigned>> C(N);
      std::vector<unsigned> NChunks(N);
      for (unsigned K = 0; K != N; ++K) {
        C[K] = chunk(K, S.Ranges[K].first, S.Ranges[K].second);
        NChunks[K] = C[K].size() - 1;
      }

      auto chunkLen = [&](unsigned K, unsigned I) { return C[K][I + 1] - C[K][I]; };
      auto same = [&](ArrayRef<unsigned> Idx) {
        unsigned Len = chunkLen(0, Idx[0]);
        for (unsigned K = 1; K != N; ++K)
          if (chunkLen(K, Idx[K]) != Len)
            return false;
        for (unsigned K = 1; K != N; ++K)
          if (!std::equal(Recs[0].Hashes.begin() + C[0][Idx[0]],
                          Recs[0].Hashes.begin() + C[0][Idx[0]] + Len,
                          Recs[K].Hashes.begin() + C[K][Idx[K]]))
            return false;
        return true;
      };

      // Bounded, because resynchronising is a heuristic: past this the region
      // is simply divergent and duplicating it is the correct answer.
      const unsigned MaxSkew = 64;
      std::vector<unsigned> Pos(N, 0);
      auto anyRemaining = [&] {
        for (unsigned K = 0; K != N; ++K)
          if (Pos[K] < NChunks[K])
            return true;
        return false;
      };
      while (anyRemaining()) {
        unsigned SharedRun = 0;
        auto canExtend = [&](unsigned R) {
          SmallVector<unsigned, 8> Idx(N);
          for (unsigned K = 0; K != N; ++K) {
            if (Pos[K] + R >= NChunks[K])
              return false;
            Idx[K] = Pos[K] + R;
          }
          return same(Idx);
        };
        while (canExtend(SharedRun))
          ++SharedRun;
        if (SharedRun) {
          std::vector<std::pair<unsigned, unsigned>> Ranges(N);
          for (unsigned K = 0; K != N; ++K)
            Ranges[K] = {C[K][Pos[K]], C[K][Pos[K] + SharedRun]};
          Split.push_back({true, std::move(Ranges)});
          for (unsigned K = 0; K != N; ++K)
            Pos[K] += SharedRun;
          continue;
        }

        // Out of step: find the resync point with the smallest *total* skew
        // across all N streams. Anchoring on one stream's smallest skip and
        // accepting the first match every other stream can reach is
        // provably biased -- a concrete case picks total skew 11 when a
        // solution with total skew 4 exists at a larger stream-0 skew,
        // purely from search order. Bound candidates per stream to MaxSkew
        // and hash each chunk so this is a set intersection, not a nested
        // scan: O(N*MaxSkew) instead of the O(MaxSkew^(N-1)) a literal
        // per-stream skew search would cost.
        SmallVector<llvm::DenseMap<uint64_t, unsigned>, 8> Candidates(N);
        for (unsigned K = 0; K != N; ++K) {
          unsigned Limit = std::min(MaxSkew, NChunks[K] - Pos[K]);
          for (unsigned A = 0; A != Limit; ++A) {
            uint64_t H = llvm::hash_combine_range(
                Recs[K].Hashes.begin() + C[K][Pos[K] + A],
                Recs[K].Hashes.begin() + C[K][Pos[K] + A + 1]);
            Candidates[K].try_emplace(H, A); // keep the smallest offset
          }
        }
        unsigned BestSum = ~0u, BestA0 = ~0u;
        SmallVector<unsigned, 8> Best(N);
        for (unsigned K = 0; K != N; ++K)
          Best[K] = NChunks[K] - Pos[K]; // fallback: consume the rest
        for (const auto &KV : Candidates[0]) {
          uint64_t H = KV.first;
          unsigned A0 = KV.second;
          SmallVector<unsigned, 8> Idx(N);
          Idx[0] = Pos[0] + A0;
          unsigned Sum = A0;
          bool Ok = true;
          for (unsigned K = 1; K != N; ++K) {
            auto It = Candidates[K].find(H);
            if (It == Candidates[K].end()) {
              Ok = false;
              break;
            }
            Idx[K] = Pos[K] + It->second;
            Sum += It->second;
          }
          if (Ok && (Sum < BestSum || (Sum == BestSum && A0 < BestA0)) &&
              same(Idx)) {
            BestSum = Sum;
            BestA0 = A0;
            for (unsigned K = 0; K != N; ++K)
              Best[K] = Idx[K] - Pos[K];
          }
        }
        std::vector<std::pair<unsigned, unsigned>> Ranges(N);
        for (unsigned K = 0; K != N; ++K)
          Ranges[K] = {C[K][Pos[K]], C[K][Pos[K] + Best[K]]};
        Split.push_back({false, std::move(Ranges)});
        for (unsigned K = 0; K != N; ++K)
          Pos[K] += Best[K];
      }

      // Splitting is only worth doing if it leaves every piece parseable on its
      // own. A chunk boundary is not always a complete declaration -- a region
      // that began mid-construct has none -- so a split can leave an
      // alternative opening more braces than it closes, and the translation
      // unit then ends with "expected '}'". Reject the split as a unit rather
      // than emit something that cannot be parsed.
      bool Balanced = llvm::all_of(Split, [&](const MergedSegment &T) {
        if (T.Shared)
          return true;
        for (unsigned K = 0; K != N; ++K)
          if (delta(K, T.Ranges[K].first, T.Ranges[K].second) !=
              std::array<int, 3>{0, 0, 0})
            return false;
        return true;
      });
      if (Balanced)
        Out.insert(Out.end(), Split.begin(), Split.end());
      else
        Out.push_back(S);
    }
    Segments = std::move(Out);
  }


  /// Make every divergent region bracket-balanced on its own.
  ///
  /// Coalescing only requires the alternatives to *agree* on their delta, which
  /// is what a stream that selects one alternative needs. Parsing every
  /// alternative is different: two arms that each open one brace open two. So
  /// for that, a region has to be balanced, and one that is not absorbs its
  /// neighbours until it is.
  void balanceAlternatives() {
    unsigned N = Recs.size();
    std::vector<MergedSegment> Out;
    for (unsigned I = 0; I != Segments.size();) {
      if (Segments[I].Shared) {
        Out.push_back(Segments[I++]);
        continue;
      }
      unsigned J = I;
      // Every recording's own delta must independently reach {0,0,0}: only
      // checking recording 0 and trusting the others stay in step is exactly
      // the gap `trimAlternatives`'s own `Balanced` predicate already closed
      // for itself -- a widening bug that desynchronises one recording's
      // delta from the rest would otherwise surface as a parse failure for
      // just that target, far from its actual cause.
      auto balanced = [&] {
        for (unsigned K = 0; K != N; ++K)
          if (delta(K, Segments[I].Ranges[K].first,
                    Segments[J].Ranges[K].second) != std::array<int, 3>{0, 0, 0})
            return false;
        return true;
      };
      while (!balanced() && J + 1 < Segments.size())
        ++J;
      std::vector<std::pair<unsigned, unsigned>> Ranges(N);
      for (unsigned K = 0; K != N; ++K)
        Ranges[K] = {Segments[I].Ranges[K].first, Segments[J].Ranges[K].second};
      Out.push_back({false, std::move(Ranges)});
      I = J + 1;
    }
    Segments = std::move(Out);
  }

public:
  Aligner(ArrayRef<TargetRecording> Recs, bool Widen = false,
          PragmaScopeClassifier Classify = noopPragmaScopeClassifier)
      : Recs(Recs), WidenToDeclarations(Widen), Classify(Classify) {
    llvm::StringMap<unsigned> Keys;
    for (const TargetRecording &R : Recs)
      Conds.push_back(collapse(R, Keys));
  }

  std::vector<MergedSegment> run() {
    SmallVector<std::pair<unsigned, unsigned>, 8> Windows;
    for (const TargetRecording &R : Recs)
      Windows.emplace_back(0u, static_cast<unsigned>(R.Tokens.size()));
    align(std::move(Windows), {});
    coalesce();
    if (WidenToDeclarations) {
      widenToDeclarations();
      // The two passes pull against each other. Trimming takes shared
      // declarations back out of an alternative, which can leave it unbalanced;
      // balancing absorbs neighbouring segments to fix that, and a shared
      // segment absorbed that way appears in *every* arm, so its declarations
      // are duplicated again and a call to one of them from shared code is
      // ambiguous between the copies. Neither alone is enough -- trim-only left
      // 122 ambiguities on convert.cu, balance-only left 122 on it too -- so
      // they alternate until nothing changes.
      for (unsigned Pass = 0; Pass != 4; ++Pass) {
        size_t Before = Segments.size();
        trimAlternatives();
        balanceAlternatives();
        if (Segments.size() == Before)
          break;
      }
      trimAlternatives();
    }
    return std::move(Segments);
  }

  std::array<int, 3> deltaOf(unsigned Which, const MergedSegment &S) const {
    return delta(Which, S.Ranges[Which].first, S.Ranges[Which].second);
  }
};

} // namespace

std::vector<MergedSegment>
clang::alignRecordings(ArrayRef<TargetRecording> Recordings,
                       bool WidenToDeclarations,
                       PragmaScopeClassifier Classify) {
  // Decl::TargetVariant is a 3-bit field: variant 1 is the primary, variants
  // 2-6 are aux targets, and 7 is reserved as TargetVariantRedundant --
  // silently truncating past 6 recordings would alias into that sentinel
  // rather than merely running out of room, so this is a hard reject, not a
  // soft cap.
  if (Recordings.size() < 2 || Recordings.size() > 6)
    return {};
  return Aligner(Recordings, WidenToDeclarations, Classify).run();
}


//===----------------------------------------------------------------------===//
// Merging
//===----------------------------------------------------------------------===//

/// An annotation token covering \p Loc, carrying \p Value.
static Token makeMarker(tok::TokenKind Kind, SourceLocation Loc, uintptr_t V) {
  Token T;
  T.startToken();
  T.setKind(Kind);
  T.setLocation(Loc);
  T.setAnnotationEndLoc(Loc);
  T.setAnnotationValue(reinterpret_cast<void *>(V));
  return T;
}

/// The primary compilation's location for \p Loc, which belongs to \p Aux.
///
/// Maps through (file, offset): the two managers read the same files, so the
/// same byte offset in the same file names the same place. A file only the aux
/// target included has no FileID in the primary manager and gets one.
///
/// Macro expansions collapse to their file location. A device-only token that
/// came from a macro therefore points at the expansion site rather than the
/// macro body, which costs "expanded from macro" notes inside divergent regions
/// and nothing else.
static SourceLocation translateLoc(SourceManager &Primary,
                                   const SourceManager &Aux,
                                   SourceLocation Loc,
                                   llvm::DenseMap<const void *, FileID> &Cache) {
  if (Loc.isInvalid())
    return Loc;
  FileIDAndOffset Decomp = Aux.getDecomposedLoc(Aux.getFileLoc(Loc));
  OptionalFileEntryRef FE = Aux.getFileEntryRefForID(Decomp.first);
  if (!FE)
    return SourceLocation();

  const void *Key = &FE->getMapEntry();
  auto It = Cache.find(Key);
  if (It == Cache.end()) {
    FileID Mapped = Primary.translateFile(*FE);
    if (Mapped.isInvalid())
      Mapped = Primary.createFileID(*FE, SourceLocation(), SrcMgr::C_User);
    It = Cache.try_emplace(Key, Mapped).first;
  }
  if (It->second.isInvalid())
    return SourceLocation();
  return Primary.getLocForStartOfFile(It->second).getLocWithOffset(Decomp.second);
}

/// Rewrite \p T to belong to \p PP's compilation.
///
/// Identifiers are re-interned: an IdentifierInfo is an identity, and Sema
/// compares declarations by it, so one from another preprocessor's table would
/// never match anything. Literal data is left pointing into the recording's own
/// buffer, which is valid for as long as the recording is, and holds the same
/// bytes either way.
static void translateToken(Token &T, Preprocessor &PP,
                           const SourceManager &Aux,
                           llvm::DenseMap<const void *, FileID> &Cache) {
  T.setLocation(translateLoc(PP.getSourceManager(), Aux, T.getLocation(), Cache));
  if (T.isAnnotation()) {
    T.setAnnotationEndLoc(
        translateLoc(PP.getSourceManager(), Aux, T.getAnnotationEndLoc(), Cache));
    return;
  }
  if (T.isLiteral())
    return;
  if (const IdentifierInfo *II = T.getIdentifierInfo())
    T.setIdentifierInfo(PP.getIdentifierInfo(II->getName()));
}

std::vector<Token> clang::buildMergedStream(Preprocessor &PP,
                                            ArrayRef<TargetRecording> Recordings,
                                            ArrayRef<MergedSegment> Segments) {
  std::vector<Token> Out;
  if (Recordings.empty())
    return Out;

  // One cache per recording: a file maps to the same primary FileID every time.
  std::vector<llvm::DenseMap<const void *, FileID>> Caches(Recordings.size());

  auto appendRange = [&](unsigned Which, unsigned First, unsigned End) {
    const TargetRecording &R = Recordings[Which];
    for (unsigned I = First; I != End; ++I) {
      Token T = R.Tokens[I];
      if (R.SM)
        translateToken(T, PP, *R.SM, Caches[Which]);
      Out.push_back(T);
    }
  };

  for (const MergedSegment &S : Segments) {
    if (S.Shared) {
      // Any target's copy will do; they compared equal.
      appendRange(0, S.Ranges[0].first, S.Ranges[0].second);
      continue;
    }
    // The region takes its location from where the first target's alternative
    // starts, so a diagnostic about the region points at real source.
    SourceLocation Loc;
    if (S.Ranges[0].first < Recordings[0].Tokens.size()) {
      Loc = Recordings[0].Tokens[S.Ranges[0].first].getLocation();
      if (Recordings[0].SM)
        Loc = translateLoc(PP.getSourceManager(), *Recordings[0].SM, Loc,
                           Caches[0]);
    }

    Out.push_back(makeMarker(tok::annot_target_alt_begin, Loc, S.Ranges.size()));
    for (auto [I, Range] : llvm::enumerate(S.Ranges)) {
      if (I)
        Out.push_back(makeMarker(tok::annot_target_alt_sep, Loc, I));
      appendRange(I, Range.first, Range.second);
    }
    Out.push_back(makeMarker(tok::annot_target_alt_end, Loc, 0));
  }
  return Out;
}

std::vector<Token> clang::selectTarget(ArrayRef<Token> Merged, unsigned Which) {
  std::vector<Token> Out;
  unsigned Alternative = 0;
  int Depth = 0;
  for (const Token &T : Merged) {
    if (T.is(tok::annot_target_alt_begin)) {
      ++Depth;
      if (Depth == 1) {
        Alternative = 0;
        continue;
      }
    } else if (T.is(tok::annot_target_alt_sep)) {
      if (Depth == 1) {
        ++Alternative;
        continue;
      }
    } else if (T.is(tok::annot_target_alt_end)) {
      --Depth;
      if (Depth == 0)
        continue;
    }
    // Regions do not nest today, but if they ever do, an inner region belongs
    // to whichever outer alternative contains it and is copied verbatim.
    if (Depth == 0 || Alternative == Which)
      Out.push_back(T);
  }
  return Out;
}


bool clang::verifyRoundTrip(raw_ostream &OS,
                            ArrayRef<TargetRecording> Recordings,
                            ArrayRef<Token> Merged) {
  bool AllExact = true;
  OS << "  merged stream:     " << Merged.size() << " tokens\n";
  for (auto [I, R] : llvm::enumerate(Recordings)) {
    std::vector<Token> Back = selectTarget(Merged, I);
    size_t First = std::min(Back.size(), R.Hashes.size());
    for (size_t J = 0; J != First; ++J)
      if (hashToken(Back[J]) != R.Hashes[J]) {
        First = J;
        break;
      }
    bool Exact = Back.size() == R.Hashes.size() && First == Back.size();
    AllExact &= Exact;
    OS << "  reconstruct " << R.Triple << ": " << (Exact ? "exact" : "MISMATCH")
       << " (" << Back.size() << " vs " << R.Tokens.size() << " tokens";
    if (!Exact)
      OS << ", first differs at " << First;
    OS << ")\n";
  }
  return AllExact;
}


void clang::reportDeclarationGranularity(raw_ostream &OS,
                                         ArrayRef<TargetRecording> Recordings,
                                         ArrayRef<MergedSegment> Segments) {
  if (Recordings.empty())
    return;
  ArrayRef<Token> Toks = Recordings[0].Tokens;

  // Brace depth *before* each token, plus whether a top-level declaration could
  // end just before it -- that is, the previous token closed one at depth 0.
  std::vector<unsigned> Depth(Toks.size() + 1, 0);
  std::vector<bool> Boundary(Toks.size() + 1, false);
  Boundary[0] = true;
  unsigned D = 0;
  for (auto [I, T] : llvm::enumerate(Toks)) {
    Depth[I] = D;
    if (T.is(tok::l_brace))
      ++D;
    else if (T.is(tok::r_brace) && D)
      --D;
    Depth[I + 1] = D;
    if (D == 0 && (T.is(tok::semi) || T.is(tok::r_brace)))
      Boundary[I + 1] = true;
  }
  Boundary[Toks.size()] = true;

  // Widen each divergent region outward to the nearest boundaries, then union
  // the results: two regions in one declaration cost that declaration once.
  std::vector<std::pair<unsigned, unsigned>> Wide;
  for (const MergedSegment &S : Segments) {
    if (S.Shared)
      continue;
    unsigned Lo = std::min<unsigned>(S.Ranges[0].first, Toks.size());
    unsigned Hi = std::min<unsigned>(S.Ranges[0].second, Toks.size());
    while (Lo && !Boundary[Lo])
      --Lo;
    while (Hi < Toks.size() && !Boundary[Hi])
      ++Hi;
    if (!Wide.empty() && Lo <= Wide.back().second)
      Wide.back().second = std::max(Wide.back().second, Hi);
    else
      Wide.emplace_back(Lo, Hi);
  }

  unsigned Narrow = 0, Widened = 0, Deepest = 0;
  for (const MergedSegment &S : Segments)
    if (!S.Shared) {
      Narrow += S.Ranges[0].second - S.Ranges[0].first;
      Deepest = std::max(Deepest, Depth[std::min<unsigned>(S.Ranges[0].first,
                                                           Toks.size())]);
    }
  for (auto [Lo, Hi] : Wide)
    Widened += Hi - Lo;

  OS << "declaration-granularity forking:\n"
     << "  divergent regions:   " << Wide.size() << " after widening\n"
     << "  tokens, as aligned:  " << Narrow;
  if (!Toks.empty())
    OS << "  (" << llvm::format("%.2f", 100.0 * Narrow / Toks.size()) << "%)";
  OS << "\n  tokens, widened:     " << Widened;
  if (!Toks.empty())
    OS << "  (" << llvm::format("%.2f", 100.0 * Widened / Toks.size()) << "%)";
  if (Narrow)
    OS << "\n  widening factor:     "
       << llvm::format("%.1f", double(Widened) / Narrow) << "x";
  OS << "\n  deepest region starts at brace depth " << Deepest << "\n";
}


void clang::reportDivergenceClosure(raw_ostream &OS,
                                    ArrayRef<TargetRecording> Recordings,
                                    ArrayRef<MergedSegment> Segments) {
  if (Recordings.empty())
    return;
  ArrayRef<Token> Toks = Recordings[0].Tokens;
  Aligner A(Recordings);

  auto namesIn = [&](unsigned W, unsigned Lo, unsigned Hi,
                     llvm::DenseSet<const IdentifierInfo *> &Out) {
    ArrayRef<Token> T = Recordings[W].Tokens;
    for (unsigned I = Lo; I < Hi && I < T.size(); ++I)
      if (!T[I].isAnnotation())
        if (const IdentifierInfo *II = T[I].getIdentifierInfo())
          Out.insert(II);
  };

  // Seed: every name mentioned inside a divergent region.
  llvm::DenseSet<const IdentifierInfo *> Divergent;
  unsigned AltTokens = 0;
  std::vector<std::pair<unsigned, unsigned>> SharedChunks;
  for (const MergedSegment &S : Segments) {
    if (!S.Shared) {
      AltTokens += S.Ranges[0].second - S.Ranges[0].first;
      for (unsigned W = 0; W != Recordings.size(); ++W)
        namesIn(W, S.Ranges[W].first, S.Ranges[W].second, Divergent);
      continue;
    }
    for (std::vector<unsigned> C =
             A.chunkFor(0, S.Ranges[0].first, S.Ranges[0].second);
         unsigned N = C.size() > 1 ? C.size() - 1 : 0;) {
      for (unsigned I = 0; I != N; ++I)
        SharedChunks.emplace_back(C[I], C[I + 1]);
      break;
    }
  }

  OS << "divergence closure:\n"
     << "  seed: " << AltTokens << " tokens in divergent regions, "
     << Divergent.size() << " distinct names\n";

  std::vector<bool> Promoted(SharedChunks.size(), false);
  unsigned PromotedTokens = 0;
  for (unsigned Round = 1; Round <= 8; ++Round) {
    unsigned Added = 0, AddedTokens = 0;
    for (auto [I, Ch] : llvm::enumerate(SharedChunks)) {
      if (Promoted[I])
        continue;
      llvm::DenseSet<const IdentifierInfo *> Here;
      namesIn(0, Ch.first, Ch.second, Here);
      if (llvm::none_of(Here, [&](const IdentifierInfo *II) {
            return Divergent.count(II);
          }))
        continue;
      Promoted[I] = true;
      ++Added;
      AddedTokens += Ch.second - Ch.first;
      Divergent.insert(Here.begin(), Here.end());
    }
    PromotedTokens += AddedTokens;
    OS << "  round " << Round << ": +" << Added << " declarations, +"
       << AddedTokens << " tokens  (cumulative "
       << llvm::format("%.2f", 100.0 * (PromotedTokens + AltTokens) /
                                   std::max<size_t>(Toks.size(), 1))
       << "% of the stream)\n";
    if (!Added)
      break;
  }
}

void clang::reportAlignment(raw_ostream &OS,
                            ArrayRef<TargetRecording> Recordings,
                            ArrayRef<MergedSegment> Segments) {
  if (Recordings.size() < 2)
    return;
  unsigned N = Recordings.size();
  Aligner A(Recordings);
  unsigned NShared = 0, NAlt = 0, Agree = 0;
  unsigned SharedToks = 0;
  std::vector<unsigned> AltToks(N, 0);
  for (const MergedSegment &S : Segments) {
    if (S.Shared) {
      ++NShared;
      SharedToks += S.Ranges[0].second - S.Ranges[0].first;
    } else {
      ++NAlt;
      for (unsigned K = 0; K != N; ++K)
        AltToks[K] += S.Ranges[K].second - S.Ranges[K].first;
      std::array<int, 3> D0 = A.deltaOf(0, S);
      bool AllAgree = true;
      for (unsigned K = 1; K != N; ++K)
        if (A.deltaOf(K, S) != D0) {
          AllAgree = false;
          break;
        }
      if (AllAgree)
        ++Agree;
    }
  }
  uint64_t TotalToks = 0;
  for (const TargetRecording &R : Recordings)
    TotalToks += R.Tokens.size();
  size_t T0 = Recordings[0].Tokens.size();

  OS << "multi-target recording:\n";
  for (const TargetRecording &R : Recordings)
    OS << "  " << R.Triple << ": " << R.Tokens.size() << " tokens, "
       << R.Regions.size() << " conditional branches\n";
  OS << "  segments:          " << NShared << " shared, " << NAlt
     << " alternative\n";
  OS << "  shared tokens:     " << SharedToks;
  if (T0)
    OS << "  (" << llvm::format("%.2f", 100.0 * SharedToks / T0) << "%)";
  OS << "\n  alternative:       ";
  uint64_t MergedSize = SharedToks;
  for (unsigned K = 0; K != N; ++K) {
    if (K)
      OS << " / ";
    OS << AltToks[K];
    MergedSize += AltToks[K];
  }
  if (T0)
    OS << "  (" << llvm::format("%.2f", 100.0 * AltToks[0] / T0) << "%)";
  OS << "\n  token saving:      ";
  if (TotalToks)
    OS << llvm::format("%.2f",
                       100.0 * (1.0 - double(MergedSize) / double(TotalToks)))
       << "%";
  OS << "\n  bracket delta agrees: " << Agree << " of " << NAlt;
  if (NAlt)
    OS << "  (" << llvm::format("%.2f", 100.0 * Agree / NAlt) << "%)";
  OS << "\n";

  SmallVector<const MergedSegment *, 8> Big;
  for (const MergedSegment &S : Segments)
    if (!S.Shared)
      Big.push_back(&S);
  llvm::sort(Big, [](const MergedSegment *A, const MergedSegment *B) {
    return (A->Ranges[0].second - A->Ranges[0].first) >
           (B->Ranges[0].second - B->Ranges[0].first);
  });
  for (const MergedSegment *S : ArrayRef(Big).take_front(8)) {
    OS << "    alt ";
    for (unsigned K = 0; K != N; ++K) {
      if (K)
        OS << " / ";
      OS << (S->Ranges[K].second - S->Ranges[K].first);
    }
    OS << "  at [" << S->Ranges[0].first << "," << S->Ranges[0].second << ")";
    // The primary recording has no SourceManager of its own -- its tokens are
    // already in the compilation -- so fall back to another target's, which
    // reads the same files.
    for (unsigned W = 0; W != N; ++W) {
      if (!Recordings[W].SM || S->Ranges[W].first >= Recordings[W].Tokens.size())
        continue;
      PresumedLoc PL = Recordings[W].SM->getPresumedLoc(
          Recordings[W].Tokens[S->Ranges[W].first].getLocation());
      if (PL.isValid())
        OS << "  " << llvm::sys::path::filename(PL.getFilename()) << ":"
           << PL.getLine();
      break;
    }
    OS << "\n";
  }
}
