//===--- SemaLookupTarget.cpp - Lookup for several targets at once --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Which of a declaration's per-target copies a name lookup sees when code is
// analysed for some of the targets of a multi-target compilation.
//
//===----------------------------------------------------------------------===//

#include "SemaLookupTarget.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/MultiTargetContext.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaCUDA.h"
#include "clang/Sema/SemaMultiTarget.h"

using namespace clang;
using namespace clang::multitarget;

/// Resolves the result kind of this lookup.

/// A using-declaration re-parsed for each target adds a shadow of each
/// target's copy of what it names, but the shadows themselves stay untagged:
/// each belongs to the target of the copy it stands for.
const NamedDecl *multitarget::getTargetVariantCarrier(const NamedDecl *D) {
  if (const auto *Shadow = dyn_cast<UsingShadowDecl>(D))
    if (!Shadow->getTargetVariant()) {
      // A shadow is its using-declaration's, which can be one target's.
      if (const BaseUsingDecl *Introducer = Shadow->getIntroducer();
          Introducer && Introducer->getTargetVariant())
        return Introducer;
      return Shadow->getTargetDecl();
    }
  return D;
}

/// Whether \p D is a variable whose type is, or is an array of or a pointer
/// or reference to, a class whose layout differs between targets.
static bool hasTargetDivergentLayoutType(ASTContext &Ctx, const NamedDecl *D) {
  const auto *VD = dyn_cast<ValueDecl>(D);
  if (!VD || isa<FunctionDecl>(VD))
    return false;
  QualType T = VD->getType();
  while (true) {
    T = T.getNonReferenceType();
    if (const ArrayType *AT = Ctx.getAsArrayType(T))
      T = AT->getElementType();
    else if (const auto *PT = T->getAs<PointerType>())
      T = PT->getPointeeType();
    else
      break;
  }
  const RecordDecl *RD = T->getAsRecordDecl();
  return RD && Ctx.getMultiTarget().hasTargetDivergentLayout(RD);
}

/// Whether what is being analysed is evaluated while it is, e.g. a template
/// argument or the initializer of a constexpr variable.
static bool isEvaluatedNow(Sema &S) {
  if (S.isConstantEvaluatedContext())
    return true;
  const auto &Record = S.currentEvaluationContext();
  const auto *VD = dyn_cast_or_null<VarDecl>(Record.ManglingContextDecl);
  return Record.ExprContext ==
             Sema::ExpressionEvaluationContextRecord::EK_VariableInit &&
         VD && VD->isConstexpr();
}

/// Whether \p D belongs to the target currently being analysed.
///
/// Shared by ordinary name lookup and by argument-dependent lookup, which
/// builds its candidate set directly and so does not pass through
/// LookupResult at all.
/// Whether \p D, a class member, was instantiated from a member that is not
/// one target's copy.
static bool isInstantiatedFromSharedMember(const NamedDecl *D) {
  const Decl *Pattern = nullptr;
  if (const auto *VD = dyn_cast<VarDecl>(D))
    Pattern = VD->getInstantiatedFromStaticDataMember();
  else if (const auto *FD = dyn_cast<FunctionDecl>(D))
    Pattern = FD->getInstantiatedFromMemberFunction();
  else if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(D))
    Pattern = FTD->getInstantiatedFromMemberTemplate();
  return Pattern && !Pattern->getTargetVariant();
}

