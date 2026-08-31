//===- MultiTargetContext.h - State of a multi-target compile ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// What a compilation for several targets at once keeps beside its AST: the
// copies of declarations made for some of the targets, how targets are
// partitioned by what an analysis consumed, specializations keyed by target,
// and the bodies, definitions and default arguments each target uses.
//
// ASTContext owns one only for such a compilation (getMultiTarget). The
// targets themselves, their numbering and the current target stay with
// ASTContext, which ordinary compiles use too.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_MULTITARGETCONTEXT_H
#define LLVM_CLANG_AST_MULTITARGETCONTEXT_H

#include "clang/AST/ASTContext.h"
#include "clang/AST/TargetSet.h"

namespace clang {

class MultiTargetContext {
  friend class ASTContext;
  ASTContext &Ctx;

public:
  explicit MultiTargetContext(ASTContext &Ctx) : Ctx(Ctx) {}
  MultiTargetContext(const MultiTargetContext &) = delete;
  MultiTargetContext &operator=(const MultiTargetContext &) = delete;

private:
  /// A copy mergeEquivalentVariants marked TargetVariantRedundant, mapped to
  /// the copy it was merged into. Code analysed before the merge can name
  /// the redundant copy.
  llvm::DenseMap<const Decl *, const Decl *> TargetMergeSurvivors;

  /// Bodies of a function definition parsed again for some of its targets,
  /// each with the targets it stands for. The definition's own body stands
  /// for the others.
  llvm::DenseMap<const FunctionDecl *,
                 SmallVector<std::pair<TargetSet, Stmt *>, 2>>
      TargetBodies;

  /// Device code instantiated for targets that might not compile it, which
  /// did not compile for some, with what each of those reported.
  llvm::DenseMap<
      const FunctionDecl *,
      SmallVector<std::pair<TargetSet, SmallVector<PartialDiagnosticAt, 2>>, 1>>
      FailedTargetInstantiations;

  /// Specializations of one target's copy of a function template, in the
  /// order they were made. See Sema::instantiateTargetCopySpecializations.
  SmallVector<FunctionDecl *, 0> TargetCopySpecializations;
  /// Declarations re-parsed per target, and the shared ones they were
  /// re-parsed from.
  llvm::DenseSet<const Decl *> ReparseCopies;
  /// Explicit specializations some targets declare and others do not, by
  /// their template, each with the declaration whose targets have it. That is
  /// a member's for a class specialization with an explicitly specialized
  /// member.
  llvm::DenseMap<const Decl *, SmallVector<std::pair<Decl *, const Decl *>, 1>>
      TargetExplicitSpecializations;
  /// Explicit specializations parsed for some targets, as above, until the
  /// alternatives they were parsed in are merged.
  SmallVector<std::tuple<const Decl *, Decl *, const Decl *>, 0>
      PendingTargetExplicitSpecializations;

  /// Records which target variant a class template specialization's pattern
  /// was tagged for (see
  /// ClassTemplateDecl::hasTargetTaggedPartialSpecialization()). A nested
  /// lookup while instantiating one of the specialization's own members has
  /// no enclosing FunctionDecl to consult -- CurContext is the specialization
  /// itself, a RecordDecl -- so isVisibleForTarget's ambient fallback walks
  /// CurContext looking for an entry here instead of defaulting to the host
  /// variant. Populated by getPatternForClassTemplateSpecialization
  /// (SemaTemplateInstantiate.cpp) once it picks a target-tagged pattern.
  llvm::DenseMap<const ClassTemplateSpecializationDecl *, unsigned>
      TargetVariantForSpecialization;

  /// Variables a device compilation would implicitly make __constant__ (see
  /// SemaCUDA::MaybeAddConstantAttr), recorded instead of adding the attribute
  /// when a host-primary multi-target Sema analyses them: the host must keep
  /// treating them as host variables, while each aux target's CodeGen emits
  /// them as a device compilation would.
  llvm::DenseSet<const VarDecl *> ImplicitAuxDeviceConstants;

  /// The target a class template specialization was keyed by when it was
  /// created (see ClassTemplateSpecializationDecl::Profile). It is completed
  /// under that target, whichever target first needs its definition, so its
  /// body is the one of the copy its identity belongs to.
  llvm::DenseMap<const ClassTemplateSpecializationDecl *, unsigned>
      SpecializationKeyVariant;

  /// See getTargetDependence: the settled results, and the declarations
  /// under evaluation, by position on the stack, with whether each one's
  /// definition is complete.
  mutable llvm::DenseMap<const Decl *, unsigned> TargetDependenceCache;
  mutable llvm::DenseMap<const Decl *, unsigned> TargetDependenceIndex;
  mutable SmallVector<std::pair<const Decl *, bool>, 16> TargetDependenceStack;
  mutable unsigned TargetDependenceLowLink = ~0u;
  mutable llvm::SmallPtrSet<const Decl *, 8> TargetDependenceAwaitingDefinition;
  llvm::SmallSetVector<const Decl *, 4> TargetDependenceRecheck;
  llvm::SmallSetVector<const Decl *, 4> TargetDependenceEscalated;
  llvm::SetVector<const Decl *> TemplatesWithUnkeyedSpecializations;
  void noteTargetDependenceEscalation();

