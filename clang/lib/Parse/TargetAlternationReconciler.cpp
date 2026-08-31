//===--- TargetAlternationReconciler.cpp - Widening/reparse merge ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Reconciles the declarations a multi-target parse makes from alternatives and
// re-parses: merges copies that turn out the same and re-parses shared
// declarations that consumed an input differing between targets.
//
//===----------------------------------------------------------------------===//

#include "TargetAlternationReconciler.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/MultiTargetContext.h"
#include "clang/AST/TargetCopyMerge.h"
#include "clang/Basic/MultiTargetDebugOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Lex/TokenStreamMerge.h"
#include "clang/Parse/Parser.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaMultiTarget.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

static void traceReject(Parser::DeclGroupPtrTy &Group, const char *Why,
                        unsigned Tokens, Sema &S);

static void traceReject(Parser::DeclGroupPtrTy &Group, const char *Why,
                        unsigned Tokens, Sema &S) {
  if (LLVM_LIKELY(!multitarget::TraceReparse))
    return;
  for (Decl *RD : Group.get())
    llvm::errs() << "reject " << Why << " " << Tokens << "t "
                 << RD->getDeclKindName() << " "
                 << (isa<NamedDecl>(RD) ? cast<NamedDecl>(RD)->getNameAsString()
                                        : std::string("<unnamed>"))
                 << " @ "
                 << RD->getLocation().printToString(S.getSourceManager())
                 << "\n";
}

/// Whether the declaration starting at \p Lo directly follows a set of
/// alternatives that itself starts where a declaration can: the alternatives
/// are whole declarations, so one can start after them. So can a set that
/// directly follows another such set.
static bool followsAlternativesAtBoundary(ArrayRef<Token> Toks,
                                          const std::vector<bool> &Boundaries,
                                          unsigned Lo) {
  if (!Lo || !Toks[Lo - 1].is(tok::annot_target_alt_end))
    return false;
  unsigned Depth = 0;
  for (unsigned I = Lo - 1; I-- > 0;) {
    if (Toks[I].is(tok::annot_target_alt_end))
      ++Depth;
    else if (Toks[I].is(tok::annot_target_alt_begin) && Depth-- == 0)
      return Boundaries[I] ||
             followsAlternativesAtBoundary(Toks, Boundaries, I);
  }
  return false;
}

/// Whether the member declaration starting at \p Lo directly follows an
/// access specifier, as a class's first member does its `{`.
static bool followsAccessSpecifier(ArrayRef<Token> Toks, unsigned Lo) {
  return Lo >= 2 && Toks[Lo - 1].is(tok::colon) &&
         Toks[Lo - 2].isOneOf(tok::kw_public, tok::kw_protected,
                              tok::kw_private);
}

/// The half-open token range \p Group occupies, or an empty range if it cannot
/// be located -- a declaration built from tokens the merged stream does not
/// contain verbatim cannot be re-parsed from it.
static std::pair<unsigned, unsigned>
tokenRangeOf(Parser::DeclGroupPtrTy Group,
             const llvm::DenseMap<unsigned, unsigned> &Index,
             ArrayRef<Token> Toks) {
  unsigned Lo = ~0u, Hi = 0;
  for (Decl *D : Group.get()) {
    // A group can carry declarations that were never written -- implicit
    // members, injected names -- which have no token of their own. They are
    // skipped rather than treated as a failure to locate the group.
    auto B = Index.find(D->getBeginLoc().getRawEncoding());
    auto E = Index.find(D->getEndLoc().getRawEncoding());
    // One ending in a later `>` of a `>>` or `>>>` the parser split ends in it.
    for (int Offset = -1;
         E == Index.end() && D->getEndLoc().isValid() && Offset >= -2; --Offset)
      if (auto Split = Index.find(
              D->getEndLoc().getLocWithOffset(Offset).getRawEncoding());
          Split != Index.end() &&
          (Toks[Split->second].is(tok::greatergreatergreater) ||
           (Offset == -1 && Toks[Split->second].is(tok::greatergreater))))
        E = Split;
    if (B == Index.end() || E == Index.end())
      continue;
    Lo = std::min(Lo, B->second);
    Hi = std::max(Hi, E->second + 1);
  }
  if (Lo == ~0u)
    return {0, 0};
  // A declaration's begin location follows the attributes it starts with.
  while (Lo >= 2) {
    unsigned Open = Lo - 1, Depth = 0;
    tok::TokenKind Close = Toks[Open].getKind();
    if (Close != tok::r_paren &&
        !(Close == tok::r_square && Toks[Lo - 2].is(tok::r_square)))
      break;
    tok::TokenKind OpenKind =
        Close == tok::r_paren ? tok::l_paren : tok::l_square;
    for (;; --Open) {
      if (Toks[Open].is(Close))
        ++Depth;
      else if (Toks[Open].is(OpenKind) && --Depth == 0)
        break;
      if (!Open || Toks[Open].isOneOf(tok::semi, tok::l_brace, tok::r_brace))
        break;
    }
    if (Depth)
      break;
    if (Close == tok::r_paren && Open &&
        Toks[Open - 1].isOneOf(tok::kw_alignas, tok::kw__Alignas,
                               tok::kw___attribute, tok::kw___declspec))
      Lo = Open - 1;
    else if (Close == tok::r_square && Open + 1 < Lo &&
             Toks[Open + 1].is(tok::l_square))
      Lo = Open;
    else
      break;
  }
  // A declaration ends on ';' or '}'; getEndLoc names the token before it for
  // some kinds, so take the following semicolon if there is one.
  if (Hi && Hi < Toks.size() && Toks[Hi].is(tok::semi))
    ++Hi;
  return Lo <= Hi ? std::make_pair(Lo, Hi) : std::make_pair(0u, 0u);
}

