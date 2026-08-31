//===- TargetCopyMerge.cpp - Merging equivalent target copies ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Decides whether the copies of a declaration made for different targets mean
// the same, and merges those that do into one declaration they share.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/TargetCopyMerge.h"
#include "MultiTargetInternal.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTStructuralEquivalence.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/MultiTargetContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/TargetDependence.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace clang;
using namespace clang::multitarget;

namespace {

/// Collects the declarations that carry a target, keyed so the copies of one
/// entity in different arms land together.
class TaggedCollector : public RecursiveASTVisitor<TaggedCollector> {
  ASTContext &Ctx;
  llvm::StringMap<SmallVector<NamedDecl *, 2>> &ByKey;

public:
  TaggedCollector(ASTContext &Ctx,
                  llvm::StringMap<SmallVector<NamedDecl *, 2>> &ByKey)
      : Ctx(Ctx), ByKey(ByKey) {}

  bool shouldVisitTemplateInstantiations() const { return true; }
  bool shouldVisitImplicitCode() const { return false; }

  bool VisitNamedDecl(NamedDecl *D) {
    if (!D->getTargetVariant() || D->isRedundantTargetVariant())
      return true;
    // A member of one target's copy of a class is merged with its class, and
    // a declaration local to a function, e.g. a parameter, with its function.
    if (const auto *Parent = dyn_cast<Decl>(D->getDeclContext());
        Parent && Parent->getTargetVariant() && isa<RecordDecl>(Parent))
      return true;
    if (D->getParentFunctionOrMethod())
      return true;
    // Instantiated for one target on purpose; see
    // MultiTargetContext::computeTargetInstantiationKey.
    if (Ctx.getMultiTarget().getEffectiveTargetInstantiationKey(D))
      return true;
    SmallString<256> Key;
    llvm::raw_svector_ostream OS(Key);
    PrintingPolicy Policy = Ctx.getPrintingPolicy();
    Policy.FullyQualifiedName = true;
    Policy.PrintAsCanonical = true;
    OS << D->getDeclKindName() << '\t';
    D->printQualifiedName(OS, Policy);
    printTemplateArgs(OS, D, Policy);
    if (const auto *VD = dyn_cast<ValueDecl>(D))
      OS << '\t' << VD->getType().getCanonicalType().getAsString(Policy);
    ByKey[Key.str()].push_back(D);
    return true;
  }
};

} // namespace

namespace {

/// The templates whose specializations are keyed by target (see
/// ClassTemplateSpecializationDecl::Profile) when unclaimTree reverts one of
/// their declarations. Reverting can end that keying, which strands every
/// specialization already inserted: a lookup no longer hashes to its bucket,
/// so the next use instantiates a second, distinct copy of it.
class TargetKeyedTemplates {
  llvm::SmallSetVector<ClassTemplateDecl *, 4> Classes;
  llvm::SmallSetVector<FunctionTemplateDecl *, 4> Functions;

public:
  void noteUnclaim(Decl *D) {
    ClassTemplateDecl *CTD = dyn_cast<ClassTemplateDecl>(D);
    if (auto *P = dyn_cast<ClassTemplatePartialSpecializationDecl>(D))
      CTD = P->getSpecializedTemplate();
    if (CTD && CTD->hasTargetKeyedSpecializations())
      Classes.insert(CTD);
    if (auto *FTD = dyn_cast<FunctionTemplateDecl>(D))
      if (FTD->hasTargetKeyedSpecializations())
        Functions.insert(FTD);
  }

  void rehashNoLongerKeyed() {
    for (ClassTemplateDecl *CTD : Classes)
      if (!CTD->hasTargetKeyedSpecializations())
        CTD->rehashSpecializations();
    for (FunctionTemplateDecl *FTD : Functions)
      if (!FTD->hasTargetKeyedSpecializations())
        FTD->rehashSpecializations();
  }
};

} // namespace

/// Revert \p D and everything it encloses from \p FromVariant to every
/// target. Mirrors the recursion that claimed them: a TemplateDecl is not a
/// DeclContext, so its templated declaration and parameter list have to be
/// reached explicitly.
///
/// \p FromVariant is almost always 1 (the host/primary): every existing
/// caller reconciles some other target's copy against the primary's. The one
/// exception is mergeWidenedAlternative reconciling two non-host targets
/// against each other (two real device archs that happen to take the same
/// #elif branch) -- there the surviving side is whichever of the two was
/// recorded first, tagged with its own variant rather than 1.
static void unclaimTree(Decl *D, unsigned FromVariant, unsigned &Count,
                        TargetKeyedTemplates *Keyed = nullptr) {
  if (!D || D->getTargetVariant() != FromVariant)
    return;
  if (Keyed)
    Keyed->noteUnclaim(D);
  D->setTargetVariant(0);
  ++Count;
  if (auto *TD = dyn_cast<TemplateDecl>(D)) {
    unclaimTree(TD->getTemplatedDecl(), FromVariant, Count, Keyed);
    if (TemplateParameterList *TPL = TD->getTemplateParameters())
      for (NamedDecl *P : *TPL)
        unclaimTree(P, FromVariant, Count, Keyed);
  }
  if (auto *DC = dyn_cast<DeclContext>(D))
    for (Decl *Sub : DC->decls())
      unclaimTree(Sub, FromVariant, Count, Keyed);
}

/// Make \p D and everything it encloses, the copy for \p FromVariant, the
/// copy for \p Targets instead. A shared declaration is every target's
/// already.
static void joinTree(Decl *D, unsigned FromVariant, TargetSet Targets) {
  if (!D || !FromVariant || D->getTargetVariant() != FromVariant)
    return;
  D->setTargetSet(Targets);
  if (auto *TD = dyn_cast<TemplateDecl>(D)) {
    joinTree(TD->getTemplatedDecl(), FromVariant, Targets);
    if (TemplateParameterList *TPL = TD->getTemplateParameters())
      for (NamedDecl *P : *TPL)
        joinTree(P, FromVariant, Targets);
  }
  if (auto *DC = dyn_cast<DeclContext>(D))
    for (Decl *Sub : DC->decls())
      joinTree(Sub, FromVariant, Targets);
}

/// Mark \p D and everything it encloses as belonging to no target.
static void markRedundantTree(Decl *D, unsigned Variant);

/// Marks \p B, merged into \p A, redundant, and records A as what code that
/// already names B stands for.
static void markMergedInto(ASTContext &Ctx, Decl *B, Decl *A,
                           unsigned Variant) {
  markRedundantTree(B, Variant);
  Ctx.getMultiTarget().setTargetMergeSurvivor(B, A);
  if (auto *BTD = dyn_cast<TemplateDecl>(B))
    if (auto *ATD = dyn_cast<TemplateDecl>(A))
      Ctx.getMultiTarget().setTargetMergeSurvivor(BTD->getTemplatedDecl(),
                                                  ATD->getTemplatedDecl());
}

static void markRedundantTree(Decl *D, unsigned Variant) {
  if (!D || D->getTargetVariant() != Variant)
    return;
  D->setTargetVariant(Decl::TargetVariantRedundant);
  if (auto *TD = dyn_cast<TemplateDecl>(D)) {
    markRedundantTree(TD->getTemplatedDecl(), Variant);
    if (TemplateParameterList *TPL = TD->getTemplateParameters())
      for (NamedDecl *P : *TPL)
        markRedundantTree(P, Variant);
  }
  if (auto *DC = dyn_cast<DeclContext>(D))
    for (Decl *Sub : DC->decls())
      markRedundantTree(Sub, Variant);
}

static bool sameTemplateArgs(ArrayRef<TemplateArgument> A,
                             ArrayRef<TemplateArgument> B);

/// \p B, the copy of the template \p A for \p Targets, is about to be merged
/// into it. Comparing them ignores default template arguments, so keep each of
/// B's that differs from A's for those targets to use.
static void recordTargetDefaultArguments(ASTContext &Ctx, Decl *A, Decl *B,
                                         TargetSet Targets) {
  auto *TA = dyn_cast<TemplateDecl>(A);
  auto *TB = dyn_cast<TemplateDecl>(B);
  if (!TA || !TB || !TA->getTemplateParameters() ||
      !TB->getTemplateParameters())
    return;
  TemplateParameterList *PA = TA->getTemplateParameters();
  TemplateParameterList *PB = TB->getTemplateParameters();
  if (PA->size() != PB->size())
    return;
  const SourceManager &SM = Ctx.getSourceManager();
  for (unsigned I = 0, E = PA->size(); I != E; ++I)
    if (defaultArgumentShape(SM, PA->getParam(I)) !=
        defaultArgumentShape(SM, PB->getParam(I)))
      for (unsigned V : Ctx.getTargetVariants())
        if (Targets.contains(V))
          Ctx.getMultiTarget().setTargetDefaultArgument(TA, PA->getParam(I), V,
                                                        PB->getParam(I));
}

