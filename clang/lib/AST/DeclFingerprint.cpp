//===- DeclFingerprint.cpp - Order-independent AST comparison -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclFingerprint.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTStructuralEquivalence.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace clang;

/// Where a body or initialiser was written, as a comparable string.
///
/// A presumed location, so that a body reached through a macro or an included
/// header names the place it was written rather than the place it was expanded.
static std::string rangeKey(const ASTContext &Ctx, SourceRange R) {
  const SourceManager &SM = Ctx.getSourceManager();
  PresumedLoc B = SM.getPresumedLoc(R.getBegin());
  PresumedLoc E = SM.getPresumedLoc(R.getEnd());
  if (B.isInvalid() || E.isInvalid())
    return "?";
  return (llvm::Twine(llvm::sys::path::filename(B.getFilename())) + ":" +
          llvm::Twine(B.getLine()) + ":" + llvm::Twine(B.getColumn()) + "-" +
          llvm::Twine(E.getLine()) + ":" + llvm::Twine(E.getColumn()))
      .str();
}

/// Append a specialization's template arguments. printQualifiedName drops
/// them, so seven specializations of one template are seven identical lines and
/// nothing downstream can tell them apart -- including the pairing in
/// mergeEquivalentVariants, which then compares an arbitrary one of a target's
/// against an arbitrary one of the other's.
///
/// Printed from the declaration's own argument list rather than through
/// getNameForDiagnostic: that recurses through canonical types and overflows
/// the stack on this code base.
static void printTemplateArgs(raw_ostream &OS, const NamedDecl *D,
                              const PrintingPolicy &Policy) {
  const TemplateArgumentList *Args = nullptr;
  if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(D))
    Args = &CTSD->getTemplateArgs();
  else if (const auto *VTSD = dyn_cast<VarTemplateSpecializationDecl>(D))
    Args = &VTSD->getTemplateArgs();
  else if (const auto *FD = dyn_cast<FunctionDecl>(D))
    Args = FD->getTemplateSpecializationArgs();
  if (Args)
    printTemplateArgumentList(OS, Args->asArray(), Policy);
}

namespace {

class FingerprintVisitor : public RecursiveASTVisitor<FingerprintVisitor> {
  ASTContext &Ctx;
  unsigned Variant;
  std::vector<std::string> &Lines;

public:
  FingerprintVisitor(ASTContext &Ctx, unsigned Variant,
                     std::vector<std::string> &Lines)
      : Ctx(Ctx), Variant(Variant), Lines(Lines) {}

  // Template patterns are visited through their instantiations too; visiting
  // the uninstantiated bodies as well would report the same entity twice and
  // depends on nothing this is trying to measure.
  bool shouldVisitTemplateInstantiations() const { return true; }
  bool shouldVisitImplicitCode() const { return false; }

  bool VisitNamedDecl(NamedDecl *D) {
    // A declaration marked for one target belongs only to that target; one
    // marked 0 belongs to all of them, which is every declaration in an
    // ordinary compilation.
    // Instantiation creates a specialization's members after the point where
    // the parser claimed the specialization, so they keep variant 0 and survive
    // a filter that looks only at the declaration itself. A declaration inside
    // a target's context belongs to that target.
    if (D->isRedundantTargetVariant())
      return true;
    const Decl *Tagged = D;
    for (const DeclContext *DC = D->getDeclContext();
         DC && !Tagged->getTargetVariant(); DC = DC->getParent())
      if (const auto *DD = dyn_cast<Decl>(DC))
        Tagged = DD;
    if (Variant && Tagged->getTargetVariant() && !Tagged->isCopyFor(Variant))
      return true;

    // A namespace is re-opened, not redeclared: `namespace std {}` written
    // twice is one namespace, and an alternative that re-opens one adds a node
    // without adding an entity. Every other kind keeps its duplicates.
    if (isa<NamespaceDecl>(D) && !D->isFirstDecl())
      return true;

    SmallString<256> Line;
    llvm::raw_svector_ostream OS(Line);
    OS << D->getDeclKindName() << '\t';

    // Qualified names are stable; printing policy is pinned so that the two
    // compilations being compared cannot disagree about spelling.
    PrintingPolicy Policy = Ctx.getPrintingPolicy();
    Policy.SuppressTagKeyword = false;
    Policy.FullyQualifiedName = true;
    Policy.PrintAsCanonical = true;
    D->printQualifiedName(OS, Policy);
    printTemplateArgs(OS, D, Policy);

    if (const auto *VD = dyn_cast<ValueDecl>(D))
      OS << '\t' << VD->getType().getCanonicalType().getAsString(Policy);
    else if (const auto *TD = dyn_cast<TypedefNameDecl>(D))
      OS << '\t'
         << TD->getUnderlyingType().getCanonicalType().getAsString(Policy);
    else
      OS << '\t';

    // Shape, not contents: enough that a declaration turning into a definition,
    // or losing an offload attribute, is a difference.
    if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
      if (FD->isThisDeclarationADefinition())
        OS << " definition";
      if (FD->isConstexpr())
        OS << " constexpr";
      if (FD->isDeleted())
        OS << " deleted";
    }
    if (const auto *TagD = dyn_cast<TagDecl>(D)) {
      if (TagD->isCompleteDefinition())
        OS << " complete";
    }
    if (const auto *VarD = dyn_cast<VarDecl>(D)) {
      if (VarD->hasInit())
        OS << " init";
    }
    if (D->hasAttr<CUDADeviceAttr>())
      OS << " device";
    if (D->hasAttr<CUDAHostAttr>())
      OS << " host";
    if (D->hasAttr<CUDAGlobalAttr>())
      OS << " global";
    if (D->isImplicit())
      OS << " implicit";

    // Where the body came from. Shape alone cannot tell that a declaration
    // kept its own definition from one handed another target's body. Both
    // compilations parse the same source, so a body that is the same body
    // has the same range.
    if (const Stmt *Body = D->getBody())
      OS << " body@" << rangeKey(Ctx, Body->getSourceRange());
    else if (const auto *VarD = dyn_cast<VarDecl>(D))
      if (const Expr *Init = VarD->getInit())
        OS << " init@" << rangeKey(Ctx, Init->getSourceRange());

