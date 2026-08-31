//===--- SemaMultiTarget.h - Semantic analysis for several targets -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares semantic analysis for a compilation for several targets
/// at once: the inputs each analysis consumes that differ between targets,
/// and the instantiations made again for the targets they differ for.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_SEMAMULTITARGET_H
#define LLVM_CLANG_SEMA_SEMAMULTITARGET_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/TargetSet.h"
#include "clang/AST/Type.h"
#include "clang/Basic/PartialDiagnostic.h"
#include "clang/Basic/SourceLocation.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Sema/SemaBase.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/SaveAndRestore.h"
#include <memory>
#include <optional>

namespace clang {
class ClassTemplateSpecializationDecl;
class Decl;
class DeclContext;
class DeclGroupRef;
class FunctionDecl;
class NamedDecl;
class Stmt;
class TargetInfo;
class TargetInputRecorder;
class TemplateArgument;
class ValueDecl;

class SemaMultiTarget : public SemaBase {
public:
  SemaMultiTarget(Sema &S);
  ~SemaMultiTarget();

  /// Set while re-parsing a shared declaration for another target, where a
  /// reference falls back to the primary target's declaration. Ordinary
  /// parsing must not: a LookupResult can be built under one ambient target
  /// and resolved under another.
  bool InTargetFallbackReparse = false;

  /// While the body of TargetBodyFunction is parsed again for the targets it
  /// differs for, those targets. The body is recorded as theirs, the
  /// function's own body is left alone.
  TargetSet TargetBodyPart;
  const FunctionDecl *TargetBodyFunction = nullptr;

  /// Diagnostics held back while instantiating device code for a target that
  /// might not compile it. See isTentativeTargetInstantiation.
  struct TargetDiagnosticCapture {
    SmallVector<PartialDiagnosticAt, 2> Diags;
    bool ErrorOccurred = false;
  };
  TargetDiagnosticCapture *CurrentTargetDiagnosticCapture = nullptr;

  /// Set when a lookup in shared code reaches a target-specific entity. The
  /// parser clears this before each top-level declaration and re-parses
  /// that declaration per target if it is set afterward.
  bool TouchedDivergentEntity = false;

  /// Set along with TouchedDivergentEntity when the entity is a function that
  /// the device architectures do not all have alike: one has no copy of it, or
  /// one with another type or CUDA target. Overload resolution can then pick
  /// differently per architecture, so device-only code is re-parsed too.
  bool TouchedArchDivergentFunction = false;

  /// Set when shared code evaluates an ambient-target predicate builtin
  /// (e.g. __builtin_amdgcn_processor_is), which answers differently per
  /// target but touches no target-specific entity a lookup could flag.
  /// Drives the same per-target re-parse as TouchedDivergentEntity.
  bool TouchedAmbientTargetBuiltin = false;

  /// Set by Parser::HandleTargetAlternationMarker while an
  /// annot_target_alt_begin/sep/end-delimited alternative's own tokens are
  /// being parsed. Lets isVisibleForTarget's host-primary device-context
  /// fallback distinguish "Current == 1 because we are inside alternative
  /// 1's own pushed TargetScope" (a V == 1 declaration here is a deliberate
  /// match, e.g. a local union's fields declared in that same alternative)
  /// from "Current == 1 only because nothing more specific ever set it"
  /// (ordinary shared code, where the fallback must still redirect Current
  /// onto a real device variant) -- both look identical as a bare
  /// Current == 1 comparison.
  bool InTargetAlternativeRegion = false;

  /// The same per-target visibility rule LookupResult applies to ordinary
  /// name lookup (see LookupResult::isTargetVisible), exposed for callers
  /// that collect candidate Decls directly -- e.g. class template partial
  /// specialization matching, which enumerates
  /// ClassTemplateDecl::getPartialSpecializations() and would otherwise see
  /// every target's partial specialization as an equally-good match.
  bool isDeclVisibleForCurrentTarget(const NamedDecl *D, bool ForRedecl = false,
                                     bool AllowFallback = true);

  /// Gives \p FD, parsed as every target's and now one target's copy, the
  /// default arguments it inherits from that target's earlier declaration.
  void inheritTargetDefaultArguments(FunctionDecl *FD);

