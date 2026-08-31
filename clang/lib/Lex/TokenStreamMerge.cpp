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
  std::vector<std::vector<Conditional>> Conds;
  std::vector<MergedSegment> Segments;

  bool equalRange(unsigned ALo, unsigned AHi, unsigned BLo, unsigned BHi) const {
    if (AHi - ALo != BHi - BLo)
      return false;
    return std::equal(Recs[0].Hashes.begin() + ALo, Recs[0].Hashes.begin() + AHi,
                      Recs[1].Hashes.begin() + BLo);
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

  void emit(bool Shared, unsigned ALo, unsigned AHi, unsigned BLo,
            unsigned BHi) {
    if (ALo == AHi && BLo == BHi)
      return;
    Segments.push_back({Shared, {{ALo, AHi}, {BLo, BHi}}});
  }

  void align(unsigned ALo, unsigned AHi, unsigned BLo, unsigned BHi,
             llvm::DenseSet<unsigned> Seen, unsigned Depth = 0) {
    // Conditional nesting is shallow in practice (deepest observed: 11), so a
    // runaway here is a bug in the child selection rather than real input.
    if (Depth > 256 || AHi < ALo || BHi < BLo) {
      emit(false, ALo, AHi, BLo, BHi);
      return;
    }
    if (equalRange(ALo, AHi, BLo, BHi)) {
      emit(true, ALo, AHi, BLo, BHi);
      return;
    }
    std::vector<Conditional> AK = childrenOf(0, ALo, AHi, Seen);
    llvm::DenseMap<unsigned, Conditional> BK;
    for (const Conditional &C : childrenOf(1, BLo, BHi, Seen))
      BK[C.Key] = C;

    unsigned AP = ALo, BP = BLo;
    bool Matched = false;
    for (const Conditional &C : AK) {
      auto It = BK.find(C.Key);
      if (It == BK.end() || C.First < AP || It->second.First < BP)
        continue;
      Matched = true;
      const Conditional &D = It->second;
      align(AP, C.First, BP, D.First, Seen, Depth + 1);
      if (equalRange(C.First, C.End, D.First, D.End)) {
        emit(true, C.First, C.End, D.First, D.End);
      } else {
        llvm::DenseSet<unsigned> Inner = Seen;
        Inner.insert(C.Key);
        align(C.First, C.End, D.First, D.End, std::move(Inner), Depth + 1);
      }
      AP = C.End;
      BP = D.End;
    }
    if (!Matched) {
      emit(false, ALo, AHi, BLo, BHi);
      return;
    }
    align(AP, AHi, BP, BHi, std::move(Seen), Depth + 1);
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
    std::vector<MergedSegment> Out;
    for (unsigned I = 0; I != Segments.size();) {
      if (Segments[I].Shared) {
        Out.push_back(Segments[I++]);
        continue;
      }
      unsigned J = I;
      while (J + 1 < Segments.size() &&
             delta(0, Segments[I].Ranges[0].first,
                   Segments[J].Ranges[0].second) !=
                 delta(1, Segments[I].Ranges[1].first,
                       Segments[J].Ranges[1].second))
        ++J;
      Out.push_back({false,
                     {{Segments[I].Ranges[0].first, Segments[J].Ranges[0].second},
                      {Segments[I].Ranges[1].first, Segments[J].Ranges[1].second}}});
      I = J + 1;
    }
    Segments = std::move(Out);
  }

  /// Whether a top-level declaration can end just before \p P in stream
  /// \p Which.
  std::vector<bool> declarationBoundaries(unsigned Which = 0) const {
    ArrayRef<Token> Toks = Recs[Which].Tokens;
    std::vector<bool> B(Toks.size() + 1, false);
    B.front() = B.back() = true;
    unsigned D = 0;
    for (auto [I, T] : llvm::enumerate(Toks)) {
      if (T.is(tok::l_brace))
        ++D;
      else if (T.is(tok::r_brace) && D)
        --D;
      // A `}` that a `;` follows does not end the declaration; the `;` does.
      // Widening a region inside `struct X { ... };` to the gap between them
      // left the `;` outside the alternative, and every arm then ended with
      // "expected ';' after struct".
      if (D == 0 && (T.is(tok::semi) ||
                     (T.is(tok::r_brace) &&
                      (I + 1 == Toks.size() || !Toks[I + 1].is(tok::semi)))))
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

    std::vector<MergedSegment> Out;
    for (unsigned I = 0; I != Segments.size();) {
      if (Segments[I].Shared) {
        Out.push_back(Segments[I++]);
        continue;
      }
      unsigned Lo0 = Segments[I].Ranges[0].first, Lo1 = Segments[I].Ranges[1].first;
      unsigned Hi0 = Segments[I].Ranges[0].second, Hi1 = Segments[I].Ranges[1].second;

      // Absorb backwards. A preceding shared run maps one-to-one between the
      // streams, so a boundary inside it moves both ends by the same amount.
      while (!Boundary[Lo0] && !Out.empty()) {
        MergedSegment &Prev = Out.back();
        if (!Prev.Shared) {
          // Two divergent regions in one declaration: they become one.
          Lo0 = Prev.Ranges[0].first;
          Lo1 = Prev.Ranges[1].first;
          Out.pop_back();
          continue;
        }
        unsigned A = Prev.Ranges[0].first;
        unsigned P = Lo0;
        while (P > A && !Boundary[P])
          --P;
        Lo1 -= Lo0 - P;
        Lo0 = P;
        if (P > A) {
          Prev.Ranges[0].second = P;
          Prev.Ranges[1].second = Prev.Ranges[1].first + (P - A);
          break;
        }
        Lo1 = Prev.Ranges[1].first;
        Out.pop_back();
      }

      // Absorb forwards, the same way.
      unsigned J = I + 1;
      while (!Boundary[Hi0] && J != Segments.size()) {
        const MergedSegment &Next = Segments[J];
        if (!Next.Shared) {
          Hi0 = Next.Ranges[0].second;
          Hi1 = Next.Ranges[1].second;
          ++J;
          continue;
        }
        unsigned B = Next.Ranges[0].second;
        unsigned P = Hi0;
        while (P < B && !Boundary[P])
          ++P;
        Hi1 += P - Hi0;
        Hi0 = P;
        ++J;
        if (P < B) {
          // The rest of that shared run survives.
          MergedSegment Rest = Next;
          Rest.Ranges[0].first = P;
          Rest.Ranges[1].first = Next.Ranges[1].first + (P - Next.Ranges[0].first);
          Out.push_back({false, {{Lo0, Hi0}, {Lo1, Hi1}}});
          Out.push_back(Rest);
          Lo0 = Hi0; // consumed
          goto advanced;
        }
        Hi1 = Next.Ranges[1].second;
      }
      Out.push_back({false, {{Lo0, Hi0}, {Lo1, Hi1}}});
    advanced:
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
      // A `}` ends a declaration only when it closes a function body.
      // `struct X { ... };` and `typedef struct { ... } fd_set;` both run on to
      // the `;`, and cutting at the `}` let trimming take the tail out of the
      // alternative as a shared run of its own -- so every arm ended at the
      // `}` and the tail was orphaned.
      if (!BodyOpen && (K == tok::semi || (K == tok::r_brace && !ClosedTag)) &&
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
    std::vector<MergedSegment> Out;
    for (const MergedSegment &S : Segments) {
      if (S.Shared) {
        Out.push_back(S);
        continue;
      }
      std::vector<MergedSegment> Split;
      std::vector<unsigned> C0 = chunk(0, S.Ranges[0].first, S.Ranges[0].second);
      std::vector<unsigned> C1 = chunk(1, S.Ranges[1].first, S.Ranges[1].second);
      unsigned N0 = C0.size() - 1, N1 = C1.size() - 1;

      auto same = [&](unsigned I, unsigned J) {
        unsigned N = C0[I + 1] - C0[I];
        return N == C1[J + 1] - C1[J] &&
               std::equal(Recs[0].Hashes.begin() + C0[I],
                          Recs[0].Hashes.begin() + C0[I + 1],
                          Recs[1].Hashes.begin() + C1[J]);
      };

      // Bounded, because resynchronising is a heuristic: past this the region
      // is simply divergent and duplicating it is the correct answer.
      const unsigned MaxSkew = 64;
      unsigned I = 0, J = 0;
      while (I < N0 || J < N1) {
        unsigned SharedRun = 0;
        while (I + SharedRun < N0 && J + SharedRun < N1 &&
               same(I + SharedRun, J + SharedRun))
          ++SharedRun;
        if (SharedRun) {
          Split.push_back({true, {{C0[I], C0[I + SharedRun]},
                                  {C1[J], C1[J + SharedRun]}}});
          I += SharedRun;
          J += SharedRun;
          continue;
        }
        // Out of step: find the nearest pair that matches again, preferring the
        // smallest total skew so the divergent run stays as short as possible.
        unsigned BestA = N0 - I, BestB = N1 - J;
        for (unsigned D = 1; D <= MaxSkew; ++D) {
          bool Found = false;
          for (unsigned A = 0; A <= D && !Found; ++A) {
            unsigned B = D - A;
            if (I + A < N0 && J + B < N1 && same(I + A, J + B)) {
              BestA = A;
              BestB = B;
              Found = true;
            }
          }
          if (Found)
            break;
        }
        Split.push_back({false, {{C0[I], C0[I + BestA]}, {C1[J], C1[J + BestB]}}});
        I += BestA;
        J += BestB;
      }

      // Splitting is only worth doing if it leaves every piece parseable on its
      // own. A chunk boundary is not always a complete declaration -- a region
      // that began mid-construct has none -- so a split can leave an
      // alternative opening more braces than it closes, and the translation
      // unit then ends with "expected '}'". Reject the split as a unit rather
      // than emit something that cannot be parsed.
      bool Balanced = llvm::all_of(Split, [&](const MergedSegment &T) {
        return T.Shared || (delta(0, T.Ranges[0].first, T.Ranges[0].second) ==
                                std::array<int, 3>{0, 0, 0} &&
                            delta(1, T.Ranges[1].first, T.Ranges[1].second) ==
                                std::array<int, 3>{0, 0, 0});
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
    std::vector<MergedSegment> Out;
    for (unsigned I = 0; I != Segments.size();) {
      if (Segments[I].Shared) {
        Out.push_back(Segments[I++]);
        continue;
      }
      unsigned J = I;
      auto balanced = [&] {
        return delta(0, Segments[I].Ranges[0].first,
                     Segments[J].Ranges[0].second) == std::array<int, 3>{0, 0, 0};
      };
      while (!balanced() && J + 1 < Segments.size())
        ++J;
      Out.push_back({false,
                     {{Segments[I].Ranges[0].first, Segments[J].Ranges[0].second},
                      {Segments[I].Ranges[1].first, Segments[J].Ranges[1].second}}});
      I = J + 1;
    }
    Segments = std::move(Out);
  }

public:
  Aligner(ArrayRef<TargetRecording> Recs, bool Widen = false)
      : Recs(Recs), WidenToDeclarations(Widen) {
    llvm::StringMap<unsigned> Keys;
    for (const TargetRecording &R : Recs)
      Conds.push_back(collapse(R, Keys));
  }

  std::vector<MergedSegment> run() {
    align(0, Recs[0].Tokens.size(), 0, Recs[1].Tokens.size(), {});
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
                       bool WidenToDeclarations) {
  // Two targets today; N follows once the driver supplies the target list.
  if (Recordings.size() != 2)
    return {};
  return Aligner(Recordings, WidenToDeclarations).run();
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
  if (Recordings.size() != 2)
    return;
  Aligner A(Recordings);
  unsigned NShared = 0, NAlt = 0;
  unsigned SharedToks = 0, AltA = 0, AltB = 0, Agree = 0;
  for (const MergedSegment &S : Segments) {
    unsigned SA = S.Ranges[0].second - S.Ranges[0].first;
    unsigned SB = S.Ranges[1].second - S.Ranges[1].first;
    if (S.Shared) {
      ++NShared;
      SharedToks += SA;
    } else {
      ++NAlt;
      AltA += SA;
      AltB += SB;
      if (A.deltaOf(0, S) == A.deltaOf(1, S))
        ++Agree;
    }
  }
  size_t TA = Recordings[0].Tokens.size(), TB = Recordings[1].Tokens.size();
  OS << "multi-target recording:\n";
  for (const TargetRecording &R : Recordings)
    OS << "  " << R.Triple << ": " << R.Tokens.size() << " tokens, "
       << R.Regions.size() << " conditional branches\n";
  OS << "  segments:          " << NShared << " shared, " << NAlt
     << " alternative\n";
  OS << "  shared tokens:     " << SharedToks;
  if (TA)
    OS << "  (" << llvm::format("%.2f", 100.0 * SharedToks / TA) << "%)";
  OS << "\n  alternative:       " << AltA << " / " << AltB;
  if (TA)
    OS << "  (" << llvm::format("%.2f", 100.0 * AltA / TA) << "%)";
  OS << "\n  token saving:      ";
  if (TA + TB)
    OS << llvm::format("%.2f",
                       100.0 * (1.0 - double(SharedToks + AltA + AltB) /
                                          double(TA + TB)))
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
    OS << "    alt " << (S->Ranges[0].second - S->Ranges[0].first) << " / "
       << (S->Ranges[1].second - S->Ranges[1].first) << "  at ["
       << S->Ranges[0].first << "," << S->Ranges[0].second << ")";
    // The primary recording has no SourceManager of its own -- its tokens are
    // already in the compilation -- so fall back to another target's, which
    // reads the same files.
    for (unsigned W = 0; W != Recordings.size(); ++W) {
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