    Lines.emplace_back(Line.str());
    return true;
  }
};

} // namespace

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
    // ASTContext::computeTargetInstantiationKey.
    if (Ctx.getEffectiveTargetInstantiationKey(D))
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
  Ctx.setTargetMergeSurvivor(B, A);
  if (auto *BTD = dyn_cast<TemplateDecl>(B))
    if (auto *ATD = dyn_cast<TemplateDecl>(A))
      Ctx.setTargetMergeSurvivor(BTD->getTemplatedDecl(),
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

static llvm::hash_code bodyShape(const SourceManager &SM, const Stmt *S);

/// What a template parameter's default argument is, as written: two copies of
/// it parsed from different tokens, e.g. a macro defined differently per
/// target, differ.
static llvm::hash_code defaultArgumentShape(const SourceManager &SM,
                                            const NamedDecl *Param) {
  if (const auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(Param))
    return NTTP->hasDefaultArgument()
               ? bodyShape(SM, NTTP->getDefaultArgument().getSourceExpression())
               : llvm::hash_code(0);
  if (const auto *TTP = dyn_cast<TemplateTypeParmDecl>(Param))
    return TTP->hasDefaultArgument()
               ? llvm::hash_value(TTP->getDefaultArgument()
                                      .getArgument()
                                      .getAsType()
                                      .getCanonicalType()
                                      .getAsString())
               : llvm::hash_code(0);
  return llvm::hash_code(0);
}

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
      for (unsigned V = 1, VE = 1 + Ctx.getNumAuxTargets(); V <= VE; ++V)
        if (Targets.contains(V))
          Ctx.setTargetDefaultArgument(TA, PA->getParam(I), V,
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

static llvm::hash_code stmtShape(const SourceManager &SM, const Stmt *S);

namespace {
class BodyShapeHasher : public RecursiveASTVisitor<BodyShapeHasher> {
public:
  explicit BodyShapeHasher(const SourceManager &SM) : SM(SM) {}
  bool VisitStmt(Stmt *S) {
    Hash = llvm::hash_combine(Hash, stmtShape(SM, S));
    return true;
  }
  llvm::hash_code Hash = llvm::hash_code(0);

private:
  const SourceManager &SM;
};
} // namespace

/// A hash of every statement position inside \p S.
///
/// Comparing only a body's begin and end is not enough. The divergence is
/// usually *inside*: `amd_hip_fp8.h` picks `__clz` or `__builtin_clz` in the
/// middle of a function, so both arms' bodies open and close on the same lines
/// and differ only in the statements between them. That includes statements
/// only reachable through a name or type written in the body: in
/// `block<BLOCK>::v`, BLOCK's value is in the qualifier, not among the
/// DeclRefExpr's children.
static llvm::hash_code bodyShape(const SourceManager &SM, const Stmt *S) {
  if (!S)
    return llvm::hash_code(0);
  BodyShapeHasher Hasher(SM);
  Hasher.TraverseStmt(const_cast<Stmt *>(S));
  return Hasher.Hash;
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
      if (const Decl *Survivor = Ctx.getTargetMergeSurvivor(D))
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
          CTSD && (CTSD->getSpecializedTemplate()->getTargetVariant() ||
                   (!Ctx.getSpecializationKeyVariant(CTSD) &&
                    !Ctx.getEffectiveTargetInstantiationKey(CTSD)))) {
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

/// -multi-target-audit-joins: \p B, the copy for \p BTargets, is being merged
/// into \p A, which then stands for every target if \p Promote. Reports
/// whether the inputs parsing A consumed tell A's targets apart from B's, and
/// whether the copies name the same entities and mean the same.
/// The targets the specializations keyed by target that \p T names tell
/// apart.
static ASTContext::TargetPartition keyedTypeInputs(const ASTContext &Ctx,
                                                   QualType T) {
  if (T.isNull())
    return 0;
  T = T.getCanonicalType();
  if (const auto *PT = T->getAs<PointerType>())
    return keyedTypeInputs(Ctx, PT->getPointeeType());
  if (const auto *RT = T->getAs<ReferenceType>())
    return keyedTypeInputs(Ctx, RT->getPointeeType());
  if (const ArrayType *AT = T->getAsArrayTypeUnsafe())
    return keyedTypeInputs(Ctx, AT->getElementType());
  if (const auto *FPT = T->getAs<FunctionProtoType>()) {
    ASTContext::TargetPartition P = keyedTypeInputs(Ctx, FPT->getReturnType());
    for (QualType Param : FPT->param_types())
      P = Ctx.refineTargetPartition(P, keyedTypeInputs(Ctx, Param));
    return P;
  }
  const auto *CTSD =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(T->getAsCXXRecordDecl());
  if (!CTSD)
    return 0;
  ASTContext::TargetPartition P = 0;
  if (Ctx.getSpecializationKeyVariant(CTSD))
    P = Ctx.reconcileTargetPartition(0, ASTContext::TDK_Arch);
  else if (Ctx.getEffectiveTargetInstantiationKey(CTSD))
    P = Ctx.reconcileTargetPartition(
        0, Ctx.getTargetDependence(CTSD->getSpecializedTemplate()));
  for (const TemplateArgument &Arg : CTSD->getTemplateArgs().asArray())
    if (Arg.getKind() == TemplateArgument::Type)
      P = Ctx.refineTargetPartition(P, keyedTypeInputs(Ctx, Arg.getAsType()));
  return P;
}

static void auditJoin(const ASTContext &Ctx, StringRef Where, const Decl *A,
                      const Decl *B, TargetSet BTargets, bool Promote) {
  if (Ctx.JoinAuditPath.empty())
    return;
  SmallVector<ASTContext::ParseInput, 4> Inputs(Ctx.getParseInputList(A));
  // A specialization's signature is formed where it is deduced, before it
  // exists to own what that consumed.
  if (const auto *FD = dyn_cast<FunctionDecl>(A);
      FD && FD->isTemplateInstantiation())
    if (ASTContext::TargetPartition Keyed = keyedTypeInputs(Ctx, FD->getType()))
      Inputs.push_back({'k', false, Keyed, "signature"});
  TargetSet ATargets = A->getTargetSet();
  unsigned Last = 1 + Ctx.getNumAuxTargets();
  auto Separates = [&](ASTContext::TargetPartition P) {
    for (unsigned X = 1; X <= Last; ++X)
      for (unsigned Y = 1; Y <= Last; ++Y)
        if (ATargets.contains(X) && BTargets.contains(Y) &&
            Ctx.separatesTargets(P, X, Y))
          return true;
    return false;
  };
  std::string Why;
  for (const ASTContext::ParseInput &In : Inputs)
    if (Separates(In.Targets))
      Why += (Why.empty() ? "" : ";") + std::string(1, In.Kind) +
             (In.FromMember ? "+" : "") + ":" + In.What;
  bool AuditSame = Why.empty();
  // The audit stands in for parsing a group of targets' copies separately
  // only where their tokens are the same; other copies are parsed anyway.
  unsigned AClass = Ctx.getTokenClass(A), BClass = Ctx.getTokenClass(B);
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
  llvm::raw_fd_ostream OS(Ctx.JoinAuditPath, EC,
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

static llvm::hash_code stmtShape(const SourceManager &SM, const Stmt *S) {
  // Positions are compared as a device recording's tokens carry them (see
  // translateLoc): a macro argument where it is written, any other token from
  // a macro expansion where the macro was expanded, the same in every arm, so
  // a macro defined differently per target (e.g. a wavefront size) tells the
  // arms apart only by the value it expanded to.
  PresumedLoc P = SM.getPresumedLoc(SM.getFileLoc(S->getBeginLoc()));
  llvm::hash_code H =
      llvm::hash_combine(S->getStmtClass(), P.isValid() ? P.getLine() : 0,
                         P.isValid() ? P.getColumn() : 0);
  if (const auto *IL = dyn_cast<IntegerLiteral>(S))
    H = llvm::hash_combine(H, IL->getValue());
  else if (const auto *FL = dyn_cast<FloatingLiteral>(S))
    H = llvm::hash_combine(H, FL->getValue());
  else if (const auto *CL = dyn_cast<CharacterLiteral>(S))
    H = llvm::hash_combine(H, CL->getValue());
  else if (const auto *SL = dyn_cast<StringLiteral>(S))
    H = llvm::hash_combine(H, SL->getBytes());
  // E.g. an AMDGPU predicate, which each target answers for itself.
  else if (const auto *BL = dyn_cast<CXXBoolLiteralExpr>(S))
    H = llvm::hash_combine(H, BL->getValue());
  // A dependent call is resolved when instantiated, among the candidates its
  // name found here, which can differ per target. Each target's copy of a
  // candidate is declared at the same place.
  else if (const auto *OE = dyn_cast<OverloadExpr>(S)) {
    SmallVector<std::pair<unsigned, unsigned>, 4> Candidates;
    for (const NamedDecl *D : OE->decls()) {
      PresumedLoc DP = SM.getPresumedLoc(
          SM.getFileLoc(D->getUnderlyingDecl()->getLocation()));
      Candidates.emplace_back(DP.isValid() ? DP.getLine() : 0,
                              DP.isValid() ? DP.getColumn() : 0);
    }
    llvm::sort(Candidates);
    for (auto [Line, Column] : Candidates)
      H = llvm::hash_combine(H, Line, Column);
  }
  return H;
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

/// Print what of \p RD's layout its field and base types do not decide, as
/// written. That is its own and its fields' alignment and packing and its
/// bit-field widths, and the same for the classes it contains.
static void printLayoutAttributes(raw_ostream &OS, const RecordDecl *RD,
                                  const PrintingPolicy &Policy,
                                  unsigned Depth = 0) {
  auto PrintAttrs = [&](const Decl *D) {
    for (const Attr *A : D->attrs())
      if (isa<AlignedAttr, PackedAttr, MaxFieldAlignmentAttr, MSStructAttr>(A))
        A->printPretty(OS, Policy);
    OS << ';';
  };
  auto Contained = [&](QualType T) {
    if (const RecordDecl *Inner =
            T->getBaseElementTypeUnsafe()->getAsRecordDecl())
      if (const RecordDecl *Def = Inner->getDefinition(); Def && Depth < 8)
        printLayoutAttributes(OS, Def, Policy, Depth + 1);
  };
  PrintAttrs(RD);
  if (const auto *CXXRD = dyn_cast<CXXRecordDecl>(RD);
      CXXRD && CXXRD->hasDefinition())
    for (const CXXBaseSpecifier &Base : CXXRD->bases())
      Contained(Base.getType());
  for (const FieldDecl *FD : RD->fields()) {
    PrintAttrs(FD);
    if (const Expr *Width = FD->getBitWidth())
      Width->printPretty(OS, nullptr, Policy);
    Contained(FD->getType());
  }
}

static std::string layoutAttributes(const ASTContext &Ctx,
                                    const RecordDecl *RD) {
  std::string S;
  llvm::raw_string_ostream OS(S);
  printLayoutAttributes(OS, RD, Ctx.getPrintingPolicy());
  return S;
}

/// Whether two copies of an entity really are interchangeable.
///
/// StructuralEquivalenceContext is not enough on its own. For a FunctionDecl it
/// compares the identifier, operator-ness and the type -- **not the body**, and
/// a FIXME in it notes that attributes are not compared either. That is right
/// for its own caller, the ASTImporter, which is deciding whether two
/// declarations name the same entity across translation units. It is wrong
/// here: `unsafeAtomicAdd` and the other HIP intrinsics have the same signature
/// on both targets and different bodies, guarded by __HIP_DEVICE_COMPILE__, and
/// merging them hands the host the device's body.
///
/// A definition parsed from the same tokens in both arms is the same
/// definition, so comparing where the body came from settles it without a
/// statement-by-statement comparison.
/// The scope a dependent using-declaration names, as written.
static std::string dependentUsingScope(const ASTContext &Ctx, const Decl *D) {
  NestedNameSpecifier Qualifier = std::nullopt;
  if (const auto *Value = dyn_cast<UnresolvedUsingValueDecl>(D))
    Qualifier = Value->getQualifier();
  else if (const auto *Typename = dyn_cast<UnresolvedUsingTypenameDecl>(D))
    Qualifier = Typename->getQualifier();
  std::string Scope;
  llvm::raw_string_ostream OS(Scope);
  Qualifier.print(OS, Ctx.getPrintingPolicy());
  return Scope;
}

static bool isInterchangeable(ASTContext &Ctx,
                              StructuralEquivalenceContext &SEC, const Decl *A,
                              const Decl *B) {
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
      D = TND->getUnderlyingType()->getBaseElementTypeUnsafe()->getAsRecordDecl();
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
    bool PromotionBlocked = !A || Ctx.areTargetCopiesKeptApart(A);
    TargetSet Covered;
    for (const auto &VariantEntry : ByVariant)
      Covered = Covered | VariantEntry.second->getTargetSet();
    unsigned NumCovered = 0;
    for (unsigned V = 1; V <= 1 + Ctx.getNumAuxTargets(); ++V)
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
          Ctx.areTargetCopiesKeptApart(Survivor))
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
  for (unsigned V = 1, E = 1 + Ctx.getNumAuxTargets(); V <= E; ++V)
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
        if (!Promote)
          llvm::erase_if(Group, [&](size_t J) {
            if (nameSameEntities(A, Cells[J]))
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

void clang::printDeclFingerprints(raw_ostream &OS, ASTContext &Ctx,
                                  unsigned Variant) {
  std::vector<std::string> Lines;
  FingerprintVisitor(Ctx, Variant, Lines)
      .TraverseDecl(Ctx.getTranslationUnitDecl());

  // Sorted, because a combined parse visits a divergent region once per target
  // and so produces declarations in a different order than a single-target
  // parse does. Duplicates are kept: two declarations of the same shape are not
  // the same as one.
  llvm::sort(Lines);
  for (const std::string &L : Lines)
    OS << L << '\n';
}

//===----------------------------------------------------------------------===//
// Target dependence
//===----------------------------------------------------------------------===//

static bool isWidenedCopy(const Decl *D) {
  return D->getTargetVariant() && !D->isRedundantTargetVariant() &&
         !D->isTargetVariantReparseOrigin();
}

/// A value-like declaration parsed once per target from tokens that differ
/// between targets: a widened copy (or the shared declaration such copies
/// stand beside). Re-parsed users of such declarations are copies too, but
/// they diverge only through what they name, which getTargetDependence
/// follows. What is evaluated at run time is not a source: each target's
/// CodeGen resolves it by name.
bool ASTContext::isTargetDivergentSource(const NamedDecl *ND) const {
  if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(ND)) {
    if (!FTD->getTemplatedDecl()->isConstexpr())
      return false;
  } else if (const auto *FD = dyn_cast<FunctionDecl>(ND)) {
    if (!FD->isConstexpr())
      return false;
  } else if (const auto *VD = dyn_cast<VarDecl>(ND)) {
    if (VD->isLocalVarDeclOrParm() || !VD->isUsableInConstantExpressions(*this))
      return false;
  } else if (!isa<EnumConstantDecl>(ND)) {
    return false;
  }
  if (isWidenedCopy(ND))
    return true;
  if (ND->getTargetVariant() || !ND->getDeclName())
    return false;
  auto [It, Inserted] = UntaggedTargetSourceCache.try_emplace(ND, false);
  if (!Inserted)
    return It->second;
  const DeclContext *DC = ND->getDeclContext()->getRedeclContext();
  for (const NamedDecl *Sibling : DC->lookup(ND->getDeclName()))
    if (Sibling != ND && Sibling->getKind() == ND->getKind() &&
        Sibling->getLocation() == ND->getLocation() && isWidenedCopy(Sibling))
      return UntaggedTargetSourceCache[ND] = true;
  return false;
}

/// Whether \p Copy, of the same name and in the same scope as \p ND, is another
/// target's copy of it. Copies from one #if arm share a location; copies from
/// different arms do not, and a function's then have to agree on their
/// parameter types and on the side they are compiled for, since functions
/// overload on both. A member function called on an object is the object's
/// class's, so only static ones are looked for in another copy of the class.
static bool isTargetCopyOf(const ASTContext &Ctx, const NamedDecl *ND,
                           const NamedDecl *Copy, bool SameScope) {
  if (Copy->getKind() != ND->getKind() || Copy->isRedundantTargetVariant())
    return false;
  if (SameScope && Copy->getLocation() == ND->getLocation())
    return true;
  if (!isWidenedCopy(ND) || !isWidenedCopy(Copy))
    return false;
  const FunctionDecl *FD = ND->getAsFunction();
  if (!FD)
    return true;
  if (const auto *MD = dyn_cast<CXXMethodDecl>(FD);
      MD && !SameScope && !MD->isStatic())
    return false;
  const FunctionDecl *CopyFD = Copy->getAsFunction();
  if (FD->getNumParams() != CopyFD->getNumParams() ||
      FD->hasAttr<CUDAHostAttr>() != CopyFD->hasAttr<CUDAHostAttr>() ||
      FD->hasAttr<CUDADeviceAttr>() != CopyFD->hasAttr<CUDADeviceAttr>() ||
      FD->hasAttr<CUDAGlobalAttr>() != CopyFD->hasAttr<CUDAGlobalAttr>())
    return false;
  for (unsigned I = 0, E = FD->getNumParams(); I != E; ++I)
    if (!Ctx.hasSameType(FD->getParamDecl(I)->getType(),
                         CopyFD->getParamDecl(I)->getType()))
      return false;
  return true;
}

void ASTContext::keepTargetCopiesApart(const Decl *D) {
  TargetCopiesKeptApart.insert(D);
  // A template's declaration is not in its DeclContext chain.
  if (const auto *TD = dyn_cast<TemplateDecl>(D))
    if (const Decl *Templated = TD->getTemplatedDecl())
      TargetCopiesKeptApart.insert(Templated);
}

void ASTContext::getTargetCopies(
    const NamedDecl *ND, SmallVectorImpl<const NamedDecl *> &Copies) const {
  const DeclContext *DC = ND->getDeclContext()->getRedeclContext();
  if (const auto *Owner = dyn_cast<RecordDecl>(DC);
      Owner && isWidenedCopy(Owner)) {
    SmallVector<const NamedDecl *, 4> Owners;
    getTargetCopies(Owner, Owners);
    for (const NamedDecl *OwnerCopy : Owners)
      for (const NamedDecl *Copy :
           cast<RecordDecl>(OwnerCopy)->lookup(ND->getDeclName()))
        if (Copy == ND ||
            isTargetCopyOf(*this, ND, Copy, /*SameScope=*/OwnerCopy == Owner))
          Copies.push_back(Copy);
    return;
  }
  for (const NamedDecl *Copy : DC->lookup(ND->getDeclName()))
    if (Copy == ND || isTargetCopyOf(*this, ND, Copy, /*SameScope=*/true))
      Copies.push_back(Copy);
}

/// What evaluating \p Def computes, as far as its source shows.
static llvm::hash_code definitionShape(const SourceManager &SM,
                                       const FunctionDecl *Def) {
  llvm::hash_code H = bodyShape(SM, Def->getBody());
  if (const auto *Ctor = dyn_cast<CXXConstructorDecl>(Def))
    for (const CXXCtorInitializer *Init : Ctor->inits())
      if (Init->isWritten())
        H = llvm::hash_combine(H, bodyShape(SM, Init->getInit()));
  return H;
}

bool clang::copiesDefineTheSame(ASTContext &Ctx, Decl *A, Decl *B) {
  auto Definition = [](Decl *D) -> const FunctionDecl * {
    if (auto *FTD = dyn_cast<FunctionTemplateDecl>(D))
      D = FTD->getTemplatedDecl();
    const auto *FD = dyn_cast<FunctionDecl>(D);
    if (!FD)
      return nullptr;
    return FD->doesThisDeclarationHaveABody() ? FD : FD->getDefinition();
  };
  const FunctionDecl *DA = Definition(A), *DB = Definition(B);
  const SourceManager &SM = Ctx.getSourceManager();
  if (DA && DB && definitionShape(SM, DA) != definitionShape(SM, DB))
    return false;
  return copiesMeanTheSame(Ctx, A, B);
}

/// The definition target \p Variant evaluates: its own copy's, else the
/// shared copy's, else, for a device, the canonical device's.
static const FunctionDecl *
definitionForVariant(const ASTContext &Ctx,
                     ArrayRef<const FunctionDecl *> ByVariant,
                     unsigned Variant) {
  if (Variant < ByVariant.size() && ByVariant[Variant])
    return ByVariant[Variant];
  if (ByVariant[0])
    return ByVariant[0];
  unsigned Canonical = Ctx.getCanonicalDeviceVariant();
  return Ctx.isDeviceVariant(Variant) && Canonical < ByVariant.size()
             ? ByVariant[Canonical]
             : nullptr;
}

const ASTContext::TargetMemberDefinitions *
ASTContext::getTargetMemberDefinitions(const FunctionDecl *FD) const {
  if (!FD->isConstexpr() ||
      FD->getTemplatedKind() != FunctionDecl::TK_NonTemplate)
    return nullptr;
  // A class template's specializations are keyed by target instead.
  const auto *RD = dyn_cast<CXXRecordDecl>(FD->getDeclContext());
  if (!RD || isa<ClassTemplateSpecializationDecl>(RD) ||
      RD->getDescribedClassTemplate() || RD->isDependentContext())
    return nullptr;
  FD = FD->getCanonicalDecl();
  auto [It, Inserted] = TargetMemberDefinitionsCache.try_emplace(FD);
  TargetMemberDefinitions &Result = It->second;
  if (!Inserted && !Result.ByVariant.empty())
    return &Result;

  unsigned NumVariants = 2 + getNumAuxTargets();
  SmallVector<const FunctionDecl *, 4> ByVariant(NumVariants, nullptr);
  SmallVector<const NamedDecl *, 4> Classes;
  getTargetCopies(RD, Classes);
  // Evaluating one copy's member against another copy's object relies on the
  // two having the same fields and bases.
  auto SameBases = [&](const CXXRecordDecl *A, const CXXRecordDecl *B) {
    return A->getNumBases() == B->getNumBases() &&
           llvm::all_of(llvm::zip(A->bases(), B->bases()), [&](auto Pair) {
             return hasSameType(std::get<0>(Pair).getType(),
                                std::get<1>(Pair).getType());
           });
  };
  if (!hasTargetDivergentLayout(RD)) {
    for (const NamedDecl *Class : Classes) {
      unsigned V = Class->getTargetVariant();
      const auto *ClassRD = dyn_cast<CXXRecordDecl>(Class);
      if (!ClassRD || Class->isRedundantTargetVariant() || V >= NumVariants ||
          !ClassRD->isCompleteDefinition() || !SameBases(RD, ClassRD))
        continue;
      TargetSet ClassTargets = Class->getTargetSet();
      for (const Decl *Member : ClassRD->decls()) {
        const auto *MemberFD = dyn_cast<FunctionDecl>(Member);
        const FunctionDecl *Def = nullptr;
        if (MemberFD && MemberFD->getKind() == FD->getKind() &&
            MemberFD->getLocation() == FD->getLocation() &&
            MemberFD->isDefined(Def)) {
          if (!V)
            ByVariant[0] = Def;
          for (unsigned T = 1; T < NumVariants; ++T)
            if (V && ClassTargets.contains(T))
              ByVariant[T] = Def;
          break;
        }
      }
    }
  }

  unsigned LastDevice = 1 + getNumAuxTargets();
  bool All = true;
  for (unsigned V = 1; V <= LastDevice; ++V)
    All &= definitionForVariant(*this, ByVariant, V) != nullptr;
  if (!All) {
    // A class copied per target may have copies still to be parsed.
    if (Classes.size() > 1 || RD->getTargetVariant()) {
      TargetMemberDefinitionsCache.erase(It);
      return nullptr;
    }
    Result.ByVariant.assign(NumVariants, nullptr);
    Result.Dependence = 0;
    return &Result;
  }

  const SourceManager &SM = getSourceManager();
  Result.Dependence = partitionTargetsByAnswer([&](unsigned V) {
    return definitionShape(SM, definitionForVariant(*this, ByVariant, V));
  });
  Result.ByVariant = std::move(ByVariant);
  return &Result;
}

static const NamedDecl *copyForVariant(ArrayRef<const NamedDecl *> Copies,
                                       unsigned Variant);

/// Whether \p Copy, one of a declaration's target copies, is the host's. A
/// shared copy is the host's where there is a host.
static bool isHostsCopy(const ASTContext &Ctx, const NamedDecl *Copy) {
  return Copy->getTargetVariant() ? Copy->isCopyFor(Ctx.getHostVariant())
                                  : Ctx.getHostVariant() != 0;
}

/// Whether \p Copy is a device's. A shared copy is where there is no host.
static bool isDevicesCopy(const ASTContext &Ctx, const NamedDecl *Copy) {
  if (!Copy->getTargetVariant())
    return !Ctx.getHostVariant();
  return Ctx.isCopyForDevice(Copy) || Copy->isRedundantTargetVariant();
}

ASTContext::TargetDependenceKind
ASTContext::getMemberTargetDependence(const FunctionDecl *FD) const {
  return getTargetDependenceKind(getMemberTargetPartition(FD));
}

ASTContext::TargetPartition
ASTContext::getMemberTargetPartition(const FunctionDecl *FD) const {
  if (LLVM_LIKELY(!clang::AllowTargetVariantDecls))
    return 0;
  const TargetMemberDefinitions *Defs = getTargetMemberDefinitions(FD);
  return Defs ? Defs->Dependence : 0;
}

const FunctionDecl *
ASTContext::getTargetDefinitionForEvaluation(const FunctionDecl *FD,
                                             const FunctionDecl *Definition,
                                             bool InConstantContext) const {
  if (LLVM_LIKELY(!clang::AllowTargetVariantDecls) || !Definition)
    return nullptr;
  if (const auto *CTSD =
          dyn_cast<ClassTemplateSpecializationDecl>(FD->getDeclContext())) {
    // Specialized per target from each target's copy of the template.
    const ClassTemplateDecl *CTD = CTSD->getSpecializedTemplate();
    if (!TargetSpecializationMemberHook || !CTD->getTargetVariant() ||
        CTD->isRedundantTargetVariant() ||
        !CTD->hasTargetKeyedSpecializations() || hasTargetDivergentLayout(CTD))
      return nullptr;
    if (InConstantContext && LLVM_UNLIKELY(TargetAuditHook)) {
      SmallVector<const NamedDecl *, 4> Copies;
      getTargetCopies(CTD, Copies);
      auditTargetInput('E', partitionTargetsBy([&](unsigned A, unsigned B) {
                         return copyForVariant(Copies, A) ==
                                copyForVariant(Copies, B);
                       }),
                       FD);
    }
    unsigned Have = getSpecializationKeyVariant(CTSD);
    unsigned Target = getInstantiationTarget();
    if (!Have || Have == Target)
      return nullptr;
    const FunctionDecl *Own =
        TargetSpecializationMemberHook(InstantiationTargetHookCtx, FD, Target);
    return Own && Own != Definition ? Own : nullptr;
  }
  // Where code analysed for every target is the primary's view, a call bound
  // to one target's copy of a function evaluates the copy of the target it is
  // evaluated for, or, for a device without one, the devices' copy.
  if (isMultiTargetCompile() && FD->getTargetVariant() &&
      !FD->isRedundantTargetVariant() && !FD->getDeclContext()->isRecord()) {
    unsigned Target = getInstantiationTarget();
    if (FD->isCopyFor(Target)) {
      // The code evaluating it means what the copy each target binds
      // computes, also where that is the copy it bound ('e', see
      // Sema::initTargetAudit).
      if (InConstantContext)
        if (TargetPartition Copies = getTargetPartition(FD))
          auditTargetInput('e', Copies, FD);
      return nullptr;
    }
    SmallVector<const NamedDecl *, 4> Copies;
    getTargetCopies(FD, Copies);
    const FunctionDecl *Own = nullptr, *StandIn = nullptr;
    for (const NamedDecl *Copy : Copies) {
      const FunctionDecl *CopyFD = Copy->getAsFunction();
      unsigned V = Copy->getTargetVariant();
      if (!CopyFD || V == Decl::TargetVariantRedundant)
        continue;
      if (Copy->isCopyFor(Target))
        Own = CopyFD;
      else if (isDeviceVariant(Target) &&
               (V == 0 || Copy->isCopyFor(getCanonicalDeviceVariant())))
        StandIn = CopyFD;
    }
    if (!Own)
      Own = StandIn;
    const FunctionDecl *OwnDefinition = nullptr;
    if (!Own || !Own->getBody(OwnDefinition))
      return nullptr;
    if (InConstantContext)
      auditTargetInput('E', reconcileTargetPartition(0, TDK_Arch), FD);
    return OwnDefinition != Definition ? OwnDefinition : nullptr;
  }
  const TargetMemberDefinitions *Defs = getTargetMemberDefinitions(FD);
  TargetDependenceKind Dependence =
      Defs ? getTargetDependenceKind(Defs->Dependence) : TDK_None;
  if (Dependence == TDK_None)
    return nullptr;
  if (InConstantContext)
    auditTargetInput('E',
                     refineTargetPartition(Defs->Dependence,
                                           reconcileTargetPartition(0, TDK_Side)),
                     FD);
  const FunctionDecl *Own =
      definitionForVariant(*this, Defs->ByVariant, getInstantiationTarget());
  return Own && Own != Definition ? Own : nullptr;
}

void ASTContext::setTargetDefaultArgument(const TemplateDecl *Template,
                                          const NamedDecl *Param,
                                          unsigned Variant, NamedDecl *Other) {
  TargetDefaultArguments[{Param, Variant}] = Other;
  TemplatesWithTargetDefaultArguments.insert(Template);
}

NamedDecl *ASTContext::getTargetDefaultArgument(const NamedDecl *Param,
                                                unsigned Variant) const {
  if (TargetDefaultArguments.empty())
    return nullptr;
  return TargetDefaultArguments.lookup({Param, Variant});
}

ASTContext::TargetDependenceKind
ASTContext::getDefaultArgumentTargetDependence(const TemplateDecl *TD) const {
  return getTargetDependenceKind(getDefaultArgumentTargetPartition(TD));
}

ASTContext::TargetPartition
ASTContext::getDefaultArgumentTargetPartition(const TemplateDecl *TD) const {
  if (!TemplatesWithTargetDefaultArguments.contains(TD))
    return 0;
  const SourceManager &SM = getSourceManager();
  auto Shape = [&](unsigned Variant) {
    llvm::hash_code H(0);
    for (const NamedDecl *Param : *TD->getTemplateParameters()) {
      const NamedDecl *Own = getTargetDefaultArgument(Param, Variant);
      H = llvm::hash_combine(H, defaultArgumentShape(SM, Own ? Own : Param));
    }
    return H;
  };
  return partitionTargetsByAnswer(Shape);
}

bool ASTContext::isArchDivergentFunction(const NamedDecl *D) const {
  if (!D->getTargetVariant() || D->isRedundantTargetVariant() ||
      !D->getAsFunction() || getNumDeviceTargets() < 2)
    return false;
  auto OnDevice = [](const FunctionDecl *FD) {
    return FD->hasAttr<CUDADeviceAttr>() || FD->hasAttr<CUDAGlobalAttr>();
  };
  // A template's copies each name their own template parameters.
  auto SameType = [&](QualType A, QualType B) {
    if (hasSameType(A, B))
      return true;
    return A->isDependentType() && B->isDependentType() &&
           A.getCanonicalType().getAsString() ==
               B.getCanonicalType().getAsString();
  };
  SmallVector<const NamedDecl *, 4> Copies;
  getTargetCopies(D, Copies);
  const FunctionDecl *First = nullptr;
  unsigned DevicesCovered = 0;
  bool AnyOnDevice = false;
  for (const NamedDecl *Copy : Copies) {
    if (!isCopyForDevice(Copy))
      continue;
    const FunctionDecl *FD = Copy->getAsFunction();
    for (unsigned V = 1, E = 1 + getNumAuxTargets(); V <= E; ++V)
      DevicesCovered += isDeviceVariant(V) && Copy->isCopyFor(V);
    AnyOnDevice |= OnDevice(FD);
    if (!First) {
      First = FD;
      continue;
    }
    if (OnDevice(First) != OnDevice(FD) ||
        !SameType(First->getType(), FD->getType()))
      return AnyOnDevice;
  }
  return AnyOnDevice && DevicesCovered &&
         DevicesCovered != getNumDeviceTargets();
}

/// Whether \p A and \p B, two targets' copies of a field or base type, lay
/// out alike: the same type, or the same shape where each target's copy of a
/// class names that target's copies of the classes it contains.
bool ASTContext::hasSameTargetLayout(QualType A, QualType B,
                                     unsigned Depth) const {
  if (hasSameType(A, B))
    return true;
  A = A.getCanonicalType();
  B = B.getCanonicalType();
  if (A.getLocalQualifiers() != B.getLocalQualifiers() || Depth > 8)
    return false;
  if (A->isPointerType() && B->isPointerType())
    return true;
  if (A->isReferenceType() && B->isReferenceType())
    return true;
  if (const auto *CA = dyn_cast<ConstantArrayType>(A.getTypePtr()))
    if (const auto *CB = dyn_cast<ConstantArrayType>(B.getTypePtr()))
      return CA->getSize() == CB->getSize() &&
             hasSameTargetLayout(CA->getElementType(), CB->getElementType(),
                                 Depth + 1);
  const RecordDecl *RA = A->getAsRecordDecl(), *RB = B->getAsRecordDecl();
  if (!RA || !RB || RA->getDeclName() != RB->getDeclName() ||
      !RA->isCompleteDefinition() || !RB->isCompleteDefinition())
    return false;
  SmallVector<QualType, 8> TA, TB;
  for (const RecordDecl *R : {RA, RB}) {
    SmallVector<QualType, 8> &T = R == RA ? TA : TB;
    if (const auto *CXXRD = dyn_cast<CXXRecordDecl>(R))
      for (const CXXBaseSpecifier &Base : CXXRD->bases())
        T.push_back(Base.getType());
    for (const FieldDecl *FD : R->fields())
      T.push_back(FD->getType());
  }
  return TA.size() == TB.size() &&
         llvm::all_of(llvm::zip(TA, TB),
                      [&](const auto &Pair) {
                        return hasSameTargetLayout(std::get<0>(Pair),
                                                   std::get<1>(Pair),
                                                   Depth + 1);
                      }) &&
         layoutAttributes(*this, RA) == layoutAttributes(*this, RB);
}

ASTContext::TargetPartition
ASTContext::internTargetPartition(ArrayRef<uint8_t> ClassOf) const {
  SmallVector<uint8_t, 8> Normal(ClassOf.size());
  SmallVector<int, 16> Renumbered;
  for (size_t I = 0; I != ClassOf.size(); ++I) {
    if (ClassOf[I] >= Renumbered.size())
      Renumbered.resize(ClassOf[I] + 1, -1);
    int &To = Renumbered[ClassOf[I]];
    if (To < 0)
      To = llvm::count_if(Renumbered, [](int C) { return C >= 0; });
    Normal[I] = To;
  }
  if (TargetPartitions.empty()) {
    SmallVector<uint8_t, 8> Alike(ClassOf.size(), 0);
    TargetPartitionIndex[StringRef(reinterpret_cast<const char *>(Alike.data()),
                                   Alike.size())] = 0;
    TargetPartitions.push_back(std::move(Alike));
  }
  auto [It, Inserted] = TargetPartitionIndex.try_emplace(
      StringRef(reinterpret_cast<const char *>(Normal.data()), Normal.size()),
      TargetPartitions.size());
  if (Inserted)
    TargetPartitions.push_back(std::move(Normal));
  return It->second;
}

ASTContext::TargetPartition ASTContext::partitionTargetsByAnswer(
    llvm::function_ref<llvm::hash_code(unsigned Variant)> Answer) const {
  SmallVector<llvm::hash_code, 8> Answers;
  for (unsigned V = 1, Last = 1 + getNumAuxTargets(); V <= Last; ++V)
    Answers.push_back(Answer(V));
  return partitionTargetsBy([&](unsigned A, unsigned B) {
    return Answers[A - 1] == Answers[B - 1];
  });
}

ASTContext::TargetPartition ASTContext::partitionTargetsBy(
    llvm::function_ref<bool(unsigned A, unsigned B)> Same) const {
  unsigned Last = 1 + getNumAuxTargets();
  SmallVector<uint8_t, 8> ClassOf(Last);
  SmallVector<unsigned, 8> Representatives;
  for (unsigned V = 1; V <= Last; ++V) {
    auto It = llvm::find_if(Representatives,
                            [&](unsigned R) { return Same(R, V); });
    ClassOf[V - 1] = It - Representatives.begin();
    if (It == Representatives.end())
      Representatives.push_back(V);
  }
  return internTargetPartition(ClassOf);
}

ASTContext::TargetPartition
ASTContext::refineTargetPartition(TargetPartition A, TargetPartition B) const {
  if (A == B || !B)
    return A;
  if (!A)
    return B;
  ArrayRef<uint8_t> CA = TargetPartitions[A], CB = TargetPartitions[B];
  return partitionTargetsBy([&](unsigned X, unsigned Y) {
    return CA[X - 1] == CA[Y - 1] && CB[X - 1] == CB[Y - 1];
  });
}

ASTContext::TargetDependenceKind
ASTContext::getTargetDependenceKind(TargetPartition P) const {
  if (!P)
    return TDK_None;
  ArrayRef<uint8_t> ClassOf = TargetPartitions[P];
  if (ClassOf.size() < 2)
    return TDK_None;
  // Without a host, every target is a device.
  unsigned Host = getHostVariant();
  SmallVector<uint8_t, 8> Devices;
  for (unsigned V = 1; V <= ClassOf.size(); ++V)
    if (V != Host)
      Devices.push_back(ClassOf[V - 1]);
  if (llvm::any_of(Devices, [&](uint8_t C) { return C != Devices.front(); }))
    return TDK_Arch;
  return !Host || ClassOf[Host - 1] == Devices.front() ? TDK_None : TDK_Side;
}

ASTContext::TargetPartition
ASTContext::reconcileTargetPartition(TargetPartition P,
                                     TargetDependenceKind Kind) const {
  if (getTargetDependenceKind(P) == Kind)
    return P;
  switch (Kind) {
  case TDK_None:
    return 0;
  case TDK_Side:
    if (!getHostVariant())
      return 0;
    return partitionTargetsBy([&](unsigned A, unsigned B) {
      return (A == getHostVariant()) == (B == getHostVariant());
    });
  case TDK_Arch:
    return partitionTargetsBy([](unsigned A, unsigned B) { return A == B; });
  }
  llvm_unreachable("unknown target dependence");
}

void ASTContext::noteParseInput(const Decl *D, char Kind,
                                TargetPartition Targets, StringRef What,
                                bool FromMember) {
  auto &Inputs = ParseInputs[D];
  for (ParseInput &Old : Inputs)
    if (Old.Kind == Kind && Old.FromMember == FromMember && Old.What == What) {
      Old.Targets = refineTargetPartition(Old.Targets, Targets);
      return;
    }
  Inputs.push_back({Kind, FromMember, Targets, What.str()});
}

SmallVector<ASTContext::ParseInput, 4>
ASTContext::getParseInputList(const Decl *D) const {
  // A template and its templated declaration are one unit.
  for (; D; D = dyn_cast_or_null<Decl>(D->getDeclContext())) {
    const Decl *Templated = nullptr;
    if (const auto *TD = dyn_cast<TemplateDecl>(D))
      Templated = TD->getTemplatedDecl();
    bool Found = false;
    SmallVector<ParseInput, 4> Result;
    for (const Decl *Unit : {D, static_cast<const Decl *>(
                                    D->getDescribedTemplate()),
                             Templated})
      if (auto It = Unit ? ParseInputs.find(Unit) : ParseInputs.end();
          It != ParseInputs.end()) {
        Found = true;
        llvm::append_range(Result, It->second);
      }
    if (Found)
      return Result;
  }
  return {};
}

unsigned ASTContext::getTokenClass(const Decl *D) const {
  while (D) {
    if (auto It = TokenClasses.find(D); It != TokenClasses.end())
      return It->second;
    if (const auto *FD = dyn_cast<FunctionDecl>(D))
      if (const FunctionDecl *Pattern = FD->getTemplateInstantiationPattern()) {
        D = Pattern;
        continue;
      }
    if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(D)) {
      auto From = CTSD->getSpecializedTemplateOrPartial();
      D = isa<ClassTemplateDecl *>(From)
              ? static_cast<const Decl *>(cast<ClassTemplateDecl *>(From))
              : cast<ClassTemplatePartialSpecializationDecl *>(From);
      continue;
    }
    if (const TemplateDecl *TD = D->getDescribedTemplate())
      if (auto It = TokenClasses.find(TD); It != TokenClasses.end())
        return It->second;
    D = dyn_cast_or_null<Decl>(D->getDeclContext());
  }
  return 0;
}

ASTContext::TargetPartition
ASTContext::getParseInputs(const Decl *D) const {
  TargetPartition Result = 0;
  for (const ParseInput &In : getParseInputList(D))
    Result = refineTargetPartition(Result, In.Targets);
  return Result;
}

Stmt *ASTContext::getTargetBodyForEvaluation(const FunctionDecl *Definition,
                                             bool InConstantContext) const {
  if (LLVM_LIKELY(TargetBodies.empty()))
    return nullptr;
  ArrayRef<std::pair<TargetSet, Stmt *>> Bodies = getTargetBodies(Definition);
  if (Bodies.empty())
    return nullptr;
  if (InConstantContext)
    auditTargetInput('E', partitionTargetsBy([&](unsigned A, unsigned B) {
                       return getBodyForTarget(Definition, A) ==
                              getBodyForTarget(Definition, B);
                     }),
                     Definition);
  Stmt *Body = getBodyForTarget(Definition, getInstantiationTarget());
  return Body != Definition->getBody() ? Body : nullptr;
}

TargetSet ASTContext::getTargetClass(TargetPartition P,
                                     unsigned Variant) const {
  TargetSet Class;
  for (unsigned V = 1, E = 1 + getNumAuxTargets(); V <= E; ++V)
    if (!separatesTargets(P, Variant, V))
      Class.insert(V);
  return Class;
}

bool ASTContext::separatesTargets(TargetPartition P, unsigned A,
                                  unsigned B) const {
  return P && TargetPartitions[P][A - 1] != TargetPartitions[P][B - 1];
}

bool ASTContext::isFinestTargetPartition(TargetPartition P) const {
  if (!P)
    return getNumAuxTargets() == 0;
  ArrayRef<uint8_t> ClassOf = TargetPartitions[P];
  return ClassOf.back() + 1u == ClassOf.size();
}

bool ASTContext::hasTargetDivergentLayout(const NamedDecl *D) const {
  return getLayoutTargetDependence(D) != TDK_None;
}

ASTContext::TargetDependenceKind
ASTContext::getLayoutTargetDependence(const NamedDecl *D) const {
  return getTargetDependenceKind(getLayoutTargetPartition(D));
}

/// The copy of an entity in \p Copies that \p Variant sees: its own, else a
/// shared one.
static const NamedDecl *copyForVariant(ArrayRef<const NamedDecl *> Copies,
                                       unsigned Variant) {
  const NamedDecl *Shared = nullptr;
  for (const NamedDecl *Copy : Copies) {
    if (Copy->isCopyFor(Variant))
      return Copy;
    if (!Copy->getTargetVariant())
      Shared = Copy;
  }
  return Shared;
}

ASTContext::TargetPartition
ASTContext::getLayoutTargetPartition(const NamedDecl *D) const {
  auto Pattern = [](const NamedDecl *ND) -> const RecordDecl * {
    if (const auto *CTD = dyn_cast<ClassTemplateDecl>(ND))
      ND = CTD->getTemplatedDecl();
    const auto *RD = dyn_cast<RecordDecl>(ND);
    return RD && RD->isCompleteDefinition() ? RD : nullptr;
  };
  // Where code analysed for every target is the primary's view, a class
  // re-parsed per target is copied per target as much as a widened one.
  bool Copied = isWidenedCopy(D) ||
                (isMultiTargetCompile() && D->getTargetVariant() &&
                 !D->isRedundantTargetVariant());
  if (!Copied || !Pattern(D))
    return 0;
  auto Cached = TargetDivergentLayoutCache.find(D);
  if (Cached != TargetDivergentLayoutCache.end())
    return Cached->second;
  SmallVector<const NamedDecl *, 4> Copies;
  getTargetCopies(D, Copies);
  struct LayoutShape {
    SmallVector<QualType, 8> Types;
    std::string Attrs;
  };
  auto Shape = [&](const RecordDecl *R) {
    LayoutShape S;
    if (const auto *CXXRD = dyn_cast<CXXRecordDecl>(R))
      for (const CXXBaseSpecifier &Base : CXXRD->bases())
        S.Types.push_back(Base.getType());
    for (const FieldDecl *FD : R->fields())
      S.Types.push_back(FD->getType());
    S.Attrs = layoutAttributes(*this, R);
    return S;
  };
  auto SameShape = [&](const LayoutShape &A, const LayoutShape &B) {
    return A.Types.size() == B.Types.size() && A.Attrs == B.Attrs &&
           llvm::all_of(llvm::zip(A.Types, B.Types), [&](const auto &Pair) {
             return hasSameTargetLayout(std::get<0>(Pair), std::get<1>(Pair));
           });
  };
  std::optional<LayoutShape> Host, Device;
  bool Differs = false, DevicesDiffer = false;
  for (const NamedDecl *Copy : Copies) {
    const RecordDecl *CopyRD = Pattern(Copy);
    if (!CopyRD)
      continue;
    LayoutShape CopyShape = Shape(CopyRD);
    if (isHostsCopy(*this, Copy))
      Host = CopyShape;
    if (!isDevicesCopy(*this, Copy))
      continue;
    if (!Device)
      Device = CopyShape;
    else if (!SameShape(*Device, CopyShape))
      DevicesDiffer = true;
  }
  Differs = DevicesDiffer || (Host && Device && !SameShape(*Host, *Device));
  TargetDependenceKind Kind =
      DevicesDiffer ? TDK_Arch : Differs ? TDK_Side : TDK_None;
  TargetPartition Result = 0;
  if (Kind != TDK_None) {
    const RecordDecl *Unknown = nullptr;
    auto Of = [&](unsigned V) {
      const NamedDecl *Copy = copyForVariant(Copies, V);
      const RecordDecl *RD = Copy ? Pattern(Copy) : nullptr;
      return RD ? RD : Unknown;
    };
    Result = reconcileTargetPartition(
        partitionTargetsBy([&](unsigned A, unsigned B) {
          const RecordDecl *RA = Of(A), *RB = Of(B);
          return RA == RB || (RA && RB && SameShape(Shape(RA), Shape(RB)));
        }),
        Kind);
  }
  // Copies not parsed yet may still differ.
  unsigned Covered = 0;
  for (unsigned V = 1, E = 1 + getNumAuxTargets(); V <= E; ++V)
    Covered += llvm::any_of(Copies, [V](const NamedDecl *Copy) {
      return Copy->getTargetVariant() && Copy->isCopyFor(V);
    });
  if (Kind != TDK_None || Covered > getNumAuxTargets())
    TargetDivergentLayoutCache[D] = Result;
  return Result;
}

namespace {
/// Hashes what a copy's definition computes: statement kinds, literal values
/// and what names refer to. Copies parsed from the same tokens agree, even
/// though each has its own parameters, locals and copies of whatever else
/// was parsed per target; those are compared by name and location.
class TargetCopyFingerprint
    : public RecursiveASTVisitor<TargetCopyFingerprint> {
public:
  llvm::FoldingSetNodeID ID;
  bool shouldVisitTemplateInstantiations() const { return false; }
  bool VisitStmt(Stmt *S) {
    ID.AddInteger(S->getStmtClass());
    return true;
  }
  bool VisitIntegerLiteral(IntegerLiteral *E) {
    ID.AddInteger(E->getValue().getLimitedValue());
    return true;
  }
  bool VisitCharacterLiteral(CharacterLiteral *E) {
    ID.AddInteger(E->getValue());
    return true;
  }
  bool VisitFloatingLiteral(FloatingLiteral *E) {
    ID.AddInteger(E->getValue().bitcastToAPInt().getLimitedValue());
    return true;
  }
  bool VisitStringLiteral(StringLiteral *E) {
    ID.AddString(E->getBytes());
    return true;
  }
  bool VisitCXXBoolLiteralExpr(CXXBoolLiteralExpr *E) {
    ID.AddBoolean(E->getValue());
    return true;
  }
  bool VisitDeclRefExpr(DeclRefExpr *E) {
    addReference(E->getDecl());
    return true;
  }
  bool VisitMemberExpr(MemberExpr *E) {
    addReference(E->getMemberDecl());
    return true;
  }

private:
  void addReference(const ValueDecl *D) {
    if (D->getTargetVariant() || D->isTemplateParameter() ||
        isa<ParmVarDecl>(D) || D->getDeclContext()->isFunctionOrMethod()) {
      ID.AddString(D->getName());
      ID.AddInteger(D->getLocation().getRawEncoding());
      return;
    }
    ID.AddPointer(D);
  }
};
} // namespace

/// Which targets' copies of \p ND compute the same thing.
static ASTContext::TargetPartition
sourceTargetPartition(const ASTContext &Ctx, const NamedDecl *ND) {
  auto Fingerprint = [](const NamedDecl *Copy) {
    TargetCopyFingerprint FP;
    if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(Copy))
      Copy = FTD->getTemplatedDecl();
    if (const auto *FD = dyn_cast<FunctionDecl>(Copy))
      FP.TraverseStmt(FD->getBody());
    else if (const auto *VD = dyn_cast<VarDecl>(Copy))
      FP.TraverseStmt(const_cast<Expr *>(VD->getInit()));
    else if (const auto *ECD = dyn_cast<EnumConstantDecl>(Copy))
      FP.ID.AddInteger(ECD->getInitVal().getLimitedValue());
    return FP.ID;
  };
  std::optional<llvm::FoldingSetNodeID> Device, Host;
  bool DevicesDiffer = false;
  SmallVector<const NamedDecl *, 4> Copies;
  Ctx.getTargetCopies(ND, Copies);
  for (const NamedDecl *Copy : Copies) {
    llvm::FoldingSetNodeID ID = Fingerprint(Copy);
    if (isHostsCopy(Ctx, Copy))
      Host = ID;
    if (!isDevicesCopy(Ctx, Copy))
      continue;
    if (!Device)
      Device = ID;
    else if (*Device != ID)
      DevicesDiffer = true;
  }
  ASTContext::TargetDependenceKind Kind =
      DevicesDiffer                       ? ASTContext::TDK_Arch
      : Host && Device && *Host == *Device ? ASTContext::TDK_None
                                          : ASTContext::TDK_Side;
  if (Kind == ASTContext::TDK_None)
    return 0;
  return Ctx.reconcileTargetPartition(
      Ctx.partitionTargetsByAnswer([&](unsigned V) -> llvm::hash_code {
        const NamedDecl *Copy = copyForVariant(Copies, V);
        return Copy ? llvm::hash_value(Fingerprint(Copy).computeStableHash())
                    : llvm::hash_code(0);
      }),
      Kind);
}

const VarDecl *
ASTContext::getTargetVariableForEvaluation(const VarDecl *VD,
                                           bool InConstantContext) const {
  if (LLVM_LIKELY(!clang::AllowTargetVariantDecls) ||
      !VD->isStaticDataMember() || !MultiTargetCompile)
    return nullptr;
  // Code naming a class copied per target names the primary's copy. A static
  // member it evaluates is the member of the copy for the target the code is
  // compiled for, also where a target's code generation evaluates it.
  const auto *RD = dyn_cast<CXXRecordDecl>(VD->getDeclContext());
  if (!RD || !isWidenedCopy(RD))
    return nullptr;
  SmallVector<const NamedDecl *, 4> Copies;
  getTargetCopies(VD, Copies);
  if (Copies.size() < 2)
    return nullptr;
  if (InConstantContext)
    auditTargetInput('E', sourceTargetPartition(*this, VD), VD);
  unsigned Target = getInstantiationTarget();
  if (RD->isCopyFor(Target))
    return nullptr;
  for (const NamedDecl *Copy : Copies)
    if (const auto *CopyVD = dyn_cast<VarDecl>(Copy);
        CopyVD && CopyVD != VD &&
        cast<Decl>(CopyVD->getDeclContext())->isCopyFor(Target))
      return CopyVD;
  return nullptr;
}

namespace {
/// Collects the strongest target dependence among what a definition names.
class TargetDependenceCollector
    : public RecursiveASTVisitor<TargetDependenceCollector> {
  const ASTContext &Ctx;

public:
  ASTContext::TargetPartition Result = 0;
  /// Whether a member function's body is yet to be parsed.
  bool PendingBody = false;
  explicit TargetDependenceCollector(const ASTContext &Ctx) : Ctx(Ctx) {}
  bool shouldVisitTemplateInstantiations() const { return false; }

  bool VisitFunctionDecl(FunctionDecl *FD) {
    PendingBody |= FD->willHaveBody();
    return true;
  }

  /// Returns false, ending the walk, once every target is told apart.
  bool consider(const Decl *D) {
    if (!D)
      return true;
    return add(referenceDependence(D));
  }
  bool add(ASTContext::TargetPartition P) {
    Result = Ctx.refineTargetPartition(Result, P);
    return !Ctx.isFinestTargetPartition(Result);
  }
  bool done() const { return Ctx.isFinestTargetPartition(Result); }
  bool VisitDeclRefExpr(DeclRefExpr *E) { return consider(E->getDecl()); }
  bool VisitMemberExpr(MemberExpr *E) { return consider(E->getMemberDecl()); }
  bool VisitCXXConstructExpr(CXXConstructExpr *E) {
    return add(Ctx.getMemberTargetPartition(E->getConstructor()));
  }
  bool VisitOverloadExpr(OverloadExpr *E) {
    for (const NamedDecl *D : E->decls())
      if (!consider(D->getUnderlyingDecl()))
        return false;
    return true;
  }
  bool VisitTagTypeLoc(TagTypeLoc TL) {
    return consider(TL.getDecl());
  }
  bool VisitTemplateSpecializationTypeLoc(TemplateSpecializationTypeLoc TL) {
    return consider(TL.getTypePtr()->getTemplateName().getAsTemplateDecl());
  }
  bool VisitDeducedTemplateSpecializationTypeLoc(
      DeducedTemplateSpecializationTypeLoc TL) {
    return consider(TL.getTypePtr()->getTemplateName().getAsTemplateDecl());
  }

private:
  /// What naming \p D contributes. Only what can be evaluated at compile
  /// time, or shapes a type, counts; and a function the device architectures
  /// have differently, which overload resolution can pick or reject.
  ASTContext::TargetPartition referenceDependence(const Decl *D) const {
    if (const auto *ND = dyn_cast<NamedDecl>(D);
        ND && Ctx.isTargetDivergentSource(ND))
      return Ctx.getTargetPartition(ND);
    if (const auto *ND = dyn_cast<NamedDecl>(D);
        ND && Ctx.isArchDivergentFunction(ND))
      return Ctx.reconcileTargetPartition(0, ASTContext::TDK_Arch);
    if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
      if (!FD->isConstexpr())
        return 0;
      if (const FunctionTemplateDecl *FTD = FD->getPrimaryTemplate())
        return Ctx.getTargetPartition(FTD);
      if (const auto *CTSD =
              dyn_cast<ClassTemplateSpecializationDecl>(FD->getDeclContext()))
        return Ctx.getTargetPartition(CTSD->getSpecializedTemplate());
      return Ctx.refineTargetPartition(Ctx.getTargetPartition(FD),
                                       Ctx.getMemberTargetPartition(FD));
    }
    if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(D))
      return FTD->getTemplatedDecl()->isConstexpr()
                 ? Ctx.getTargetPartition(FTD)
                 : 0;
    if (const auto *VD = dyn_cast<VarDecl>(D)) {
      if (const auto *CTSD =
              dyn_cast<ClassTemplateSpecializationDecl>(VD->getDeclContext()))
        return Ctx.getTargetPartition(CTSD->getSpecializedTemplate());
      if (VD->getDeclContext()->isFunctionOrMethod() || !VD->getInit() ||
          !VD->isUsableInConstantExpressions(const_cast<ASTContext &>(Ctx)))
        return 0;
      return Ctx.getTargetPartition(VD);
    }
    // Naming a class template whose layout differs between the host and the
    // devices gives each its own specialization already (see
    // ClassTemplateSpecializationDecl::Profile); one that differs between
    // device architectures needs code naming it instantiated per
    // architecture.
    if (const auto *CTD = dyn_cast<ClassTemplateDecl>(D)) {
      ASTContext::TargetPartition Layout = Ctx.getLayoutTargetPartition(CTD);
      if (Ctx.getTargetDependenceKind(Layout) == ASTContext::TDK_Arch)
        return Layout;
      // Each target instantiates its own copy of a template redeclared per
      // target, which matters where the copies declare different things.
      return Ctx.refineTargetPartition(Ctx.getTargetPartition(CTD),
                                       Ctx.getClassTemplateShapePartition(CTD));
    }
    if (isa<TypeAliasTemplateDecl, VarTemplateDecl>(D))
      return Ctx.getTargetPartition(D);
    if (const auto *RD = dyn_cast<RecordDecl>(D))
      return Ctx.getLayoutTargetPartition(RD);
    if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(D))
      return Ctx.getTargetPartition(CTSD->getSpecializedTemplate());
    return 0;
  }
};
} // namespace

/// Walks the definitions of \p RD's members given outside the class, which
/// walking the class itself does not reach. Returns whether a member is still
/// to be defined.
static bool traverseOutOfLineMembers(TargetDependenceCollector &Collector,
                                     const CXXRecordDecl *RD) {
  bool Pending = false;
  for (Decl *Member : RD->decls()) {
    if (Collector.done())
      break;
    FunctionDecl *FD = Member->getAsFunction();
    if (FD && !isa<CXXDeductionGuideDecl>(FD)) {
      if (FD->isThisDeclarationADefinition() || FD->isDefaulted() ||
          FD->isDeleted() || FD->isPureVirtual() || FD->willHaveBody())
        continue;
      const FunctionDecl *Def = nullptr;
      if (FD->isDefined(Def))
        Collector.TraverseDecl(const_cast<FunctionDecl *>(Def));
      else
        Pending = true;
    } else if (const auto *VD = dyn_cast<VarDecl>(Member)) {
      if (const VarDecl *Def = VD->getDefinition(); Def && Def != VD)
        Collector.TraverseStmt(const_cast<Expr *>(Def->getInit()));
    } else if (const auto *Nested = dyn_cast<CXXRecordDecl>(Member)) {
      if (Nested->isThisDeclarationADefinition() && !Nested->isImplicit())
        Pending |= traverseOutOfLineMembers(Collector, Nested);
    } else if (const auto *NestedCTD = dyn_cast<ClassTemplateDecl>(Member)) {
      if (const CXXRecordDecl *Def =
              NestedCTD->getTemplatedDecl()->getDefinition())
        Pending |= traverseOutOfLineMembers(Collector, Def);
    }
  }
  return Pending;
}

/// A hash of what \p RD declares: bases, members and their types, typedefs,
/// static initializers unless \p StaticInitializers is false, nested classes
/// and what using-declarations introduce, but not function bodies, nor the
/// members a class declares implicitly where they are used.
static llvm::hash_code recordShape(const ASTContext &Ctx,
                                   const CXXRecordDecl *RD,
                                   bool StaticInitializers = true) {
  if (!RD || !(RD = RD->getDefinition()))
    return llvm::hash_code(0);
  PrintingPolicy Policy = Ctx.getPrintingPolicy();
  auto TypeStr = [&](QualType T) { return T.getAsString(Policy); };
  llvm::hash_code H = llvm::hash_value(RD->getNumBases());
  for (const CXXBaseSpecifier &Base : RD->bases())
    H = llvm::hash_combine(H, TypeStr(Base.getType()));
  for (const Decl *Member : RD->decls()) {
    if (Member->isImplicit())
      continue;
    H = llvm::hash_combine(H, Member->getKind());
    if (const auto *ND = dyn_cast<NamedDecl>(Member))
      H = llvm::hash_combine(H, ND->getNameAsString());
    if (const auto *TND = dyn_cast<TypedefNameDecl>(Member)) {
      H = llvm::hash_combine(H, TypeStr(TND->getUnderlyingType()));
    } else if (const auto *VD = dyn_cast<ValueDecl>(Member)) {
      H = llvm::hash_combine(H, TypeStr(VD->getType()));
      const Expr *E = nullptr;
      if (const auto *Var = dyn_cast<VarDecl>(VD); Var && StaticInitializers)
        E = Var->getInit();
      else if (const auto *FD = dyn_cast<FieldDecl>(VD))
        E = FD->getInClassInitializer();
      if (E) {
        std::string Init;
        llvm::raw_string_ostream OS(Init);
        E->printPretty(OS, nullptr, Policy);
        H = llvm::hash_combine(H, Init);
      }
    } else if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(Member)) {
      H = llvm::hash_combine(H, TypeStr(FTD->getTemplatedDecl()->getType()));
    } else if (const auto *Nested = dyn_cast<CXXRecordDecl>(Member)) {
      if (!Nested->isInjectedClassName())
        H = llvm::hash_combine(H, recordShape(Ctx, Nested, StaticInitializers));
    } else if (const auto *Nested = dyn_cast<ClassTemplateDecl>(Member)) {
      H = llvm::hash_combine(
          H, recordShape(Ctx, Nested->getTemplatedDecl(), StaticInitializers));
    } else if (const auto *Using = dyn_cast<BaseUsingDecl>(Member)) {
      for (const UsingShadowDecl *Shadow : Using->shadows()) {
        const NamedDecl *Target = Shadow->getTargetDecl();
        H = llvm::hash_combine(H, Target->getQualifiedNameAsString());
        if (const auto *VD = dyn_cast<ValueDecl>(Target))
          H = llvm::hash_combine(H, TypeStr(VD->getType()));
      }
    } else if (isa<UnresolvedUsingValueDecl, UnresolvedUsingTypenameDecl>(
                   Member)) {
      H = llvm::hash_combine(H, dependentUsingScope(Ctx, Member));
    }
  }
  return H;
}

ASTContext::TargetPartition
ASTContext::getRecordShapePartition(const RecordDecl *RD) const {
  const auto *CXXRD = dyn_cast<CXXRecordDecl>(RD);
  if (!CXXRD || !isWidenedCopy(RD) || !RD->isCompleteDefinition())
    return 0;
  auto Cached = RecordShapeCache.find(RD);
  if (Cached != RecordShapeCache.end())
    return Cached->second;
  SmallVector<const NamedDecl *, 4> Copies;
  getTargetCopies(RD, Copies);
  if (Copies.size() < 2)
    return 0;
  // What each copy declares, as for a class template's copies, except static
  // members' values, which each target evaluates as its own.
  TargetPartition Result = partitionTargetsByAnswer([&](unsigned V) {
    return recordShape(
        *this, dyn_cast_or_null<CXXRecordDecl>(copyForVariant(Copies, V)),
        /*StaticInitializers=*/false);
  });
  // Copies not parsed yet may still differ.
  unsigned Covered = 0;
  for (unsigned V = 1, E = 1 + getNumAuxTargets(); V <= E; ++V)
    Covered += llvm::any_of(Copies, [V](const NamedDecl *Copy) {
      return Copy->getTargetVariant() && Copy->isCopyFor(V);
    });
  if (Covered > getNumAuxTargets())
    RecordShapeCache[RD] = Result;
  return Result;
}

ASTContext::TargetPartition
ASTContext::getClassTemplateShapePartition(const ClassTemplateDecl *CTD) const {
  const ASTContext &Ctx = *this;
  SmallVector<const NamedDecl *, 4> Copies;
  Ctx.getTargetCopies(CTD, Copies);
  if (Copies.size() < 2)
    return 0;
  auto Shape = [&](const NamedDecl *Copy) {
    const auto *C = dyn_cast_or_null<ClassTemplateDecl>(Copy);
    if (!C)
      return llvm::hash_code(0);
    llvm::hash_code H = recordShape(Ctx, C->getTemplatedDecl());
    SmallVector<ClassTemplatePartialSpecializationDecl *, 4> Partials;
    const_cast<ClassTemplateDecl *>(C)->getPartialSpecializations(Partials);
    for (const ClassTemplatePartialSpecializationDecl *P : Partials) {
      std::string Args;
      llvm::raw_string_ostream OS(Args);
      printTemplateArgumentList(OS, P->getTemplateArgs().asArray(),
                                Ctx.getPrintingPolicy());
      H = llvm::hash_combine(H, Args, recordShape(Ctx, P));
    }
    return H;
  };
  return Ctx.partitionTargetsByAnswer([&](unsigned V) {
    const NamedDecl *For = nullptr;
    for (const NamedDecl *Copy : Copies)
      if (Copy->isCopyFor(V) || (!For && !Copy->getTargetVariant()))
        For = Copy;
    return Shape(For);
  });
}

ASTContext::TargetPartition
ASTContext::getDefaultArgumentDependence(const TemplateDecl *TD) const {
  TargetDependenceCollector Collector(*this);
  if (const TemplateParameterList *TPL = TD->getTemplateParameters())
    for (const NamedDecl *Param : *TPL) {
      if (const auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(Param)) {
        if (NTTP->hasDefaultArgument())
          Collector.TraverseStmt(const_cast<Expr *>(
              NTTP->getDefaultArgument().getSourceExpression()));
      } else if (const auto *TTP = dyn_cast<TemplateTypeParmDecl>(Param)) {
        if (TTP->hasDefaultArgument())
          if (const TypeSourceInfo *TSI =
                  TTP->getDefaultArgument().getTypeSourceInfo())
            Collector.TraverseTypeLoc(TSI->getTypeLoc());
      }
    }
  return refineTargetPartition(Collector.Result,
                               getDefaultArgumentTargetPartition(TD));
}

ASTContext::TargetDependenceKind
ASTContext::getTargetDependence(const Decl *D) const {
  return getTargetDependenceKind(getTargetPartition(D));
}

ASTContext::TargetPartition
ASTContext::getTargetPartition(const Decl *D) const {
  if (!D)
    return 0;
  if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(D)) {
    if (const FunctionDecl *Def = FTD->getTemplatedDecl()->getDefinition())
      if (const FunctionTemplateDecl *DefTemplate =
              Def->getDescribedFunctionTemplate())
        D = DefTemplate;
  } else if (const auto *CTD = dyn_cast<ClassTemplateDecl>(D)) {
    if (const CXXRecordDecl *Def = CTD->getTemplatedDecl()->getDefinition())
      if (const ClassTemplateDecl *DefTemplate =
              Def->getDescribedClassTemplate())
        D = DefTemplate;
  }
  // Mutually dependent declarations (e.g. overloads calling each other)
  // depend on the target alike; each strongly connected component is
  // settled when its first declaration is (Tarjan's algorithm).
  auto Cached = TargetDependenceCache.find(D);
  if (Cached != TargetDependenceCache.end())
    return Cached->second;
  auto [OnStack, Inserted] =
      TargetDependenceIndex.try_emplace(D, TargetDependenceStack.size());
  if (!Inserted) {
    TargetDependenceLowLink =
        std::min(TargetDependenceLowLink, OnStack->second);
    return 0;
  }
  unsigned Index = TargetDependenceStack.size();
  TargetDependenceStack.push_back({D, /*Complete=*/true});
  unsigned OuterLowLink = TargetDependenceLowLink;
  TargetDependenceLowLink = Index;

  TargetDependenceCollector Collector(*this);
  bool Complete = true;
  if (const auto *ND = dyn_cast<NamedDecl>(D);
      ND && isTargetDivergentSource(ND))
    Collector.Result = sourceTargetPartition(*this, ND);
  if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(D)) {
    Complete = FTD->getTemplatedDecl()->hasBody();
    Collector.TraverseDecl(const_cast<FunctionTemplateDecl *>(FTD));
    // Re-parsed for each device architecture, it can name in each copy
    // functions the others do not have.
    if (FTD->isTargetVariantReparseOrigin() && !Collector.done()) {
      SmallVector<const NamedDecl *, 4> Copies;
      getTargetCopies(FTD, Copies);
      for (const NamedDecl *Copy : Copies)
        if (isCopyForDevice(Copy) || Copy->isRedundantTargetVariant())
          Collector.TraverseDecl(const_cast<NamedDecl *>(Copy));
    }
  } else if (const auto *CTD = dyn_cast<ClassTemplateDecl>(D)) {
    const CXXRecordDecl *Def = CTD->getTemplatedDecl()->getDefinition();
    Complete = Def;
    Collector.TraverseDecl(const_cast<ClassTemplateDecl *>(CTD));
    if (Def)
      Complete &= !traverseOutOfLineMembers(Collector, Def);
    SmallVector<ClassTemplatePartialSpecializationDecl *, 4> PS;
    const_cast<ClassTemplateDecl *>(CTD)->getPartialSpecializations(PS);
    for (ClassTemplatePartialSpecializationDecl *P : PS) {
      if (Collector.done())
        break;
      Collector.TraverseDecl(P);
      if (P->isThisDeclarationADefinition())
        Complete &= !traverseOutOfLineMembers(Collector, P);
    }
  } else if (isa<TypeAliasTemplateDecl, VarTemplateDecl>(D)) {
    Collector.TraverseDecl(const_cast<Decl *>(D));
  } else if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
    Complete = FD->hasBody();
    Collector.TraverseStmt(FD->getBody());
  } else if (const auto *VD = dyn_cast<VarDecl>(D)) {
    Collector.TraverseStmt(const_cast<Expr *>(VD->getInit()));
  }
  // Each target's copy names its own callees and values, which can differ
  // where the copies' tokens do not.
  if (const auto *ND = dyn_cast<NamedDecl>(D);
      ND && isa<FunctionDecl, VarDecl>(ND) && isTargetDivergentSource(ND)) {
    SmallVector<const NamedDecl *, 4> Copies;
    getTargetCopies(ND, Copies);
    for (const NamedDecl *Copy : Copies) {
      if (Copy == ND || Collector.done())
        continue;
      if (const auto *CopyFD = dyn_cast<FunctionDecl>(Copy))
        Collector.TraverseStmt(CopyFD->getBody());
      else if (const auto *CopyVD = dyn_cast<VarDecl>(Copy))
        Collector.TraverseStmt(const_cast<Expr *>(CopyVD->getInit()));
    }
  }
  if (const auto *TD = dyn_cast<TemplateDecl>(D))
    Collector.add(getDefaultArgumentTargetPartition(TD));
  TargetPartition Result = Collector.Result;
  TargetDependenceStack[Index].second = Complete && !Collector.PendingBody;
  unsigned LowLink = TargetDependenceLowLink;
  if (LowLink != Index) {
    // Settled with the rest of its component.
    TargetDependenceLowLink = std::min(OuterLowLink, LowLink);
    return Result;
  }
  // A definition seen later may still add to the component and to whatever
  // depends on it; see noteTargetDependenceDefinition. Most declarations
  // seen without one (e.g. std::declval) never get one.
  for (unsigned I = Index, E = TargetDependenceStack.size(); I != E; ++I) {
    auto [Member, MemberComplete] = TargetDependenceStack[I];
    TargetDependenceIndex.erase(Member);
    TargetDependenceCache[Member] = Result;
    if (!MemberComplete)
      TargetDependenceAwaitingDefinition.insert(Member);
  }
  TargetDependenceStack.truncate(Index);
  TargetDependenceLowLink = OuterLowLink;
  return Result;
}