  /// The ambient target variant a pending instantiation was queued under,
  /// captured at the point it was first referenced (ODR-used) and lost to
  /// deferral otherwise: by the time PerformPendingInstantiations drains the
  /// queue at the end of the translation unit, the ambient variant is always
  /// 0, regardless of which target actually reached the callee first.
  ///
  /// For a __device__/__global__ callee this is forced to 1 regardless of the
  /// caller's ambient, so that its body is instantiated (and its own nested
  /// callees resolved) under the true device TargetInfo -- see the capture
  /// sites in SemaExpr.cpp. That forcing is right for compiling the body, but
  /// wrong for deciding provenance: see PendingInstantiationProvenanceVariant.
  llvm::DenseMap<const ValueDecl *, unsigned> PendingInstantiationTargetVariant;

  /// Like PendingInstantiationTargetVariant, but never forced to 1 for a
  /// __device__/__global__ callee: this is the caller's actual ambient at the
  /// point of first reference, unconditionally. A reparse-duplicated function
  /// template (see mmq.cu's mul_mat_q_case) produces a second copy of its
  /// definition tagged variant 2; calls made from inside that copy's body
  /// queue their callees (e.g. mul_mat_q, a __global__ kernel) with the same
  /// provenance, even though those callees' own bodies must still compile
  /// under the true device TargetInfo (variant 1). Stamping the resulting
  /// Decl's TargetVariant from this map, rather than from the (possibly
  /// forced-to-1) TargetInfo-scope ambient, is what lets CodeGen's existing
  /// TargetVariant>1 skip filter recognize the callee as attributable to the
  /// redundant copy and suppress it, without affecting how its body is
  /// compiled.
  llvm::DenseMap<const ValueDecl *, unsigned>
      PendingInstantiationProvenanceVariant;

  /// The target variant ambient when a function template specialization's
  /// declaration was instantiated. Its signature may name specializations of
  /// class templates keyed by target, so its body must be instantiated under
  /// the same target, or the body names another target's specializations.
  /// No target (outside every widened region) keys as the primary, 1.
  llvm::DenseMap<const FunctionDecl *, unsigned> SpecializationDeclVariant;

  /// The target the code being analysed or instantiated is compiled for, in
  /// a multi-target compilation: the target key of the
  /// innermost keyed specialization being instantiated; the device for a
  /// device-only function; otherwise the target that started the innermost
  /// substitution, or the ambient one. See
  /// MultiTargetContext::computeTargetInstantiationKey.
  unsigned getInstantiationTarget() const;

  /// A template's definition was analysed for the primary target, so where
  /// it names one target's copy of a target-divergent constant (see
  /// MultiTargetContext::isTargetDivergentSource), the copy for the target it
  /// is now instantiated for; null if that is \p D itself.
  NamedDecl *getInstantiationTargetCopy(NamedDecl *D);

  /// Whether \p T is, or is a pointer, reference or array to, one target's
  /// copy of a class whose layout differs between targets: analysed once for
  /// a template's definition, it is each instantiation's own target's there.
  bool namesTargetDivergentLayout(QualType T) const;

  /// Whether \p T names a member typedef that the target an instantiation is
  /// made for has its own copy of.
  bool namesOtherTargetsMemberType(QualType T) const;

  /// \p FD, a member of a specialization of a class template copied per
  /// target: the definition of the same member of the specialization made
  /// for \p Target, which specializes the template from its own copy; see
  /// MultiTargetContext::getTargetDefinitionForEvaluation. Instantiated if need
  /// be while code is being analysed.
  const FunctionDecl *getTargetSpecializationMember(const FunctionDecl *FD,
                                                    unsigned Target);

  /// Whether what is being parsed is part of the header of a template with
  /// parameters, e.g. a default template argument or the arguments of a
  /// partial specialization, rather than its body.
  bool inTemplateHeader() const;

  /// A class specialization keyed by one target (see
  /// MultiTargetContext::computeTargetInstantiationKey) passed as a template
  /// argument into code instantiated for another, e.g. a kernel's traits class
  /// named by the host code launching it, stands for the other target's
  /// specialization of the same arguments there.
  QualType getTypeForInstantiationTarget(QualType T, SourceLocation Loc);

  /// The number of \p CTSD's template arguments it is named with for
  /// \p Target. Trailing arguments \p Target's default arguments give are
  /// left out, so that naming the class for another target takes that
  /// target's defaults.
  unsigned getWrittenArgumentCount(const ClassTemplateSpecializationDecl *CTSD,
                                   unsigned Target, SourceLocation Loc);

  /// \p T, written in code analysed for one target, as written in code
  /// analysed for \p Target: a class template specialization keyed by target,
  /// or one with such a type among its arguments, is formed again from its
  /// arguments as written, since the defaulted ones can differ between
  /// targets.
  QualType getWrittenTypeForTarget(QualType T, unsigned Target,
                                   SourceLocation Loc);