/// Whether \p A and \p B name the same template argument, treating a
/// Type-kind argument built by one independent re-parse of a declaration as
/// equal to the "same" argument built by another.
///
/// TemplateArgument::structurallyEquals compares a Type-kind argument by its
/// raw QualType pointer, the same non-canonical identity
/// FunctionTemplateSpecializationInfo::Profile's FoldingSet hashing relies
/// on. A re-parse re-lexes and re-parses a declaration's tokens from
/// scratch, so it mints its own, independent TemplateTypeParmDecl for a
/// template parameter like `T` -- any type built by substituting into it
/// (even something as plain as `float`) is a distinct, non-uniquable sugar
/// node from the one another parse's `T` produces, even though both denote
/// the same canonical type. Comparing by QualType::getCanonicalType() strips
/// that per-parse substitution sugar, so the independently-parsed copies'
/// specializations line up.
static bool sameTemplateArg(const TemplateArgument &A,
                            const TemplateArgument &B) {
  if (A.getKind() != B.getKind())
    return false;
  if (A.getKind() == TemplateArgument::Type)
    return A.getAsType().getCanonicalType() == B.getAsType().getCanonicalType();
  if (A.getKind() == TemplateArgument::Pack)
    return sameTemplateArgs(A.getPackAsArray(), B.getPackAsArray());
  return A.structurallyEquals(B);
}

static bool sameTemplateArgs(ArrayRef<TemplateArgument> A,
                             ArrayRef<TemplateArgument> B) {
  if (A.size() != B.size())
    return false;
  for (unsigned I = 0, E = A.size(); I != E; ++I)
    if (!sameTemplateArg(A[I], B[I]))
      return false;
  return true;
}

/// Hide the specializations of a just-redundant \p B that \p A already has
/// an equivalent for.
///
/// A specialization instantiated against B while it was still visible lives
/// only in B's own Common::Specializations FoldingSet: markRedundantTree's
/// recursion above walks B's DeclContext and template parameters, but a
/// FunctionTemplateDecl/VarTemplateDecl's implicit specializations are not
/// children in any DeclContext, so that recursion never reaches them. Left
/// alone, such a specialization stays visible (TargetVariant 0), and
/// end-of-TU processing that only looks at A's copy never touches it
/// either. Comparing template arguments directly, rather than through
/// A->findSpecialization()'s FoldingSet lookup, avoids relying on the
/// ambient target variant that was active when each side's specialization
/// was created still matching now.
static void hideRedundantSpecializations(FunctionTemplateDecl *A,
                                         FunctionTemplateDecl *B) {
  for (FunctionDecl *BSpec : B->specializations()) {
    if (BSpec->getTargetVariant() != 0)
      continue;
    const TemplateArgumentList *BArgs = BSpec->getTemplateSpecializationArgs();
    if (!BArgs)
      continue;
    bool HasEquivalent =
        llvm::any_of(A->specializations(), [&](const FunctionDecl *ASpec) {
          const TemplateArgumentList *AArgs =
              ASpec->getTemplateSpecializationArgs();
          return AArgs && sameTemplateArgs(AArgs->asArray(), BArgs->asArray());
        });
    if (HasEquivalent)
      BSpec->setTargetVariant(Decl::TargetVariantRedundant);
  }
}

static void hideRedundantSpecializations(VarTemplateDecl *A,
                                         VarTemplateDecl *B) {
  for (VarTemplateSpecializationDecl *BSpec : B->specializations()) {
    if (BSpec->getTargetVariant() != 0)
      continue;
    ArrayRef<TemplateArgument> BArgs = BSpec->getTemplateArgs().asArray();
    bool HasEquivalent = llvm::any_of(
        A->specializations(), [&](const VarTemplateSpecializationDecl *ASpec) {
          return sameTemplateArgs(ASpec->getTemplateArgs().asArray(), BArgs);
        });
    if (HasEquivalent)
      BSpec->setTargetVariant(Decl::TargetVariantRedundant);
  }
}

/// Propagate an explicit-instantiation tag from one target-tagged sibling's
/// specialization to the other's.
///
/// Unlike hideRedundantSpecializations above, this must run even when A and B
/// are *not* interchangeable: a function template that itself directly
/// branches on arch macros produces two FunctionTemplateDecls with genuinely
/// different bodies -- never interchangeable, so the pairing loop below
/// never reaches them otherwise. Each keeps its own, entirely separate
/// Common::Specializations FoldingSet. A file's `extern template`
/// declarations are ordinary, non-divergent code: parsed once, they only
/// ever tag the primary's copy, while every real call site resolves to the
/// *other* copy's FoldingSet instead, so its specializations never see the
/// extern-template tag and get fully defined at end-of-TU regardless.
/// Comparing template arguments directly, as hideRedundantSpecializations
/// does, avoids relying on the ambient target variant that was active when
/// each side's specialization was created still matching now.
static void propagateExplicitInstantiationKind(FunctionTemplateDecl *A,
                                               FunctionTemplateDecl *B) {
  auto Propagate = [](FunctionTemplateDecl *From, FunctionTemplateDecl *To) {
    for (FunctionDecl *FromSpec : From->specializations()) {
      TemplateSpecializationKind FromTSK =
          FromSpec->getTemplateSpecializationKind();
      if (FromTSK != TSK_ExplicitInstantiationDeclaration &&
          FromTSK != TSK_ExplicitInstantiationDefinition)
        continue;
      const TemplateArgumentList *FromArgs =
          FromSpec->getTemplateSpecializationArgs();
      if (!FromArgs)
        continue;
      for (FunctionDecl *ToSpec : To->specializations()) {
        TemplateSpecializationKind ToTSK =
            ToSpec->getTemplateSpecializationKind();
        if (ToTSK == TSK_ExplicitInstantiationDeclaration ||
            ToTSK == TSK_ExplicitInstantiationDefinition)
          continue;
        const TemplateArgumentList *ToArgs =
            ToSpec->getTemplateSpecializationArgs();
        if (!ToArgs ||
            !sameTemplateArgs(ToArgs->asArray(), FromArgs->asArray()))
          continue;
        ToSpec->setTemplateSpecializationKind(
            FromTSK, FromSpec->getPointOfInstantiation());
      }
    }
  };
  Propagate(A, B);
  Propagate(B, A);
}

namespace {
/// The canonical types and the declarations other than its own locals a
/// declaration names, in traversal order.
class NamedEntitiesCollector
    : public RecursiveASTVisitor<NamedEntitiesCollector> {
public:
  bool VisitTypeLoc(TypeLoc TL) {
    QualType T = TL.getType();
    Types.push_back(T.isNull() ? T : T.getCanonicalType());
    return true;
  }
  bool VisitExpr(Expr *E) {
    QualType T = E->getType();
    Types.push_back(T.isNull() ? T : T.getCanonicalType());
    return true;
  }
  bool VisitDeclRefExpr(DeclRefExpr *E) {
    add(E->getDecl());
    return true;
  }
  bool VisitMemberExpr(MemberExpr *E) {
    add(E->getMemberDecl());
    return true;
  }
  SmallVector<QualType, 32> Types;
  SmallVector<const Decl *, 16> Decls;

private:
  void add(const ValueDecl *D) {
    if (D->isTemplateParameter() || D->getDeclContext()->isFunctionOrMethod())
      return;
    Decls.push_back(D->getTargetVariant() ? D : D->getCanonicalDecl());
  }
};
} // namespace

/// Whether \p A and \p B, two targets' copies of a declaration, name the same
/// types and declarations. Each was parsed for its own target: a type keyed by
/// target (see ClassTemplateSpecializationDecl::Profile) is that target's in
/// each, and so is a function its targets have different copies of. One copy
/// can only stand for the other's target where they name the same ones.
static bool nameSameEntities(const Decl *A, const Decl *B) {
  NamedEntitiesCollector CA, CB;
  CA.TraverseDecl(const_cast<Decl *>(A));
  CB.TraverseDecl(const_cast<Decl *>(B));
  return CA.Types == CB.Types && CA.Decls == CB.Decls;
}

/// For -multi-target-audit-joins: what a type or declaration named in a copy
/// means to the code emitted for it. A builtin is its builtin, a declaration
/// local to the copy is the copy's own, and a specialization keyed by target
/// is that target's. Classes, templates and declarations are otherwise named
/// by their names, signatures being compared as types. Targets' copies of
/// them are compared where they are merged.
struct EntityKeys {
  SmallVector<uintptr_t, 32> Types;
  SmallVector<uintptr_t, 16> Decls;
};

namespace {
class EntityKeyer {
  const ASTContext &Ctx;
  const Decl *Copy;

public:
  EntityKeyer(const ASTContext &Ctx, const Decl *Copy) : Ctx(Ctx), Copy(Copy) {}

  /// The copy \p D was merged into, which it stands for, if it was.
  const Decl *resolve(const Decl *D) const {
    for (unsigned Depth = 0; D && D->isRedundantTargetVariant() && Depth < 8;
         ++Depth)
      if (const Decl *Survivor = Ctx.getMultiTarget().getTargetMergeSurvivor(D))
        D = Survivor;
      else
        break;
    return D;
  }