/// Whether \p D is a copy standing for several targets.
static bool isCopyForSeveral(const Decl *D) {
  return D->getTargetVariant() && !D->isRedundantTargetVariant() &&
         !D->getTargetSet().isSingle();
}

unsigned ASTContext::getSpecializationTarget(const Decl *Template,
                                            unsigned Variant) const {
  // A target without the copy names it through the copy's own target.
  if (isCopyForSeveral(Template) && !Template->isCopyFor(Variant))
    return Template->getTargetSet().first();
  return Variant;
}

TargetSet ASTContext::getInstantiationKeyTargets(const Decl *Template,
                                                unsigned Key) const {
  bool Side = getTargetDependence(Template) == TDK_Side;
  TargetSet Targets;
  for (unsigned V = 1, E = 1 + getNumAuxTargets(); V <= E; ++V)
    if (Template->isCopyFor(V) &&
        (Side && isDeviceVariant(V) ? getCanonicalDeviceVariant() : V) == Key)
      Targets.insert(V);
  // Keyed by the target ambient where the copy is not that target's.
  return Targets == TargetSet() ? TargetSet::single(Key) : Targets;
}

TargetSet ASTContext::getMemberInstantiationTargets(const Decl *D,
                                                   unsigned Variant) const {
  if (const auto *Parent = dyn_cast<CXXRecordDecl>(D->getDeclContext());
      Parent && Parent->isCopyFor(Variant))
    return Parent->getTargetSet();
  return TargetSet::single(Variant);
}