  /// Instantiates a specialization keyed by the canonical device target
  /// whose template depends on the device architecture once more for each
  /// other device target, which then compiles its own; and the canonical
  /// device's copy of an explicit instantiation keyed by the host of a
  /// function the devices can call.
  void instantiateForOtherDeviceTargets(SourceLocation PointOfInstantiation,
                                        FunctionDecl *Function);

  /// Instantiates \p Function, a kernel whose template arguments name a class
  /// copied per target, once more for each device with a copy of its own,
  /// with that copy as the argument.
  void instantiateKernelForClassCopies(SourceLocation PointOfInstantiation,
                                       FunctionDecl *Function);
  llvm::SmallPtrSet<const FunctionDecl *, 4> KernelClassCopies;

  /// Instantiates, for \p Function, a member of a specialization of one
  /// target's copy of a class template, the same member of each other
  /// device's copy, which those devices compile in its place.
  void instantiateMemberForClassCopies(SourceLocation PointOfInstantiation,
                                       FunctionDecl *Function);
  llvm::SmallPtrSet<const FunctionDecl *, 4> ClassCopyMembers;
  /// Whether such a member, which no target may compile, is being
  /// instantiated.
  bool InClassCopyMember = false;

  /// Notes the code referencing \p Func, a function to be instantiated: the
  /// innermost instantiation or the function being analysed, if any. The
  /// other devices instantiate it only for the targets that code is for.
  void noteInstantiationRequester(const FunctionDecl *Func);
  /// Notes that code for \p Variant calls \p Func in place of what a caller
  /// standing for several targets bound.
  void noteRequestedForTarget(const FunctionDecl *Func, unsigned Variant);
  /// Makes what was made for the code requesting \p Func, and what that
  /// requested in turn, for the targets added since.
  void propagateRequestingTargets(const FunctionDecl *Func);

  /// The targets the code requesting \p Function stands for (see
  /// noteInstantiationRequester), every target if that is not known. An
  /// instantiation for every target stands for every target, or with
  /// \p FollowInstantiations for the targets of the code requesting it.
  TargetSet getRequestingTargets(const FunctionDecl *Function,
                                 bool FollowInstantiations = false,
                                 unsigned Depth = 0);

  /// The targets requesting a function when its body was last split or its
  /// other devices' specializations made, and the functions each
  /// function requested. See noteInstantiationRequester.
  llvm::DenseMap<const FunctionDecl *, TargetSet> RequestingTargetsUsed;
  llvm::DenseMap<const FunctionDecl *, SmallVector<const FunctionDecl *, 2>>
      RequestedFunctions;
  /// See noteRequestedForTarget.
  llvm::DenseMap<const FunctionDecl *, TargetSet> RequestedForTargets;

  /// See noteInstantiationRequester. Null stands for code outside functions.
  llvm::DenseMap<const FunctionDecl *, SmallVector<const FunctionDecl *, 1>>
      InstantiationRequesters;

  /// Explicitly instantiates \p Specialization's arguments in each other
  /// target's copy of its template, as \p TSK: an explicit instantiation names
  /// one copy, but stands for every target's.
  /// Where code analysed for every target is the host's view, a function
  /// template is copied per target where that matters, and code for every
  /// target names one copy's specialization. Make each other device target's
  /// copy's specialization with the same arguments, for that target's
  /// CodeGen to emit under the same name.
  void instantiateTargetCopySpecializations();
  /// Makes the other targets' copies' specializations for \p Spec, as
  /// instantiateTargetCopySpecializations does. With \p Now, a kernel's are
  /// made right away, in the order the targets' own compiles make them.
  void instantiateTargetCopySpecialization(FunctionDecl *Spec, bool Now);
  void instantiateInOtherTargetCopies(SourceLocation Loc,
                                      FunctionDecl *Specialization,
                                      TemplateSpecializationKind TSK);

  /// Keys the specializations made of templates found to depend on the
  /// target only later, once a definition they had not seen was parsed (see
  /// MultiTargetContext::takeTargetDependenceEscalations).
  void keySpecializationsMadeTooEarly();

  /// The host compilation of the regular pipeline analyses device code too,
  /// as the host sees it, and reports what its host-only `#if` arms get wrong.
  /// Device code here is instantiated for the devices only, so a
  /// specialization made for the canonical device target and named by code
  /// that means the same to the host is instantiated once more as the host
  /// sees it, for its diagnostics only.
  void instantiateHostView(SourceLocation Loc, FunctionDecl *Function);

