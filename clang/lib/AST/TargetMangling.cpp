//===- TargetMangling.cpp - Mangling equivalence of target copies ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Gives specializations keyed by target, and classes copied per target, the
// names one compilation for each target would give them.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/MultiTargetContext.h"
#include "llvm/ADT/FoldingSet.h"

using namespace clang;

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
  Ctx.getMultiTarget().getTargetCopies(D, Copies);
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
    bool Keyed = Ctx.getMultiTarget().getTargetInstantiationKey(
                     CTSD->getCanonicalDecl()) ||
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
MultiTargetContext::getManglingEquivalentImpl(const CXXRecordDecl *RD) const {
  if (!LLVM_UNLIKELY(clang::AllowTargetVariantDecls))
    return RD;
  RD = RD->getCanonicalDecl();
  if (auto It = ManglingEquivalents.find(RD); It != ManglingEquivalents.end())
    return It->second;
  llvm::FoldingSetNodeID ID;
  const CXXRecordDecl *Equivalent = RD;
  if (profileManglingShape(Ctx, RD, ID, 0)) {
    llvm::BumpPtrAllocator Scratch;
    llvm::FoldingSetNodeIDRef Ref = ID.Intern(Scratch);
    StringRef Shape(reinterpret_cast<const char *>(Ref.data()),
                    Ref.size() * sizeof(unsigned));
    Equivalent = ManglingEquivalentByShape.try_emplace(Shape, RD).first->second;
  }
  ManglingEquivalents[RD] = Equivalent;
  return Equivalent;
}

QualType MultiTargetContext::getManglingEquivalentImpl(QualType T) const {
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
        Result = Ctx.getCanonicalTagType(Equivalent);
    } else if (const auto *PT = dyn_cast<PointerType>(Unqualified)) {
      QualType Pointee = getManglingEquivalent(PT->getPointeeType());
      if (Pointee != PT->getPointeeType())
        Result = Ctx.getCanonicalType(Ctx.getPointerType(Pointee));
    } else if (const auto *RT = dyn_cast<ReferenceType>(Unqualified)) {
      QualType Pointee = getManglingEquivalent(RT->getPointeeTypeAsWritten());
      if (Pointee != RT->getPointeeTypeAsWritten())
        Result = Ctx.getCanonicalType(
            isa<LValueReferenceType>(RT) ? Ctx.getLValueReferenceType(Pointee)
                                         : Ctx.getRValueReferenceType(Pointee));
    }
    // Inserting into the map may have moved it.
    It = ManglingEquivalentTypes.find(Unqualified);
    It->second = Result;
  }
  return It->second.isNull()
             ? T
             : Ctx.getQualifiedType(It->second, C.getLocalQualifiers());
}