std::optional<TargetSet>
ASTContext::getRecordedSpecializationTargets(const Decl *D) const {
  if (SpecializationTargets.empty())
    return std::nullopt;
  for (; D; D = dyn_cast_or_null<Decl>(D->getDeclContext()))
    if (auto It = SpecializationTargets.find(D);
        It != SpecializationTargets.end())
      return It->second;
  return std::nullopt;
}

/// The declaration whose dependence a definition of \p D completes.
static const Decl *getTargetDependenceKey(const Decl *D) {
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    if (const FunctionTemplateDecl *FTD = FD->getDescribedFunctionTemplate())
      return FTD;
  if (const auto *RD = dyn_cast<CXXRecordDecl>(D))
    if (const ClassTemplateDecl *CTD = RD->getDescribedClassTemplate())
      return CTD;
  return D;
}

void ASTContext::noteTargetDependenceDefinition(const Decl *D) {
  if (TargetDependenceAwaitingDefinition.empty())
    return;
  if (!isa<CXXRecordDecl>(D) && !isa<CXXRecordDecl>(D->getDeclContext())) {
    TargetDependenceRecheck.insert(getTargetDependenceKey(D));
    recheckTargetDependence();
    return;
  }
  SmallVector<const Decl *, 4> Classes;
  for (const DeclContext *DC = isa<CXXRecordDecl>(D) ? cast<DeclContext>(D)
                                                     : D->getDeclContext();
       isa<CXXRecordDecl>(DC); DC = DC->getParent())
    Classes.push_back(getTargetDependenceKey(cast<Decl>(DC)));
  // A class, and a member function whose body is parsed after its class,
  // complete the outermost class only once all of its inline member
  // functions have been parsed; see recheckTargetDependence.
  if (isa<CXXRecordDecl>(D) || isa<CXXRecordDecl>(D->getLexicalDeclContext())) {
    TargetDependenceRecheck.insert(Classes.begin(), Classes.end());
    if (isa<FunctionDecl>(D))
      TargetDependenceRecheck.insert(getTargetDependenceKey(D));
    return;
  }
  // A member function defined outside its classes adds its own dependence to
  // theirs, which need not be walked again; their other members may still be
  // to come.
  TargetDependenceRecheck.insert(getTargetDependenceKey(D));
  recheckTargetDependence();
  TargetDependenceCollector Collector(*this);
  Collector.TraverseDecl(const_cast<Decl *>(D));
  for (const Decl *Class : Classes)
    for (const Decl *Redecl : Class->redecls()) {
      if (!TargetDependenceAwaitingDefinition.count(Redecl))
        continue;
      auto Cached = TargetDependenceCache.find(Redecl);
      if (Cached != TargetDependenceCache.end() &&
          getTargetDependenceKind(Collector.Result) >
              getTargetDependenceKind(Cached->second)) {
        noteTargetDependenceEscalation();
        return;
      }
      break;
    }
}