  /// See isTargetDivergentSource; only declarations without a tag of their
  /// own need their siblings looked up.
  mutable llvm::DenseMap<const NamedDecl *, bool> UntaggedTargetSourceCache;

  /// See TargetKeySuppressionRAII.
  bool TargetKeysSuppressed = false;

  /// See HostKernelKeyRAII.
  bool KernelsKeyedForHost = false;
  /// See HostViewRAII.
  bool HostViewOfDeviceCode = false;
  /// See setTargetInputHook.
  void (*TargetInputHook)(void *, char, unsigned, const Decl *) = nullptr;
  void *TargetInputHookCtx = nullptr;
  unsigned TargetInputsSuspended = 0;

public:
  /// An input recorded by noteParseInput.
  struct ParseInput {
    char Kind;
    /// Consumed by a member, not by the declaration itself.
    bool FromMember;
    /// A TargetPartition.
    unsigned Targets;
    std::string What;
  };

private:
  /// See noteParseInput. Only kept when JoinAuditPath is set.
  llvm::DenseMap<const Decl *, SmallVector<ParseInput, 2>> ParseInputs;
  /// See noteTokenClass. Only kept when JoinAuditPath is set.
  llvm::DenseMap<const Decl *, unsigned> TokenClasses;

  /// See keepTargetCopiesApart.
  llvm::DenseSet<const Decl *> TargetCopiesKeptApart;

  /// See getTargetDefaultArgument.
  llvm::DenseMap<std::pair<const NamedDecl *, unsigned>, NamedDecl *>
      TargetDefaultArguments;
  llvm::DenseSet<const TemplateDecl *> TemplatesWithTargetDefaultArguments;

  /// See getTargetInstantiationKey.
  llvm::DenseMap<const Decl *, unsigned> TargetInstantiationKeys;

  /// See getTargetInstantiationTargets: for a specialization keyed by the
  /// canonical device target, the variants that have a specialization of
  /// their own, as a bit mask.
  llvm::DenseMap<const Decl *, uint64_t> TargetInstanceReplacements;

  /// See setInstantiationTargetHook.
  unsigned (*InstantiationTargetHook)(void *) = nullptr;
  const FunctionDecl *(*TargetSpecializationMemberHook)(void *,
                                                        const FunctionDecl *,
                                                        unsigned) = nullptr;
  void *InstantiationTargetHookCtx = nullptr;

  /// See hasTargetDivergentLayout.
  mutable llvm::DenseMap<const NamedDecl *, unsigned>
      TargetDivergentLayoutCache;
  /// See getRecordShapePartition.
  mutable llvm::DenseMap<const RecordDecl *, unsigned> RecordShapeCache;
  /// See getRecordDeclarationShape.
  mutable llvm::DenseMap<const CXXRecordDecl *, size_t>
      RecordDeclarationShapeCache;
  /// See getKeptShared.
  llvm::DenseMap<const Decl *, std::pair<unsigned, const Decl *>> KeptShared;
  /// See getManglingEquivalent.
  mutable llvm::DenseMap<const CXXRecordDecl *, const CXXRecordDecl *>
      ManglingEquivalents;
  mutable llvm::StringMap<const CXXRecordDecl *> ManglingEquivalentByShape;
  mutable llvm::DenseMap<const Type *, QualType> ManglingEquivalentTypes;

  /// See getTargetPartition: each partition seen, as the class of each target
  /// variant (index Variant - 1), classes numbered in order of appearance.
  mutable SmallVector<SmallVector<uint8_t, 8>, 8> TargetPartitions;
  mutable llvm::StringMap<unsigned> TargetPartitionIndex;
  /// For a specialization of a copy standing for several targets, the target
  /// it is for; see getSpecializationTarget.
  llvm::DenseMap<const Decl *, TargetSet> SpecializationTargets;

  /// See getTargetDefinitionForEvaluation. Indexed by target variant.
  struct TargetMemberDefinitions {
    unsigned Dependence = 0;
    SmallVector<const FunctionDecl *, 4> ByVariant;
  };
  mutable llvm::DenseMap<const FunctionDecl *, TargetMemberDefinitions>
      TargetMemberDefinitionsCache;
  const TargetMemberDefinitions *
  getTargetMemberDefinitions(const FunctionDecl *FD) const;

  /// Side-table storage for Decl::TargetVariant (bits 0-5) and
  /// TargetVariantIsReparseOrigin (bit 6), keeping both off Decl's own
  /// bitfields. Only populated for a Decl that has actually been tagged;
  /// ordinary compiles insert nothing since tagging only happens behind
  /// AllowTargetVariantDecls. See Decl::getTargetVariant() et al.
  /// (DeclBase.cpp).
  llvm::DenseMap<const Decl *, uint8_t> DeclTargetVariantStorage;

  /// For a declaration that is a copy for several targets (bit 7 of its
  /// DeclTargetVariantStorage entry), the targets; its TargetVariant is the
  /// first of them.
  llvm::DenseMap<const Decl *, TargetSet> DeclTargetSets;

public:
  /// See ReparseCopies.
  void noteReparseCopy(const Decl *D) { ReparseCopies.insert(D); }
  bool isReparseCopy(const Decl *D) const { return ReparseCopies.count(D); }

