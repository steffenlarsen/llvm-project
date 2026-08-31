//===- DeclFingerprint.cpp - Order-independent AST comparison -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclFingerprint.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/SourceManager.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/ASTStructuralEquivalence.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;

llvm::cl::opt<bool> clang::HideRedundantVariants(
    "hide-redundant-variants", llvm::cl::Hidden, llvm::cl::init(false),
    llvm::cl::desc("Prototype: with -merge-equivalent-variants, also hide the "
                   "copy that turned out to match the original"));

llvm::cl::opt<std::string> clang::TraceUnclaim(
    "trace-unclaim", llvm::cl::Hidden,
    llvm::cl::desc("Prototype: report declarations whose name contains this "
                   "string as mergeEquivalentVariants un-claims them"));

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
      OS << '\t' << TD->getUnderlyingType().getCanonicalType().getAsString(Policy);
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
    // kept its own definition from that it was handed another target's: the
    // merge did exactly that for 147 declarations per translation unit and
    // every check passed. Both compilations parse the same source, so a body
    // that is the same body has the same range.
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

/// Revert \p D and everything it encloses from the primary target to every
/// target. Mirrors the recursion that claimed them: a TemplateDecl is not a
/// DeclContext, so its templated declaration and parameter list have to be
/// reached explicitly.
static void unclaimTree(Decl *D, unsigned &Count) {
  if (!D || D->getTargetVariant() != 1)
    return;
  if (LLVM_UNLIKELY(!clang::TraceUnclaim.empty()))
    if (auto *ND = dyn_cast<NamedDecl>(D))
      if (StringRef(ND->getNameAsString()).contains(clang::TraceUnclaim))
        llvm::errs() << "unclaim " << ND->getDeclKindName() << " "
                     << ND->getNameAsString() << " @ "
                     << ND->getLocation().printToString(
                            ND->getASTContext().getSourceManager())
                     << "\n";
  D->setTargetVariant(0);
  ++Count;
  if (auto *TD = dyn_cast<TemplateDecl>(D)) {
    unclaimTree(TD->getTemplatedDecl(), Count);
    if (TemplateParameterList *TPL = TD->getTemplateParameters())
      for (NamedDecl *P : *TPL)
        unclaimTree(P, Count);
  }
  if (auto *DC = dyn_cast<DeclContext>(D))
    for (Decl *Sub : DC->decls())
      unclaimTree(Sub, Count);
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
  llvm::hash_code H = llvm::hash_combine(
      S->getStmtClass(), P.isValid() ? P.getLine() : 0,
      P.isValid() ? P.getColumn() : 0);
  for (const Stmt *C : S->children())
    H = llvm::hash_combine(H, bodyShape(SM, C));
  return H;
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
static bool isInterchangeable(ASTContext &Ctx, const Decl *A, const Decl *B) {
  // Decl::getBody() returns null for a TemplateDecl -- the body belongs to the
  // declaration it wraps -- so a template pair compared as "neither has a body"
  // and merged unconditionally. That is how `internal::cast_from_f8` was
  // un-claimed despite picking __clz or __builtin_clz per target, and because
  // the merge runs before PerformPendingInstantiations, all seven of its
  // instantiations then inherited variant 0 and carried the device's body into
  // the host's view.
  if (const auto *TA = dyn_cast<TemplateDecl>(A))
    if (const auto *TB = dyn_cast<TemplateDecl>(B))
      if (TA->getTemplatedDecl() && TB->getTemplatedDecl())
        if (!isInterchangeable(Ctx, TA->getTemplatedDecl(),
                               TB->getTemplatedDecl()))
          return false;

  if (A->hasAttr<CUDADeviceAttr>() != B->hasAttr<CUDADeviceAttr>() ||
      A->hasAttr<CUDAHostAttr>() != B->hasAttr<CUDAHostAttr>() ||
      A->hasAttr<CUDAGlobalAttr>() != B->hasAttr<CUDAGlobalAttr>())
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

  if (!SameBody(A->getBody(), B->getBody()))
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
    NamedDecl *A = nullptr, *B = nullptr;
    for (NamedDecl *D : Entry.second) {
      if (D->getTargetVariant() == 1 && !A)
        A = D;
      else if (D->getTargetVariant() == 2 && !B)
        B = D;
    }
    // An implicit instantiation that the aux target never got a copy of is not
    // target-specific -- it is shared, and claimed only because the primary
    // happened to be current when it was instantiated. A specialization is
    // found through the template's FoldingSet, not by name lookup, so the
    // target filter never sees it and the aux target silently reuses the
    // primary's: 116 declarations, `__is_integer_nonstrict<char>` and the like,
    // present in the device's view and absent from the host's.
    if (A && !B) {
      const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(A);
      if (CTSD && CTSD->getSpecializationKind() == TSK_ImplicitInstantiation) {
        if (LLVM_UNLIKELY(!clang::TraceUnclaim.empty()))
          llvm::errs() << "unclaim-root(unpaired) " << Entry.first() << "\n";
        unclaimTree(A, Count);
      }
      continue;
    }
    if (!A || !B)
      continue;
    StructuralEquivalenceContext SEC(
        Ctx.getLangOpts(), Ctx, Ctx, NonEquiv,
        StructuralEquivalenceKind::Default, /*StrictTypeSpelling=*/false,
        /*Complain=*/false);
    // The copy B is left in place. Hiding it as well was tried: it removes the
    // 668-1,690 redundant duplicates but takes 633 declarations the host needs
    // with them (missing 126 -> 759), because the copy's subtree is not the
    // same shape as the original's. A duplicate is a lesser defect than an
    // absence.
    if (!SEC.IsEquivalent(A, B) || !isInterchangeable(Ctx, A, B))
      continue;
    if (LLVM_UNLIKELY(!clang::TraceUnclaim.empty()))
      llvm::errs() << "unclaim-root(paired) " << Entry.first() << "\n";
    unclaimTree(A, Count);
    // A CXXRecordDecl/ClassTemplateDecl's subtree can contain an implicit
    // specialization reached only through its template's FoldingSet (see the
    // comment on the unpaired-A case above): hiding it is unsafe without
    // -hide-redundant-variants' documented 633-declaration loss. A function
    // or variable's subtree contains no such node -- a FunctionTemplateDecl's
    // specializations live in Common->Specializations, never in
    // DeclContext::decls() -- so hiding it is always safe once A/B are
    // confirmed interchangeable.
    if (clang::HideRedundantVariants || isa<FunctionDecl>(B) ||
        isa<FunctionTemplateDecl>(B) || isa<VarDecl>(B) ||
        isa<VarTemplateDecl>(B))
      markRedundantTree(B, 2);
  }
  return Count;
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
    StructuralEquivalenceContext SEC(
        Ctx.getLangOpts(), Ctx, Ctx, NonEquiv,
        StructuralEquivalenceKind::Default, /*StrictTypeSpelling=*/false,
        /*Complain=*/false);
    if (SEC.IsEquivalent(A, B)) {
      if (!isInterchangeable(Ctx, A, B)) {
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