  bool isLocal(const Decl *D) const {
    for (const DeclContext *Start :
         {D->getDeclContext(), D->getLexicalDeclContext()})
      for (const DeclContext *DC = Start; DC; DC = DC->getParent())
        if (DC->isFunctionOrMethod() || cast<Decl>(DC) == Copy ||
            cast<Decl>(DC)->getDescribedTemplate() == Copy)
          return true;
    return false;
  }
  static uintptr_t nameKey(const NamedDecl *ND) {
    return llvm::hash_value(ND->getQualifiedNameAsString());
  }
  /// A dependent type by the template it names and its arguments.
  uintptr_t dependentTypeKey(QualType T) const {
    if (const auto *DNT = T->getAs<DependentNameType>()) {
      NestedNameSpecifier NNS = DNT->getQualifier();
      return NNS.getKind() == NestedNameSpecifier::Kind::Type
                 ? dependentTypeKey(QualType(NNS.getAsType(), 0))
                 : 0;
    }
    const auto *TST = T->getAs<TemplateSpecializationType>();
    if (!TST)
      return 0;
    llvm::hash_code H(0);
    const TemplateDecl *TD = TST->getTemplateName().getAsTemplateDecl();
    if (TD)
      H = llvm::hash_value(nameKey(TD));
    for (const TemplateArgument &Arg : TST->template_arguments())
      if (Arg.getKind() == TemplateArgument::Type)
        H = llvm::hash_combine(H, typeKey(Arg.getAsType()));
    return H;
  }

  uintptr_t typeKey(QualType T) const {
    if (T.isNull())
      return 0;
    if (T->isDependentType())
      return dependentTypeKey(T.getCanonicalType());
    T = T.getCanonicalType();
    if (const auto *PT = T->getAs<PointerType>())
      return llvm::hash_combine(1, typeKey(PT->getPointeeType()),
                                T.getCVRQualifiers());
    if (const auto *RT = T->getAs<ReferenceType>())
      return llvm::hash_combine(2, typeKey(RT->getPointeeType()),
                                RT->isLValueReferenceType());
    if (const ArrayType *AT = T->getAsArrayTypeUnsafe()) {
      const auto *CAT = dyn_cast<ConstantArrayType>(AT);
      return llvm::hash_combine(
          3, typeKey(AT->getElementType()),
          CAT ? CAT->getSize().getZExtValue() : uint64_t(0),
          T.getCVRQualifiers());
    }
    if (const auto *FPT = T->getAs<FunctionProtoType>()) {
      llvm::hash_code H = llvm::hash_combine(4, typeKey(FPT->getReturnType()));
      for (QualType P : FPT->param_types())
        H = llvm::hash_combine(H, typeKey(P));
      return H;
    }
    if (const TagDecl *TD = T->getAsTagDecl()) {
      TD = cast<TagDecl>(resolve(TD));
      if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(TD);
          CTSD &&
          (CTSD->getSpecializedTemplate()->getTargetVariant() ||
           (!Ctx.getMultiTarget().getSpecializationKeyVariant(CTSD) &&
            !Ctx.getMultiTarget().getEffectiveTargetInstantiationKey(CTSD)))) {
        // A specialization of a template every target shares, unless keyed
        // by target, or of a target's copy of one, by its template and
        // arguments.
        llvm::hash_code H = llvm::hash_combine(
            5, nameKey(CTSD->getSpecializedTemplate()), T.getCVRQualifiers());
        for (const TemplateArgument &Arg : CTSD->getTemplateArgs().asArray())
          H = llvm::hash_combine(H, Arg.getKind() == TemplateArgument::Type
                                        ? typeKey(Arg.getAsType())
                                        : uintptr_t(0));
        return H;
      }
      // A specialization keyed by target is that target's.
      if (isa<ClassTemplateSpecializationDecl>(TD))
        return llvm::hash_combine(7, TD->getCanonicalDecl(),
                                  T.getCVRQualifiers());
      return llvm::hash_combine(6, nameKey(TD), T.getCVRQualifiers());
    }
    return reinterpret_cast<uintptr_t>(T.getAsOpaquePtr());
  }

  uintptr_t declKey(const Decl *D) const {
    if (isLocal(D) || isLocal(D = resolve(D)))
      return 0;
    if (const auto *FD = dyn_cast<FunctionDecl>(D))
      if (unsigned ID = FD->getBuiltinID())
        return ID;
    // By name and type. Targets' copies of it and of its class are compared
    // where they are merged.
    if (const auto *VD = dyn_cast<ValueDecl>(D))
      return llvm::hash_combine(nameKey(VD), typeKey(VD->getType()));
    return reinterpret_cast<uintptr_t>(D);
  }
};
} // namespace

static EntityKeys entityKeys(const ASTContext &Ctx,
                             const NamedEntitiesCollector &C, bool Meaning,
                             const Decl *Copy) {
  EntityKeyer Keyer(Ctx, Copy);
  EntityKeys Keys;
  for (QualType T : C.Types)
    Keys.Types.push_back(Meaning ? Keyer.typeKey(T)
                                 : reinterpret_cast<uintptr_t>(
                                       T.getAsOpaquePtr()));
  for (const Decl *D : C.Decls)
    Keys.Decls.push_back(Meaning ? Keyer.declKey(D)
                                 : reinterpret_cast<uintptr_t>(D));
  return Keys;
}

/// The first type or declaration \p A and \p B name differently, or "".
static std::string describeEntityDifference(const ASTContext &Ctx,
                                            const Decl *A, const Decl *B,
                                            bool Meaning) {
  NamedEntitiesCollector CA, CB;
  CA.TraverseDecl(const_cast<Decl *>(A));
  CB.TraverseDecl(const_cast<Decl *>(B));
  EntityKeys KA = entityKeys(Ctx, CA, Meaning, A),
             KB = entityKeys(Ctx, CB, Meaning, B);
  std::string Out;
  llvm::raw_string_ostream OS(Out);
  for (size_t I = 0; I != std::min(KA.Types.size(), KB.Types.size()); ++I)
    if (KA.Types[I] != KB.Types[I])
      return OS << "type " << CA.Types[I].getAsString() << " vs "
                << CB.Types[I].getAsString(),
             Out;
  if (KA.Types.size() != KB.Types.size())
    return "type count";
  auto Name = [](const Decl *D) {
    const auto *ND = dyn_cast<NamedDecl>(D);
    return (ND ? ND->getQualifiedNameAsString() : std::string("?")) + "#" +
           std::to_string(D->getTargetVariant());
  };
  for (size_t I = 0; I != std::min(KA.Decls.size(), KB.Decls.size()); ++I)
    if (KA.Decls[I] != KB.Decls[I])
      return OS << "decl " << Name(CA.Decls[I]) << " vs "
                << Name(CB.Decls[I]),
             Out;
  return KA.Decls.size() != KB.Decls.size() ? "decl count" : "";
}

/// The targets the specializations keyed by target that \p T names tell
/// apart.
static TargetPartition keyedTypeInputs(const ASTContext &Ctx, QualType T) {
  TargetPartition P = 0;
  anyNamedClass(Ctx, T, [&](const CXXRecordDecl *RD) {
    const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RD);
    if (!CTSD)
      return false;
    if (Ctx.getMultiTarget().getSpecializationKeyVariant(CTSD))
      P = Ctx.getMultiTarget().refineTargetPartition(
          P, Ctx.getMultiTarget().reconcileTargetPartition(
                 0, MultiTargetContext::TDK_Arch));
    else if (Ctx.getMultiTarget().getEffectiveTargetInstantiationKey(CTSD))
      P = Ctx.getMultiTarget().refineTargetPartition(
          P, Ctx.getMultiTarget().reconcileTargetPartition(
                 0, Ctx.getMultiTarget().getTargetDependence(
                        CTSD->getSpecializedTemplate())));
    return false;
  });
  return P;
}

/// -multi-target-audit-joins: \p B, the copy for \p BTargets, is being merged
/// into \p A, which then stands for every target if \p Promote. Reports
/// whether the inputs parsing A consumed tell A's targets apart from B's, and
/// whether the copies name the same entities and mean the same.
static void auditJoin(const ASTContext &Ctx, StringRef Where, const Decl *A,
                      const Decl *B, TargetSet BTargets, bool Promote) {
  if (Ctx.getMultiTarget().JoinAuditPath.empty())
    return;
  SmallVector<MultiTargetContext::ParseInput, 4> Inputs(
      Ctx.getMultiTarget().getParseInputList(A));
  // A specialization's signature is formed where it is deduced, before it
  // exists to own what that consumed.
  if (const auto *FD = dyn_cast<FunctionDecl>(A);
      FD && FD->isTemplateInstantiation())
    if (TargetPartition Keyed = keyedTypeInputs(Ctx, FD->getType()))
      Inputs.push_back({'k', false, Keyed, "signature"});
  TargetSet ATargets = A->getTargetSet();
  unsigned Last = 1 + Ctx.getNumAuxTargets();
  auto Separates = [&](TargetPartition P) {
    for (unsigned X = 1; X <= Last; ++X)
      for (unsigned Y = 1; Y <= Last; ++Y)
        if (ATargets.contains(X) && BTargets.contains(Y) &&
            Ctx.getMultiTarget().separatesTargets(P, X, Y))
          return true;
    return false;
  };
  std::string Why;
  for (const MultiTargetContext::ParseInput &In : Inputs)
    if (Separates(In.Targets))
      Why += (Why.empty() ? "" : ";") + std::string(1, In.Kind) +
             (In.FromMember ? "+" : "") + ":" + In.What;
  bool AuditSame = Why.empty();
  // The audit stands in for parsing a group of targets' copies separately
  // only where their tokens are the same; other copies are parsed anyway.
  unsigned AClass = Ctx.getMultiTarget().getTokenClass(A),
           BClass = Ctx.getMultiTarget().getTokenClass(B);
  bool SameTokens = Where == "reparse" || (AClass && AClass == BClass);
  // A container's declarations are merged, and compared, one by one.
  bool Container = isa<NamespaceDecl, LinkageSpecDecl>(A);
  std::string Difference =
      Container ? "" : describeEntityDifference(Ctx, A, B, /*Meaning=*/false);
  std::string MeaningDifference =
      Container ? "" : describeEntityDifference(Ctx, A, B, /*Meaning=*/true);
  auto Set = [&](TargetSet T) {
    std::string S;
    for (unsigned V = 1; V <= Last; ++V)
      S += T.contains(V) ? '1' : '0';
    return S;
  };
  std::error_code EC;
  llvm::raw_fd_ostream OS(Ctx.getMultiTarget().JoinAuditPath, EC,
                          llvm::sys::fs::OF_Append | llvm::sys::fs::OF_Text);
  if (EC)
    return;
  const auto *ND = dyn_cast<NamedDecl>(A);
  PresumedLoc PL = Ctx.getSourceManager().getPresumedLoc(A->getLocation());
  OS << Where << (Promote ? " promote" : " join")
     << " tokens=" << (SameTokens ? "same" : "differ")
     << " audit=" << (AuditSame ? "same" : "differs")
     << " entities=" << (Difference.empty() ? "same" : "differ") << " meaning="
     << (Container                   ? "container"
         : MeaningDifference.empty() ? "same"
                                     : "differ")
     << " " << A->getDeclKindName() << " "
     << (ND ? ND->getQualifiedNameAsString() : std::string("-")) << " @"
     << (PL.isValid()
             ? (llvm::Twine(llvm::sys::path::filename(PL.getFilename())) + ":" +
                llvm::Twine(PL.getLine()))
                   .str()
             : std::string("?"))
     << " A=" << Set(ATargets) << " B=" << Set(BTargets);
  if (!MeaningDifference.empty())
    OS << " diff=" << MeaningDifference;
  if (!Why.empty())
    OS << " why=" << Why;
  OS << "\n";
}