  /// \p Func is referenced: notes whether its instantiation needs the host's
  /// view too (see instantiateHostView).
  void noteHostViewReference(FunctionDecl *Func);

  /// Whether the code being analysed means to the host what it means to the
  /// devices, so what it names is what the host would name.
  bool isAnalysedAsHostWould() const;

  /// \p FD, device code, evaluated something that the host evaluates
  /// differently (see isAnalysedAsHostWould).
  void noteHostViewDependentUse(const FunctionDecl *FD);

  /// What each unit analysed for several targets consumed that differs
  /// between them (see SemaTargetInputs.cpp). Null unless this is a
  /// multi-target compilation.
  std::unique_ptr<TargetInputRecorder> TargetInputs;
  /// With MultiTargetContext::RecordGroupInputs: which targets the inputs
  /// consumed since the parser last reset it tell apart.
  TargetPartition GroupInputs = 0;
  /// The language options a multi-target compilation's targets set
  /// differently: each target's value, by target variant - 1.
  llvm::StringMap<SmallVector<uint64_t, 4>> TargetLangOptDivergence;
  void initTargetInputs();
  /// Which targets what the body of \p FD, a member function, consumed tells
  /// apart, or 0.
  TargetPartition getBodyTargetSplit(const FunctionDecl *FD) const;
  /// Records an input of the unit being analysed, or of declaration \p Owner
  /// if the input decides only what that declaration means: \p Kind as in
  /// TargetInputRecorder::Input.
  /// \p JoinTargets: see TargetInputRecorder::Input; ~0u for \p Targets.
  /// These record nothing unless inputs are recorded.
  void recordTargetInput(char Kind, TargetPartition Targets, StringRef What,
                         const Decl *Owner = nullptr,
                         TargetPartition JoinTargets = ~0u) {
    if (LLVM_UNLIKELY(TargetInputs))
      recordTargetInputImpl(Kind, Targets, What, Owner, JoinTargets);
  }
  /// Lookup found \p D, in an evaluated context if \p Evaluated.
  void recordTargetLookup(const NamedDecl *D, bool Evaluated) {
    if (LLVM_UNLIKELY(TargetInputs))
      recordTargetLookupImpl(D, Evaluated);
  }
  /// Whether -multi-target-audit measures splitting shared instantiations.
  bool measuresTargetParts() const;
  /// Measures how the inputs \p Function's body consumed, instantiated for
  /// \p View, would split \p Own, the targets compiling it. \p Redirected:
  /// its body names a divergent callee or member.
  void auditTargetParts(const FunctionDecl *Function, unsigned View,
                        TargetSet Own, bool Redirected);
  /// Measures splitting \p Function's just-instantiated body.
  void measureTargetParts(FunctionDecl *Function);
  /// The groups of \p Own that what \p Function's body consumed while it was
  /// instantiated, and \p Extra, tell apart.
  SmallVector<TargetSet, 4> getInstantiationParts(const FunctionDecl *Function,
                                                  TargetSet Own,
                                                  TargetPartition Extra) const;
  /// Instantiates \p Function's body again for each group of the targets
  /// compiling it that what the body consumed tells apart, other than the
  /// group it was instantiated for.
  void instantiateTargetParts(FunctionDecl *Function);
  /// Whether \p Function's signature names a specialization of \p Template.
  bool signatureNamesTemplate(const FunctionDecl *Function,
                              const NamedDecl *Template) const;
  /// The target each function split into parts was first instantiated for.
  llvm::DenseMap<const FunctionDecl *, unsigned> TargetPartMainTarget;
  /// The layout of \p T is used.
  void recordTargetType(QualType T) {
    if (LLVM_UNLIKELY(TargetInputs))
      recordTargetTypeImpl(T);
  }
  /// Whether this compile is for the host or a device was read at \p Site:
  /// \p Kind 'C' where it decides what the code means, 'D' where it only
  /// decides how a diagnostic is reported.
  void recordCUDAModeRead(char Kind, StringRef Site,
                          const Decl *Owner = nullptr) {
    if (LLVM_UNLIKELY(TargetInputs))
      recordCUDAModeReadImpl(Kind, Site, Owner);
  }
  /// The language option \p Name decided what the code means.
  void recordLangOptRead(StringRef Name, const Decl *Owner = nullptr) {
    if (LLVM_UNLIKELY(TargetInputs))
      recordLangOptReadImpl(Name, Owner);
  }
  /// A target query answered by \p Query decided what the code means.
  void recordTargetQuery(StringRef What,
                         llvm::function_ref<bool(const TargetInfo &)> Query,
                         const Decl *Owner = nullptr) {
    if (LLVM_UNLIKELY(TargetInputs))
      recordTargetQueryImpl(What, Query, Owner);
  }
  /// Parsing \p Decls is done; their inputs are attributed to them. Returns
  /// whether they consumed an input that differs between the targets they
  /// are compiled for, so that they have to be analysed per target.
  bool flushParseUnitInputs(ArrayRef<Decl *> Decls, bool TargetSpecific);
  /// Notes that the declaration being parsed uses \p D.
  void recordKeptSharedUse(const Decl *D);
  /// \p DG is analysed for every target after all.
  void recordKeptShared(DeclGroupRef DG);
  /// A default argument of template parameter \p Param is substituted: its
  /// references are the user's inputs.
  void recordDefaultArgument(const NamedDecl *Param);
  void writeTargetInputReport();