/// Which targets' declarations of \p FD give its parameters the same default
/// arguments. A definition taking a declaration's place in lookup leaves it
/// in its redeclaration chain only.
static TargetPartition defaultArgumentPartition(ASTContext &Ctx,
                                                const FunctionDecl *FD) {
  SmallVector<const NamedDecl *, 4> Copies;
  Ctx.getMultiTarget().getTargetCopies(FD, Copies);
  for (const NamedDecl *ND :
       FD->getDeclContext()->getRedeclContext()->lookup(FD->getDeclName()))
    if (const auto *Other = dyn_cast<FunctionDecl>(ND);
        Other && Ctx.hasSameType(Other->getType(), FD->getType()))
      Copies.push_back(Other);
  llvm::SmallPtrSet<const FunctionDecl *, 8> Seen;
  SmallVector<const FunctionDecl *, 8> Writers;
  for (const NamedDecl *Copy : Copies)
    if (const FunctionDecl *Function = Copy->getAsFunction())
      for (const FunctionDecl *Redecl : Function->redecls())
        if (!Redecl->isRedundantTargetVariant() && Seen.insert(Redecl).second &&
            llvm::any_of(Redecl->parameters(), [](const ParmVarDecl *P) {
              return P->hasDefaultArg() && !P->hasInheritedDefaultArg() &&
                     !P->hasUnparsedDefaultArg() &&
                     !P->hasUninstantiatedDefaultArg();
            }))
          Writers.push_back(Redecl);
  if (llvm::none_of(
          Writers, [](const FunctionDecl *W) { return W->getTargetVariant(); }))
    return 0;
  PrintingPolicy Policy = Ctx.getPrintingPolicy();
  return Ctx.getMultiTarget().partitionTargetsByAnswer([&](unsigned V) {
    std::string Defaults;
    llvm::raw_string_ostream OS(Defaults);
    for (unsigned I = 0, N = FD->getNumParams(); I != N; ++I) {
      OS << ',';
      for (const FunctionDecl *W : Writers) {
        const ParmVarDecl *P = W->getParamDecl(I);
        if ((!W->getTargetVariant() || W->isCopyFor(V)) && P->hasDefaultArg() &&
            !P->hasInheritedDefaultArg() && !P->hasUnparsedDefaultArg() &&
            !P->hasUninstantiatedDefaultArg())
          P->getDefaultArg()->printPretty(OS, nullptr, Policy);
      }
    }
    return llvm::hash_value(Defaults);
  });
}

/// A member of one target's copy of a class is found by looking into that
/// copy, which was chosen already; so is a member of a specialization
/// instantiated while some target was ambient from a member every target
/// shares, which the specialization has no other copy of.
static bool isFoundInChosenCopy(const NamedDecl *D, bool ForRedecl) {
  if (!ForRedecl && isa<ValueDecl, FunctionTemplateDecl>(D))
    if (const auto *Parent = dyn_cast<RecordDecl>(D->getDeclContext()))
      if (Parent->getTargetSet() == D->getTargetSet() ||
          isInstantiatedFromSharedMember(D))
        return true;
  return false;
}

/// A bare (non-redeclaration) reference to a target-tagged struct/class
/// always means the primary target's copy, regardless of which
/// alternative is currently being parsed: the struct's own divergent axis
/// (e.g. data layout) is independent of why the referencing code was
/// spliced for another target, so every caller must agree on which copy
/// "the type" names. What the copies declare differently stays each
/// target's own. Shared code consuming it is split per target, and a static
/// member read in a constant expression is read from the copy for the target
/// the code is compiled for. Scoped to RecordDecl, not TagDecl: unscoped enums
/// inject their enumerators into the enclosing scope, so an enumerator
/// reference is looked up independently and must still follow
/// ambient/Current; forcing the enum type itself to v1 would decouple it
/// from its own enumerators. Must be exclusive rather than a fallback,
/// since resolveKind() treats two simultaneously visible TagDecls of the
/// same name as ambiguous.
static bool isClassReferenceVisible(Sema &S, const NamedDecl *D, unsigned V) {
  // A member class instantiated from an untagged pattern, or from one
  // merged away, was tagged only with whichever target happened to be
  // ambient when it was completed; its enclosing specialization holds no
  // sibling copy of it.
  if (const auto *CXXRD = dyn_cast<CXXRecordDecl>(D))
    if (const CXXRecordDecl *MemberPattern =
            CXXRD->getInstantiatedFromMemberClass())
      if (!MemberPattern->getTargetVariant() ||
          MemberPattern->isRedundantTargetVariant())
        return true;
  // A class nested in a widened class copy, referenced from inside that
  // exact copy (e.g. `using T = S;` in the device copy's body), was found
  // in that copy's own member scope and is tagged to match it; no sibling
  // copy of it is reachable from there, so pinning it to V==1 only makes
  // the name undeclared. While the copy's own body is still being parsed
  // the enclosing class is not tagged yet, but its alternative's variant
  // is the one currently pushed.
  unsigned RawCurrent = S.getASTContext().getCurrentTargetVariant();
  if (const auto *ParentRD = dyn_cast<RecordDecl>(D->getDeclContext())) {
    unsigned ParentV = ParentRD->getTargetVariant();
    if (!ParentV && S.MultiTarget().InTargetAlternativeRegion)
      ParentV = RawCurrent;
    if (ParentV == V && ParentRD->Encloses(S.CurContext))
      return true;
  }
  // Where code is analysed for one target, a class whose layout, or what it
  // declares (e.g. a member initializer), differs between targets is that
  // target's.
  ASTContext &Ctx = S.getASTContext();
  const auto *RD = cast<RecordDecl>(D);
  bool Divergent = Ctx.getMultiTarget().hasTargetDivergentLayout(RD);
  if (!Divergent) {
    TargetPartition Shape = Ctx.getMultiTarget().getRecordShapePartition(RD);
    Divergent =
        RawCurrent ? Ctx.getMultiTarget().separatesTargets(Shape, RawCurrent, 1)
                   : Shape != 0;
  }
  if (Divergent && RawCurrent &&
      (S.MultiTarget().InTargetAlternativeRegion ||
       S.MultiTarget().InTargetFallbackReparse))
    return D->isCopyFor(RawCurrent);
  return D->isCopyFor(1);
}