/// An alternation marker, so the re-parse goes through the same path a spliced
/// alternative does: the parser enters the target's scope and marks whatever it
/// declares.
static Token altMarker(tok::TokenKind Kind, SourceLocation Loc, uintptr_t V) {
  Token T;
  T.startToken();
  T.setKind(Kind);
  T.setLocation(Loc);
  T.setAnnotationEndLoc(Loc);
  T.setAnnotationValue(reinterpret_cast<void *>(V));
  return T;
}

/// The declarations \p DG makes. A linkage specification without braces makes
/// those it wraps, which are parsed again with it.
static DeclGroupRef entitiesOf(ASTContext &Ctx, DeclGroupRef DG) {
  if (DG.isSingleDecl())
    if (auto *LSD = dyn_cast_or_null<LinkageSpecDecl>(DG.getSingleDecl());
        LSD && !LSD->hasBraces()) {
      SmallVector<Decl *, 4> Wrapped(LSD->decls());
      if (!Wrapped.empty())
        return DeclGroupRef::Create(Ctx, Wrapped.data(), Wrapped.size());
    }
  return DG;
}

bool clang::TargetAlternationReconciler::Observe(Parser::DeclGroupPtrTy &ADecl,
                                                 bool NotifyConsumer) {
  if (ConsumerStopped)
    return false;
  // Whether the declaration consumed something that differs between the
  // targets it is compiled for (see Sema::flushParseUnitInputs).
  bool ByDivergentInput = false;
  if (LLVM_UNLIKELY(S.MultiTarget().TargetInputs)) {
    SmallVector<Decl *, 4> Decls;
    if (ADecl) {
      DeclGroupRef DG = entitiesOf(S.getASTContext(), ADecl.get());
      Decls.append(DG.begin(), DG.end());
    }
    ByDivergentInput =
        S.MultiTarget().flushParseUnitInputs(Decls, P.inTargetAlternative());
    // The recorded inputs decide instead of the entities this declaration
    // touched. Not recorded: an architecture builtin.
    S.MultiTarget().TouchedDivergentEntity = false;
    S.MultiTarget().TouchedArchDivergentFunction = false;
  }
  // A shared declaration whose lookups reached a target-specific entity
  // means something different for each target, so parse it again for each
  // of the others. Bracketing the tokens with alternation markers routes it
  // through the same path a spliced alternative takes: the parser enters
  // that target's scope and marks whatever it declares.
  // Only shared declarations are candidates. A re-parsed copy arrives back
  // through this same loop, and without this it would be re-parsed again --
  // and its copies re-parsed, without end.
  if (!Reparse ||
      !(S.MultiTarget().TouchedDivergentEntity ||
        S.MultiTarget().TouchedAmbientTargetBuiltin || ByDivergentInput) ||
      !ADecl || P.inTargetAlternative()) {
    // Kept shared without a re-parse being considered.
    if (LLVM_UNLIKELY(S.MultiTarget().TargetInputs) && !Reparse &&
        ByDivergentInput && ADecl)
      S.MultiTarget().recordKeptShared(
          entitiesOf(S.getASTContext(), ADecl.get()));
    // A returning reparse alternate: record it for its batch's merge, which
    // runs once the batch's last alternate returns -- before the parser
    // advances to the next real declaration in the file, and before any of
    // the batch's alternates reach Consumer/CodeGen. The merge is what marks
    // an alternate TargetVariantRedundant (see mergeReparseAlternatives) --
    // CodeGen's shouldEmitForTargetVariant only skips a redundant decl's own
    // emission if that has already happened by the time this alternate
    // reaches Consumer/CodeGen; notifying first (the ordinary timing) would
    // let this alternate's own CodeGenModule emit it as still-live.
    // An alternate's members return here before it does; the declaration
    // returned last is the alternate's own, recorded when it ends.
    bool IsReparseAlternate = false;
    if (P.inTargetAlternative() && S.MultiTarget().InTargetFallbackReparse &&
        ADecl && !PendingReparseGroups.empty() &&
        PendingMergeAlternatesRemaining) {
      DeclGroupRef AlternativeGroup =
          entitiesOf(S.getASTContext(), ADecl.get());
      CurrentReparseAlternate = {
          SmallVector<Decl *, 4>(AlternativeGroup.begin(),
                                 AlternativeGroup.end()),
          P.getTargetAlternative(), ADecl, NotifyConsumer,
          P.getTargetAlternativeStandIn(P.getTargetAlternative())};
      IsReparseAlternate = true;
    }
    // If we got a null return and something *was* parsed, ignore it. This is
    // due to a top-level semicolon, an action override, or a parse error
    // skipping something. This notification is not given unconditionally as
    // soon as ADecl is parsed: a declaration that is still a candidate for
    // the reparse-and-merge treatment below must not reach here yet (it
    // takes the accepted-candidate path further down instead, which defers
    // this same call into PendingPrimaryEmit); a reparse alternate recorded
    // just above is notified by FlushPendingReparseBatch instead, once its
    // batch has been merged.
    if (!IsReparseAlternate) {
      unsigned CurTargetAlternative = P.getTargetAlternative();
      bool InWidenedAlternative =
          CurTargetAlternative && !S.MultiTarget().InTargetFallbackReparse;
      // A region is parsed once this is outside it or starts another.
      if (!S.MultiTarget().InTargetFallbackReparse &&
          CurTargetAlternative != LastTargetAlternative &&
          CurTargetAlternative <= 1 && !FlushPendingAlternativeEmit())
        return false;
      if (P.parsesIdenticalAlternativesOnce() && InWidenedAlternative &&
          NotifyConsumer && ADecl)
        PendingAlternativeEmit.push_back(ADecl.get());
      else if (!notify(ADecl, NotifyConsumer))
        return false;
    }
    // A genuine #if/#elif-widened alternative: reconcile it the same way and
    // for the same reason as the reparse case above, excluding the reparse
    // mechanism's own alternates (S.InTargetFallbackReparse), which the block
    // above already merges with its own bookkeeping. A reparse alternate
    // returning from a nested divergent-user reparse triggered from *inside*
    // a widened arm also has a nonzero P.getTargetAlternative() (its own
    // alternate index, not the enclosing arm's), so the whole block -- not
    // just the append -- must be skipped for it; otherwise it looks like a
    // transition between widened arms and corrupts the accumulation below
    // with unrelated declarations.
    if (!S.MultiTarget().InTargetFallbackReparse) {
      unsigned CurTargetAlternative = P.getTargetAlternative();
      if (CurTargetAlternative != LastTargetAlternative) {
        if (WideningCurVariant) {
          // Record this arm's declarations without reconciling them yet --
          // see reconcileWideningGroups's doc comment for why every arm
          // must finish its own pass first.
          WideningGroups.push_back({std::move(WideningCurDecls),
                                    WideningCurVariant, WideningCurStandIn,
                                    WideningCurRegion});
          WideningCurDecls.clear();
          WideningCurVariant = 0;
        }
        if (CurTargetAlternative <= 1)
          reconcileWideningGroups();
        LastTargetAlternative = CurTargetAlternative;
      }
      if (ADecl && CurTargetAlternative >= 1) {
        if (!WideningCurVariant) {
          WideningCurStandIn =
              P.getTargetAlternativeStandIn(CurTargetAlternative);
          WideningCurRegion = P.getTargetRegion();
        }
        WideningCurVariant = CurTargetAlternative;
        DeclGroupRef DG = entitiesOf(S.getASTContext(), ADecl.get());
        WideningCurDecls.append(DG.begin(), DG.end());
      }
    }
    S.MultiTarget().TouchedDivergentEntity = false;
    S.MultiTarget().TouchedArchDivergentFunction = false;
    S.MultiTarget().TouchedAmbientTargetBuiltin = false;
    return true;
  }
  const bool ByEntityLookup = S.MultiTarget().TouchedDivergentEntity;
  const bool ByArchDivergentFunction =
      S.MultiTarget().TouchedArchDivergentFunction;
  const bool ByAmbientBuiltin = S.MultiTarget().TouchedAmbientTargetBuiltin;
  S.MultiTarget().TouchedDivergentEntity = false;
  S.MultiTarget().TouchedArchDivergentFunction = false;
  S.MultiTarget().TouchedAmbientTargetBuiltin = false;
  auto [Lo, Hi] = tokenRangeOf(ADecl, LocIndex, MergedStream);
  DeclGroupRef Entities = entitiesOf(S.getASTContext(), ADecl.get());
  ++ReparseCandidates;
  // A declaration that CUDA attributes already pin to one target has one
  // meaning, so there is nothing for a second target to parse differently.
  // `__global__` is the sharpest case -- re-parsing `cumsum_kernel` for the
  // host produced a kernel nothing could launch and broke two-phase lookup
  // at its call site -- but `__device__`-only and `__host__`-only are the
  // same argument. 280 of the 438 host-primary re-parses were the
  // `__device__` overloads in `__clang_hip_cmath.h`, and re-parsing them gave
  // `abs` a second device definition.
  // This reasoning is about the host/device axis specifically, so it does
  // not apply when the only reason to reparse is an ambient-target builtin
  // (e.g. __builtin_amdgcn_processor_is): that answers differently across
  // device architectures, never across host vs. device, so a __device__-only
  // (or __global__) declaration can still genuinely diverge along that axis.
  //
  // It likewise does not apply when this whole compile has no host axis to
  // begin with. LangOptions::CUDAIsDevice is fixed for the entire cc1 job
  // (ASTContext::TargetScope deliberately never swaps it), so a
  // -fcuda-is-device job's target variants (primary and every aux) are all
  // device architectures -- there is no "the host" for a
  // __global__/__device__-only declaration to have a single meaning relative
  // to. A plain (non-builtin) callee like ggml_cuda_get_physical_warp_size,
  // whose *body* -- not signature -- is gated by raw target-CPU macros,
  // diverges across exactly this axis, and a __global__ kernel that calls it
  // must still be reparsed per device arch for its own divergent value
  // members/array bounds to resolve correctly
  // (MultiTarget/class-template-value-member.hip).
  //
  // Nor does it apply to a class layout or a template specialization that
  // differs between the targets the declaration is compiled for: those are
  // settled while it is analysed, so each target needs its own analysis. Nor
  // to device code calling a function the device architectures have
  // differently, which overload resolution can pick, or reject, differently
  // per architecture.
  if (!S.getLangOpts().CUDAIsDevice && ByEntityLookup && !ByDivergentInput &&
      llvm::any_of(Entities, [&](Decl *RD) {
        const auto *FD = dyn_cast<FunctionDecl>(RD);
        if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(RD))
          FD = FTD->getTemplatedDecl();
        if (!FD)
          return false;
        bool OnDevice =
            FD->hasAttr<CUDAGlobalAttr>() || FD->hasAttr<CUDADeviceAttr>();
        if (ByArchDivergentFunction && OnDevice)
          return false;
        return FD->hasAttr<CUDAGlobalAttr>() ||
               (FD->hasAttr<CUDADeviceAttr>() != FD->hasAttr<CUDAHostAttr>());
      })) {
    if (!notify(ADecl, NotifyConsumer))
      return false;
    ++ReparseRejected;
    traceReject(ADecl, "single-target", Hi - Lo, S);
    if (LLVM_UNLIKELY(S.MultiTarget().TargetInputs))
      S.MultiTarget().recordKeptShared(Entities);
    return true;
  }
  // A namespace or a linkage specification is a container, not an entity.
  // The divergent lookup happened in one declaration inside it, and
  // re-parsing the whole block re-creates every other declaration in it and
  // claims them all for the primary target: 72 of 100 candidates were
  // `namespace std`, 97.5% of the re-parsed tokens, and six copies of
  // std::basic_string, whose parameter list then had no default arguments.
  if (llvm::any_of(Entities, [](Decl *RD) {
        return isa<NamespaceDecl>(RD) || isa<LinkageSpecDecl>(RD);
      })) {
    if (!notify(ADecl, NotifyConsumer))
      return false;
    ++ReparseRejected;
    traceReject(ADecl, "container", Hi - Lo, S);
    if (LLVM_UNLIKELY(S.MultiTarget().TargetInputs))
      S.MultiTarget().recordKeptShared(Entities);
    return true;
  }
  // A field is part of its class's layout, which only re-parsing the class
  // gives each target. A second copy of the field in the same class made
  // every use of it ambiguous, and a template's was ignored.
  if (llvm::any_of(Entities, [](Decl *RD) {
        return isa<FieldDecl, IndirectFieldDecl>(RD);
      })) {
    if (const auto *Record =
            dyn_cast<CXXRecordDecl>((*Entities.begin())->getDeclContext()))
      if (const ClassTemplateDecl *CTD = Record->getDescribedClassTemplate())
        TemplatesWithTargetFields.insert(CTD);
      else if (isa<ClassTemplatePartialSpecializationDecl>(Record))
        TemplatesWithTargetFields.insert(Record);
    if (!notify(ADecl, NotifyConsumer))
      return false;
    ++ReparseRejected;
    traceReject(ADecl, "field", Hi - Lo, S);
    if (LLVM_UNLIKELY(S.MultiTarget().TargetInputs))
      S.MultiTarget().recordKeptShared(Entities);
    return true;
  }
  // A declaration with no tokens of its own has nothing to re-parse. There is
  // no size limit since the checks below keep the extent to one declaration.
  if (Lo == Hi) {
    if (!notify(ADecl, NotifyConsumer))
      return false;
    ++ReparseRejected;
    traceReject(ADecl, "empty-range", Hi - Lo, S);
    if (LLVM_UNLIKELY(S.MultiTarget().TargetInputs))
      S.MultiTarget().recordKeptShared(Entities);
    return true;
  }
  // The extent comes from source locations, and one that maps to the wrong
  // token gives a range covering neighbouring declarations too. Re-parsing
  // those re-creates them, and the copies are visible alongside the
  // originals: 346 "call is ambiguous", both candidates at the same line. A
  // re-parse must cover exactly one declaration.
  // A static data member or typedef after an access specifier, or first in
  // its class, is one too.
  bool StaticMemberAfterAccess =
      (followsAccessSpecifier(MergedStream, Lo) ||
       (Lo && MergedStream[Lo - 1].is(tok::l_brace) &&
        isTagBrace(MergedStream, Lo - 1))) &&
      llvm::all_of(Entities, [](Decl *RD) {
        const auto *VD = dyn_cast<VarDecl>(RD);
        return (VD && VD->isStaticDataMember()) || isa<TypedefNameDecl>(RD);
      });
  if (Lo && !Boundaries[Lo] && !StaticMemberAfterAccess &&
      !((ByDivergentInput || ByArchDivergentFunction) &&
        followsAlternativesAtBoundary(MergedStream, Boundaries, Lo))) {
    if (!notify(ADecl, NotifyConsumer))
      return false;
    ++ReparseRejected;
    traceReject(ADecl, "not-at-a-boundary", Hi - Lo, S);
    if (LLVM_UNLIKELY(S.MultiTarget().TargetInputs))
      S.MultiTarget().recordKeptShared(Entities);
    return true;
  }
  unsigned ChunkEnd = Lo;
  do
    ++ChunkEnd;
  while (ChunkEnd < Boundaries.size() - 1 && !Boundaries[ChunkEnd]);
  // A class's members are declarations too; a class that has to be analysed
  // per target spans their boundaries, and ends at one of its own. So does a
  // class template.
  if (Hi > ChunkEnd && ByDivergentInput &&
      llvm::any_of(Entities,
                   [&](Decl *D) {
                     return isa<TagDecl, ClassTemplateDecl>(D) ||
                            TemplatesWithTargetFields.count(D);
                   }) &&
      (Hi >= Boundaries.size() - 1 || Boundaries[Hi]))
    ChunkEnd = Hi;
  if (Hi > ChunkEnd) {
    if (!notify(ADecl, NotifyConsumer))
      return false;
    ++ReparseRejected;
    traceReject(ADecl, "spans-several-declarations", Hi - Lo, S);
    if (LLVM_UNLIKELY(S.MultiTarget().TargetInputs))
      S.MultiTarget().recordKeptShared(Entities);
    return true;
  }
  Hi = ChunkEnd;
  ReparseTokens += Hi - Lo;
  if (LLVM_UNLIKELY(multitarget::TraceReparse))
    for (Decl *RD : Entities)
      llvm::errs() << "reparse " << (Hi - Lo) << "t " << RD->getDeclKindName()
                   << " "
                   << (isa<NamedDecl>(RD)
                           ? cast<NamedDecl>(RD)->getNameAsString()
                           : std::string("<unnamed>"))
                   << " @ "
                   << RD->getLocation().printToString(S.getSourceManager())
                   << "\n";

  // Claim it for the primary first: while it still belongs to every target,
  // lookup during the re-parse finds it and merges the new declaration into
  // it, so nothing per-target is produced.
  P.ClaimForTargetVariant(ADecl, 1, /*IsReparseOrigin=*/true);
  // Defensive: a well-formed batch always reaches
  // PendingMergeAlternatesRemaining == 0 and flushes itself -- all of one
  // batch's alternates return through Observe consecutively before the next
  // real top-level declaration is reached -- but an abandoned batch must not
  // leave its primary or alternates un-notified.
  if (!FlushPendingReparseBatch(/*Complete=*/false))
    return false;
  PendingBatchAmbientBuiltinTriggered = ByAmbientBuiltin || ByDivergentInput;
  // This declaration's own HandleTopLevelDecl notification is held back
  // until its batch's merges complete (see PendingPrimaryEmit's doc comment
  // above), rather than given here as it would be for a non-candidate
  // declaration.
  PendingPrimaryEmit = ADecl;
  PendingPrimaryEmitNotify = NotifyConsumer;
  {
    DeclGroupRef OriginGroup = Entities;
    PendingReparseGroups.push_back(
        {SmallVector<Decl *, 4>(OriginGroup.begin(), OriginGroup.end()), 1u});
  }
  PendingMergeAlternatesRemaining = S.getASTContext().getNumAuxTargets();

  SourceLocation Loc = MergedStream[Lo].getLocation();
  // Every alternate has the first one's tokens, so the parser can let the
  // first stand for the targets its inputs do not tell apart. Not with
  // alternatives inside, which the parser tracks one region at a time.
  bool SameTokens =
      P.parsesIdenticalAlternativesOnce() &&
      llvm::none_of(MergedStream.slice(Lo, Hi - Lo), [](const Token &T) {
        return T.isOneOf(tok::annot_target_alt_begin, tok::annot_target_alt_sep,
                         tok::annot_target_alt_end);
      });
  auto Buf = std::make_unique<std::vector<Token>>();
  for (unsigned V = 2; V <= S.getASTContext().getNumAuxTargets() + 1; ++V) {
    // annot_target_alt_sep carries a 0-based alternative index.
    uintptr_t Marker = (V - 1) | Parser::FallbackReparseBit;
    if (SameTokens && V > 2)
      Marker |= uintptr_t(2) << TargetAltSameAsShift;
    Buf->push_back(altMarker(tok::annot_target_alt_sep, Loc, Marker));
    Buf->insert(Buf->end(), MergedStream.begin() + Lo,
                MergedStream.begin() + Hi);
    Buf->push_back(altMarker(tok::annot_target_alt_end, Loc, 0));
  }
  // The parser is already holding the first token of the *next* declaration.
  // A stream entered now would be spliced after it, cutting that declaration
  // in half, so it is appended and the look-ahead re-read from the stream --
  // the same thing late-parsed method bodies do.
  Buf->push_back(P.getCurToken());
  S.getPreprocessor().EnterTokenStream(*Buf, /*DisableMacroExpansion=*/true,
                                       /*IsReinject=*/true);
  P.ResetLookaheadFromStream();
  Reparsed.push_back(std::move(Buf));
  return true;
}