  /// See PendingTargetExplicitSpecializations.
  void noteTargetExplicitSpecialization(const Decl *Template, Decl *Spec,
                                        const Decl *Tagged) {
    PendingTargetExplicitSpecializations.push_back(
        {Template->getCanonicalDecl(), Spec, Tagged});
  }
  /// Keeps those of the pending explicit specializations some targets lack,
  /// once the alternatives they were parsed in are merged.
  LLVM_ABI void resolveTargetExplicitSpecializations();
  /// Whether some target has an explicit specialization of \p Template that
  /// others do not, which makes its specializations each target's own.
  LLVM_ABI bool hasTargetExplicitSpecializations(const Decl *Template) const;
  /// The explicit specialization of \p Template for \p Args that \p Variant
  /// has where only some targets have it, or null.
  LLVM_ABI Decl *
  getTargetExplicitSpecialization(const Decl *Template,
                                  ArrayRef<TemplateArgument> Args,
                                  unsigned Variant) const;

  /// See TargetCopySpecializations.
  void noteTargetCopySpecialization(FunctionDecl *Spec) {
    TargetCopySpecializations.push_back(Spec);
  }
  ArrayRef<FunctionDecl *> getTargetCopySpecializations() const {
    return TargetCopySpecializations;
  }

  /// Record that \p Redundant was merged into \p Survivor. See
  /// TargetMergeSurvivors.
  void setTargetMergeSurvivor(const Decl *Redundant, const Decl *Survivor) {
    TargetMergeSurvivors[Redundant] = Survivor;
  }

  /// The copy \p D was merged into, if it was.
  const Decl *getTargetMergeSurvivor(const Decl *D) const {
    return TargetMergeSurvivors.lookup(D);
  }

  /// See TargetBodies.
  void addTargetBody(const FunctionDecl *Definition, TargetSet Targets,
                     Stmt *Body) {
    TargetBodies[Definition].push_back({Targets, Body});
  }
  ArrayRef<std::pair<TargetSet, Stmt *>>
  getTargetBodies(const FunctionDecl *Definition) const {
    auto It = TargetBodies.find(Definition);
    return It == TargetBodies.end()
               ? ArrayRef<std::pair<TargetSet, Stmt *>>()
               : ArrayRef<std::pair<TargetSet, Stmt *>>(It->second);
  }
  /// See FailedTargetInstantiations.
  void addFailedTargetInstantiation(const FunctionDecl *FD, TargetSet Targets,
                                    ArrayRef<PartialDiagnosticAt> Diags) {
    FailedTargetInstantiations[FD].push_back(
        {Targets, SmallVector<PartialDiagnosticAt, 2>(Diags)});
  }
  /// What instantiating \p FD for \p Variant reported, if it did not
  /// compile for it.
  ArrayRef<PartialDiagnosticAt>
  getFailedTargetInstantiation(const FunctionDecl *FD, unsigned Variant) const {
    if (LLVM_LIKELY(FailedTargetInstantiations.empty()))
      return {};
    auto It = FailedTargetInstantiations.find(FD);
    if (It == FailedTargetInstantiations.end())
      return {};
    for (const auto &[Targets, Diags] : It->second)
      if (Targets.contains(Variant))
        return Diags;
    return {};
  }
  /// The body \p Definition has for \p Variant: its own, or one parsed again
  /// for targets including \p Variant.
  Stmt *getBodyForTarget(const FunctionDecl *Definition,
                         unsigned Variant) const {
    if (LLVM_LIKELY(TargetBodies.empty()))
      return Definition->getBody();
    for (const auto &[Targets, Body] : getTargetBodies(Definition))
      if (Targets.contains(Variant))
        return Body;
    return Definition->getBody();
  }


  /// Record that \p CTSD was instantiated from a pattern tagged for target
  /// variant \p Variant. See TargetVariantForSpecialization. A no-op if
  /// \p Variant is 0 (untagged pattern -- nothing to record).
  void
  setTargetVariantForSpecialization(const ClassTemplateSpecializationDecl *CTSD,
                                    unsigned Variant) {
    if (Variant)
      TargetVariantForSpecialization[CTSD] = Variant;
  }

  /// Look up which target variant \p CTSD's pattern was tagged for, if any.
  /// Returns 0 for the overwhelming common case (untagged, ordinary
  /// specialization).
  unsigned getTargetVariantForSpecialization(
      const ClassTemplateSpecializationDecl *CTSD) const {
    auto It = TargetVariantForSpecialization.find(CTSD);
    return It == TargetVariantForSpecialization.end() ? 0 : It->second;
  }

  /// How a declaration's meaning depends on the target it is compiled for,
  /// in a multi-target compilation: not at all; only between the host and
  /// the devices (TDK_Side, e.g. a constexpr function whose body
  /// tests __HIP_DEVICE_COMPILE__); or also between device architectures
  /// (TDK_Arch, e.g. one reading __amdgcn_processor__). A template depends
  /// on the target if anything its definition names, including default
  /// arguments, other templates and constexpr functions it can evaluate,
  /// does. Calls evaluated at run time are not followed: each target's
  /// CodeGen resolves them by name.
  enum TargetDependenceKind : uint8_t {
    TDK_None = 0,
    TDK_Side = 1,
    TDK_Arch = 2
  };
  LLVM_ABI TargetDependenceKind getTargetDependence(const Decl *D) const;