/// Otherwise code outside the template's own copies names one copy, the
/// host's if there is one, as it does a class: specializations of it made
/// for a target instantiate that target's definition, and host and device
/// code agree on what the specializations are.
static bool namesPrimaryTemplateCopy(Sema &S, const NamedDecl *D,
                                     bool ForRedecl) {
  if (const auto *CTD = dyn_cast<ClassTemplateDecl>(D);
      CTD && !ForRedecl && !CTD->isTargetVariantReparseOrigin() &&
      (!S.MultiTarget().InTargetAlternativeRegion ||
       S.MultiTarget().InTargetFallbackReparse) &&
      !S.getASTContext().getMultiTarget().hasTargetDivergentLayout(CTD) &&
      !CTD->getTemplatedDecl()->Encloses(S.CurContext))
    return S.getASTContext().getMultiTarget().getCopyFor(CTD, 1) != nullptr;
  return false;
}

/// The target a lookup is for. Shared code reaching a target-specific entity
/// is noted and analysed for the primary target.
static unsigned getLookupTarget(Sema &S, const NamedDecl *D, bool ForRedecl,
                                bool SuppressDivergenceFlagForTags) {
  unsigned Current = S.getASTContext().getCurrentTargetVariant();
  if (!Current) {
    // Shared code reaching for a target-specific entity is what decides
    // whether divergence has to propagate. Count declarations, not lookups.
    //
    // A bare reference to a target-tagged *type* (or class template) is
    // exempted: once a specific class copy is picked here, its own divergent
    // members (if any) are reached later through member lookup scoped to
    // that copy, which needs no flag of its own. A referencing declaration
    // that never touches the diverging members behaves identically
    // regardless of which copy it resolves to, so it doesn't need a
    // per-target copy merely for naming the type.
    if (!SuppressDivergenceFlagForTags || !isa<TagDecl, ClassTemplateDecl>(D)) {
      S.MultiTarget().TouchedDivergentEntity = true;
      if (!ForRedecl && !S.MultiTarget().TouchedArchDivergentFunction &&
          S.getASTContext().getMultiTarget().isArchDivergentFunction(D))
        S.MultiTarget().TouchedArchDivergentFunction = true;
      // A constant that differs between the targets can be folded into a
      // type or a template argument while this code is analysed, and so can
      // the layout of a variable's type, which the host evaluates differently.
      ASTContext &Ctx = S.getASTContext();
      if (Ctx.getMultiTarget().isTargetDivergentSource(D)) {
        const FunctionDecl *FD = S.getCurFunctionDecl(/*AllowLambda=*/true);
        if (FD && isEvaluatedNow(S) &&
            Ctx.getMultiTarget().getTargetDependence(D) !=
                MultiTargetContext::TDK_None)
          S.MultiTarget().noteHostViewDependentUse(FD);
      } else if (hasTargetDivergentLayoutType(Ctx, D)) {
        if (const FunctionDecl *FD = S.getCurFunctionDecl(/*AllowLambda=*/true))
          S.MultiTarget().noteHostViewDependentUse(FD);
      }
    }
    Current = 1;
  }
  return Current;
}

