//===- TargetShape.cpp - Shapes of target copies -----------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Hashes of what a body, a default argument, a record or a layout is, as
// written, so copies made for different targets can be compared.
//
//===----------------------------------------------------------------------===//

#include "MultiTargetInternal.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"

using namespace clang;
using namespace clang::multitarget;

/// What a template parameter's default argument is, as written: two copies of
/// it parsed from different tokens, e.g. a macro defined differently per
/// target, differ.
llvm::hash_code multitarget::defaultArgumentShape(const SourceManager &SM,
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
llvm::hash_code multitarget::bodyShape(const SourceManager &SM, const Stmt *S) {
  if (!S)
    return llvm::hash_code(0);
  BodyShapeHasher Hasher(SM);
  Hasher.TraverseStmt(const_cast<Stmt *>(S));
  return Hasher.Hash;
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

std::string multitarget::layoutAttributes(const ASTContext &Ctx,
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
std::string multitarget::dependentUsingScope(const ASTContext &Ctx,
                                             const Decl *D) {
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

/// What evaluating \p Def computes, as far as its source shows.
llvm::hash_code multitarget::definitionShape(const SourceManager &SM,
                                             const FunctionDecl *Def) {
  llvm::hash_code H = bodyShape(SM, Def->getBody());
  if (const auto *Ctor = dyn_cast<CXXConstructorDecl>(Def))
    for (const CXXCtorInitializer *Init : Ctor->inits())
      if (Init->isWritten())
        H = llvm::hash_combine(H, bodyShape(SM, Init->getInit()));
  return H;
}

/// A hash of what \p RD declares: bases, members and their types, typedefs,
/// static initializers unless \p StaticInitializers is false, nested classes
/// and what using-declarations introduce, but not function bodies other than
/// constexpr friends', nor the members a class declares implicitly where they
/// are used.
llvm::hash_code multitarget::recordShape(const ASTContext &Ctx,
                                         const CXXRecordDecl *RD,
                                         bool StaticInitializers) {
  if (!RD || !(RD = RD->getDefinition()))
    return llvm::hash_code(0);
  PrintingPolicy Policy = Ctx.getPrintingPolicy();
  auto TypeStr = [&](QualType T) { return T.getAsString(Policy); };
  llvm::hash_code H = llvm::hash_value(RD->getNumBases());
  for (const CXXBaseSpecifier &Base : RD->bases())
    H = llvm::hash_combine(H, TypeStr(Base.getType()));
  for (const Decl *Member : RD->decls()) {
    // A static_assert declares nothing.
    if (Member->isImplicit() || isa<StaticAssertDecl>(Member))
      continue;
    H = llvm::hash_combine(H, Member->getKind());
    if (const auto *ND = dyn_cast<NamedDecl>(Member))
      H = llvm::hash_combine(H, ND->getNameAsString());
    if (const auto *TND = dyn_cast<TypedefNameDecl>(Member)) {
      H = llvm::hash_combine(H, TypeStr(TND->getUnderlyingType()));
    } else if (const auto *VD = dyn_cast<ValueDecl>(Member)) {
      H = llvm::hash_combine(H, TypeStr(VD->getType()));
      // What overload resolution reads besides the type.
      if (const auto *FD = dyn_cast<FunctionDecl>(VD))
        H = llvm::hash_combine(H, FD->isDeleted(),
                               ExplicitSpecifier::getFromDecl(FD).getKind());
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
      for (const NamedDecl *Param : *FTD->getTemplateParameters())
        H = llvm::hash_combine(
            H, Param->getKind(),
            defaultArgumentShape(Ctx.getSourceManager(), Param));
    } else if (const auto *Friend = dyn_cast<FriendDecl>(Member)) {
      // A friend function is found through its class, and a constexpr one's
      // body is evaluated where it is called.
      const NamedDecl *ND = Friend->getFriendDecl();
      if (const FunctionDecl *FD = ND ? ND->getAsFunction() : nullptr) {
        H = llvm::hash_combine(H, FD->getNameAsString(),
                               TypeStr(FD->getType()));
        if (FD->isConstexpr() && FD->doesThisDeclarationHaveABody())
          H = llvm::hash_combine(
              H, bodyShape(Ctx.getSourceManager(), FD->getBody()));
      }
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
