//===- TargetCopies.cpp - Finding and evaluating target copies ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Finds the copies of a declaration made for each target, and the definition,
// body, variable or default argument each target evaluates.
//
//===----------------------------------------------------------------------===//

#include "MultiTargetInternal.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/MultiTargetContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/TargetCopyMerge.h"

using namespace clang;
using namespace clang::multitarget;

//===----------------------------------------------------------------------===//
// Target dependence
//===----------------------------------------------------------------------===//

bool multitarget::isWidenedCopy(const Decl *D) {
  return D->getTargetVariant() && !D->isRedundantTargetVariant() &&
         !D->isTargetVariantReparseOrigin();
}

/// A value-like declaration parsed once per target from tokens that differ
/// between targets: a widened copy (or the shared declaration such copies
/// stand beside). Re-parsed users of such declarations are copies too, but
/// they diverge only through what they name, which getTargetDependence
/// follows. What is evaluated at run time is not a source: each target's
/// CodeGen resolves it by name.
bool MultiTargetContext::isTargetDivergentSource(const NamedDecl *ND) const {
  if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(ND)) {
    if (!FTD->getTemplatedDecl()->isConstexpr())
      return false;
  } else if (const auto *FD = dyn_cast<FunctionDecl>(ND)) {
    if (!FD->isConstexpr())
      return false;
  } else if (const auto *VD = dyn_cast<VarDecl>(ND)) {
    if (VD->isLocalVarDeclOrParm() || !VD->isUsableInConstantExpressions(Ctx))
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

void MultiTargetContext::keepTargetCopiesApart(const Decl *D) {
  TargetCopiesKeptApart.insert(D);
  // A template's declaration is not in its DeclContext chain.
  if (const auto *TD = dyn_cast<TemplateDecl>(D))
    if (const Decl *Templated = TD->getTemplatedDecl())
      TargetCopiesKeptApart.insert(Templated);
}

void MultiTargetContext::getTargetCopies(
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
            isTargetCopyOf(Ctx, ND, Copy, /*SameScope=*/OwnerCopy == Owner))
          Copies.push_back(Copy);
    return;
  }
  for (const NamedDecl *Copy : DC->lookup(ND->getDeclName()))
    if (Copy == ND || isTargetCopyOf(Ctx, ND, Copy, /*SameScope=*/true))
      Copies.push_back(Copy);
}

