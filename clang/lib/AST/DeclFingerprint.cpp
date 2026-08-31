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
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <cassert>

using namespace clang;

llvm::cl::opt<bool> clang::HideRedundantVariants(
    "hide-redundant-variants", llvm::cl::Hidden, llvm::cl::init(false),
    llvm::cl::desc("Prototype: with -merge-equivalent-variants, also hide the "
                   "copy that turned out to match the original"));

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
    unsigned V = D->getTargetVariant();
    if (V == Decl::TargetVariantRedundant)
      return true;
    for (const DeclContext *DC = D->getDeclContext(); DC && !V;
         DC = DC->getParent())
      if (const auto *DD = dyn_cast<Decl>(DC))
        V = DD->getTargetVariant();
    if (Variant && V && V != Variant)
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
static void unclaimTree(Decl *D, unsigned FromVariant, unsigned &Count) {
  if (!D || D->getTargetVariant() != FromVariant)
    return;
  D->setTargetVariant(0);
  ++Count;
  if (auto *TD = dyn_cast<TemplateDecl>(D)) {
    unclaimTree(TD->getTemplatedDecl(), FromVariant, Count);
    if (TemplateParameterList *TPL = TD->getTemplateParameters())
      for (NamedDecl *P : *TPL)
        unclaimTree(P, FromVariant, Count);
  }
  if (auto *DC = dyn_cast<DeclContext>(D))
    for (Decl *Sub : DC->decls())
      unclaimTree(Sub, FromVariant, Count);
}

/// Mark \p D and everything it encloses as belonging to no target.
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