/// Whether \p VD's own declared type names a per-target-forked class
/// template specialization (ClassTemplateDecl::hasTargetTaggedValueMember())
/// -- e.g. `tile_like<16,16,int> t`, where each target's Sema pass
/// instantiates its own, distinct ClassTemplateSpecializationDecl (see
/// ClassTemplateSpecializationDecl::Profile's target fold-in for this
/// predicate).
static bool isTargetForkedRecordVar(const VarDecl *VD) {
  const auto *CTSD = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      VD->getType()->getAsCXXRecordDecl());
  return CTSD && CTSD->getSpecializedTemplate()->hasTargetTaggedValueMember();
}

/// Walk \p A and \p B in lockstep over Stmt::children(), additionally
/// special-casing DeclStmt: its children() yields each decl's initializer
/// expression, not the decl itself (StmtIteratorBase::GetDeclExpr), so a
/// local variable with no initializer -- `tile_like<16,16,int> t;`, an
/// aggregate with no user-declared constructor -- is otherwise invisible to
/// any Stmt-tree walk.
///
/// This is the check SameBody/bodyShape cannot make: two reparse alternates
/// of the same declaration replay the identical token range under a
/// different target ambient, so they have identical source ranges and
/// identical statement shape throughout -- the only place they can disagree
/// is in what a name resolved to, which a shape-only hash never sees. A
/// local variable's type is exactly such a resolution: `t`'s type is bound
/// to whichever target's Sema pass parsed this alternate, and each real
/// target instantiates its own distinct specialization for a class template
/// with ClassTemplateDecl::hasTargetTaggedValueMember() (e.g. tile_like<>,
/// whose `ne` is computed from a target-divergent callee).
static bool sameLocalDeclTypes(const Stmt *A, const Stmt *B) {
  if (!A || !B)
    return true;
  if (const auto *DSA = dyn_cast<DeclStmt>(A)) {
    const auto *DSB = cast<DeclStmt>(B);
    auto ItA = DSA->decl_begin(), EndA = DSA->decl_end();
    auto ItB = DSB->decl_begin();
    for (; ItA != EndA; ++ItA, ++ItB) {
      const auto *VDA = dyn_cast<VarDecl>(*ItA);
      const auto *VDB = dyn_cast<VarDecl>(*ItB);
      if (!VDA || !VDB)
        continue;
      if (isTargetForkedRecordVar(VDA) && isTargetForkedRecordVar(VDB) &&
          VDA->getType()->getAsCXXRecordDecl() !=
              VDB->getType()->getAsCXXRecordDecl())
        return false;
    }
  }
  auto ChildrenA = A->children();
  auto ChildrenB = B->children();
  auto ItA = ChildrenA.begin(), EndA = ChildrenA.end();
  auto ItB = ChildrenB.begin(), EndB = ChildrenB.end();
  for (; ItA != EndA && ItB != EndB; ++ItA, ++ItB)
    if (!sameLocalDeclTypes(*ItA, *ItB))
      return false;
  return true;
}

/// \p SEC.IsEquivalent for a context that already answered a query: it keeps
/// one search's visited pairs, and a new search must start without them.
static bool isEquivalentType(StructuralEquivalenceContext &SEC, QualType A,
                             QualType B) {
  SEC.DeclsToCheck = {};
  SEC.VisitedDecls.clear();
  return SEC.IsEquivalent(A, B);
}

/// The same for declarations.
static bool isEquivalentDecl(StructuralEquivalenceContext &SEC, const Decl *A,
                             const Decl *B) {
  SEC.DeclsToCheck = {};
  SEC.VisitedDecls.clear();
  return SEC.IsEquivalent(const_cast<Decl *>(A), const_cast<Decl *>(B));
}