/// The template arguments of \p Spec, an explicit specialization or a class
/// specialization with an explicitly specialized member.
static ArrayRef<TemplateArgument> explicitSpecializationArgs(const Decl *Spec) {
  if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(Spec))
    return CTSD->getTemplateArgs().asArray();
  if (const TemplateArgumentList *List =
          cast<FunctionDecl>(Spec)->getTemplateSpecializationArgs())
    return List->asArray();
  return {};
}

static llvm::FoldingSetNodeID profileArgs(const ASTContext &Ctx,
                                          ArrayRef<TemplateArgument> Args) {
  llvm::FoldingSetNodeID ID;
  for (const TemplateArgument &Arg : Args)
    Arg.Profile(ID, Ctx);
  return ID;
}

void ASTContext::resolveTargetExplicitSpecializations() {
  if (PendingTargetExplicitSpecializations.empty())
    return;
  TargetSet All;
  for (unsigned V = 1, E = 1 + getNumAuxTargets(); V <= E; ++V)
    All.insert(V);
  // Which targets have an explicit specialization for each template and
  // arguments. One every target has, in copies or shared, is each target's
  // own already.
  llvm::DenseMap<std::pair<const Decl *, unsigned>, TargetSet> Covered;
  auto KeyOf = [&](const Decl *Template, const Decl *Spec) {
    return std::make_pair(
        Template, profileArgs(*this, explicitSpecializationArgs(Spec))
                      .computeHash());
  };
  for (auto [Template, Spec, Tagged] : PendingTargetExplicitSpecializations)
    if (!Tagged->isRedundantTargetVariant()) {
      TargetSet &Targets = Covered[KeyOf(Template, Spec)];
      Targets = Targets | (Tagged->getTargetVariant() ? Tagged->getTargetSet()
                                                      : All);
    }
  for (auto [Template, Spec, Tagged] : PendingTargetExplicitSpecializations)
    if (Tagged->getTargetVariant() && !Tagged->isRedundantTargetVariant() &&
        !((Covered[KeyOf(Template, Spec)] & All) == All))
      TargetExplicitSpecializations[Template].push_back({Spec, Tagged});
  PendingTargetExplicitSpecializations.clear();
}