/// A hash of every statement position inside \p S.
///
/// Comparing only a body's begin and end is not enough. The divergence is
/// usually *inside*: `amd_hip_fp8.h` picks `__clz` or `__builtin_clz` in the
/// middle of a function, so both arms' bodies open and close on the same lines
/// and differ only in the statements between them.
static llvm::hash_code bodyShape(const SourceManager &SM, const Stmt *S) {
  if (!S)
    return llvm::hash_code(0);
  PresumedLoc P = SM.getPresumedLoc(S->getBeginLoc());
  llvm::hash_code H =
      llvm::hash_combine(S->getStmtClass(), P.isValid() ? P.getLine() : 0,
                         P.isValid() ? P.getColumn() : 0);
  for (const Stmt *C : S->children())
    H = llvm::hash_combine(H, bodyShape(SM, C));
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
static bool isInterchangeable(ASTContext &Ctx,
                              StructuralEquivalenceContext &SEC, const Decl *A,
                              const Decl *B) {
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
      if (!SEC.IsEquivalent(MA[I]->getUnderlyingType(),
                            MB[I]->getUnderlyingType()))
        return false;
    }

    auto MethodMembers = [](const CXXRecordDecl *R) {
      SmallVector<const CXXMethodDecl *, 8> V;
      for (const Decl *D : R->decls())
        if (const auto *MD = dyn_cast<CXXMethodDecl>(D))
          V.push_back(MD);
      return V;
    };
    SmallVector<const CXXMethodDecl *, 8> MethA = MethodMembers(RA),
                                          MethB = MethodMembers(RB);
    if (MethA.size() != MethB.size())
      return false;
    for (unsigned I = 0, E = MethA.size(); I != E; ++I) {
      if (MethA[I]->getDeclName() != MethB[I]->getDeclName())
        return false;
      if (!SameBody(MethA[I]->getBody(), MethB[I]->getBody()))
        return false;
    }

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

  if (!SameBody(A->getBody(), B->getBody()))
    return false;

  if (!sameLocalDeclTypes(A->getBody(), B->getBody()))
    return false;

  const auto *VA = dyn_cast<VarDecl>(A), *VB = dyn_cast<VarDecl>(B);
  if (VA && VB && !SameBody(VA->getInit(), VB->getInit()))
    return false;
  return true;
}

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
    for (NamedDecl *D : Entry.second)
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
    if (!A)
      continue;
    // Every other target's copy is checked against the primary, but A is
    // only promoted to shared -- and any B hidden -- once *every* present
    // variant agrees, not merely the first one checked: with two or more aux
    // targets, one target's copy can be interchangeable with the primary's
    // while another's genuinely differs. Promoting A on partial agreement
    // would hide the disagreeing variant's own correctly-tagged copy behind
    // an already-"shared" A that was never queued for that variant's device
    // codegen (A is the primary's own declaration; a host-only pass never
    // queues a __device__-only function for codegen), leaving that variant's
    // device module with a call and no body -- an undefined symbol at link
    // time, not a Sema-visible ambiguity.
    bool AllAgree = true;
    SmallVector<std::pair<unsigned, NamedDecl *>, 4> Agreeing;
    for (auto &VariantEntry : ByVariant) {
      unsigned Variant = VariantEntry.first;
      NamedDecl *B = VariantEntry.second;
      if (Variant == 1)
        continue;
      // Must run regardless of whether A and B turn out interchangeable
      // below -- see propagateExplicitInstantiationKind's comment.
      if (auto *AFTD = dyn_cast<FunctionTemplateDecl>(A))
        if (auto *BFTD = dyn_cast<FunctionTemplateDecl>(B))
          propagateExplicitInstantiationKind(AFTD, BFTD);
      StructuralEquivalenceContext SEC(Ctx.getLangOpts(), Ctx, Ctx, NonEquiv,
                                       StructuralEquivalenceKind::Default,
                                       /*StrictTypeSpelling=*/false,
                                       /*Complain=*/false);
      // The copy B is left in place: hiding it too would remove redundant
      // duplicates but also declarations the host needs, because the copy's
      // subtree is not the same shape as the original's. A duplicate is a
      // lesser defect than an absence.
      if (!SEC.IsEquivalent(A, B) || !isInterchangeable(Ctx, SEC, A, B)) {
        AllAgree = false;
        continue;
      }
      Agreeing.emplace_back(Variant, B);
    }
    if (!AllAgree)
      continue;
    for (auto &VariantAndB : Agreeing) {
      unsigned Variant = VariantAndB.first;
      NamedDecl *B = VariantAndB.second;
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
      // declaration is always safe once A/B are confirmed interchangeable;
      // hiding an already-materialized specialization additionally needs A
      // to already have an equivalent, so a specialization A doesn't have
      // isn't lost.
      //
      // EnumDecl is included here for the same reason as
      // FunctionDecl/VarDecl: an enum cannot be a template and has no
      // FoldingSet-reached specializations, so markRedundantTree's
      // DeclContext::decls() walk reaches every one of its enumerators --
      // there is no hidden subtree this could lose. Without this, an
      // unscoped enum re-touched by a later, unrelated divergent #if/#endif
      // gets a second EnumDecl copy that stays tagged and visible after A is
      // unclaimed to variant 0: A's now-shared enumerator and B's
      // still-tagged one are simultaneously visible whenever B's variant is
      // current, making every reference to it ambiguous even though both
      // candidates are the same declaration re-parsed.
      if (clang::HideRedundantVariants || isa<FunctionDecl>(B) ||
          isa<FunctionTemplateDecl>(B) || isa<VarDecl>(B) ||
          isa<VarTemplateDecl>(B) || isa<EnumDecl>(B)) {
        markRedundantTree(B, Variant);
      }
      if (auto *BFTD = dyn_cast<FunctionTemplateDecl>(B))
        hideRedundantSpecializations(cast<FunctionTemplateDecl>(A), BFTD);
      else if (auto *BVTD = dyn_cast<VarTemplateDecl>(B))
        hideRedundantSpecializations(cast<VarTemplateDecl>(A), BVTD);
    }
    if (!Agreeing.empty())
      unclaimTree(A, 1, Count);
  }
  return Count;
}

/// Whether \p D is a per-target-forked static data member: a VarDecl whose
/// DeclContext is a ClassTemplateSpecializationDecl of a template with
/// ClassTemplateDecl::hasTargetTaggedValueMember() (e.g.
/// ggml_cuda_mma::tile<>::ne) -- one of several distinct, per-target
/// instances of the same named member, not a single VarDecl shared across
/// targets.
static bool isTargetForkedValueMember(const ValueDecl *D) {
  const auto *VD = dyn_cast<VarDecl>(D);
  if (!VD)
    return false;
  const auto *CTSD =
      dyn_cast_or_null<ClassTemplateSpecializationDecl>(VD->getDeclContext());
  return CTSD && CTSD->getSpecializedTemplate()->hasTargetTaggedValueMember();
}