/// Under host-primary ambient, TargetVariant 1 names the host, not a
/// device arch (see ASTContext::getTargetForVariant). A lookup performed
/// while substituting a device-only construct's body must not be forced
/// onto the host's own view.
///
/// This must fire whenever Current is exactly the default primary value 1
/// in host-primary shape, not only when ambient started out unset above:
/// ParseAST pushes a TU-wide ASTContext::TargetScope(Context, 1) for the
/// entire primary parse, so a device-only function's body instantiation
/// already has Current == 1 from that outer scope. Without this redirect,
/// a lookup like ggml_cuda_mma::tile<>::ne (tagged only for real device
/// variants, never host) fails before the per-target reparse pass produces
/// a working copy. Device-primary compiles (CUDAIsDevice) are excluded:
/// there, variant 1 already names a real device arch.
///
/// Also skipped when S.InTargetAlternativeRegion is set (genuinely inside
/// one widened #if/#elif/#else alternative's own pushed TargetScope, which
/// can itself be numbered 1), D's tag already equals Current == 1, and D
/// is a FieldDecl: a field declared inside that same alternative (e.g. a
/// local union's fields) is tagged to match, so this is already a correct
/// exact match, not a case needing redirection. Field lookup is scoped to
/// one already-resolved RecordDecl copy, so this cannot make a sibling
/// field from a different arm's copy of the same union simultaneously
/// visible.
///
/// Deliberately restricted to FieldDecl: isVisibleForTarget is called once
/// per candidate in a multi-candidate lookup (overload resolution,
/// partial-spec arm matching), and Current is recomputed independently for
/// each call. Exempting a broader class of D whose V happens to equal 1
/// would let it stay visible under Current == 1 while a different
/// candidate in the same set (V == 2, say) still takes the unconditional
/// redirect below, making both simultaneously visible. A FieldDecl's
/// owning RecordDecl is fixed before this function runs, so there is no
/// sibling-candidate set for the exemption to destabilize.
static unsigned getDeviceCodeTarget(Sema &S, const NamedDecl *D,
                                    unsigned Current, bool SoleCandidate) {
  if (LLVM_UNLIKELY(clang::AllowTargetVariantDecls) && Current == 1 &&
      !(S.MultiTarget().InTargetAlternativeRegion && D->isCopyFor(Current) &&
        isa<FieldDecl>(D)) &&
      !S.getLangOpts().CUDAIsDevice) {
    bool FoundExactContext = false;
    // Most direct: the enclosing function itself may carry its own
    // TargetVariant tag, one specific widened #if/#elif/#else arm's redecl.
    // A lookup inside its body for another member of the same class redecl
    // must agree with this exact tag, not get redirected to a sibling
    // redecl's copy by the canonical-device heuristic below, which is
    // blind to which specific redecl the reference is inside.
    if (const FunctionDecl *CurFD = S.getCurFunctionDecl()) {
      // A function template specialization's own FunctionDecl never carries
      // the tag -- only its template-instantiation pattern does -- so a
      // lookup inside a genuinely-tagged reparse copy's body would
      // otherwise fall through to the canonical-device fallback below and
      // get redirected onto a different real target.
      unsigned FDVariant = CurFD->getTargetVariant();
      bool FromPattern = false;
      if (!FDVariant) {
        if (const FunctionDecl *Pattern =
                CurFD->getTemplateInstantiationPattern()) {
          FDVariant = Pattern->getTargetVariant();
          FromPattern = true;
        }
      }
      if (FDVariant) {
        Current = FDVariant;
        FoundExactContext = true;
      }
    }
    // Last resort: the enclosing function carries no tag -- an ordinary call
    // from untagged/shared device code into a separately target-tagged
    // construct. Force onto a real device target so lookup succeeds;
    // post-hoc per-target redirect mechanisms correct the value/callee seen
    // by every other real target afterward.
    //
    // Skipped when D itself has an untagged ("shared", visible under any
    // ambient) sibling redeclaration: that sibling is already an
    // unambiguous, correct host-ambient answer on its own. Forcing Current
    // onto one specific real device variant here wouldn't help such a
    // reference and would instead make that variant's override
    // simultaneously visible next to the always-visible shared redecl,
    // turning a single unambiguous candidate into two.
    // Not in the host's own #if arm: that is the host's view of the code.
    // Device-only code is the devices' even where code analysed for every
    // target is the host's view: the host never compiles it.
    if (!FoundExactContext && !(S.MultiTarget().InTargetAlternativeRegion &&
                                !S.MultiTarget().InTargetFallbackReparse)) {
      if (const FunctionDecl *CurFD = S.getCurFunctionDecl()) {
        CUDAFunctionTarget FT = S.CUDA().IdentifyTarget(CurFD);
        if (FT == CUDAFunctionTarget::Device ||
            FT == CUDAFunctionTarget::Global) {
          bool HasUntaggedSibling = false;
          if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
            // Widened alternatives of the same declaration are not
            // necessarily linked into one redeclaration chain, so the
            // DeclContext's lookup table is what actually holds every
            // widened copy together.
            for (const NamedDecl *Sibling :
                 FD->getDeclContext()->lookup(FD->getDeclName())) {
              const auto *SiblingFD = dyn_cast<FunctionDecl>(Sibling);
              if (SiblingFD && !SiblingFD->getTargetVariant() &&
                  S.Context.hasSameType(SiblingFD->getType(), FD->getType())) {
                HasUntaggedSibling = true;
                break;
              }
            }
          } else if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(D)) {
            // Same as above, for a function *template*'s widened
            // alternatives: a plain dyn_cast<FunctionDecl> above misses this
            // decl kind entirely.
            for (const NamedDecl *Sibling :
                 FTD->getDeclContext()->lookup(FTD->getDeclName())) {
              const auto *SiblingFTD = dyn_cast<FunctionTemplateDecl>(Sibling);
              if (SiblingFTD && !SiblingFTD->getTargetVariant() &&
                  S.Context.hasSameType(
                      SiblingFTD->getTemplatedDecl()->getType(),
                      FTD->getTemplatedDecl()->getType())) {
                HasUntaggedSibling = true;
                break;
              }
            }
          }
          // Only escalate onto a real device variant when D doesn't already
          // match Current, and only when the caller guarantees D is the
          // sole member of its frozen candidate set (SoleCandidate):
          // escalating an already-matching candidate can hurt when a
          // differently-tagged sibling is simultaneously live in that set,
          // since that sibling's own call independently escalates Current
          // for itself and becomes visible too, producing two candidates
          // instead of one. Restricting the exemption to
          // LookupResult::resolveKind()'s N==1 fast path keeps it safe:
          // there, by construction, no other sibling can be in the same
          // frozen set. This matters for a lone frozen candidate with no
          // untagged sibling and no other sibling in its own set:
          // escalating Current away from 1 unconditionally would reject
          // the sole candidate outright with nothing else to accept
          // instead, even when V == 1 already matched.
          //
          // While a widened alternative is parsed (e.g. a kernel whose
          // __launch_bounds__ diverges, calling a helper whose body does
          // too), the device's copy of the primary's own function may not be
          // parsed yet; until it is, the primary's copy stands in for it.
          unsigned Device = S.getASTContext().getCanonicalDeviceVariant();
          bool StandsIn =
              S.MultiTarget().InTargetAlternativeRegion &&
              D->isCopyFor(Current) &&
              isa<FunctionDecl, FunctionTemplateDecl>(D) &&
              !findTargetVariantSibling(const_cast<NamedDecl *>(D), Device);
          // Where code analysed for every target is the host's view, only a
          // copy that differs in its body alone: one differing in its
          // signature makes the code using it analysed per target instead.
          bool SignatureDiffers = false;
          if (const FunctionDecl *Callee = D->getAsFunction()) {
            SmallVector<const NamedDecl *, 4> Copies;
            S.getASTContext().getMultiTarget().getTargetCopies(D, Copies);
            SignatureDiffers = llvm::any_of(Copies, [&](const NamedDecl *C) {
              const FunctionDecl *CopyFD = C->getAsFunction();
              return CopyFD && !S.getASTContext().hasSameType(
                                   CopyFD->getType(), Callee->getType());
            });
          }
          if (!HasUntaggedSibling &&
              (!SoleCandidate || !D->isCopyFor(Current)) && !StandsIn &&
              !SignatureDiffers) {
            Current = Device;
          }
        }
      }
    }
  }
  return Current;
}