void clang::TargetAlternationReconciler::verifyStandIn(
    ArrayRef<Decl *> StandIn, unsigned StandInVariant, ArrayRef<Decl *> Decls,
    unsigned Variant) {
  ASTContext &Ctx = S.getASTContext();
  auto CPU = [&](unsigned V) {
    return Ctx.getTargetForVariant(V)->getTargetOpts().getDisplayName();
  };
  unsigned DiagID = S.getDiagnostics().getCustomDiagID(
      DiagnosticsEngine::Error,
      "declaration parsed from the same tokens for '%0' and '%1' means "
      "something else for each; parsing it once for both would be wrong");
  SourceLocation Loc = !Decls.empty()      ? Decls.front()->getLocation()
                       : !StandIn.empty() ? StandIn.front()->getLocation()
                                          : SourceLocation();
  if (StandIn.size() != Decls.size()) {
    S.Diag(Loc, DiagID) << CPU(StandInVariant) << CPU(Variant);
    return;
  }
  for (auto [A, B] : llvm::zip_equal(StandIn, Decls))
    if (A && B && !copiesMeanTheSame(Ctx, A, B))
      S.Diag(B->getLocation(), DiagID) << CPU(StandInVariant) << CPU(Variant);
}

void clang::TargetAlternationReconciler::NoteReparseAlternateEnd(
    bool Skipped) {
  std::optional<ReparseGroup> Alternate =
      std::exchange(CurrentReparseAlternate, std::nullopt);
  if (!PendingMergeAlternatesRemaining)
    return;
  if (!Skipped && Alternate)
    PendingReparseGroups.push_back(std::move(*Alternate));
  if (--PendingMergeAlternatesRemaining == 0 &&
      !FlushPendingReparseBatch(/*Complete=*/true))
    ConsumerStopped = true;
}