/// Walk \p A and \p B -- two reparse alternates already confirmed
/// interchangeable by isInterchangeable's shape-only SameBody/bodyShape check
/// -- in lockstep over Stmt::children(), and for every corresponding pair of
/// DeclRefExprs that resolve to different per-target-forked static data
/// members (isTargetForkedValueMember), record \p B's (the alternate about to
/// be hidden for \p Variant) VarDecl as \p A's own DeclRefExpr's answer for
/// \p Variant. Without this, a shared caller whose reparse alternates are
/// merged here permanently keeps whichever target's copy happened to survive
/// the merge for every other target too, even though
/// ClassTemplateSpecializationDecl::Profile already forked a correct answer
/// for each target before this merge discards all but one reference to them.
///
/// isInterchangeable's SameBody guarantees A and B have the same
/// Stmt::children() shape throughout, so the lockstep walk never needs to
/// resolve identity independently -- it only has to notice where two
/// structurally-identical positions disagree on which Decl they resolved to.
static void recordDivergentValueRefs(ASTContext &Ctx, const Stmt *A,
                                     const Stmt *B, unsigned Variant) {
  if (!A || !B)
    return;
  if (const auto *DRA = dyn_cast<DeclRefExpr>(A)) {
    if (const auto *DRB = dyn_cast<DeclRefExpr>(B)) {
      if (DRA->getDecl() != DRB->getDecl() &&
          isTargetForkedValueMember(DRB->getDecl()))
        Ctx.setTargetVariantValueDecl(
            DRA, Variant, const_cast<VarDecl *>(cast<VarDecl>(DRB->getDecl())));
    }
  }
  auto ChildrenA = A->children();
  auto ChildrenB = B->children();
  auto ItA = ChildrenA.begin(), EndA = ChildrenA.end();
  auto ItB = ChildrenB.begin(), EndB = ChildrenB.end();
  for (; ItA != EndA && ItB != EndB; ++ItA, ++ItB)
    recordDivergentValueRefs(Ctx, *ItA, *ItB, Variant);
}

void clang::mergeReparseAlternative(ASTContext &Ctx, ArrayRef<Decl *> Primary,
                                    ArrayRef<Decl *> Alternative,
                                    unsigned Variant,
                                    SmallVectorImpl<Decl *> &PendingUnclaim) {
  assert(Primary.size() == Alternative.size() &&
         "Design 1 re-parses one declaration's tokens whole, so a primary "
         "and its alternate must have the same shape");
  StructuralEquivalenceContext::NonEquivalentDeclSet NonEquiv;
  for (size_t I = 0, E = Primary.size(); I != E; ++I) {
    Decl *A = Primary[I];
    Decl *B = Alternative[I];
    if (!A || !B)
      continue;
    if (A == B)
      continue;
    StructuralEquivalenceContext SEC(Ctx.getLangOpts(), Ctx, Ctx, NonEquiv,
                                     StructuralEquivalenceKind::Default,
                                     /*StrictTypeSpelling=*/false,
                                     /*Complain=*/false);
    bool Equiv = SEC.IsEquivalent(A, B) && isInterchangeable(Ctx, SEC, A, B);
    if (!Equiv)
      continue;
    // Recover, from B before it is discarded, any per-target-forked static
    // data member reference (see recordDivergentValueRefs) that A's own,
    // about-to-be-sole-surviving body would otherwise lose.
    recordDivergentValueRefs(Ctx, A->getBody(), B->getBody(), Variant);
    // Unlike mergeEquivalentVariants, B's kind is never gated here: this runs
    // before any caller anywhere in the file has been parsed, so neither A
    // nor B's FoldingSet can already hold a materialized specialization that
    // hiding B's declaration would take out of reach.
    markRedundantTree(B, Variant);
    // A is compared against every group seen so far for this reparse, not
    // just the true host origin, so a later call can see an A that an
    // earlier call already marked redundant. Unclaiming would revert that
    // already-redundant A's tag back to shared, reviving a duplicate that
    // must stay hidden -- only a live (non-redundant) A is ever the thing to
    // revert.
    if (A->getTargetVariant() == Decl::TargetVariantRedundant)
      continue;
    // A is only recorded here, not unclaimed yet: see this function's doc
    // comment for why reverting A's tag has to wait until the whole reparse
    // batch (every alternate, not just this one) has been merged.
    PendingUnclaim.push_back(A);
  }
}