bool multitarget::isVisibleForTarget(Sema &S, const NamedDecl *D,
                                     bool ForRedecl, bool AllowFallback,
                                     bool SuppressDivergenceFlagForTags,
                                     bool PreferPrimaryForRecordReferences,
                                     bool SoleCandidate) {
  // What a specialization's using-declaration introduces is what its
  // template's copy for the target names.
  if (LLVM_UNLIKELY(S.MultiTarget().TargetInputs) && !ForRedecl)
    if (const auto *Shadow = dyn_cast<UsingShadowDecl>(D))
      if (const BaseUsingDecl *Introducer = Shadow->getIntroducer();
          Introducer &&
          isa<ClassTemplateSpecializationDecl>(Introducer->getDeclContext()))
        S.MultiTarget().recordTargetLookup(Introducer, isEvaluatedNow(S));
  D = getTargetVariantCarrier(D);
  if (!ForRedecl)
    S.MultiTarget().recordTargetLookup(D, isEvaluatedNow(S));
  // A redeclaration of a function the targets declare with different default
  // arguments inherits each target's own.
  if (LLVM_UNLIKELY(S.MultiTarget().TargetInputs) && ForRedecl &&
      D->getTargetVariant() && isa<FunctionDecl>(D))
    if (TargetPartition Defaults =
            defaultArgumentPartition(S.getASTContext(), cast<FunctionDecl>(D)))
      S.MultiTarget().recordTargetInput('L', Defaults,
                                        D->getQualifiedNameAsString());
  unsigned V = D->getTargetVariant();
  if (!V) {
    return true;
  }
  if (V == Decl::TargetVariantRedundant)
    return false;

  if (isFoundInChosenCopy(D, ForRedecl))
    return true;

  // Excludes the injected-class-name: that reference is found via member
  // lookup already scoped to one specific RecordDecl copy, so it must
  // follow Current like any other member. Otherwise an out-of-line
  // template member definition for a non-primary target can't find its own
  // class's name.
  if (PreferPrimaryForRecordReferences && !ForRedecl && isa<RecordDecl>(D) &&
      !(isa<CXXRecordDecl>(D) && cast<CXXRecordDecl>(D)->isInjectedClassName()))
    return isClassReferenceVisible(S, D, V);

  if (namesPrimaryTemplateCopy(S, D, ForRedecl))
    return D->isCopyFor(1);

  unsigned Current =
      getLookupTarget(S, D, ForRedecl, SuppressDivergenceFlagForTags);
  Current = getDeviceCodeTarget(S, D, Current, SoleCandidate);
  if (D->isCopyFor(Current))
    return true;

  // A member of a class whose own type reference is forced to the primary
  // target above must accept that class's own primary-tagged members too:
  // once the type name resolves to the primary's copy, member lookup
  // inside it still names that copy's members, tagged to match their
  // enclosing class (v1), not whichever target is currently ambient.
  // Otherwise a divergent caller compiled for a non-primary target that
  // reaches into a primary-preferred class's members sees them as
  // belonging to "some other target" and fails lookup entirely.
  if (PreferPrimaryForRecordReferences && !ForRedecl && D->isCopyFor(1)) {
    if (const auto *RD = dyn_cast<RecordDecl>(D->getDeclContext()))
      if (RD->getTargetVariant())
        return true;
  }

  // A *reference* during a re-parse falls back to the primary target's
  // declaration: most tagged declarations carry a target only for having been
  // parsed inside a divergent region, and code re-analysed for another target
  // still has to name them. A *redeclaration* must not, or the declaration
  // being built merges into the other target's. Nor must a reference to a
  // function that other device architectures have copies of but this one
  // does not: its preprocessing left the function out.
  return AllowFallback && D->isCopyFor(1) &&
         S.MultiTarget().InTargetFallbackReparse && !ForRedecl &&
         !(Current >= 2 &&
           S.getASTContext().getMultiTarget().isArchDivergentFunction(D));
}