bool ASTContext::hasTargetExplicitSpecializations(const Decl *Template) const {
  if (TargetExplicitSpecializations.empty())
    return false;
  auto It = TargetExplicitSpecializations.find(Template->getCanonicalDecl());
  return It != TargetExplicitSpecializations.end() &&
         llvm::any_of(It->second, [](const auto &Entry) {
           return Entry.second->getTargetVariant() &&
                  !Entry.second->isRedundantTargetVariant();
         });
}

Decl *ASTContext::getTargetExplicitSpecialization(
    const Decl *Template, ArrayRef<TemplateArgument> Args,
    unsigned Variant) const {
  if (TargetExplicitSpecializations.empty())
    return nullptr;
  auto It = TargetExplicitSpecializations.find(Template->getCanonicalDecl());
  if (It == TargetExplicitSpecializations.end())
    return nullptr;
  llvm::FoldingSetNodeID Want = profileArgs(*this, Args);
  for (auto [Spec, Tagged] : It->second)
    if (Tagged->isCopyFor(Variant) &&
        profileArgs(*this, explicitSpecializationArgs(Spec)) == Want)
      return Spec;
  return nullptr;
}

void ASTContext::noteUnkeyedSpecialization(const Decl *Template) {
  if (isMultiTargetCompile() && !TargetKeysSuppressed)
    TemplatesWithUnkeyedSpecializations.insert(Template->getCanonicalDecl());
}