void clang::applyPendingReparseUnclaims(ArrayRef<Decl *> PendingUnclaim) {
  llvm::SmallPtrSet<Decl *, 8> Seen;
  unsigned Count = 0;
  for (Decl *A : PendingUnclaim) {
    if (!Seen.insert(A).second)
      continue;
    // A recorded here is not always the host origin: this also reconciles
    // one already-returned alternate against another, so the variant being
    // reverted to shared has to be read off A itself rather than assumed to
    // be 1 (mirrors mergeWidenedAlternative's identical reasoning below).
    unclaimTree(A, A->getTargetVariant(), Count);
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

/// Declarations from one arm, indexed by widenedAlternativeKey, with an
/// independent consumption cursor per key so repeated keys (overload sets,
/// repeated static_asserts) are paired in declaration order rather than
/// arbitrarily.
class KeyedDeclSet {
  llvm::StringMap<SmallVector<Decl *, 2>> ByKey;
  llvm::StringMap<unsigned> Cursor;

public:
  KeyedDeclSet(ASTContext &Ctx, ArrayRef<Decl *> Decls) {
    llvm::StringMap<unsigned> AnonOrdinal;
    for (Decl *D : Decls)
      if (D)
        ByKey[widenedAlternativeKey(Ctx, D, AnonOrdinal)].push_back(D);
  }

  Decl *takeNextMatching(StringRef Key) {
    auto It = ByKey.find(Key);
    if (It == ByKey.end())
      return nullptr;
    unsigned &Idx = Cursor[Key];
    if (Idx >= It->second.size())
      return nullptr;
    return It->second[Idx++];
  }
};

} // namespace

void clang::mergeWidenedAlternatives(ASTContext &Ctx,
                                     ArrayRef<WidenedArm> Arms) {
  if (Arms.size() < 2)
    return;
  StructuralEquivalenceContext::NonEquivalentDeclSet NonEquiv;
  unsigned Count = 0;
  SmallVector<KeyedDeclSet, 4> Sets;
  Sets.reserve(Arms.size());
  for (const WidenedArm &Arm : Arms)
    Sets.emplace_back(Ctx, Arm.Decls);
  llvm::StringMap<unsigned> AnonOrdinal;
  for (size_t I = 0, NArms = Arms.size(); I != NArms; ++I) {
    for (Decl *A : Arms[I].Decls) {
      if (!A)
        continue;
      // An arm is reused as the "other side" of every other arm's own pass
      // over its declarations. If an earlier pass already folded A into some
      // other arm as the redundant side, A no longer names a live per-target
      // declaration, and re-deriving FromVariant from its current (redundant)
      // tag and unclaiming it would revive it to shared -- producing a second
      // live copy alongside whichever decl it was folded into.
      if (A->getTargetVariant() == Decl::TargetVariantRedundant)
        continue;
      std::string Key = widenedAlternativeKey(Ctx, A, AnonOrdinal);
      // Every other arm must have a matching declaration that structurally
      // agrees with A before A can be promoted to shared -- see
      // mergeWidenedAlternatives' doc comment for why partial agreement is
      // not enough.
      SmallVector<Decl *, 4> Matches(NArms, nullptr);
      bool AllAgree = true;
      for (size_t J = 0; J != NArms && AllAgree; ++J) {
        if (J == I)
          continue;
        Decl *B = Sets[J].takeNextMatching(Key);
        if (!B || A == B) {
          AllAgree = false;
          break;
        }
        StructuralEquivalenceContext SEC(Ctx.getLangOpts(), Ctx, Ctx, NonEquiv,
                                         StructuralEquivalenceKind::Default,
                                         /*StrictTypeSpelling=*/false,
                                         /*Complain=*/false);
        bool WidenEquiv =
            SEC.IsEquivalent(A, B) && isInterchangeable(Ctx, SEC, A, B);
        if (!WidenEquiv) {
          AllAgree = false;
          break;
        }
        Matches[J] = B;
      }
      if (!AllAgree)
        continue;
      for (size_t J = 0; J != NArms; ++J)
        if (Matches[J])
          markRedundantTree(Matches[J], Arms[J].Variant);
      // The variant being reverted to shared is read off A itself rather
      // than assumed to be 1, since A is not always the host's own
      // declaration.
      unclaimTree(A, A->getTargetVariant(), Count);
    }
  }
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
      if (D->getTargetVariant() == 1 && !A)
        A = D;
      else if (D->getTargetVariant() == 2 && !B)
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