/// When member lookup scoped to one specific widened RecordDecl copy finds
/// only a TargetVariantRedundant field (its own copy of the member was
/// superseded by a different sibling copy's during
/// mergeWidenedAlternatives's cross-target field merge, TargetCopyMerge.cpp's
/// markRedundantTree/unclaimTree), retry the same member name against the
/// class's sibling redecls -- other #if/#elif/#else widened copies of the
/// same class, found via a DeclContext::lookup() walk of the enclosing scope
/// for the record's own name, since widened copies of a plain (non-template)
/// declaration are not linked into one Decl::redecls() chain (confirmed for
/// FunctionDecl siblings by the unsafeAtomicAdd regression above; the same is
/// true for RecordDecl siblings) -- to find the sibling whose own copy of the
/// member is the live, unclaimed one. Copies that define a class declared
/// before them are chained to that declaration instead, and the lookup table
/// holds only the most recent of them.
NamedDecl *clang::findLiveFieldInSiblingRecord(const FieldDecl *D) {
  const RecordDecl *RD = D->getParent();
  auto FindIn = [&](const NamedDecl *Sibling) -> NamedDecl * {
    const auto *SiblingRD = dyn_cast<RecordDecl>(Sibling);
    if (!SiblingRD || SiblingRD == RD || !SiblingRD->isCompleteDefinition())
      return nullptr;
    // Not lookup(): that searches the definition for the current target,
    // which for chained copies is the one D was found in.
    for (Decl *Member : SiblingRD->decls())
      if (auto *ND = dyn_cast<NamedDecl>(Member);
          ND && ND->getDeclName() == D->getDeclName() &&
          ND->getTargetVariant() != Decl::TargetVariantRedundant)
        return ND;
    return nullptr;
  };
  for (NamedDecl *Sibling : RD->getDeclContext()->lookup(RD->getDeclName()))
    if (NamedDecl *Live = FindIn(Sibling))
      return Live;
  for (const TagDecl *Redecl : RD->redecls())
    if (NamedDecl *Live = FindIn(Redecl))
      return Live;
  return nullptr;
}