void ASTContext::noteTargetDependenceEscalation() {
  // Every result computed so far may have missed the definition, and a
  // template specialized while it seemed not to depend on the target has to
  // be keyed after the fact; see Sema::keySpecializationsMadeTooEarly.
  TargetDependenceCache.clear();
  TargetDependenceAwaitingDefinition.clear();
  for (const Decl *D : TemplatesWithUnkeyedSpecializations.takeVector()) {
    if (getTargetDependence(D) != TDK_None)
      TargetDependenceEscalated.insert(D);
    else
      TemplatesWithUnkeyedSpecializations.insert(D);
  }
}

void ASTContext::recheckTargetDependence() {
  SmallVector<const Decl *, 4> Defined(TargetDependenceRecheck.begin(),
                                       TargetDependenceRecheck.end());
  TargetDependenceRecheck.clear();
  // Dependence only grows with a definition, so what was computed without
  // one stays right unless the definition makes it stronger.
  for (const Decl *Def : Defined) {
    for (const Decl *Redecl : Def->redecls()) {
      if (!TargetDependenceAwaitingDefinition.erase(Redecl))
        continue;
      auto Stale = TargetDependenceCache.find(Redecl);
      TargetDependenceKind Before =
          Stale == TargetDependenceCache.end()
              ? TDK_None
              : getTargetDependenceKind(Stale->second);
      if (Stale != TargetDependenceCache.end())
        TargetDependenceCache.erase(Stale);
      if (getTargetDependence(Def) > Before) {
        noteTargetDependenceEscalation();
        return;
      }
      break;
    }
  }
}

bool ASTContext::isMultiTargetCompile() const {
  return LLVM_UNLIKELY(clang::AllowTargetVariantDecls) && LangOpts.CUDA &&
         MultiTargetCompile &&
         LangOpts.CUDAIsDevice == MultiTargetPrimaryIsDevice;
}

bool ASTContext::isCopyForDevice(const Decl *D) const {
  return D->getTargetVariant() && !D->isRedundantTargetVariant() &&
         !getDevices(D->getTargetSet()).empty();
}

bool ASTContext::hasTargetInstantiationKeys(const Decl *Template) const {
  return isMultiTargetCompile() &&
         getTargetDependence(Template) != TDK_None;
}

/// The key of a class specialization keyed by architecture that \p T names,
/// itself or through its template arguments or enclosing classes, or 0.
static unsigned getArchKeyNamed(const ASTContext &Ctx, QualType T,
                                unsigned Depth);

static unsigned getArchKeyNamed(const ASTContext &Ctx,
                                const TemplateArgument &Arg, unsigned Depth) {
  if (Arg.getKind() == TemplateArgument::Pack) {
    for (const TemplateArgument &Element : Arg.pack_elements())
      if (unsigned Key = getArchKeyNamed(Ctx, Element, Depth))
        return Key;
    return 0;
  }
  return Arg.getKind() == TemplateArgument::Type
             ? getArchKeyNamed(Ctx, Arg.getAsType(), Depth)
             : 0;
}

static unsigned getArchKeyNamed(const ASTContext &Ctx, QualType T,
                                unsigned Depth) {
  if (T.isNull() || T->isDependentType() || Depth > 8)
    return 0;
  T = T.getCanonicalType();
  if (const auto *PT = T->getAs<PointerType>())
    return getArchKeyNamed(Ctx, PT->getPointeeType(), Depth);
  if (const auto *RT = T->getAs<ReferenceType>())
    return getArchKeyNamed(Ctx, RT->getPointeeType(), Depth);
  if (const auto *AT = Ctx.getAsArrayType(T))
    return getArchKeyNamed(Ctx, AT->getElementType(), Depth);
  const auto *RD = T->getAsCXXRecordDecl();
  for (const DeclContext *DC = RD; DC; DC = DC->getParent()) {
    const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(DC);
    if (!CTSD)
      continue;
    if (unsigned Key = Ctx.getTargetInstantiationKey(CTSD->getCanonicalDecl());
        Key && Ctx.getTargetDependence(CTSD->getSpecializedTemplate()) ==
                   ASTContext::TDK_Arch)
      return Key;
    // A specialization keyed by the target it was made for, through a static
    // member's value (see ClassTemplateSpecializationDecl::Profile), is that
    // target's.
    if (unsigned Key = Ctx.getSpecializationKeyVariant(CTSD);
        Key && !Ctx.getTargetInstantiationKey(CTSD->getCanonicalDecl()) &&
        CTSD->getSpecializedTemplate()->hasTargetKeyedValuesOnly())
      return Key;
    for (const TemplateArgument &Arg : CTSD->getTemplateArgs().asArray())
      if (unsigned Key = getArchKeyNamed(Ctx, Arg, Depth + 1))
        return Key;
  }
  return 0;
}

/// Whether a device architecture sees other partial specializations of
/// \p CTD than the canonical device does.
static bool partialSpecializationsDifferBetweenDevices(
    const ASTContext &Ctx, const ClassTemplateDecl *CTD) {
  SmallVector<ClassTemplatePartialSpecializationDecl *, 4> Partials;
  const_cast<ClassTemplateDecl *>(CTD)->getPartialSpecializations(Partials);
  auto Shape = [&](unsigned V) {
    SmallVector<size_t, 4> Hashes;
    for (const ClassTemplatePartialSpecializationDecl *P : Partials) {
      if (P->isRedundantTargetVariant() ||
          (P->getTargetVariant() && !P->isCopyFor(V)))
        continue;
      std::string Args;
      llvm::raw_string_ostream OS(Args);
      printTemplateArgumentList(OS, P->getTemplateArgs().asArray(),
                                Ctx.getPrintingPolicy());
      Hashes.push_back(llvm::hash_combine(Args, recordShape(Ctx, P)));
    }
    llvm::sort(Hashes);
    return llvm::hash_combine_range(Hashes);
  };
  unsigned Canonical = Ctx.getCanonicalDeviceVariant();
  llvm::hash_code Expected = Shape(Canonical);
  for (unsigned V = 1, E = 1 + Ctx.getNumAuxTargets(); V <= E; ++V)
    if (V != Canonical && Ctx.isDeviceVariant(V) && Shape(V) != Expected)
      return true;
  return false;
}