  using TargetPartition = clang::TargetPartition;
  LLVM_ABI TargetPartition getTargetPartition(const Decl *D) const;
  LLVM_ABI TargetPartition getLayoutTargetPartition(const NamedDecl *D) const;
  LLVM_ABI TargetPartition
  getMemberTargetPartition(const FunctionDecl *FD) const;
  LLVM_ABI TargetPartition
  getDefaultArgumentTargetPartition(const TemplateDecl *TD) const;

  LLVM_ABI TargetDependenceKind
  getTargetDependenceKind(TargetPartition P) const;
  /// The partition that tells apart what either of \p A and \p B does.
  LLVM_ABI TargetPartition refineTargetPartition(TargetPartition A,
                                                 TargetPartition B) const;
  /// The partition telling targets apart by their answers: by \p Answer, or
  /// by \p Same between them.
  LLVM_ABI TargetPartition partitionTargetsByAnswer(
      llvm::function_ref<llvm::hash_code(unsigned Variant)> Answer) const;
  LLVM_ABI TargetPartition partitionTargetsBy(
      llvm::function_ref<bool(unsigned A, unsigned B)> Same) const;
  /// \p P if it tells apart what \p Kind says, else the coarsest partition
  /// that does: every target alike, the host apart from the devices, or every
  /// target apart.
  LLVM_ABI TargetPartition reconcileTargetPartition(
      TargetPartition P, TargetDependenceKind Kind) const;
  LLVM_ABI bool isFinestTargetPartition(TargetPartition P) const;
  /// Whether \p P tells targets \p A and \p B apart.
  LLVM_ABI bool separatesTargets(TargetPartition P, unsigned A,
                                 unsigned B) const;
  /// The body a constant expression calling \p Definition evaluates, where
  /// it was parsed again for some targets: that of the target the code is
  /// compiled for, or null if that is its own. Notes the use.
  LLVM_ABI Stmt *getTargetBodyForEvaluation(const FunctionDecl *Definition,
                                            bool InConstantContext) const;
  /// The targets \p P does not tell apart from \p Variant.
  LLVM_ABI TargetSet getTargetClass(TargetPartition P, unsigned Variant) const;

  /// The target a specialization of \p Template requested by \p Variant is
  /// made for: \p Variant, or the first target of \p Template where it is a
  /// copy standing for several targets that \p Variant is not among. Every
  /// target has its own: a specialization names types keyed by target (see
  /// ClassTemplateSpecializationDecl::Profile).
  LLVM_ABI unsigned getSpecializationTarget(const Decl *Template,
                                            unsigned Variant) const;
  /// The targets of \p Template, a copy standing for several, that a
  /// specialization keyed \p Key stands for. A key that tells only the host
  /// from the devices stands for every device of the copy.
  LLVM_ABI TargetSet getInstantiationKeyTargets(const Decl *Template,
                                                unsigned Key) const;
  /// Which targets' copies of \p RD, a class copied per target, declare the
  /// same bases and members, and initialize their fields alike.
  LLVM_ABI TargetPartition getRecordShapePartition(const RecordDecl *RD) const;
  /// A hash of what \p RD, a class definition, declares, its static members'
  /// initializers included.
  LLVM_ABI size_t getRecordDeclarationShape(const CXXRecordDecl *RD) const;
  /// Notes that \p D was analysed once for every target although what it
  /// consumed differs between the targets \p P tells apart: \p Root, kept
  /// shared, or a declaration naming it.
  void noteKeptShared(const Decl *D, TargetPartition P, const Decl *Root) {
    auto &[Kept, KeptRoot] = KeptShared[D->getCanonicalDecl()];
    Kept = refineTargetPartition(Kept, P);
    if (!KeptRoot)
      KeptRoot = Root;
  }
  bool hasKeptShared() const { return !KeptShared.empty(); }
  /// The targets told apart by what \p D, kept shared, consumed, and the
  /// declaration kept shared, or 0 and null.
  std::pair<TargetPartition, const Decl *> getKeptShared(const Decl *D) const {
    return KeptShared.lookup(D->getCanonicalDecl());
  }
  /// Calls \p Found for each declaration kept shared that \p D is, is in, or
  /// was instantiated from.
  LLVM_ABI void forEachKeptSharedPart(
      const Decl *D,
      llvm::function_ref<void(const Decl *Part, TargetPartition, const Decl *)>
          Found) const;
  /// The class standing for \p RD in a mangled name's substitutions. Targets'
  /// specializations of the same template with the same arguments, and
  /// classes declared in them, mangle alike and are one entity to the other
  /// targets' compilations.
  /// Gated on the compilation, as each target's code generation sees its own
  /// language options.
  const CXXRecordDecl *getManglingEquivalent(const CXXRecordDecl *RD) const {
    return LLVM_LIKELY(!Ctx.MultiTargetCompile) ? RD
                                                : getManglingEquivalentImpl(RD);
  }
  /// \p T with each class in it replaced by its getManglingEquivalent.
  QualType getManglingEquivalent(QualType T) const {
    return LLVM_LIKELY(!Ctx.MultiTargetCompile) ? T
                                                : getManglingEquivalentImpl(T);
  }
  /// Which targets see copies of \p CTD that declare the same things, member
  /// bodies aside, which each target compiles its own of. Member initializers
  /// count.
  LLVM_ABI TargetPartition
  getClassTemplateShapePartition(const ClassTemplateDecl *CTD) const;
  /// Which targets the default template arguments of \p TD tell apart: what
  /// they name, and their own copies.
  LLVM_ABI TargetPartition
  getDefaultArgumentDependence(const TemplateDecl *TD) const;
  void setSpecializationTargets(const Decl *Spec, TargetSet Targets) {
    SpecializationTargets[Spec] = Targets;
  }
  /// The targets recorded for \p D or the specialization enclosing it, if
  /// any.
  LLVM_ABI std::optional<TargetSet>
  getRecordedSpecializationTargets(const Decl *D) const;
  /// \p Targets, narrowed to those recorded for \p D, if any.
  TargetSet narrowToSpecializationTargets(const Decl *D,
                                          TargetSet Targets) const {
    if (std::optional<TargetSet> Recorded =
            getRecordedSpecializationTargets(D))
      if (!((Targets & *Recorded) == TargetSet()))
        return Targets & *Recorded;
    return Targets;
  }
  /// The targets of \p D, a member instantiated for target \p Variant: those
  /// of the copy of the class it is a member of, if that is \p Variant's.
  LLVM_ABI TargetSet getMemberInstantiationTargets(const Decl *D,
                                                   unsigned Variant) const;

private:
  TargetPartition internTargetPartition(ArrayRef<uint8_t> ClassOf) const;

public:

  /// How the layout of \p D, one target's copy of a class or class template,
  /// depends on the target; see hasTargetDivergentLayout.
  LLVM_ABI TargetDependenceKind
  getLayoutTargetDependence(const NamedDecl *D) const;

  /// \p D, a function or class, is now defined. A result computed while it
  /// had no definition is stale, as is everything that depended on it.
  LLVM_ABI void noteTargetDependenceDefinition(const Decl *D);

  /// Settles what noteTargetDependenceDefinition deferred until a class's
  /// inline member functions had been parsed.
  LLVM_ABI void recheckTargetDependence();

  /// A specialization of \p Template was made without a target key.
  LLVM_ABI void noteUnkeyedSpecialization(const Decl *Template);

  /// Templates found to depend on the target only after some of their
  /// specializations were made without a key.
  bool hasTargetDependenceEscalations() const {
    return !TargetDependenceEscalated.empty();
  }
  llvm::SmallVector<const Decl *, 4> takeTargetDependenceEscalations() {
    auto Result = TargetDependenceEscalated.takeVector();
    return llvm::SmallVector<const Decl *, 4>(Result.begin(), Result.end());
  }

  /// Whether \p ND is one target's copy of a function, variable or
  /// enumerator whose tokens differ between targets, and which can be
  /// evaluated at compile time: the source of every target dependence.
  LLVM_ABI bool isTargetDivergentSource(const NamedDecl *ND) const;

  /// Every target's copy of \p ND, \p ND included: the declarations parsed
  /// from the same tokens, or from the other arms of the #if it is in, and for
  /// a member of a class whose definition differs between targets, the same
  /// member of each target's copy of the class.
  LLVM_ABI void
  getTargetCopies(const NamedDecl *ND,
                  SmallVectorImpl<const NamedDecl *> &Copies) const;
  /// \p ND's copy for \p Variant, or null if it has none.
  LLVM_ABI const NamedDecl *getCopyFor(const NamedDecl *ND,
                                       unsigned Variant) const;

  /// Whether \p D is one target's copy of a function that the device
  /// architectures do not all have alike: one has no copy, or a copy of
  /// another type or for another CUDA target. Overload resolution can then
  /// pick differently per architecture.
  LLVM_ABI bool isArchDivergentFunction(const NamedDecl *D) const;

  /// Whether \p D is one target's copy of a class, or of a class template,
  /// whose fields or bases differ between targets, so that its layout does too.
  LLVM_ABI bool hasTargetDivergentLayout(const NamedDecl *D) const;
  LLVM_ABI bool hasSameTargetLayout(QualType A, QualType B,
                                    unsigned Depth = 0) const;

  /// For \p FD, a member function of a class copied per target: whether the
  /// same member of the targets' copies of the class is defined differently.
  /// Code naming the class names one copy for every target, so a constant
  /// expression calling the member depends on the target it is evaluated for.
  LLVM_ABI TargetDependenceKind
  getMemberTargetDependence(const FunctionDecl *FD) const;

  /// The definition a constant expression calling \p FD, defined by
  /// \p Definition, evaluates: that of the same member of the copy of the class
  /// for the target the code is compiled for (getInstantiationTarget), or of
  /// that target's specialization of a class template copied per target; or
  /// null if that is \p Definition. The use is an input of the code evaluating
  /// it if \p InConstantContext, i.e. unless the evaluation is speculative.
  LLVM_ABI const FunctionDecl *
  getTargetDefinitionForEvaluation(const FunctionDecl *FD,
                                   const FunctionDecl *Definition,
                                   bool InConstantContext) const;