bool clang::TargetAlternationReconciler::FlushPendingReparseBatch(
    bool Complete) {
  if (Complete)
    for (const ReparseGroup &G : PendingReparseGroups)
      if (G.StandIn)
        for (const ReparseGroup &Ref : PendingReparseGroups)
          if (Ref.Variant == G.StandIn)
            verifyStandIn(Ref.Decls, Ref.Variant, G.Decls, G.Variant);
  // An alternate that did not return a declaration of the primary's shape,
  // e.g. after a parse error, cannot be shown to agree.
  if (Complete &&
      llvm::any_of(
          llvm::drop_begin(PendingReparseGroups), [&](const ReparseGroup &G) {
            return G.Decls.size() != PendingReparseGroups.front().Decls.size();
          }))
    Complete = false;
  if (Complete && !PendingBatchAmbientBuiltinTriggered &&
      PendingReparseGroups.size() > 1) {
    SmallVector<ReparseAlternate, 4> Alternates;
    for (const ReparseGroup &G : llvm::drop_begin(PendingReparseGroups)) {
      // An alternate stands for the targets of those skipped after it too.
      TargetSet Targets = TargetSet::single(G.Variant);
      for (Decl *D : G.Decls)
        if (D && D->getTargetVariant() == G.Variant)
          Targets = Targets | D->getTargetSet();
      Alternates.push_back({G.Decls, G.Variant, Targets});
    }
    mergeReparseAlternatives(S.getASTContext(),
                             PendingReparseGroups.front().Decls, Alternates);
  }
  if (PendingBatchAmbientBuiltinTriggered)
    for (const ReparseGroup &G : PendingReparseGroups)
      for (Decl *D : G.Decls)
        if (D)
          S.getASTContext().getMultiTarget().keepTargetCopiesApart(D);
  for (const ReparseGroup &G : PendingReparseGroups)
    for (Decl *D : G.Decls)
      if (D)
        S.getASTContext().getMultiTarget().noteReparseCopy(D);
  SmallVector<ReparseGroup, 4> Batch = std::move(PendingReparseGroups);
  PendingReparseGroups.clear();
  PendingMergeAlternatesRemaining = 0;
  PendingBatchAmbientBuiltinTriggered = false;
  for (ReparseGroup &G : Batch) {
    // The ambient each alternate was parsed under, as its notification would
    // have seen had it not been held back.
    ASTContext::TargetScope AlternateAmbient(S.getASTContext(), G.Variant);
    if (!notify(G.Emit, G.EmitNotify))
      return false;
  }
  return FlushPendingPrimaryEmit();
}

