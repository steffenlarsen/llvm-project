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
  // A token from a macro expansion is presumed to be where the macro was
  // expanded, the same in every arm, so a macro defined differently per target
  // (e.g. a wavefront size) tells the arms apart only by the value it expanded
  // to.
  if (const auto *IL = dyn_cast<IntegerLiteral>(S))
    H = llvm::hash_combine(H, IL->getValue());
  else if (const auto *FL = dyn_cast<FloatingLiteral>(S))
    H = llvm::hash_combine(H, FL->getValue());
  else if (const auto *CL = dyn_cast<CharacterLiteral>(S))
    H = llvm::hash_combine(H, CL->getValue());
  else if (const auto *SL = dyn_cast<StringLiteral>(S))
    H = llvm::hash_combine(H, SL->getBytes());
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

void clang::mergeReparseAlternatives(ASTContext &Ctx,
                                     ArrayRef<Decl *> Primary,
                                     ArrayRef<ReparseAlternate> Alternates) {
  StructuralEquivalenceContext::NonEquivalentDeclSet NonEquiv;
  unsigned Count = 0;
  for (size_t I = 0, E = Primary.size(); I != E; ++I) {
    Decl *A = Primary[I];
    if (!A || A->getTargetVariant() == Decl::TargetVariantRedundant)
      continue;
    // A is only promoted to shared once every alternate agrees -- see this
    // function's doc comment for why partial agreement is not enough.
    SmallVector<std::pair<Decl *, unsigned>, 4> Agreeing;
    bool AllAgree = true;
    for (const ReparseAlternate &Alt : Alternates) {
      assert(Primary.size() == Alt.Decls.size() &&
             "Design 1 re-parses one declaration's tokens whole, so a primary "
             "and its alternate must have the same shape");
      Decl *B = Alt.Decls[I];
      if (!B || A == B)
        continue;
      StructuralEquivalenceContext SEC(Ctx.getLangOpts(), Ctx, Ctx, NonEquiv,
                                       StructuralEquivalenceKind::Default,
                                       /*StrictTypeSpelling=*/false,
                                       /*Complain=*/false);
      if (!SEC.IsEquivalent(A, B) || !isInterchangeable(Ctx, SEC, A, B)) {
        AllAgree = false;
        break;
      }
      Agreeing.emplace_back(B, Alt.Variant);
    }
    if (!AllAgree || Agreeing.empty())
      continue;
    for (auto &[B, Variant] : Agreeing) {
      // Recover, from B before it is discarded, any per-target-forked static
      // data member reference (see recordDivergentValueRefs) that A's own,
      // about-to-be-sole-surviving body would otherwise lose.
      recordDivergentValueRefs(Ctx, A->getBody(), B->getBody(), Variant);
      // Unlike mergeEquivalentVariants, B's kind is never gated here: this
      // runs before any caller anywhere in the file has been parsed, so
      // neither A nor B's FoldingSet can already hold a materialized
      // specialization that hiding B's declaration would take out of reach.
      markRedundantTree(B, Variant);
    }
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

/// Whether a target with no arm in \p Arms already has its own copy of \p A,
/// declared outside this episode -- e.g. an include-guarded header whose first
/// inclusion sat under a host-only #if, so a later inclusion yields an episode
/// holding only the device arms. Promoting A to shared would make it visible
/// to that target alongside its own copy.
bool absentTargetHasOwnCopy(ASTContext &Ctx, const Decl *A, StringRef Key,
                            ArrayRef<WidenedArm> Arms) {
  const auto *ND = dyn_cast<NamedDecl>(A);
  if (!ND || !ND->getDeclName())
    return false;
  llvm::StringMap<unsigned> AnonOrdinal;
  for (const NamedDecl *Other :
       ND->getDeclContext()->getRedeclContext()->lookup(ND->getDeclName())) {
    unsigned V = Other->getTargetVariant();
    if (!V || V == Decl::TargetVariantRedundant ||
        llvm::any_of(Arms,
                     [V](const WidenedArm &Arm) { return Arm.Variant == V; }))
      continue;
    if (widenedAlternativeKey(Ctx, Other, AnonOrdinal) == Key)
      return true;
  }
  return false;
}

} // namespace

void clang::mergeWidenedAlternatives(ASTContext &Ctx,
                                     ArrayRef<WidenedArm> Arms) {
  if (Arms.size() < 2)
    return;
  StructuralEquivalenceContext::NonEquivalentDeclSet NonEquiv;
  unsigned Count = 0;
  TargetKeyedTemplates Keyed;
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
      if (!AllAgree || absentTargetHasOwnCopy(Ctx, A, Key, Arms))
        continue;
      for (size_t J = 0; J != NArms; ++J)
        if (Matches[J])
          markRedundantTree(Matches[J], Arms[J].Variant);
      // The variant being reverted to shared is read off A itself rather
      // than assumed to be 1, since A is not always the host's own
      // declaration.
      unclaimTree(A, A->getTargetVariant(), Count, &Keyed);
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

/// Side if every device target's copy of \p ND computes the same thing,
/// Arch otherwise.
static ASTContext::TargetDependenceKind
sourceTargetDependence(const ASTContext &Ctx, const NamedDecl *ND) {
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
  const DeclContext *DC = ND->getDeclContext()->getRedeclContext();
  for (const NamedDecl *Copy : DC->lookup(ND->getDeclName())) {
    if (Copy->getKind() != ND->getKind() ||
        Copy->getLocation() != ND->getLocation() ||
        Copy->isRedundantTargetVariant())
      continue;
    llvm::FoldingSetNodeID ID = Fingerprint(Copy);
    if (Copy->getTargetVariant() < 2)
      Host = ID;
    else if (!Device)
      Device = ID;
    else if (*Device != ID)
      DevicesDiffer = true;
  }
  (void)Ctx;
  if (DevicesDiffer)
    return ASTContext::TDK_Arch;
  if (Host && Device && *Host == *Device)
    return ASTContext::TDK_None;
  return ASTContext::TDK_Side;
}

namespace {
/// Collects the strongest target dependence among what a definition names.
class TargetDependenceCollector
    : public RecursiveASTVisitor<TargetDependenceCollector> {
  const ASTContext &Ctx;

public:
  ASTContext::TargetDependenceKind Result = ASTContext::TDK_None;
  /// Whether a member function's body is yet to be parsed.
  bool PendingBody = false;
  explicit TargetDependenceCollector(const ASTContext &Ctx) : Ctx(Ctx) {}
  bool shouldVisitTemplateInstantiations() const { return false; }

  bool VisitFunctionDecl(FunctionDecl *FD) {
    PendingBody |= FD->willHaveBody();
    return true;
  }

  /// Returns false, ending the walk, once nothing stronger can be found.
  bool consider(const Decl *D) {
    if (!D)
      return true;
    ASTContext::TargetDependenceKind K = referenceDependence(D);
    if (K > Result)
      Result = K;
    return Result != ASTContext::TDK_Arch;
  }
  bool VisitDeclRefExpr(DeclRefExpr *E) { return consider(E->getDecl()); }
  bool VisitMemberExpr(MemberExpr *E) { return consider(E->getMemberDecl()); }
  bool VisitOverloadExpr(OverloadExpr *E) {
    for (const NamedDecl *D : E->decls())
      if (!consider(D->getUnderlyingDecl()))
        return false;
    return true;
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
  /// time, or shapes a type, counts.
  ASTContext::TargetDependenceKind referenceDependence(const Decl *D) const {
    if (const auto *ND = dyn_cast<NamedDecl>(D);
        ND && Ctx.isTargetDivergentSource(ND))
      return Ctx.getTargetDependence(ND);
    if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
      if (!FD->isConstexpr())
        return ASTContext::TDK_None;
      if (const FunctionTemplateDecl *FTD = FD->getPrimaryTemplate())
        return Ctx.getTargetDependence(FTD);
      if (const auto *CTSD =
              dyn_cast<ClassTemplateSpecializationDecl>(FD->getDeclContext()))
        return Ctx.getTargetDependence(CTSD->getSpecializedTemplate());
      return Ctx.getTargetDependence(FD);
    }
    if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(D))
      return FTD->getTemplatedDecl()->isConstexpr()
                 ? Ctx.getTargetDependence(FTD)
                 : ASTContext::TDK_None;
    if (const auto *VD = dyn_cast<VarDecl>(D)) {
      if (const auto *CTSD =
              dyn_cast<ClassTemplateSpecializationDecl>(VD->getDeclContext()))
        return Ctx.getTargetDependence(CTSD->getSpecializedTemplate());
      if (VD->getDeclContext()->isFunctionOrMethod() || !VD->getInit() ||
          !VD->isUsableInConstantExpressions(const_cast<ASTContext &>(Ctx)))
        return ASTContext::TDK_None;
      return Ctx.getTargetDependence(VD);
    }
    if (isa<ClassTemplateDecl, TypeAliasTemplateDecl, VarTemplateDecl>(D))
      return Ctx.getTargetDependence(D);
    if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(D))
      return Ctx.getTargetDependence(CTSD->getSpecializedTemplate());
    return ASTContext::TDK_None;
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
    if (Collector.Result == ASTContext::TDK_Arch)
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

ASTContext::TargetDependenceKind
ASTContext::getTargetDependence(const Decl *D) const {
  if (!D)
    return TDK_None;
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
    return TargetDependenceKind(Cached->second);
  auto [OnStack, Inserted] =
      TargetDependenceIndex.try_emplace(D, TargetDependenceStack.size());
  if (!Inserted) {
    TargetDependenceLowLink =
        std::min(TargetDependenceLowLink, OnStack->second);
    return TDK_None;
  }
  unsigned Index = TargetDependenceStack.size();
  TargetDependenceStack.push_back({D, /*Complete=*/true});
  unsigned OuterLowLink = TargetDependenceLowLink;
  TargetDependenceLowLink = Index;

  TargetDependenceCollector Collector(*this);
  bool Complete = true;
  if (const auto *ND = dyn_cast<NamedDecl>(D);
      ND && isTargetDivergentSource(ND))
    Collector.Result = sourceTargetDependence(*this, ND);
  if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(D)) {
    Complete = FTD->getTemplatedDecl()->hasBody();
    Collector.TraverseDecl(const_cast<FunctionTemplateDecl *>(FTD));
  } else if (const auto *CTD = dyn_cast<ClassTemplateDecl>(D)) {
    const CXXRecordDecl *Def = CTD->getTemplatedDecl()->getDefinition();
    Complete = Def;
    Collector.TraverseDecl(const_cast<ClassTemplateDecl *>(CTD));
    if (Def)
      Complete &= !traverseOutOfLineMembers(Collector, Def);
    SmallVector<ClassTemplatePartialSpecializationDecl *, 4> PS;
    const_cast<ClassTemplateDecl *>(CTD)->getPartialSpecializations(PS);
    for (ClassTemplatePartialSpecializationDecl *P : PS) {
      if (Collector.Result == TDK_Arch)
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
  TargetDependenceKind Result = Collector.Result;
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
          Collector.Result > Cached->second) {
        noteTargetDependenceEscalation();
        return;
      }
      break;
    }
}

void ASTContext::noteUnkeyedSpecialization(const Decl *Template) {
  if (isHostPrimaryMultiTarget() && !TargetKeysSuppressed)
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
      uint8_t Before =
          Stale == TargetDependenceCache.end() ? TDK_None : Stale->second;
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

bool ASTContext::isHostPrimaryMultiTarget() const {
  return LLVM_UNLIKELY(clang::AllowTargetVariantDecls) && LangOpts.CUDA &&
         !LangOpts.CUDAIsDevice && !AuxTargets.empty();
}

bool ASTContext::hasTargetInstantiationKeys(const Decl *Template) const {
  return isHostPrimaryMultiTarget() &&
         getTargetDependence(Template) != TDK_None;
}

unsigned ASTContext::computeTargetInstantiationKey(const Decl *Template) const {
  if (!isHostPrimaryMultiTarget() || TargetKeysSuppressed)
    return 0;
  TargetDependenceKind Dependence = getTargetDependence(Template);
  if (Dependence == TDK_None)
    return 0;
  unsigned Target = getInstantiationTarget();
  if (Target < 2)
    if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(Template)) {
      const FunctionDecl *FD = FTD->getTemplatedDecl();
      if (FD->hasAttr<CUDAGlobalAttr>() ||
          (FD->hasAttr<CUDADeviceAttr>() && !FD->hasAttr<CUDAHostAttr>()))
        Target = getCanonicalDeviceVariant();
    }
  if (Dependence == TDK_Side)
    return Target >= 2 ? getCanonicalDeviceVariant() : 1;
  return Target;
}

unsigned ASTContext::getEffectiveTargetInstantiationKey(const Decl *D) const {
  if (TargetInstantiationKeys.empty())
    return 0;
  for (; D; D = dyn_cast_or_null<Decl>(D->getDeclContext()))
    if (unsigned Key = getTargetInstantiationKey(D))
      return Key;
  return 0;
}

bool ASTContext::isTargetInstanceReplaced(const Decl *D,
                                          unsigned Variant) const {
  if (TargetInstanceReplacements.empty())
    return false;
  for (; D; D = dyn_cast_or_null<Decl>(D->getDeclContext())) {
    auto It = TargetInstanceReplacements.find(D);
    if (It != TargetInstanceReplacements.end() && (It->second >> Variant & 1))
      return true;
  }
  return false;
}