static bool isInterchangeable(ASTContext &Ctx,
                              StructuralEquivalenceContext &SEC, const Decl *A,
                              const Decl *B) {
  // StructuralEquivalenceContext compares a class's fields and bases, not
  // what its other members declare.
  if (const auto *RA = dyn_cast<CXXRecordDecl>(A))
    if (const auto *RB = dyn_cast<CXXRecordDecl>(B);
        RB && RA->isThisDeclarationADefinition() &&
        RB->isThisDeclarationADefinition() &&
        Ctx.getMultiTarget().getRecordDeclarationShape(RA) !=
            Ctx.getMultiTarget().getRecordDeclarationShape(RB))
      return false;
  // A field is part of its class's layout: the fields of two targets' copies
  // of a class are only as interchangeable as the classes, or a copy kept
  // apart loses its own.
  if (isa<FieldDecl, IndirectFieldDecl>(A))
    if (const auto *PA = dyn_cast<CXXRecordDecl>(A->getDeclContext()))
      if (const auto *PB = dyn_cast<CXXRecordDecl>(B->getDeclContext());
          PB && PA != PB && PA->getTargetVariant() && PB->getTargetVariant() &&
          !isInterchangeable(Ctx, SEC, PA, PB))
        return false;
  // So is a member function, whose body reads its class's layout.
  if (isa_and_nonnull<CXXMethodDecl>(A->getAsFunction()))
    if (const auto *PA = dyn_cast<CXXRecordDecl>(A->getDeclContext()))
      if (const auto *PB = dyn_cast<CXXRecordDecl>(B->getDeclContext());
          PB && PA != PB && PA->getTargetVariant() && PB->getTargetVariant() &&
          (!isEquivalentDecl(SEC, PA, PB) ||
           !isInterchangeable(Ctx, SEC, PA, PB)))
        return false;
  // Nor the namespace an alias names.
  if (const auto *NA = dyn_cast<NamespaceAliasDecl>(A))
    if (const auto *NB = dyn_cast<NamespaceAliasDecl>(B);
        NB && NA->getNamespace()->getCanonicalDecl() !=
                  NB->getNamespace()->getCanonicalDecl())
      return false;
  // StructuralEquivalenceContext does not compare an enum's underlying type.
  if (const auto *EA = dyn_cast<EnumDecl>(A))
    if (const auto *EB = dyn_cast<EnumDecl>(B);
        EB &&
        !isEquivalentType(SEC, EA->getIntegerType(), EB->getIntegerType()))
      return false;
  // Nor what a using-declaration introduces. Copies of one entity are found
  // in the same scope, or in copies of one class.
  if (const auto *UA = dyn_cast<BaseUsingDecl>(A)) {
    const auto *UB = dyn_cast<BaseUsingDecl>(B);
    if (!UB || UA->shadow_size() != UB->shadow_size())
      return false;
    auto SameScope = [](const DeclContext *DA, const DeclContext *DB) {
      DA = DA->getRedeclContext();
      DB = DB->getRedeclContext();
      if (DA->Equals(DB))
        return true;
      const auto *RA = dyn_cast<RecordDecl>(DA), *RB = dyn_cast<RecordDecl>(DB);
      return RA && RB &&
             RA->getQualifiedNameAsString() == RB->getQualifiedNameAsString();
    };
    for (auto [SA, SB] : llvm::zip_equal(UA->shadows(), UB->shadows())) {
      NamedDecl *TA = SA->getTargetDecl(), *TB = SB->getTargetDecl();
      if (TA->getCanonicalDecl() != TB->getCanonicalDecl() &&
          (!SameScope(TA->getDeclContext(), TB->getDeclContext()) ||
           !isEquivalentDecl(SEC, TA, TB)))
        return false;
    }
  }
  if (isa<UnresolvedUsingValueDecl, UnresolvedUsingTypenameDecl>(A) &&
      dependentUsingScope(Ctx, A) != dependentUsingScope(Ctx, B))
    return false;
  if (const auto *DA = dyn_cast<UsingDirectiveDecl>(A))
    if (const auto *DB = dyn_cast<UsingDirectiveDecl>(B);
        DB && DA->getNominatedNamespace()->getCanonicalDecl() !=
                  DB->getNominatedNamespace()->getCanonicalDecl())
      return false;
  SourceManager &SM = Ctx.getSourceManager();
  auto SameRange = [&](SourceRange RA, SourceRange RB) {
    return RA.getBegin().printToString(SM) == RB.getBegin().printToString(SM) &&
           RA.getEnd().printToString(SM) == RB.getEnd().printToString(SM);
  };

  auto SameBody = [&](const Stmt *SA, const Stmt *SB) {
    if (!SA != !SB)
      return false;
    if (!SA)
      return true;
    return SameRange(SA->getSourceRange(), SB->getSourceRange()) &&
           bodyShape(SM, SA) == bodyShape(SM, SB);
  };

  // A constructor's member initializers are part of its definition.
  auto SameCtorInits = [&](const Decl *DA, const Decl *DB) {
    const auto *CA = dyn_cast<CXXConstructorDecl>(DA);
    const auto *CB = dyn_cast<CXXConstructorDecl>(DB);
    if (!CA || !CB)
      return true;
    if (CA->getNumCtorInitializers() != CB->getNumCtorInitializers())
      return false;
    for (auto ItA = CA->init_begin(), ItB = CB->init_begin(),
              EndA = CA->init_end();
         ItA != EndA; ++ItA, ++ItB)
      if ((*ItA)->isWritten() != (*ItB)->isWritten() ||
          ((*ItA)->isWritten() &&
           !SameBody((*ItA)->getInit(), (*ItB)->getInit())))
        return false;
    return true;
  };

  // Nor does it compare default arguments, which callers use.
  if (const auto *FA = dyn_cast<FunctionDecl>(A))
    if (const auto *FB = dyn_cast<FunctionDecl>(B);
        FB && FA->getNumParams() == FB->getNumParams()) {
      auto DefaultArg = [](const ParmVarDecl *P) -> const Expr * {
        if (P->hasUninstantiatedDefaultArg())
          return P->getUninstantiatedDefaultArg();
        if (P->hasDefaultArg() && !P->hasUnparsedDefaultArg())
          return P->getDefaultArg();
        return nullptr;
      };
      for (unsigned I = 0, E = FA->getNumParams(); I != E; ++I) {
        const ParmVarDecl *PA = FA->getParamDecl(I), *PB = FB->getParamDecl(I);
        const Expr *DA = DefaultArg(PA), *DB = DefaultArg(PB);
        if (PA->hasDefaultArg() != PB->hasDefaultArg() || !DA != !DB ||
            (DA && bodyShape(SM, DA) != bodyShape(SM, DB)))
          return false;
      }
    }

  // Decl::getBody() returns null for a TemplateDecl -- the body belongs to
  // the declaration it wraps -- so without recursing into it, a template
  // pair would compare as "neither has a body" and merge unconditionally,
  // silently carrying one target's body into another's specializations.
  if (const auto *TA = dyn_cast<TemplateDecl>(A))
    if (const auto *TB = dyn_cast<TemplateDecl>(B))
      if (TA->getTemplatedDecl() && TB->getTemplatedDecl())
        if (!isInterchangeable(Ctx, SEC, TA->getTemplatedDecl(),
                               TB->getTemplatedDecl()))
          return false;

  // StructuralEquivalenceContext compares no attributes, yet alignment is
  // part of a class's layout, also of one only a typedef names.
  auto LayoutRecord = [](const Decl *D) -> const RecordDecl * {
    if (const auto *TND = dyn_cast<TypedefNameDecl>(D))
      D = TND->getUnderlyingType()
              ->getBaseElementTypeUnsafe()
              ->getAsRecordDecl();
    const auto *RD = dyn_cast_or_null<RecordDecl>(D);
    return RD ? RD->getDefinition() : nullptr;
  };
  if (const RecordDecl *LA = LayoutRecord(A), *LB = LayoutRecord(B);
      LA && LB && LA != LB &&
      layoutAttributes(Ctx, LA) != layoutAttributes(Ctx, LB))
    return false;

  // CXXRecordDecl::getBody() is unconditionally null too -- a class has no
  // Stmt* body, it has members -- so the same blind spot recurs one level
  // deeper for a class template than for a function template:
  // StructuralEquivalenceContext's own RecordDecl comparison walks base
  // classes, friends, and *data fields* (RecordDecl::field_iterator) only,
  // never type-alias members, and never a method's body. A struct built
  // entirely out of member typedefs has zero fields, so that comparison
  // passes vacuously between two arms whose aliases actually resolve to
  // differently-shaped tile<> partial specializations; a struct with a
  // genuinely divergent method body but no divergent typedefs has zero
  // fields *and* zero divergent typedefs, so neither this function's own
  // typedef check nor StructuralEquivalenceContext's field-only walk ever
  // inspects the method body that actually differs.
  if (const auto *RA = dyn_cast<CXXRecordDecl>(A)) {
    const auto *RB = cast<CXXRecordDecl>(B);
    auto TypedefMembers = [](const CXXRecordDecl *R) {
      SmallVector<const TypedefNameDecl *, 8> V;
      for (const Decl *D : R->decls())
        if (const auto *TND = dyn_cast<TypedefNameDecl>(D))
          V.push_back(TND);
      return V;
    };
    SmallVector<const TypedefNameDecl *, 8> MA = TypedefMembers(RA),
                                            MB = TypedefMembers(RB);
    if (MA.size() != MB.size())
      return false;
    for (unsigned I = 0, E = MA.size(); I != E; ++I) {
      if (MA[I]->getName() != MB[I]->getName())
        return false;
      if (!isEquivalentType(SEC, MA[I]->getUnderlyingType(),
                            MB[I]->getUnderlyingType()))
        return false;
    }

    // Member function templates included: their bodies can differ too
    // (rocPRIM's warp_reduce_dpp::reduce_impl). Implicit members are declared
    // when first needed, so a copy has them depending on how it was used.
    auto MethodMembers = [](const CXXRecordDecl *R) {
      SmallVector<const CXXMethodDecl *, 8> V;
      for (const Decl *D : R->decls())
        if (const auto *MD = dyn_cast<CXXMethodDecl>(D)) {
          if (!MD->isImplicit())
            V.push_back(MD);
        } else if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(D)) {
          if (const auto *MD = dyn_cast<CXXMethodDecl>(FTD->getTemplatedDecl()))
            V.push_back(MD);
        }
      return V;
    };
    // A constructor's or destructor's name is its class's type, and so is
    // each copy's own.
    auto SameMemberName = [](const CXXMethodDecl *X, const CXXMethodDecl *Y) {
      DeclarationName NX = X->getDeclName(), NY = Y->getDeclName();
      if (NX.getNameKind() != NY.getNameKind())
        return false;
      switch (NX.getNameKind()) {
      case DeclarationName::CXXConstructorName:
      case DeclarationName::CXXDestructorName:
        return true;
      case DeclarationName::CXXConversionFunctionName:
        return NX.getAsString() == NY.getAsString();
      default:
        return NX == NY;
      }
    };
    SmallVector<const CXXMethodDecl *, 8> MethA = MethodMembers(RA),
                                          MethB = MethodMembers(RB);
    if (MethA.size() != MethB.size())
      return false;
    for (unsigned I = 0, E = MethA.size(); I != E; ++I) {
      if (!SameMemberName(MethA[I], MethB[I]))
        return false;
      if (!SameBody(MethA[I]->getBody(), MethB[I]->getBody()) ||
          !SameCtorInits(MethA[I], MethB[I]))
        return false;
    }

    // Nor the bodies of friend functions defined in the class.
    auto FriendFunctions = [](const CXXRecordDecl *R) {
      SmallVector<const FunctionDecl *, 4> V;
      for (const Decl *D : R->decls())
        if (const auto *Friend = dyn_cast<FriendDecl>(D))
          if (const NamedDecl *ND = Friend->getFriendDecl())
            if (const FunctionDecl *FD = ND->getAsFunction())
              V.push_back(FD);
      return V;
    };
    SmallVector<const FunctionDecl *, 4> FriendsA = FriendFunctions(RA),
                                         FriendsB = FriendFunctions(RB);
    if (FriendsA.size() != FriendsB.size())
      return false;
    for (auto [FA, FB] : llvm::zip_equal(FriendsA, FriendsB))
      if (FA->getDeclName() != FB->getDeclName() ||
          FA->doesThisDeclarationHaveABody() !=
              FB->doesThisDeclarationHaveABody() ||
          (FA->doesThisDeclarationHaveABody() &&
           !SameBody(FA->getBody(), FB->getBody())))
        return false;

    // Copies that redeclare one class share its canonical declaration, which
    // StructuralEquivalenceContext takes as proof enough; their fields can
    // still differ (`struct S;` then a definition per target).
    SmallVector<const FieldDecl *, 8> FA(RA->fields()), FB(RB->fields());
    if (FA.size() != FB.size())
      return false;
    for (unsigned I = 0, E = FA.size(); I != E; ++I) {
      if (FA[I]->getDeclName() != FB[I]->getDeclName() ||
          FA[I]->isBitField() != FB[I]->isBitField() ||
          !isEquivalentType(SEC, FA[I]->getType(), FB[I]->getType()) ||
          !SameBody(FA[I]->getInClassInitializer(),
                    FB[I]->getInClassInitializer()) ||
          (FA[I]->isBitField() &&
           !SameBody(FA[I]->getBitWidth(), FB[I]->getBitWidth())))
        return false;
    }
    if (RA->hasDefinition() && RB->hasDefinition() &&
        RA->isThisDeclarationADefinition() &&
        RB->isThisDeclarationADefinition()) {
      if (RA->getNumBases() != RB->getNumBases())
        return false;
      for (auto ItA = RA->bases_begin(), ItB = RB->bases_begin(),
                EndA = RA->bases_end();
           ItA != EndA; ++ItA, ++ItB)
        if (!isEquivalentType(SEC, ItA->getType(), ItB->getType()))
          return false;
    }

    // Nor does StructuralEquivalenceContext compare member classes.
    auto ClassMembers = [](const CXXRecordDecl *R) {
      SmallVector<const CXXRecordDecl *, 4> V;
      for (const Decl *D : R->decls())
        if (const auto *Nested = dyn_cast<CXXRecordDecl>(D);
            Nested && !Nested->isImplicit())
          V.push_back(Nested);
      return V;
    };
    SmallVector<const CXXRecordDecl *, 4> NestedA = ClassMembers(RA),
                                          NestedB = ClassMembers(RB);
    if (NestedA.size() != NestedB.size())
      return false;
    for (unsigned I = 0, E = NestedA.size(); I != E; ++I)
      if (NestedA[I]->getDeclName() != NestedB[I]->getDeclName() ||
          NestedA[I]->isThisDeclarationADefinition() !=
              NestedB[I]->isThisDeclarationADefinition() ||
          !isEquivalentDecl(SEC, NestedA[I], NestedB[I]) ||
          !isInterchangeable(Ctx, SEC, NestedA[I], NestedB[I]))
        return false;
    // Nor member using-declarations.
    auto UsingMembers = [](const CXXRecordDecl *R) {
      SmallVector<const NamedDecl *, 4> V;
      for (const Decl *D : R->decls())
        if (isa<BaseUsingDecl, UnresolvedUsingValueDecl,
                UnresolvedUsingTypenameDecl>(D))
          V.push_back(cast<NamedDecl>(D));
      return V;
    };
    SmallVector<const NamedDecl *, 4> UsingA = UsingMembers(RA),
                                      UsingB = UsingMembers(RB);
    if (UsingA.size() != UsingB.size())
      return false;
    for (unsigned I = 0, E = UsingA.size(); I != E; ++I)
      if (UsingA[I]->getDeclName() != UsingB[I]->getDeclName() ||
          UsingA[I]->getKind() != UsingB[I]->getKind() ||
          !isInterchangeable(Ctx, SEC, UsingA[I], UsingB[I]))
        return false;

    // Same blind spot again, one member kind over: a static data member's
    // initializer is neither a field (StructuralEquivalenceContext's own
    // walk) nor a method body (the check just above), so without this check
    // two arms whose only difference is a divergent initializer would be
    // declared interchangeable and merged anyway.
    auto DataMembers = [](const CXXRecordDecl *R) {
      SmallVector<const VarDecl *, 8> V;
      for (const Decl *D : R->decls())
        if (const auto *VD = dyn_cast<VarDecl>(D))
          V.push_back(VD);
      return V;
    };
    SmallVector<const VarDecl *, 8> DMA = DataMembers(RA),
                                    DMB = DataMembers(RB);
    if (DMA.size() != DMB.size())
      return false;
    for (unsigned I = 0, E = DMA.size(); I != E; ++I) {
      if (DMA[I]->getDeclName() != DMB[I]->getDeclName())
        return false;
      if (!SameBody(DMA[I]->getInit(), DMB[I]->getInit()))
        return false;
    }
  }

  if (A->hasAttr<CUDADeviceAttr>() != B->hasAttr<CUDADeviceAttr>() ||
      A->hasAttr<CUDAHostAttr>() != B->hasAttr<CUDAHostAttr>() ||
      A->hasAttr<CUDAGlobalAttr>() != B->hasAttr<CUDAGlobalAttr>())
    return false;

  // An attribute both copies carry can diverge in its arguments (e.g.
  // __launch_bounds__ sized by a target-dependent macro); compare those as
  // printed. One carried by only one copy (inherited from another
  // redeclaration, or guarded by the target) is not compared.
  auto PrintedAttrs = [&](const Decl *D) {
    llvm::SmallDenseMap<unsigned, std::string, 4> ByKind;
    if (D->hasAttrs())
      for (const Attr *At : D->attrs())
        if (!At->isImplicit()) {
          llvm::raw_string_ostream OS(ByKind[At->getKind()]);
          At->printPretty(OS, Ctx.getPrintingPolicy());
        }
    return ByKind;
  };
  if (A->hasAttrs() && B->hasAttrs()) {
    auto AttrsB = PrintedAttrs(B);
    for (const auto &[Kind, Printed] : PrintedAttrs(A)) {
      auto It = AttrsB.find(Kind);
      if (It != AttrsB.end() && It->second != Printed)
        return false;
    }
  }

  if (!SameBody(A->getBody(), B->getBody()) || !SameCtorInits(A, B))
    return false;

  if (!sameLocalDeclTypes(A->getBody(), B->getBody()))
    return false;

  const auto *VA = dyn_cast<VarDecl>(A), *VB = dyn_cast<VarDecl>(B);
  if (VA && VB && !SameBody(VA->getInit(), VB->getInit()))
    return false;
  return true;
}