void clang::TargetAlternationReconciler::reconcileWideningGroups() {
  // Every arm in this episode is already fully parsed and Sema-checked by
  // this point (that is the whole reason this is deferred to episode-close
  // rather than run incrementally as each arm closes -- see this method's
  // doc comment), so all of them can be handed to mergeWidenedAlternatives
  // at once: an entity is only promoted to shared once every arm that
  // declares it is confirmed to agree, not just the first arm checked
  // against it -- see mergeWidenedAlternatives' doc comment for why partial
  // agreement isn't enough (e.g. host and one device arch agreeing while a
  // second, not-yet-checked device arch diverges).
  for (const WideningGroup &G : WideningGroups)
    if (G.StandIn)
      for (const WideningGroup &Ref : WideningGroups)
        if (Ref.Region == G.Region && Ref.Variant == G.StandIn)
          verifyStandIn(Ref.Decls, Ref.Variant, G.Decls, G.Variant);
  SmallVector<WidenedArm, 4> Arms;
  Arms.reserve(WideningGroups.size());
  for (const WideningGroup &G : WideningGroups) {
    // An arm parsed for several targets tagged its declarations with them.
    TargetSet Targets = TargetSet::single(G.Variant);
    for (const Decl *D : G.Decls)
      if (D && D->getTargetVariant() == G.Variant)
        Targets = Targets | D->getTargetSet();
    Arms.push_back({G.Decls, G.Variant, Targets});
  }
  mergeWidenedAlternatives(S.getASTContext(), Arms);
  S.getASTContext().getMultiTarget().resolveTargetExplicitSpecializations();
  WideningGroups.clear();
}