  /// The variable a constant expression reading \p VD, a static member of one
  /// target's copy of a class, evaluates: the same member of the copy for the
  /// target the code is compiled for, or null if that is \p VD.
  LLVM_ABI const VarDecl *
  getTargetVariableForEvaluation(const VarDecl *VD,
                                 bool InConstantContext) const;

  /// Sema supplies the target the code currently being analysed or
  /// instantiated is compiled for (see Sema::getInstantiationTarget); the
  /// AST cannot tell on its own, e.g. whether the enclosing function is
  /// device-only.
  void setInstantiationTargetHook(unsigned (*Hook)(void *), void *HookCtx) {
    InstantiationTargetHook = Hook;
    InstantiationTargetHookCtx = HookCtx;
  }
  /// \p Hook gives, for a member of a specialization of a class template
  /// copied per target, the definition of the same member of the
  /// specialization made for a given target (see
  /// Sema::getTargetSpecializationMember).
  void setTargetSpecializationMemberHook(
      const FunctionDecl *(*Hook)(void *, const FunctionDecl *, unsigned)) {
    TargetSpecializationMemberHook = Hook;
  }
  unsigned getInstantiationTarget() const {
    if (InstantiationTargetHook)
      return InstantiationTargetHook(InstantiationTargetHookCtx);
    return std::max(Ctx.CurrentTargetVariant, 1u);
  }

  /// The key a specialization of \p Template requested now gets, or 0 if
  /// its specializations are shared by every target: the host's (1); the
  /// devices' (the canonical device variant), when only the host differs;
  /// or one device architecture's, when they differ among themselves.
  /// \p Args, if given, are the specialization's, whose classes can make it
  /// differ between architectures where the template itself does not.
  LLVM_ABI unsigned
  computeTargetInstantiationKey(const Decl *Template,
                                ArrayRef<TemplateArgument> Args = {}) const;

  /// The dependence of \p Template's specialization with \p Args. It is the
  /// template's, or architectures where an argument names a class
  /// specialization keyed by architecture.
  LLVM_ABI TargetDependenceKind getSpecializationTargetDependence(
      const Decl *Template, ArrayRef<TemplateArgument> Args) const;

  /// Whether specializations of \p Template are keyed by target at all.
  LLVM_ABI bool hasTargetInstantiationKeys(const Decl *Template) const;


  /// Passed as a profiling key, selects the specializations created without
  /// one: explicit specializations, which every target shares.
  static constexpr unsigned UnkeyedTarget = ~0u;

  /// An explicit specialization is written once for every target, so it is
  /// not keyed, and neither is the specialization its declaration redeclares.
  class TargetKeySuppressionRAII {
    ASTContext &Ctx;
    bool Saved;

  public:
    explicit TargetKeySuppressionRAII(ASTContext &Ctx)
        : Ctx(Ctx), Saved(Ctx.getMultiTarget().TargetKeysSuppressed) {
      Ctx.getMultiTarget().TargetKeysSuppressed = true;
    }
    ~TargetKeySuppressionRAII() {
      Ctx.getMultiTarget().TargetKeysSuppressed = Saved;
    }
  };

  /// A kernel's specialization is keyed by the canonical device target, even
  /// where host code names it. While this is alive, one named by host code is
  /// keyed by the host instead: the host's own view of its signature.
  class HostKernelKeyRAII {
    ASTContext &Ctx;
    bool Saved;

  public:
    explicit HostKernelKeyRAII(ASTContext &Ctx)
        : Ctx(Ctx), Saved(Ctx.getMultiTarget().KernelsKeyedForHost) {
      Ctx.getMultiTarget().KernelsKeyedForHost = true;
    }
    ~HostKernelKeyRAII() { Ctx.getMultiTarget().KernelsKeyedForHost = Saved; }
  };
  bool areKernelsKeyedForHost() const { return KernelsKeyedForHost; }

  /// The host compilation of the regular pipeline analyses device code too,
  /// as the host sees it. While this is alive with \p Enable set, device code
  /// is instantiated that way: device-only templates are keyed by the host
  /// like any other (see Sema::instantiateHostView).
  class HostViewRAII {
    ASTContext &Ctx;
    bool Saved;

  public:
    HostViewRAII(ASTContext &Ctx, bool Enable)
        : Ctx(Ctx), Saved(Ctx.getMultiTarget().HostViewOfDeviceCode) {
      Ctx.getMultiTarget().HostViewOfDeviceCode = Enable;
    }
    ~HostViewRAII() { Ctx.getMultiTarget().HostViewOfDeviceCode = Saved; }
  };
  bool isHostViewOfDeviceCode() const { return HostViewOfDeviceCode; }