namespace {
std::string widenedAlternativeKey(ASTContext &Ctx, const Decl *D,
                                  llvm::StringMap<unsigned> &AnonOrdinal);
bool absentTargetBlocksSharing(ASTContext &Ctx, const Decl *A, StringRef Key,
                               ArrayRef<WidenedArm> Arms);
} // namespace

unsigned clang::mergeEquivalentVariants(ASTContext &Ctx) {
  llvm::StringMap<SmallVector<NamedDecl *, 2>> ByKey;
  TaggedCollector(Ctx, ByKey).TraverseDecl(Ctx.getTranslationUnitDecl());

  StructuralEquivalenceContext::NonEquivalentDeclSet NonEquiv;
  unsigned Count = 0;
  for (auto &Entry : ByKey) {
    // One representative per target: two real device archs plus the host tag
    // three targets 1..3, not just a primary and one aux. Keyed on the first
    // declaration seen for each variant.
    llvm::SmallDenseMap<unsigned, NamedDecl *, 4> ByVariant;
    // A declaration merged since it was collected has gone with its survivor.
    for (NamedDecl *D : Entry.second)
      if (!D->isRedundantTargetVariant())
        ByVariant.try_emplace(D->getTargetVariant(), D);
    NamedDecl *A = ByVariant.lookup(1);
    // An implicit instantiation that an aux target never got a copy of is not
    // target-specific -- it is shared, and claimed only because the primary
    // happened to be current when it was instantiated. A specialization is
    // found through the template's FoldingSet, not by name lookup, so the
    // target filter never sees it and the aux target silently reuses the
    // primary's.
    if (A && ByVariant.size() == 1) {
      const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(A);
      if (CTSD && CTSD->getSpecializationKind() == TSK_ImplicitInstantiation) {
        unclaimTree(A, 1, Count);
      }
      continue;
    }
    // A target with no copy of its own is handed A by promoting it.
    bool PromotionBlocked =
        !A || Ctx.getMultiTarget().areTargetCopiesKeptApart(A);
    TargetSet Covered;
    for (const auto &VariantEntry : ByVariant)
      Covered = Covered | VariantEntry.second->getTargetSet();
    unsigned NumCovered = 0;
    for (unsigned V : Ctx.getTargetVariants())
      NumCovered += Covered.contains(V);
    if (!PromotionBlocked && NumCovered < 1 + Ctx.getNumAuxTargets()) {
      SmallVector<WidenedArm, 4> Present;
      for (const auto &VariantEntry : ByVariant)
        Present.push_back({{},
                           VariantEntry.first,
                           VariantEntry.second->getTargetSet()});
      llvm::StringMap<unsigned> AnonOrdinal;
      PromotionBlocked = absentTargetBlocksSharing(
          Ctx, A, widenedAlternativeKey(Ctx, A, AnonOrdinal), Present);
    }
    // Copies that agree become one copy for all of their targets: the first
    // one's. It is promoted to shared only once *every* present copy agrees
    // with the primary's, not merely the first one checked: with two or more
    // aux targets, one target's copy can be interchangeable with the
    // primary's while another's genuinely differs, and a shared A would hide
    // the disagreeing target's own copy behind a declaration that target's
    // device codegen never queued.
    SmallVector<std::pair<unsigned, NamedDecl *>, 4> Cells(ByVariant.begin(),
                                                           ByVariant.end());
    llvm::sort(Cells, llvm::less_first());
    SmallVector<bool, 4> Taken(Cells.size(), false);
    // A shared declaration is no copy: it only decides whether the primary's
    // can be promoted, and is then merged into it.
    for (size_t J = 0; J != Cells.size(); ++J) {
      NamedDecl *Survivor = Cells[J].second;
      if (Taken[J] || !Cells[J].first ||
          Ctx.getMultiTarget().areTargetCopiesKeptApart(Survivor))
        continue;
      Taken[J] = true;
      bool IsPrimary = Survivor == A;
      bool AllAgree = true;
      SmallVector<std::pair<unsigned, NamedDecl *>, 4> Agreeing;
      for (size_t K = J + 1; K != Cells.size(); ++K) {
        if (Taken[K] || (!Cells[K].first && !IsPrimary))
          continue;
        auto [Variant, B] = Cells[K];
        // Must run regardless of whether the copies turn out interchangeable
        // below -- see propagateExplicitInstantiationKind's comment.
        if (IsPrimary)
          if (auto *AFTD = dyn_cast<FunctionTemplateDecl>(Survivor))
            if (auto *BFTD = dyn_cast<FunctionTemplateDecl>(B))
              propagateExplicitInstantiationKind(AFTD, BFTD);
        StructuralEquivalenceContext SEC(Ctx.getLangOpts(), Ctx, Ctx,
                                         NonEquiv,
                                         StructuralEquivalenceKind::Default,
                                         /*StrictTypeSpelling=*/false,
                                         /*Complain=*/false);
        // The copy B is left in place: hiding it too would remove redundant
        // duplicates but also declarations the host needs, because the copy's
        // subtree is not the same shape as the original's. A duplicate is a
        // lesser defect than an absence.
        if (!SEC.IsEquivalent(Survivor, B) ||
            !isInterchangeable(Ctx, SEC, Survivor, B)) {
          AllAgree = false;
          continue;
        }
        Taken[K] = true;
        Agreeing.emplace_back(Variant, B);
      }
      TargetSet Targets = Survivor->getTargetSet();
      auto MergeInto = [&](unsigned Variant, NamedDecl *B) {
        // A CXXRecordDecl/ClassTemplateDecl's subtree can contain an implicit
        // specialization reached only through its template's FoldingSet, so
        // hiding it unconditionally can lose declarations the host still
        // needs -- gated behind -hide-redundant-variants instead. A function
        // or variable's own subtree contains no such node -- but a
        // FunctionTemplateDecl/VarTemplateDecl's *specializations* live in
        // Common->Specializations, reached only through the template's
        // FoldingSet, not through DeclContext::decls() -- so markRedundantTree
        // below never hides one already instantiated against B before this
        // pass ran (see hideRedundantSpecializations above). Hiding B's own
        // declaration is always safe once the copies are confirmed
        // interchangeable; hiding an already-materialized specialization
        // additionally needs the survivor to already have an equivalent, so a
        // specialization it doesn't have isn't lost.
        //
        // EnumDecl is included here for the same reason as
        // FunctionDecl/VarDecl: an enum cannot be a template and has no
        // FoldingSet-reached specializations, so markRedundantTree's
        // DeclContext::decls() walk reaches every one of its enumerators --
        // there is no hidden subtree this could lose. Without this, an
        // unscoped enum re-touched by a later, unrelated divergent #if/#endif
        // gets a second EnumDecl copy that stays tagged and visible after the
        // survivor is unclaimed to variant 0: its now-shared enumerator and
        // B's still-tagged one are simultaneously visible whenever B's variant
        // is current, making every reference to it ambiguous even though both
        // candidates are the same declaration re-parsed.
        TargetSet BTargets = Variant ? B->getTargetSet() : TargetSet();
        recordTargetDefaultArguments(Ctx, Survivor, B, BTargets);
        if (isa<FunctionDecl, FunctionTemplateDecl, VarDecl, VarTemplateDecl,
                EnumDecl>(B)) {
          markMergedInto(Ctx, B, Survivor, Variant);
          Targets = Targets | BTargets;
        }
        if (auto *BFTD = dyn_cast<FunctionTemplateDecl>(B))
          hideRedundantSpecializations(cast<FunctionTemplateDecl>(Survivor),
                                       BFTD);
        else if (auto *BVTD = dyn_cast<VarTemplateDecl>(B))
          hideRedundantSpecializations(cast<VarTemplateDecl>(Survivor), BVTD);
      };
      bool Promote = IsPrimary && AllAgree && !PromotionBlocked;
      for (auto &[Variant, B] : Agreeing) {
        if (Variant)
          auditJoin(Ctx, "equivalent", Survivor, B, B->getTargetSet(), Promote);
        if (Promote || (Variant && nameSameEntities(Survivor, B)))
          MergeInto(Variant, B);
      }
      if (Promote) {
        if (!Agreeing.empty())
          unclaimTree(Survivor, 1, Count);
      } else if (Survivor->getTargetVariant() && !Targets.isSingle()) {
        joinTree(Survivor, Survivor->getTargetVariant(), Targets);
      }
    }
  }
  return Count;
}