/// A widened function or function template that mergeWidenedAlternatives (or
/// a reparse merge) folded into another target's matching copy is
/// TargetVariantRedundant, while the copy it was folded into became shared.
/// A reference frozen before that merge -- e.g. an unresolved call inside
/// another widened template's body -- still names the redundant copy; find
/// the shared one that replaced it.
///
/// Only a sibling that is the same entity -- the same canonical signature --
/// is substituted: a signature naming one arm's copy of a widened class
/// template matches another arm's only structurally, and substituting it
/// would mix the two arms' types. The exception is a copy of the same
/// declaration (see findTargetVariantSibling), whose signature names its own
/// arm's copy of a class template folded along with it (e.g. rocThrust's
/// make_partition returning simple_partition<T>).
///
/// Copies that redeclare a shared forward declaration are chained instead,
/// and the lookup table keeps only the most recent of them, which may itself
/// be redundant; the shared copy is then found in the redeclaration chain,
/// preferring the definition over the forward declaration.
NamedDecl *clang::findLiveFunctionSibling(NamedDecl *D) {
  if (!D->isFunctionOrFunctionTemplate())
    return nullptr;
  ASTContext &Ctx = D->getASTContext();
  for (NamedDecl *Sibling : D->getDeclContext()->lookup(D->getDeclName())) {
    if (Sibling == D || Sibling->getTargetVariant() != 0 ||
        Sibling->getKind() != D->getKind())
      continue;
    if (Sibling->getLocation() == D->getLocation() ||
        Ctx.isSameEntity(D, Sibling))
      return Sibling;
  }
  NamedDecl *Shared = nullptr;
  for (Decl *Redecl : D->redecls()) {
    if (Redecl == D || Redecl->getTargetVariant() != 0)
      continue;
    const auto *FD = Redecl->getAsFunction();
    if (FD && FD->isThisDeclarationADefinition())
      return cast<NamedDecl>(Redecl);
    if (!Shared)
      Shared = cast<NamedDecl>(Redecl);
  }
  return Shared;
}