  /// Sema's -mllvm -multi-target-audit: told when the AST consumes an input
  /// that differs between targets, e.g. constant evaluation of a per-target
  /// definition.
  void setTargetInputHook(void (*Hook)(void *, char, TargetPartition,
                                       const Decl *),
                          void *HookCtx) {
    TargetInputHook = Hook;
    TargetInputHookCtx = HookCtx;
  }
  /// \p Targets: which targets get which answer.
  void recordTargetInput(char Kind, TargetPartition Targets,
                         const Decl *D) const {
    if (LLVM_UNLIKELY(TargetInputHook) && !TargetInputsSuspended)
      TargetInputHook(TargetInputHookCtx, Kind, Targets, D);
  }
  bool areTargetInputsSuspended() const { return TargetInputsSuspended; }
  bool isRecordingTargetInputs() const {
    return TargetInputHook && !TargetInputsSuspended;
  }

  /// -mllvm -multi-target-audit-joins: where each merge of targets' copies
  /// reports whether the audit would have allowed it. Empty if not auditing.
  std::string JoinAuditPath;
  /// Whether the inputs consumed while parsing for one target are recorded
  /// as what would tell apart the targets of a group parsed once (see
  /// Parser::HandleTargetAlternationMarker), or for JoinAuditPath.
  bool RecordGroupInputs = false;
  /// While set, every declaration whose target tag is set, in order.
  bool LogTargetTags = false;
  std::vector<const Decl *> TargetTagLog;
  /// Parsing \p D for one target, or instantiating it, consumed an input of
  /// kind \p Kind (see Sema's TargetInputRecorder) telling \p Targets apart, or
  /// one of its members did if \p FromMember.
  LLVM_ABI void noteParseInput(const Decl *D, char Kind,
                               TargetPartition Targets, StringRef What,
                               bool FromMember);
  /// \p D was parsed as a unit, whatever it consumed.
  void noteParseUnit(const Decl *D) { ParseInputs[D]; }
  /// \p D was parsed for one target from tokens that are the same as those of
  /// every other copy in \p Class: the first target whose tokens they were, or
  /// ReparseTokenClass for a declaration re-parsed per target.
  void noteTokenClass(const Decl *D, unsigned Class) {
    TokenClasses[D] = Class;
  }
  static constexpr unsigned ReparseTokenClass = ~0u;
  /// The token class of \p D, or of what it was instantiated from or is
  /// declared in; 0 if unknown.
  LLVM_ABI unsigned getTokenClass(const Decl *D) const;
  /// What parsing \p D, or the declaration enclosing it that was parsed as a
  /// unit, consumed.
  LLVM_ABI SmallVector<ParseInput, 4> getParseInputList(const Decl *D) const;
  /// The targets those inputs tell apart.
  LLVM_ABI TargetPartition getParseInputs(const Decl *D) const;

  /// What the consumer (CodeGen) evaluates while it handles declarations is
  /// not an input of the code being analysed.
  class TargetInputSuspension {
    MultiTargetContext *MT;

  public:
    TargetInputSuspension(ASTContext &Ctx)
        : MT(Ctx.hasMultiTarget() ? &Ctx.getMultiTarget() : nullptr) {
      if (MT)
        ++MT->TargetInputsSuspended;
    }
    ~TargetInputSuspension() {
      if (MT)
        --MT->TargetInputsSuspended;
    }
  };

  /// \p D, one target's copy of a declaration re-parsed for each target,
  /// differs from the others in a way comparing them cannot see, so they are
  /// never merged (see mergeEquivalentVariants).
  LLVM_ABI void keepTargetCopiesApart(const Decl *D);

  /// The template parameter whose default argument \p Param, of \p Template,
  /// has for target \p Variant: one of the target's own copy of the template,
  /// merged into \p Template though its default differs (e.g. one a macro
  /// gives each target a value of its own for); or null if it is Param's own.
  LLVM_ABI void setTargetDefaultArgument(const TemplateDecl *Template,
                                         const NamedDecl *Param,
                                         unsigned Variant, NamedDecl *Other);
  LLVM_ABI NamedDecl *getTargetDefaultArgument(const NamedDecl *Param,
                                               unsigned Variant) const;

  /// How \p TD's default arguments depend on the target; see
  /// getTargetDefaultArgument.
  LLVM_ABI TargetDependenceKind
  getDefaultArgumentTargetDependence(const TemplateDecl *TD) const;
  bool areTargetCopiesKeptApart(const Decl *D) const {
    for (; D; D = dyn_cast_or_null<Decl>(D->getDeclContext()))
      if (TargetCopiesKeptApart.contains(D))
        return true;
    return false;
  }

  /// The key a specialization was created with (see
  /// computeTargetInstantiationKey), or 0. It is stored rather than
  /// recomputed: the specialization's identity must not depend on where it
  /// is looked up from.
  unsigned getTargetInstantiationKey(const Decl *D) const {
    auto It = TargetInstantiationKeys.find(D);
    return It == TargetInstantiationKeys.end() ? 0 : It->second;
  }
  void setTargetInstantiationKey(const Decl *D, unsigned Key) {
    TargetInstantiationKeys[D] = Key;
  }

  /// The target \p D, a specialization keyed by target, is for: the key it
  /// was made with, or for a class specialization the target ambient when it
  /// was made. 0 if it is not keyed.
  LLVM_ABI unsigned getSpecializationKey(const Decl *D) const;
  /// The target \p CTSD is keyed by if that is the target ambient when it
  /// was made rather than its arguments, else 0.
  LLVM_ABI unsigned getAmbientSpecializationKey(
      const ClassTemplateSpecializationDecl *CTSD) const;