bool clang::copiesMeanTheSame(ASTContext &Ctx, Decl *A, Decl *B) {
  return describeEntityDifference(Ctx, A, B, /*Meaning=*/true).empty();
}

void clang::mergeReparseAlternatives(ASTContext &Ctx,
                                     ArrayRef<Decl *> Primary,
                                     ArrayRef<ReparseAlternate> Alternates) {
  StructuralEquivalenceContext::NonEquivalentDeclSet NonEquiv;
  unsigned Count = 0;
  auto Agree = [&](Decl *A, Decl *B) {
    StructuralEquivalenceContext SEC(Ctx.getLangOpts(), Ctx, Ctx, NonEquiv,
                                     StructuralEquivalenceKind::Default,
                                     /*StrictTypeSpelling=*/false,
                                     /*Complain=*/false);
    return SEC.IsEquivalent(A, B) && isInterchangeable(Ctx, SEC, A, B);
  };
  for (size_t I = 0, E = Primary.size(); I != E; ++I) {
    // The primary's copy, then each alternate's, with the variant each was
    // parsed for and the targets it stands for.
    struct Cell {
      Decl *D;
      unsigned Variant;
      TargetSet Targets;
    };
    SmallVector<Cell, 4> Cells;
    Cells.push_back({Primary[I], 1u, TargetSet::single(1)});
    for (const ReparseAlternate &Alt : Alternates) {
      assert(Primary.size() == Alt.Decls.size() &&
             "Design 1 re-parses one declaration's tokens whole, so a primary "
             "and its alternate must have the same shape");
      Cells.push_back({Alt.Decls[I], Alt.Variant, Alt.Targets});
    }
    // Copies that agree become one copy for all of their targets: the first
    // one's, which is shared if it is every target's. A copy that disagrees
    // keeps its own.
    SmallVector<bool, 4> Taken(Cells.size(), false);
    for (size_t J = 0; J != Cells.size(); ++J) {
      Decl *A = Cells[J].D;
      if (!A || Taken[J] || A->isRedundantTargetVariant())
        continue;
      Taken[J] = true;
      SmallVector<size_t, 4> Group;
      for (size_t K = J + 1; K != Cells.size(); ++K) {
        Decl *B = Cells[K].D;
        if (B && !Taken[K] && B != A && !B->isRedundantTargetVariant() &&
            Agree(A, B)) {
          Taken[K] = true;
          Group.push_back(K);
        }
      }
      // Shared once every alternate that has a copy agrees with the primary.
      size_t Present = llvm::count_if(llvm::drop_begin(Cells), [&](auto &C) {
        return C.D && C.D != A;
      });
      bool Promote = J == 0 && Group.size() == Present;
      for (size_t K : Group)
        auditJoin(Ctx, "reparse", A, Cells[K].D, Cells[K].Targets, Promote);
      if (!Promote)
        llvm::erase_if(Group, [&](size_t K) {
          if (nameSameEntities(A, Cells[K].D))
            return false;
          Taken[K] = false;
          return true;
        });
      if (Group.empty())
        continue;
      TargetSet Targets = A->getTargetSet();
      for (size_t K : Group) {
        auto [B, Variant, BTargets] = Cells[K];
        recordTargetDefaultArguments(Ctx, A, B, BTargets);
        // Unlike mergeEquivalentVariants, B's kind is never gated here: this
        // runs before any caller anywhere in the file has been parsed, so
        // neither A nor B's FoldingSet can already hold a materialized
        // specialization that hiding B's declaration would take out of reach.
        markMergedInto(Ctx, B, A, Variant);
        Targets = Targets | BTargets;
      }
      if (Promote)
        unclaimTree(A, A->getTargetVariant(), Count);
      else
        joinTree(A, A->getTargetVariant(), Targets);
    }
  }
}