/// Every target's copy of a declaration forked by widening or by the
/// per-target re-parse replays the same tokens, so the copies share a
/// location.
NamedDecl *clang::findTargetVariantSibling(NamedDecl *D, unsigned Variant) {
  for (NamedDecl *Sibling : D->getDeclContext()->lookup(D->getDeclName()))
    if (Sibling->isCopyFor(Variant) && Sibling->getKind() == D->getKind() &&
        Sibling->getLocation() == D->getLocation())
      return Sibling;
  return nullptr;
}

NamedDecl *LookupResult::getCopyIgnoringTargetVisibility(NamedDecl *D) const {
  unsigned V = D->getTargetVariant();
  if (V == Decl::TargetVariantRedundant) {
    // A copy merged away stands for the copy it was merged into.
    const Decl *Survivor =
        getSema().getASTContext().getMultiTarget().getTargetMergeSurvivor(D);
    if (Survivor && !Survivor->isRedundantTargetVariant())
      D = const_cast<NamedDecl *>(cast<NamedDecl>(Survivor));
    else
      D = findLiveFunctionSibling(D);
    if (!D)
      return nullptr;
    V = D->getTargetVariant();
  }
  if (V) {
    unsigned Current = getSema().getASTContext().getCurrentTargetVariant();
    if (Current && !D->isCopyFor(Current))
      if (NamedDecl *Sibling = findTargetVariantSibling(D, Current))
        D = Sibling;
  }
  return D;
}

/// Widened copies of a function (template) that redeclare one shared
/// declaration form a single redeclaration chain, and a DeclContext's lookup
/// table keeps only the most recent of them, which may belong to another
/// target. Copies that redeclare nothing are unchained siblings sharing one
/// location instead, and a candidate set frozen while parsing one target's
/// copy of a caller names only that target's copy of the callee; the caller
/// may then be merged and instantiated for every target. Either way, find the
/// copy that \p IsVisible accepts instead. The shared declaration is visible
/// to every target but is not this target's definition, so it is only the
/// last resort.
NamedDecl *multitarget::findTargetVisibleRedecl(
    NamedDecl *D, llvm::function_ref<bool(const NamedDecl *)> IsVisible) {
  // A using-declaration names the entity, whichever of its copies lookup
  // found when it was declared.
  if (auto *Shadow = dyn_cast<UsingShadowDecl>(D)) {
    NamedDecl *Target = Shadow->getTargetDecl();
    if (Target->getDeclContext()->isRecord())
      return nullptr;
    return IsVisible(Target) ? Target
                             : findTargetVisibleRedecl(Target, IsVisible);
  }
  if (!isa<FunctionDecl, FunctionTemplateDecl>(D))
    return nullptr;
  NamedDecl *Shared = nullptr;
  auto Accept = [&](NamedDecl *ND) {
    if (ND == D || !IsVisible(ND))
      return false;
    if (ND->getTargetVariant())
      return true;
    if (!Shared)
      Shared = ND;
    return false;
  };
  for (Decl *Redecl : D->redecls())
    if (Accept(cast<NamedDecl>(Redecl)))
      return cast<NamedDecl>(Redecl);
  // Copies from different #if arms are declared at different locations.
  SmallVector<const NamedDecl *, 4> Copies;
  D->getASTContext().getMultiTarget().getTargetCopies(D, Copies);
  for (const NamedDecl *Copy : Copies)
    if (Accept(const_cast<NamedDecl *>(Copy)))
      return const_cast<NamedDecl *>(Copy);
  return Shared;
}

bool LookupResult::isTargetVisible(const NamedDecl *D,
                                   bool SoleCandidate) const {
  return isVisibleForTarget(getSema(), D, isForRedeclaration(),
                            /*AllowFallback=*/true,
                            /*SuppressDivergenceFlagForTags=*/true,
                            /*PreferPrimaryForRecordReferences=*/true,
                            SoleCandidate);
}

bool SemaMultiTarget::isDeclVisibleForCurrentTarget(const NamedDecl *D,
                                                    bool ForRedecl,
                                                    bool AllowFallback) {
  return isVisibleForTarget(SemaRef, D, ForRedecl, AllowFallback);
}