  /// The key of \p D or of the innermost keyed specialization enclosing it,
  /// which \p Keyed is set to if given.
  LLVM_ABI unsigned
  getEffectiveTargetInstantiationKey(const Decl *D,
                                     const Decl **Keyed = nullptr) const;

  /// Whether \p D, a specialization keyed by target, is the one every device
  /// shares, its template differing only between the host and the devices.
  LLVM_ABI bool isSharedByDevices(const Decl *D) const;

  /// A specialization keyed by the canonical device variant is shared by
  /// every target that has no specialization of its own; \p Variant has one.
  void markTargetInstanceReplaced(const Decl *D, unsigned Variant) {
    TargetInstanceReplacements[D] |= uint64_t(1) << Variant;
  }

  /// The targets \p D, a specialization keyed by target or a member of one,
  /// is for: its key's target, or, keyed by the canonical device, every
  /// target without a specialization of its own. Every target if \p D is
  /// not keyed.
  LLVM_ABI TargetSet getTargetInstantiationTargets(const Decl *D) const;

  /// See SpecializationKeyVariant.
  void setSpecializationKeyVariant(const ClassTemplateSpecializationDecl *CTSD,
                                   unsigned Variant) {
    SpecializationKeyVariant[CTSD] = Variant;
  }
  unsigned getSpecializationKeyVariant(
      const ClassTemplateSpecializationDecl *CTSD) const {
    auto It = SpecializationKeyVariant.find(CTSD);
    return It == SpecializationKeyVariant.end() ? 0 : It->second;
  }

  /// See ImplicitAuxDeviceConstants.
  void setImplicitAuxDeviceConstant(const VarDecl *VD) {
    ImplicitAuxDeviceConstants.insert(VD);
  }
  bool isImplicitAuxDeviceConstant(const VarDecl *VD) const {
    return ImplicitAuxDeviceConstants.contains(VD);
  }

  /// EXPERIMENT: get/set \p D's TargetVariant (bits 0-5) and
  /// TargetVariantIsReparseOrigin (bit 6), backed by DeclTargetVariantStorage
  /// instead of a Decl-object bitfield. See Decl::getTargetVariant() et al.
  /// (DeclBase.cpp), the only callers of these.
  static constexpr uint8_t DeclTargetVariantMask = 0x3f;
  static constexpr uint8_t DeclTargetVariantReparseOriginBit = 0x40;
  static constexpr uint8_t DeclTargetVariantSetBit = 0x80;
  static_assert(Decl::TargetVariantRedundant <= DeclTargetVariantMask);
  unsigned getDeclTargetVariant(const Decl *D) const {
    auto It = DeclTargetVariantStorage.find(D);
    return It == DeclTargetVariantStorage.end()
               ? 0
               : (It->second & DeclTargetVariantMask);
  }
  void setDeclTargetVariant(const Decl *D, unsigned V) {
    assert(V <= Decl::TargetVariantRedundant && "variant out of range");
    if (LLVM_UNLIKELY(LogTargetTags))
      TargetTagLog.push_back(D);
    uint8_t &Byte = DeclTargetVariantStorage[D];
    if (Byte & DeclTargetVariantSetBit)
      DeclTargetSets.erase(D);
    Byte = (Byte & ~(DeclTargetVariantMask | DeclTargetVariantSetBit)) |
           (V & DeclTargetVariantMask);
  }
  void setDeclTargetSet(const Decl *D, TargetSet Targets) {
    if (LLVM_UNLIKELY(LogTargetTags))
      TargetTagLog.push_back(D);
    uint8_t &Byte = DeclTargetVariantStorage[D];
    Byte = (Byte & ~DeclTargetVariantMask) | DeclTargetVariantSetBit |
           Targets.first();
    DeclTargetSets[D] = Targets;
  }
  TargetSet getDeclTargetSet(const Decl *D) const {
    auto It = DeclTargetVariantStorage.find(D);
    unsigned V = It == DeclTargetVariantStorage.end()
                     ? 0
                     : (It->second & DeclTargetVariantMask);
    if (!V)
      return TargetSet::all();
    if (V == Decl::TargetVariantRedundant)
      return TargetSet();
    if (It->second & DeclTargetVariantSetBit)
      return DeclTargetSets.lookup(D);
    return TargetSet::single(V);
  }
  bool getDeclTargetVariantIsReparseOrigin(const Decl *D) const {
    auto It = DeclTargetVariantStorage.find(D);
    return It != DeclTargetVariantStorage.end() &&
           (It->second & DeclTargetVariantReparseOriginBit);
  }
  void setDeclTargetVariantIsReparseOrigin(const Decl *D, bool V) {
    uint8_t &Byte = DeclTargetVariantStorage[D];
    if (V)
      Byte |= DeclTargetVariantReparseOriginBit;
    else
      Byte &= ~DeclTargetVariantReparseOriginBit;
  }

private:
  LLVM_ABI const CXXRecordDecl *
  getManglingEquivalentImpl(const CXXRecordDecl *RD) const;
  LLVM_ABI QualType getManglingEquivalentImpl(QualType T) const;
};

} // namespace clang

#endif // LLVM_CLANG_AST_MULTITARGETCONTEXT_H