namespace {

/// A per-name/signature key used to pair declarations across two arms of a
/// widened #if/#elif alternative when the two arms' declaration counts
/// differ -- e.g. one arm's branch declares extra overloads or conversion
/// helpers that the other arms' branches don't. Pairing by raw index breaks
/// down the moment such a divergence appears anywhere in the region: every
/// declaration after that point -- even ones that are otherwise byte-
/// identical across every arm -- goes unreconciled along with it, leaving it
/// individually tagged and colliding with its own already-reconciled,
/// now-shared copy.
std::string widenedAlternativeKey(ASTContext &Ctx, const Decl *D,
                                  llvm::StringMap<unsigned> &AnonOrdinal) {
  SmallString<256> Key;
  llvm::raw_svector_ostream OS(Key);
  OS << D->getDeclKindName() << '\t';
  if (const auto *ND = dyn_cast<NamedDecl>(D)) {
    PrintingPolicy Policy = Ctx.getPrintingPolicy();
    Policy.FullyQualifiedName = true;
    Policy.PrintAsCanonical = true;
    ND->printQualifiedName(OS, Policy);
    printTemplateArgs(OS, ND, Policy);
    if (const auto *VD = dyn_cast<ValueDecl>(D))
      OS << '\t' << VD->getType().getCanonicalType().getAsString(Policy);
  } else {
    // An unnamed declaration (e.g. a static_assert) has nothing to key on
    // but its kind and its position among other unnamed declarations of
    // that kind.
    OS << '#' << AnonOrdinal[D->getDeclKindName()]++;
  }
  return std::string(Key);
}

/// Whether promoting \p A to shared would be wrong for a target with no arm
/// in \p Arms. Such a target either declared it outside this episode -- e.g.
/// an include-guarded header whose first inclusion sat under a host-only #if,
/// so a later inclusion yields an episode holding only the device arms -- and
/// promoting A would make it visible alongside that target's own copy; or did
/// not declare it at all, its arm being empty, and must not see it. Seeing a
/// device's declaration does the host no harm: it calls the host's.
bool absentTargetBlocksSharing(ASTContext &Ctx, const Decl *A, StringRef Key,
                               ArrayRef<WidenedArm> Arms) {
  const auto *ND = dyn_cast<NamedDecl>(A);
  if (!ND || !ND->getDeclName())
    return false;
  auto HasArm = [&](unsigned V) {
    return llvm::any_of(
        Arms, [V](const WidenedArm &Arm) { return Arm.Targets.contains(V); });
  };
  bool Shared = false;
  llvm::StringMap<unsigned> AnonOrdinal;
  for (const NamedDecl *Other :
       ND->getDeclContext()->getRedeclContext()->lookup(ND->getDeclName())) {
    unsigned V = Other->getTargetVariant();
    if (V == Decl::TargetVariantRedundant || (V && HasArm(V)) ||
        widenedAlternativeKey(Ctx, Other, AnonOrdinal) != Key)
      continue;
    if (!V)
      Shared = true;
    else
      return true;
  }
  if (Shared)
    return false;
  for (unsigned V : Ctx.getTargetVariants())
    if (V != Ctx.getHostVariant() && !HasArm(V))
      return true;
  return false;
}

} // namespace

void clang::mergeWidenedAlternatives(ASTContext &Ctx,
                                     ArrayRef<WidenedArm> Arms) {
  size_t NArms = Arms.size();
  if (NArms < 2)
    return;
  StructuralEquivalenceContext::NonEquivalentDeclSet NonEquiv;
  unsigned Count = 0;
  TargetKeyedTemplates Keyed;
  // Each arm's declarations by key, in order; the keys in the order they
  // first appear.
  SmallVector<llvm::StringMap<SmallVector<Decl *, 2>>, 4> ByKey(NArms);
  std::vector<std::string> Keys;
  llvm::StringSet<> Seen;
  for (size_t J = 0; J != NArms; ++J) {
    llvm::StringMap<unsigned> AnonOrdinal;
    for (Decl *D : Arms[J].Decls) {
      if (!D)
        continue;
      std::string Key = widenedAlternativeKey(Ctx, D, AnonOrdinal);
      if (Seen.insert(Key).second)
        Keys.push_back(Key);
      ByKey[J][Key].push_back(D);
    }
  }
  auto Agree = [&](Decl *A, Decl *B) {
    StructuralEquivalenceContext SEC(Ctx.getLangOpts(), Ctx, Ctx, NonEquiv,
                                     StructuralEquivalenceKind::Default,
                                     /*StrictTypeSpelling=*/false,
                                     /*Complain=*/false);
    return SEC.IsEquivalent(A, B) && isInterchangeable(Ctx, SEC, A, B);
  };
  for (const std::string &Key : Keys) {
    size_t Rows = 0;
    for (size_t J = 0; J != NArms; ++J)
      if (auto It = ByKey[J].find(Key); It != ByKey[J].end())
        Rows = std::max(Rows, It->second.size());
    // The Nth declaration of a key in one arm is paired with the Nth in each
    // other arm.
    for (size_t Row = 0; Row != Rows; ++Row) {
      SmallVector<Decl *, 4> Cells(NArms, nullptr);
      for (size_t J = 0; J != NArms; ++J)
        if (auto It = ByKey[J].find(Key);
            It != ByKey[J].end() && Row < It->second.size() &&
            !It->second[Row]->isRedundantTargetVariant())
          Cells[J] = It->second[Row];
      // Arms whose copies agree get one copy for all of them: the first
      // arm's, standing for each target it absorbed. Shared, if that is every
      // arm and no target without an arm already has its own.
      SmallVector<bool, 4> Taken(NArms, false);
      for (size_t I = 0; I != NArms; ++I) {
        Decl *A = Cells[I];
        if (!A || Taken[I])
          continue;
        Taken[I] = true;
        SmallVector<size_t, 4> Group;
        for (size_t J = I + 1; J != NArms; ++J)
          if (Cells[J] && !Taken[J] && Cells[J] != A && Agree(A, Cells[J])) {
            Taken[J] = true;
            Group.push_back(J);
          }
        bool Promote = Group.size() + 1 == NArms &&
                       !absentTargetBlocksSharing(Ctx, A, Key, Arms);
        for (size_t J : Group)
          auditJoin(Ctx, "widened", A, Cells[J], Cells[J]->getTargetSet(),
                    Promote);
        // A member of a copy of a class kept apart is that copy's own.
        auto SameClass = [&](const Decl *B) {
          const auto *PA = dyn_cast<RecordDecl>(A->getDeclContext());
          const auto *PB = dyn_cast<RecordDecl>(B->getDeclContext());
          return !PA || !PB || PA == PB ||
                 Ctx.getMultiTarget().getTargetMergeSurvivor(PB) == PA;
        };
        if (!Promote)
          llvm::erase_if(Group, [&](size_t J) {
            if (nameSameEntities(A, Cells[J]) && SameClass(Cells[J]))
              return false;
            Taken[J] = false;
            return true;
          });
        if (Group.empty())
          continue;
        TargetSet Targets = A->getTargetSet();
        for (size_t J : Group) {
          recordTargetDefaultArguments(Ctx, A, Cells[J], Arms[J].Targets);
          markMergedInto(Ctx, Cells[J], A, Arms[J].Variant);
          Targets = Targets | Arms[J].Targets;
        }
        if (Promote)
          unclaimTree(A, A->getTargetVariant(), Count, &Keyed);
        else
          joinTree(A, A->getTargetVariant(), Targets);
      }
    }
  }
  Keyed.rehashNoLongerKeyed();
}

void clang::reportVariantEquivalence(raw_ostream &OS, ASTContext &Ctx) {
  llvm::StringMap<SmallVector<NamedDecl *, 2>> ByKey;
  TaggedCollector(Ctx, ByKey).TraverseDecl(Ctx.getTranslationUnitDecl());

  unsigned Tagged = 0, Pairs = 0, Equivalent = 0, Differ = 0, Unpaired = 0,
           NotInterchangeable = 0;
  std::vector<std::string> Different;
  StructuralEquivalenceContext::NonEquivalentDeclSet NonEquiv;

  for (auto &Entry : ByKey) {
    SmallVectorImpl<NamedDecl *> &Ds = Entry.second;
    Tagged += Ds.size();
    // Pair one declaration from each target. More than two copies of a name in
    // one arm are left alone: which pairs with which is not decidable here.
    NamedDecl *A = nullptr, *B = nullptr;
    for (NamedDecl *D : Ds) {
      if (D->isCopyFor(1) && !A)
        A = D;
      else if (D->isCopyFor(2) && !B)
        B = D;
    }
    if (!A || !B) {
      Unpaired += Ds.size();
      continue;
    }
    ++Pairs;
    StructuralEquivalenceContext SEC(Ctx.getLangOpts(), Ctx, Ctx, NonEquiv,
                                     StructuralEquivalenceKind::Default,
                                     /*StrictTypeSpelling=*/false,
                                     /*Complain=*/false);
    if (SEC.IsEquivalent(A, B)) {
      if (!isInterchangeable(Ctx, SEC, A, B)) {
        ++NotInterchangeable;
        Different.push_back("[body/attrs] " + Entry.first().str());
      } else
        ++Equivalent;
    } else {
      ++Differ;
      Different.push_back(Entry.first().str());
    }
  }

  OS << "variant equivalence:\n"
     << "  tagged declarations: " << Tagged << "\n"
     << "  paired across targets: " << Pairs << "\n"
     << "    structurally equivalent (mergeable): " << Equivalent << "\n"
     << "    genuinely different:                 " << Differ << "\n"
     << "    same signature, different body/attrs: " << NotInterchangeable
     << "\n"
     << "  unpaired (one target only):            " << Unpaired << "\n";
  llvm::sort(Different);
  for (const std::string &N : Different)
    OS << "    differs: " << N << "\n";
}