bool clang::TargetAlternationReconciler::Finish(bool FinalFlush) {
  if (ConsumerStopped)
    return false;
  if (FinalFlush && !FlushPendingAlternativeEmit())
    return false;
  // Defensive, mirroring the abandoned-batch check in Observe: every
  // well-formed batch flushes PendingPrimaryEmit itself before the file (or
  // the enclosing declaration-sequence) ends, but a batch truncated by EOF,
  // a parse error, or the end of a nested loop must still notify the
  // Consumer about its primary. Its alternates are only flushed at the true
  // end of the TU: a nested loop can end inside one of a still-live batch's
  // alternates (e.g. that alternate's own class body), which must not reach
  // Consumer/CodeGen before the batch is merged.
  if (FinalFlush ? !FlushPendingReparseBatch(/*Complete=*/false)
                 : !FlushPendingPrimaryEmit())
    return false;
  // A widened episode only reconciles once its ambient drops back to
  // shared/host (see reconcileWideningGroups's callers in Observe); a TU
  // that ends while still inside one (its last top-level construct was
  // itself a widened arm, with nothing shared following it) would otherwise
  // leave that episode's arms unreconciled forever. Only the true end of
  // the TU means this, not a nested declaration-sequence loop's own end --
  // this reconciler is one shared, TU-wide instance (see its class comment),
  // and a nested loop closing (e.g. a class body) does not itself mean the
  // widened episode that opened it is over; the very next top-level
  // declaration may continue the same episode across that boundary.
  if (FinalFlush && (WideningCurVariant || !WideningGroups.empty())) {
    if (WideningCurVariant) {
      WideningGroups.push_back(
          {std::move(WideningCurDecls), WideningCurVariant});
      WideningCurDecls.clear();
      WideningCurVariant = 0;
    }
    reconcileWideningGroups();
  }
  if (FinalFlush && Reparse &&
      LLVM_UNLIKELY(multitarget::DumpConditionalRegions))
    llvm::errs() << "re-parsed divergent users: " << ReparseCandidates
                 << " candidates, " << ReparseRejected << " rejected, "
                 << ReparseTokens << " tokens re-parsed\n";
  return true;
}

void Parser::InitializeTargetAlternationReconciler(
    Sema &S, ASTConsumer *Consumer, ArrayRef<Token> MergedStream,
    const llvm::DenseMap<unsigned, unsigned> &LocIndex,
    const std::vector<bool> &Boundaries, bool Reparse) {
  ActiveReconciler = std::make_unique<TargetAlternationReconciler>(
      *this, S, Consumer, MergedStream, LocIndex, Boundaries, Reparse);
}

bool Parser::ObserveForTargetReconciliation(DeclGroupPtrTy &ADecl,
                                            bool NotifyConsumer) {
  if (!ActiveReconciler)
    return true;
  return ActiveReconciler->Observe(ADecl, NotifyConsumer);
}

void Parser::noteReparseAlternateEnd(bool Skipped) {
  if (ActiveReconciler)
    ActiveReconciler->NoteReparseAlternateEnd(Skipped);
}

bool Parser::FinishTargetAlternationReconciliation(bool FinalFlush) {
  if (!ActiveReconciler)
    return true;
  return ActiveReconciler->Finish(FinalFlush);
}