/// Whether \p FTD's signature names a member of its template parameter
/// \p Index, e.g. `typename Tr::Elem`.
static bool signatureNamesParameterMember(const FunctionTemplateDecl *FTD,
                                          unsigned Index) {
  unsigned Depth = FTD->getTemplateParameters()->getDepth();
  auto Names = [&](QualType T) {
    while (!T.isNull()) {
      if (const auto *DNT = T->getAs<DependentNameType>()) {
        NestedNameSpecifier Qualifier = DNT->getQualifier();
        if (Qualifier.getKind() != NestedNameSpecifier::Kind::Type)
          return false;
        const auto *Param =
            Qualifier.getAsType()->getAs<TemplateTypeParmType>();
        return Param && Param->getDepth() == Depth &&
               Param->getIndex() == Index;
      }
      if (const auto *PT = T->getAs<PointerType>())
        T = PT->getPointeeType();
      else if (const auto *RT = T->getAs<ReferenceType>())
        T = RT->getPointeeType();
      else
        return false;
    }
    return false;
  };
  const FunctionDecl *FD = FTD->getTemplatedDecl();
  return Names(FD->getReturnType()) ||
         llvm::any_of(FD->parameters(), [&](const ParmVarDecl *P) {
           return Names(P->getType());
         });
}

/// The key of a class specialization keyed by architecture that \p Args, the
/// arguments of \p Template, name, or 0.
static unsigned getArchKeyNamed(const ASTContext &Ctx, const Decl *Template,
                                ArrayRef<TemplateArgument> Args) {
  const auto *FTD = dyn_cast<FunctionTemplateDecl>(Template);
  for (auto [Index, Arg] : llvm::enumerate(Args)) {
    if (unsigned Key = getArchKeyNamed(Ctx, Arg, 0))
      return Key;
    // A specialization keyed by target through its partial specializations is
    // that target's too where the signature names a member of it and the
    // devices see different partial specializations. Each target's part of the
    // body would otherwise name a member of its own specialization, and the
    // parameters one of the signature's.
    if (!FTD || Arg.getKind() != TemplateArgument::Type)
      continue;
    const auto *CTSD = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
        Arg.getAsType()->getAsCXXRecordDecl());
    if (unsigned Key = CTSD ? Ctx.getSpecializationKeyVariant(CTSD) : 0;
        Key && !Ctx.getTargetInstantiationKey(CTSD->getCanonicalDecl()) &&
        signatureNamesParameterMember(FTD, Index) &&
        partialSpecializationsDifferBetweenDevices(
            Ctx, CTSD->getSpecializedTemplate()))
      return Key;
  }
  return 0;
}

ASTContext::TargetDependenceKind ASTContext::getSpecializationTargetDependence(
    const Decl *Template, ArrayRef<TemplateArgument> Args) const {
  TargetDependenceKind Dependence = getTargetDependence(Template);
  if (Dependence == TDK_Arch ||
      (TargetInstantiationKeys.empty() && SpecializationKeyVariant.empty()))
    return Dependence;
  return getArchKeyNamed(*this, Template, Args) ? TDK_Arch : Dependence;
}

unsigned ASTContext::computeTargetInstantiationKey(
    const Decl *Template, ArrayRef<TemplateArgument> Args) const {
  if (!isMultiTargetCompile() || TargetKeysSuppressed)
    return 0;
  TargetDependenceKind Dependence =
      getSpecializationTargetDependence(Template, Args);
  if (Dependence == TDK_None)
    return 0;
  bool ByArguments =
      Dependence == TDK_Arch && getTargetDependence(Template) != TDK_Arch;
  // A template declared inside a specialization instantiated for one target,
  // e.g. a generic lambda's call operator, is that target's whichever code
  // instantiates it. Inside the host's or one every device shares, arguments
  // naming an architecture's classes make it that architecture's, or the
  // instantiating device's where they name the canonical device's.
  const Decl *Keyed = nullptr;
  unsigned Target = 0;
  if (unsigned Enclosing =
          getEffectiveTargetInstantiationKey(Template, &Keyed)) {
    if (!ByArguments ||
        !(Enclosing == getHostVariant() || isSharedByDevices(Keyed)))
      return Enclosing;
    if (unsigned Key = getArchKeyNamed(*this, Template, Args);
        isDeviceVariant(Key) &&
        (Key != getCanonicalDeviceVariant() || HostViewOfDeviceCode))
      return Key;
    if (HostViewOfDeviceCode ||
        !isDeviceVariant(Target = getInstantiationTarget()))
      return Enclosing;
  } else if (ByArguments && HostViewOfDeviceCode) {
    // The host's view of device code naming a device's classes views that
    // device's specialization.
    if (unsigned Key = getArchKeyNamed(*this, Template, Args); isDeviceVariant(Key))
      return Key;
  }
  if (!Target)
    Target = getInstantiationTarget();
  if (!isDeviceVariant(Target) && !HostViewOfDeviceCode)
    if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(Template)) {
      const FunctionDecl *FD = FTD->getTemplatedDecl();
      if (FD->hasAttr<CUDAGlobalAttr>()
              ? !KernelsKeyedForHost
              : FD->hasAttr<CUDADeviceAttr>() && !FD->hasAttr<CUDAHostAttr>())
        Target = getCanonicalDeviceVariant();
    }
  // Code compiled for a target a copy standing for several is not for (e.g.
  // a host function launching a kernel) reaches it for the target ambient
  // now. Keys follow the dependence, not the copy's targets: a copy grows
  // targets after its first specializations are made.
  if (isCopyForSeveral(Template))
    if (unsigned Ambient = getCurrentTargetVariant();
        !Template->isCopyFor(Target) && Template->isCopyFor(Ambient))
      Target = Ambient;
  if (Dependence == TDK_Side)
    return isDeviceVariant(Target) ? getCanonicalDeviceVariant()
                                   : getHostVariant();
  return Target;
}

/// Profiles \p RD as getManglingEquivalent compares classes, returning
/// whether a specialization keyed by target is part of it.
static bool profileManglingShape(const ASTContext &Ctx, const CXXRecordDecl *RD,
                                 llvm::FoldingSetNodeID &ID, unsigned Depth);

static bool profileManglingShape(const ASTContext &Ctx, QualType T,
                                 llvm::FoldingSetNodeID &ID, unsigned Depth) {
  T = T.getCanonicalType();
  ID.AddInteger(T.getLocalFastQualifiers());
  if (const auto *RD = T->getAsCXXRecordDecl(); RD && T->isRecordType())
    return profileManglingShape(Ctx, RD, ID, Depth);
  if (const auto *PT = T->getAs<PointerType>()) {
    ID.AddInteger(1);
    return profileManglingShape(Ctx, PT->getPointeeType(), ID, Depth);
  }
  if (const auto *RT = T->getAs<ReferenceType>()) {
    ID.AddInteger(isa<LValueReferenceType>(RT) ? 2 : 3);
    return profileManglingShape(Ctx, RT->getPointeeTypeAsWritten(), ID, Depth);
  }
  ID.AddPointer(T.getTypePtr());
  return false;
}

/// One of the targets' copies of \p D, the same for each of them.
static const Decl *getCopyRepresentative(const ASTContext &Ctx,
                                         const NamedDecl *D) {
  const Decl *Representative = D->getCanonicalDecl();
  if (!D->getTargetVariant())
    return Representative;
  SmallVector<const NamedDecl *, 4> Copies;
  Ctx.getTargetCopies(D, Copies);
  for (const NamedDecl *Copy : Copies)
    if (std::less<const Decl *>()(Copy->getCanonicalDecl(), Representative))
      Representative = Copy->getCanonicalDecl();
  return Representative;
}

static bool profileManglingShape(const ASTContext &Ctx, const CXXRecordDecl *RD,
                                 llvm::FoldingSetNodeID &ID, unsigned Depth) {
  if (Depth > 8) {
    ID.AddPointer(RD->getCanonicalDecl());
    return false;
  }
  if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
    const ClassTemplateDecl *CTD = CTSD->getSpecializedTemplate();
    ID.AddPointer(getCopyRepresentative(Ctx, CTD));
    bool Keyed = Ctx.getTargetInstantiationKey(CTSD->getCanonicalDecl()) ||
                 CTSD->getTargetVariant() || CTD->getTargetVariant();
    for (const TemplateArgument &Arg : CTSD->getTemplateArgs().asArray()) {
      if (Arg.getKind() == TemplateArgument::Type)
        Keyed |= profileManglingShape(Ctx, Arg.getAsType(), ID, Depth + 1);
      else
        Arg.Profile(ID, Ctx);
    }
    return Keyed;
  }
  if (const auto *Parent = dyn_cast<CXXRecordDecl>(RD->getDeclContext())) {
    bool Keyed = profileManglingShape(Ctx, Parent, ID, Depth + 1);
    ID.AddPointer(RD->getDeclName().getAsOpaquePtr());
    ID.AddInteger(RD->getLocation().getRawEncoding());
    return Keyed;
  }
  ID.AddPointer(RD->getCanonicalDecl());
  return false;
}

const CXXRecordDecl *
ASTContext::getManglingEquivalentImpl(const CXXRecordDecl *RD) const {
  if (!LLVM_UNLIKELY(clang::AllowTargetVariantDecls))
    return RD;
  RD = RD->getCanonicalDecl();
  if (auto It = ManglingEquivalents.find(RD); It != ManglingEquivalents.end())
    return It->second;
  llvm::FoldingSetNodeID ID;
  const CXXRecordDecl *Equivalent = RD;
  if (profileManglingShape(*this, RD, ID, 0)) {
    llvm::BumpPtrAllocator Scratch;
    llvm::FoldingSetNodeIDRef Ref = ID.Intern(Scratch);
    StringRef Shape(reinterpret_cast<const char *>(Ref.getData()),
                    Ref.getSize() * sizeof(unsigned));
    Equivalent = ManglingEquivalentByShape.try_emplace(Shape, RD).first->second;
  }
  ManglingEquivalents[RD] = Equivalent;
  return Equivalent;
}

QualType ASTContext::getManglingEquivalentImpl(QualType T) const {
  // A type mangled as written is compared as written.
  if (!LLVM_UNLIKELY(clang::AllowTargetVariantDecls) || T.isNull() ||
      T->isInstantiationDependentType())
    return T;
  QualType C = T.getCanonicalType();
  const Type *Unqualified = C.getTypePtr();
  auto [It, Inserted] = ManglingEquivalentTypes.try_emplace(Unqualified);
  if (Inserted) {
    // Null where nothing in the type is replaced.
    QualType Result;
    if (const auto *RD = Unqualified->getAsCXXRecordDecl();
        RD && Unqualified->isRecordType()) {
      const CXXRecordDecl *Equivalent = getManglingEquivalent(RD);
      if (Equivalent != RD->getCanonicalDecl())
        Result = getCanonicalTagType(Equivalent);
    } else if (const auto *PT = dyn_cast<PointerType>(Unqualified)) {
      QualType Pointee = getManglingEquivalent(PT->getPointeeType());
      if (Pointee != PT->getPointeeType())
        Result = getCanonicalType(getPointerType(Pointee));
    } else if (const auto *RT = dyn_cast<ReferenceType>(Unqualified)) {
      QualType Pointee = getManglingEquivalent(RT->getPointeeTypeAsWritten());
      if (Pointee != RT->getPointeeTypeAsWritten())
        Result = getCanonicalType(isa<LValueReferenceType>(RT)
                                      ? getLValueReferenceType(Pointee)
                                      : getRValueReferenceType(Pointee));
    }
    // Inserting into the map may have moved it.
    It = ManglingEquivalentTypes.find(Unqualified);
    It->second = Result;
  }
  return It->second.isNull()
             ? T
             : getQualifiedType(It->second, C.getLocalQualifiers());
}

unsigned
ASTContext::getEffectiveTargetInstantiationKey(const Decl *D,
                                               const Decl **Keyed) const {
  if (TargetInstantiationKeys.empty())
    return 0;
  for (; D; D = dyn_cast_or_null<Decl>(D->getDeclContext()))
    if (unsigned Key = getTargetInstantiationKey(D)) {
      if (Keyed)
        *Keyed = D;
      return Key;
    }
  return 0;
}

bool ASTContext::isSharedByDevices(const Decl *D) const {
  if (getTargetInstantiationKey(D) != getCanonicalDeviceVariant())
    return false;
  if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(D))
    return getTargetDependence(CTSD->getSpecializedTemplate()) == TDK_Side;
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    if (const FunctionTemplateDecl *FTD = FD->getPrimaryTemplate())
      return getSpecializationTargetDependence(
                 FTD, FD->getTemplateSpecializationArgs()->asArray()) ==
             TDK_Side;
  return false;
}

TargetSet ASTContext::getTargetInstantiationTargets(const Decl *D) const {
  unsigned Key = getEffectiveTargetInstantiationKey(D);
  if (!Key)
    return TargetSet::all();
  if (std::optional<TargetSet> Recorded = getRecordedSpecializationTargets(D))
    return *Recorded;
  if (Key != getCanonicalDeviceVariant())
    return TargetSet::single(Key);
  TargetSet Targets = TargetSet::all();
  if (TargetInstanceReplacements.empty())
    return Targets;
  for (; D; D = dyn_cast_or_null<Decl>(D->getDeclContext()))
    if (uint64_t Replaced = TargetInstanceReplacements.lookup(D))
      for (unsigned V = 1; V <= TargetSet::MaxVariant; ++V)
        if (Replaced >> V & 1)
          Targets.erase(V);
  return Targets;
}