  /// See instantiateHostView.
  llvm::DenseSet<const FunctionDecl *> HostViewRoots;
  llvm::DenseSet<const FunctionDecl *> HostViewTrustedShared;
  llvm::DenseSet<const FunctionDecl *> HostViewDependentFunctions;
  llvm::DenseSet<const FunctionDecl *> HostViewTwins;

  /// While set, the target getInstantiationTarget answers with.
  unsigned ForcedInstantiationTarget = 0;

  /// While it lives, the code being analysed and what it instantiates are
  /// for \p Target.
  class TargetAnalysisScope {
    ASTContext::TargetScope Scope;
    llvm::SaveAndRestore<unsigned> Forced;

  public:
    TargetAnalysisScope(SemaMultiTarget &MT, unsigned Target)
        : Scope(MT.getASTContext(), Target),
          Forced(MT.ForcedInstantiationTarget, Target) {}
  };
  /// While set, the target whose copies getInstantiationTargetCopy names: that
  /// of the member copy being instantiated.
  unsigned MemberCopyTarget = 0;

  /// Instantiates \p PatternDecl's body for \p Function as the body of the
  /// targets \p Part, analysed for \p View, if \p Keep. Returns the body, or
  /// null if it did not compile, with what it reported in \p Capture.
  Stmt *instantiateBodyPart(FunctionDecl *Function,
                            const FunctionDecl *PatternDecl, TargetSet Part,
                            unsigned View, bool Keep, DeclContext *DC,
                            std::optional<ArrayRef<TemplateArgument>> Innermost,
                            TargetDiagnosticCapture &Capture,
                            Stmt *Pattern = nullptr);
  /// Instantiates \p PatternDecl's body for \p Function as the body of the
  /// targets \p Part, analysed for \p View. If it does not compile, those
  /// targets do not compile it. Otherwise what it reported is reported.
  void instantiatePart(FunctionDecl *Function, const FunctionDecl *PatternDecl,
                       TargetSet Part, unsigned View, DeclContext *DC,
                       std::optional<ArrayRef<TemplateArgument>> Innermost,
                       Stmt *Pattern = nullptr);
  /// \p Function's body was instantiated from \p UsedPattern, a body of
  /// \p PatternDef or of another definition of its template for some targets.
  /// Instantiates the others as the bodies of their targets.
  void instantiateForOtherDefinitions(
      FunctionDecl *Function, const FunctionDecl *PatternDef, Stmt *UsedPattern,
      DeclContext *DC, std::optional<ArrayRef<TemplateArgument>> Innermost);
  /// \p Function's body did not compile for \p View, reporting \p ViewDiags.
  /// Instantiates it for each other device it is for, as a body of their own,
  /// and records the devices it does not compile for (see
  /// MultiTargetContext::getFailedTargetInstantiation).
  void
  instantiateForOtherViews(FunctionDecl *Function, unsigned View,
                           ArrayRef<PartialDiagnosticAt> ViewDiags,
                           DeclContext *DC,
                           std::optional<ArrayRef<TemplateArgument>> Innermost);

private:
  void recordTargetInputImpl(char Kind, TargetPartition Targets, StringRef What,
                             const Decl *Owner, TargetPartition JoinTargets);
  void recordTargetLookupImpl(const NamedDecl *D, bool Evaluated);
  void recordTargetTypeImpl(QualType T);
  void recordCUDAModeReadImpl(char Kind, StringRef Site, const Decl *Owner);
  void recordLangOptReadImpl(StringRef Name, const Decl *Owner);
  void recordTargetQueryImpl(StringRef What,
                             llvm::function_ref<bool(const TargetInfo &)> Query,
                             const Decl *Owner);
};

} // namespace clang

#endif // LLVM_CLANG_SEMA_SEMAMULTITARGET_H