const NamedDecl *MultiTargetContext::getCopyFor(const NamedDecl *ND,
                                                unsigned Variant) const {
  SmallVector<const NamedDecl *, 4> Copies;
  getTargetCopies(ND, Copies);
  for (const NamedDecl *Copy : Copies)
    if (Copy->isCopyFor(Variant))
      return Copy;
  return nullptr;
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

const MultiTargetContext::TargetMemberDefinitions *
MultiTargetContext::getTargetMemberDefinitions(const FunctionDecl *FD) const {
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

  unsigned NumVariants = 2 + Ctx.getNumAuxTargets();
  SmallVector<const FunctionDecl *, 4> ByVariant(NumVariants, nullptr);
  SmallVector<const NamedDecl *, 4> Classes;
  getTargetCopies(RD, Classes);
  // Evaluating one copy's member against another copy's object relies on the
  // two having the same fields and bases.
  auto SameBases = [&](const CXXRecordDecl *A, const CXXRecordDecl *B) {
    return A->getNumBases() == B->getNumBases() &&
           llvm::all_of(llvm::zip(A->bases(), B->bases()), [&](auto Pair) {
             return Ctx.hasSameType(std::get<0>(Pair).getType(),
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

  bool All = true;
  for (unsigned V : Ctx.getTargetVariants())
    All &= definitionForVariant(Ctx, ByVariant, V) != nullptr;
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

  const SourceManager &SM = Ctx.getSourceManager();
  Result.Dependence = partitionTargetsByAnswer([&](unsigned V) {
    return definitionShape(SM, definitionForVariant(Ctx, ByVariant, V));
  });
  Result.ByVariant = std::move(ByVariant);
  return &Result;
}

/// Whether \p Copy, one of a declaration's target copies, is the host's. A
/// shared copy is the host's where there is a host.
bool multitarget::isHostsCopy(const ASTContext &Ctx, const NamedDecl *Copy) {
  return Copy->getTargetVariant() ? Copy->isCopyFor(Ctx.getHostVariant())
                                  : Ctx.getHostVariant() != 0;
}

/// Whether \p Copy is a device's. A shared copy is where there is no host.
bool multitarget::isDevicesCopy(const ASTContext &Ctx, const NamedDecl *Copy) {
  if (!Copy->getTargetVariant())
    return !Ctx.getHostVariant();
  return Ctx.isCopyForDevice(Copy) || Copy->isRedundantTargetVariant();
}

MultiTargetContext::TargetDependenceKind
MultiTargetContext::getMemberTargetDependence(const FunctionDecl *FD) const {
  return getTargetDependenceKind(getMemberTargetPartition(FD));
}

TargetPartition
MultiTargetContext::getMemberTargetPartition(const FunctionDecl *FD) const {
  if (LLVM_LIKELY(!clang::AllowTargetVariantDecls))
    return 0;
  const TargetMemberDefinitions *Defs = getTargetMemberDefinitions(FD);
  return Defs ? Defs->Dependence : 0;
}

const FunctionDecl *MultiTargetContext::getTargetDefinitionForEvaluation(
    const FunctionDecl *FD, const FunctionDecl *Definition,
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
    if (InConstantContext && LLVM_UNLIKELY(TargetInputHook)) {
      SmallVector<const NamedDecl *, 4> Copies;
      getTargetCopies(CTD, Copies);
      recordTargetInput('E', partitionTargetsBy([&](unsigned A, unsigned B) {
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
  // evaluated for, or, for a device without one, the devices' copy. Also where
  // a target's code generation evaluates it.
  if (Ctx.MultiTargetCompile && FD->getTargetVariant() &&
      !FD->isRedundantTargetVariant() && !FD->getDeclContext()->isRecord()) {
    unsigned Target = getInstantiationTarget();
    SmallVector<const NamedDecl *, 4> Copies;
    getTargetCopies(FD, Copies);
    if (FD->isCopyFor(Target)) {
      // The code evaluating it means what the copy each target binds
      // computes, also where that is the copy it bound ('e', see
      // Sema::initTargetInputs). A copy re-parsed for some targets computes
      // what it does for them, even where its source is the others'.
      if (InConstantContext)
        if (TargetPartition Bound = refineTargetPartition(
                getTargetPartition(FD),
                partitionTargetsBy([&](unsigned A, unsigned B) {
                  return copyForVariant(Copies, A) == copyForVariant(Copies, B);
                })))
          recordTargetInput('e', Bound, FD);
      return nullptr;
    }
    const FunctionDecl *Own = nullptr, *StandIn = nullptr;
    for (const NamedDecl *Copy : Copies) {
      const FunctionDecl *CopyFD = Copy->getAsFunction();
      unsigned V = Copy->getTargetVariant();
      if (!CopyFD || V == Decl::TargetVariantRedundant)
        continue;
      if (Copy->isCopyFor(Target))
        Own = CopyFD;
      else if (Ctx.isDeviceVariant(Target) &&
               (V == 0 || Copy->isCopyFor(Ctx.getCanonicalDeviceVariant())))
        StandIn = CopyFD;
    }
    if (!Own)
      Own = StandIn;
    const FunctionDecl *OwnDefinition = nullptr;
    if (!Own || !Own->getBody(OwnDefinition))
      return nullptr;
    if (InConstantContext)
      recordTargetInput('E', reconcileTargetPartition(0, TDK_Arch), FD);
    return OwnDefinition != Definition ? OwnDefinition : nullptr;
  }
  const TargetMemberDefinitions *Defs = getTargetMemberDefinitions(FD);
  TargetDependenceKind Dependence =
      Defs ? getTargetDependenceKind(Defs->Dependence) : TDK_None;
  if (Dependence == TDK_None)
    return nullptr;
  if (InConstantContext)
    recordTargetInput(
        'E',
        refineTargetPartition(Defs->Dependence,
                              reconcileTargetPartition(0, TDK_Side)),
        FD);
  const FunctionDecl *Own =
      definitionForVariant(Ctx, Defs->ByVariant, getInstantiationTarget());
  return Own && Own != Definition ? Own : nullptr;
}

void MultiTargetContext::setTargetDefaultArgument(const TemplateDecl *Template,
                                                  const NamedDecl *Param,
                                                  unsigned Variant,
                                                  NamedDecl *Other) {
  TargetDefaultArguments[{Param, Variant}] = Other;
  TemplatesWithTargetDefaultArguments.insert(Template);
}

NamedDecl *
MultiTargetContext::getTargetDefaultArgument(const NamedDecl *Param,
                                             unsigned Variant) const {
  if (TargetDefaultArguments.empty())
    return nullptr;
  return TargetDefaultArguments.lookup({Param, Variant});
}

MultiTargetContext::TargetDependenceKind
MultiTargetContext::getDefaultArgumentTargetDependence(
    const TemplateDecl *TD) const {
  return getTargetDependenceKind(getDefaultArgumentTargetPartition(TD));
}

TargetPartition MultiTargetContext::getDefaultArgumentTargetPartition(
    const TemplateDecl *TD) const {
  if (!TemplatesWithTargetDefaultArguments.contains(TD))
    return 0;
  const SourceManager &SM = Ctx.getSourceManager();
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

bool MultiTargetContext::isArchDivergentFunction(const NamedDecl *D) const {
  if (!D->getTargetVariant() || D->isRedundantTargetVariant() ||
      !D->getAsFunction() || Ctx.getNumDeviceTargets() < 2)
    return false;
  auto OnDevice = [](const FunctionDecl *FD) {
    return FD->hasAttr<CUDADeviceAttr>() || FD->hasAttr<CUDAGlobalAttr>();
  };
  // A template's copies each name their own template parameters.
  auto SameType = [&](QualType A, QualType B) {
    if (Ctx.hasSameType(A, B))
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
    if (!Ctx.isCopyForDevice(Copy))
      continue;
    const FunctionDecl *FD = Copy->getAsFunction();
    for (unsigned V : Ctx.getTargetVariants())
      DevicesCovered += Ctx.isDeviceVariant(V) && Copy->isCopyFor(V);
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
         DevicesCovered != Ctx.getNumDeviceTargets();
}

Stmt *
MultiTargetContext::getTargetBodyForEvaluation(const FunctionDecl *Definition,
                                               bool InConstantContext) const {
  if (LLVM_LIKELY(TargetBodies.empty()))
    return nullptr;
  ArrayRef<std::pair<TargetSet, Stmt *>> Bodies = getTargetBodies(Definition);
  if (Bodies.empty())
    return nullptr;
  if (InConstantContext)
    recordTargetInput('E', partitionTargetsBy([&](unsigned A, unsigned B) {
                        return getBodyForTarget(Definition, A) ==
                               getBodyForTarget(Definition, B);
                      }),
                      Definition);
  Stmt *Body = getBodyForTarget(Definition, getInstantiationTarget());
  return Body != Definition->getBody() ? Body : nullptr;
}

/// The copy of an entity in \p Copies that \p Variant sees: its own, else a
/// shared one.
const NamedDecl *multitarget::copyForVariant(ArrayRef<const NamedDecl *> Copies,
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
TargetPartition multitarget::sourceTargetPartition(const ASTContext &Ctx,
                                                   const NamedDecl *ND) {
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
  Ctx.getMultiTarget().getTargetCopies(ND, Copies);
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
  MultiTargetContext::TargetDependenceKind Kind =
      DevicesDiffer                        ? MultiTargetContext::TDK_Arch
      : Host && Device && *Host == *Device ? MultiTargetContext::TDK_None
                                           : MultiTargetContext::TDK_Side;
  if (Kind == MultiTargetContext::TDK_None)
    return 0;
  return Ctx.getMultiTarget().reconcileTargetPartition(
      Ctx.getMultiTarget().partitionTargetsByAnswer(
          [&](unsigned V) -> llvm::hash_code {
            const NamedDecl *Copy = copyForVariant(Copies, V);
            return Copy
                       ? llvm::hash_value(Fingerprint(Copy).computeStableHash())
                       : llvm::hash_code(0);
          }),
      Kind);
}

const VarDecl *MultiTargetContext::getTargetVariableForEvaluation(
    const VarDecl *VD, bool InConstantContext) const {
  if (LLVM_LIKELY(!clang::AllowTargetVariantDecls) ||
      !VD->isStaticDataMember() || !Ctx.MultiTargetCompile)
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
    recordTargetInput('E', sourceTargetPartition(Ctx, VD), VD);
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
