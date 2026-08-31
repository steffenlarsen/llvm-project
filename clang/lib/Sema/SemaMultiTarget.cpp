//===--- SemaMultiTarget.cpp - Semantic analysis for several targets -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The target code being analysed is compiled for, and the types and members
// it names for that target.
//
//===----------------------------------------------------------------------===//

#include "clang/Sema/SemaMultiTarget.h"
#include "SemaLookupTarget.h"
#include "TargetInputRecorder.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/MultiTargetContext.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaCUDA.h"
#include "clang/Sema/Template.h"
#include "llvm/Support/SaveAndRestore.h"

using namespace clang;

SemaMultiTarget::SemaMultiTarget(Sema &S) : SemaBase(S) {}

SemaMultiTarget::~SemaMultiTarget() = default;

void SemaMultiTarget::setHostViewReparse(bool On) {
  if (On == InHostViewReparse)
    return;
  InHostViewReparse = On;
  MultiTargetContext &MT = SemaRef.Context.getMultiTarget();
  if (On)
    SavedHostViewOfDeviceCode = MT.isHostViewOfDeviceCode();
  MT.setHostViewOfDeviceCode(On || SavedHostViewOfDeviceCode);
}

unsigned SemaMultiTarget::getInstantiationTarget() const {
  if (ForcedInstantiationTarget)
    return ForcedInstantiationTarget;
  unsigned Ambient = SemaRef.Context.getCurrentTargetVariant();
  for (const DeclContext *DC = SemaRef.CurContext; DC; DC = DC->getParent()) {
    const auto *D = cast<Decl>(DC);
    if (unsigned Key =
            SemaRef.Context.getMultiTarget().getTargetInstantiationKey(D))
      return Key;
    const auto *FD = dyn_cast<FunctionDecl>(D);
    if (!FD)
      continue;
    // A host function is host code, even where it is analysed for a device
    // for the sake of a lambda in it. Without a host it is the ambient
    // target's.
    if (!isLambdaCallOperator(FD) && !FD->hasAttr<CUDADeviceAttr>() &&
        !FD->hasAttr<CUDAGlobalAttr>()) {
      // One parsed from a device's own alternative is that device's code. Its
      // class's members were declared with the device's types, as a separate
      // device compilation would check it.
      if (InTargetAlternativeRegion &&
          SemaRef.Context.isDeviceVariant(Ambient) &&
          !FD->isTemplateInstantiation())
        return Ambient;
      return SemaRef.Context.getHostVariant() ? SemaRef.Context.getHostVariant()
                                              : std::max(Ambient, 1u);
    }
    // A copy for the host and some devices is compiled for the devices, as
    // device code shared by every target is.
    if (unsigned V = FD->getTargetVariant();
        V && !FD->isRedundantTargetVariant())
      return FD->isCopyFor(SemaRef.Context.getHostVariant()) &&
                     !FD->getTargetSet().isSingle()
                 ? SemaRef.Context.getDevices(FD->getTargetSet()).first()
                 : V;
    // The host's view of device code is the host's. A member of a
    // specialization keyed by target is that target's, unless the host's or
    // every device's is reached by a device.
    if (FD->hasAttr<CUDAGlobalAttr>()
            ? !SemaRef.Context.getMultiTarget().areKernelsKeyedForHost()
            : FD->hasAttr<CUDADeviceAttr>() && !FD->hasAttr<CUDAHostAttr>()) {
      const Decl *Keyed = nullptr;
      if (unsigned Key = SemaRef.Context.getMultiTarget()
                             .getEffectiveTargetInstantiationKey(
                                 cast<Decl>(FD->getDeclContext()), &Keyed))
        return SemaRef.Context.isDeviceVariant(Ambient) &&
                       ((Key == SemaRef.Context.getHostVariant() &&
                         !SemaRef.Context.getMultiTarget()
                              .isHostViewOfDeviceCode()) ||
                        SemaRef.Context.getMultiTarget().isSharedByDevices(
                            Keyed))
                   ? Ambient
                   : Key;
      return SemaRef.Context.isDeviceVariant(Ambient) ? Ambient
             : SemaRef.Context.getMultiTarget().isHostViewOfDeviceCode()
                 ? SemaRef.Context.getHostVariant()
                 : SemaRef.Context.getCanonicalDeviceVariant();
    }
    // A lambda is compiled for whatever its enclosing function is.
    if (!isLambdaCallOperator(FD))
      break;
  }
  if (Ambient > 1)
    return Ambient;
  if (!SemaRef.CodeSynthesisContexts.empty() &&
      SemaRef.CodeSynthesisContexts.back().InstantiationTarget)
    return SemaRef.CodeSynthesisContexts.back().InstantiationTarget;
  return std::max(Ambient, 1u);
}

/// Whether \p D is one target's copy of a class or class template whose
/// layout differs between targets, or a field of one.
static bool isTargetDivergentLayoutEntity(const ASTContext &Ctx,
                                          const NamedDecl *D) {
  if (isa<RecordDecl, ClassTemplateDecl>(D))
    return Ctx.getMultiTarget().hasTargetDivergentLayout(D);
  return isa<FieldDecl>(D) && Ctx.getMultiTarget().hasTargetDivergentLayout(
                                  cast<RecordDecl>(D->getDeclContext()));
}

bool SemaMultiTarget::namesTargetDivergentLayout(QualType T) const {
  if (T.isNull() || !SemaRef.Context.isMultiTargetCompile())
    return false;
  T = T.getCanonicalType().getNonReferenceType();
  while (true) {
    if (const ArrayType *AT = SemaRef.Context.getAsArrayType(T))
      T = AT->getElementType();
    else if (const auto *PT = T->getAs<PointerType>())
      T = PT->getPointeeType();
    else
      break;
  }
  const RecordDecl *RD = T->getAsRecordDecl();
  if (!RD)
    return false;
  if (RD->getTargetVariant())
    return SemaRef.Context.getMultiTarget().hasTargetDivergentLayout(RD);
  const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RD);
  return CTSD && CTSD->getSpecializedTemplate()->getTargetVariant() &&
         SemaRef.Context.getMultiTarget().hasTargetDivergentLayout(
             CTSD->getSpecializedTemplate());
}

const FunctionDecl *
SemaMultiTarget::getTargetSpecializationMember(const FunctionDecl *FD,
                                               unsigned Target) {
  const auto *Old =
      dyn_cast<ClassTemplateSpecializationDecl>(FD->getDeclContext());
  if (!Old)
    return nullptr;
  ClassTemplateDecl *CTD = Old->getSpecializedTemplate();
  ArrayRef<TemplateArgument> Args = Old->getTemplateArgs().asArray();
  SourceLocation Loc = FD->getLocation();
  ASTContext::TargetScope Scope(SemaRef.Context, Target);
  // Once the translation unit is analysed, only what exists can be used.
  bool Analysing = SemaRef.CurScope != nullptr;
  ClassTemplateSpecializationDecl *New = nullptr;
  if (Analysing) {
    TemplateArgumentListInfo ArgInfo(Loc, Loc);
    for (const TemplateArgument &Arg : Args)
      ArgInfo.addArgument(
          SemaRef.getTrivialTemplateArgumentLoc(Arg, QualType(), Loc));
    QualType NewType = SemaRef.CheckTemplateIdType(
        ElaboratedTypeKeyword::None, TemplateName(CTD), Loc, ArgInfo,
        /*Scope=*/nullptr, /*ForNestedNameSpecifier=*/false);
    struct : Sema::TypeDiagnoser {
      void diagnose(Sema &, SourceLocation, QualType) override {}
    } Silent;
    if (NewType.isNull() || SemaRef.RequireCompleteType(Loc, NewType, Silent))
      return nullptr;
    New = dyn_cast<ClassTemplateSpecializationDecl>(NewType->getAsRecordDecl());
  } else {
    llvm::FoldingSetInsertToken InsertToken;
    New = CTD->findSpecialization(Args, InsertToken);
  }
  if (!New || New == Old || !New->isCompleteDefinition())
    return nullptr;
  // Each target's copy of the template replays the same tokens.
  for (Decl *Member : New->decls()) {
    auto *MemberFD = dyn_cast<FunctionDecl>(Member);
    if (!MemberFD || MemberFD->getKind() != FD->getKind() ||
        MemberFD->getLocation() != FD->getLocation() ||
        MemberFD->getNameAsString() != FD->getNameAsString())
      continue;
    const FunctionDecl *Def = nullptr;
    if (!MemberFD->isDefined(Def) && Analysing && MemberFD->isConstexpr())
      SemaRef.InstantiateFunctionDefinition(Loc, MemberFD);
    return MemberFD->isDefined(Def) ? Def : nullptr;
  }
  return nullptr;
}

NamedDecl *SemaMultiTarget::getInstantiationTargetCopy(NamedDecl *D) {
  if (!SemaRef.Context.isMultiTargetCompile() ||
      D->getDeclContext()->isDependentContext() ||
      !(SemaRef.Context.getMultiTarget().isTargetDivergentSource(D) ||
        isTargetDivergentLayoutEntity(SemaRef.Context, D)))
    return nullptr;
  unsigned Target =
      MemberCopyTarget ? MemberCopyTarget : getInstantiationTarget();
  if (D->isCopyFor(Target))
    return nullptr;
  return const_cast<NamedDecl *>(
      SemaRef.Context.getMultiTarget().getCopyFor(D, Target));
}

void SemaMultiTarget::joinTargetClassFamily(CXXRecordDecl *New) {
  ASTContext &Ctx = SemaRef.Context;
  unsigned Variant = Ctx.getDeclTagVariant();
  if (!Ctx.isMultiTargetCompile() || !Variant || !New->getDeclName())
    return;
  for (NamedDecl *Found :
       New->getDeclContext()->getRedeclContext()->lookup(New->getDeclName())) {
    auto *RD = dyn_cast<CXXRecordDecl>(Found);
    if (!RD || RD->isInjectedClassName() || RD->getDescribedClassTemplate() ||
        isa<ClassTemplateSpecializationDecl>(RD) ||
        RD->getCanonicalDecl() == New->getCanonicalDecl() ||
        RD->isRedundantTargetVariant() ||
        (RD->getTargetVariant() && RD->isCopyFor(Variant)))
      continue;
    Ctx.getMultiTarget().addToTargetFamily(New, Variant, RD);
    return;
  }
}

bool SemaMultiTarget::isAnalysedAsHostWould() const {
  // Where code analysed for every target is the host's view, code re-parsed
  // for a device is that device's. A device's first parse of device code is
  // the host's view too where the host does not parse it again.
  if (SemaRef.Context.isMultiTargetCompile() && InTargetFallbackReparse &&
      !SemaRef.Context.InFirstParse &&
      SemaRef.Context.isDeviceVariant(
          SemaRef.Context.getCurrentTargetVariant()))
    return false;
  for (const DeclContext *DC = SemaRef.CurContext; DC; DC = DC->getParent()) {
    const auto *D = cast<Decl>(DC);
    if (unsigned Key = SemaRef.Context.getMultiTarget().getSpecializationKey(D))
      return Key == SemaRef.Context.getHostVariant();
    const auto *FD = dyn_cast<FunctionDecl>(D);
    if (!FD)
      continue;
    if (FD->getTargetVariant() && !FD->isRedundantTargetVariant())
      return FD->isCopyFor(SemaRef.Context.getHostVariant());
    if (isLambdaCallOperator(FD))
      continue;
    if (FD->isTemplateInstantiation())
      return HostViewTrustedShared.count(FD->getCanonicalDecl());
    return true;
  }
  return true;
}

bool SemaMultiTarget::isDeducedFromUse(unsigned Depth, unsigned Index) const {
  if (!DeducingTemplate || SemaRef.CodeSynthesisContexts.empty())
    return false;
  const Sema::CodeSynthesisContext &Ctx = SemaRef.CodeSynthesisContexts.back();
  return Ctx.Kind ==
             Sema::CodeSynthesisContext::DeducedTemplateArgumentSubstitution &&
         Ctx.Entity == DeducingTemplate &&
         Depth == DeducingTemplate->getTemplateParameters()->getDepth() &&
         Index >= DeducingNumExplicit;
}

bool SemaMultiTarget::reanalysesHostCode() const {
  return SemaRef.Context.hasTargetDivergence();
}

bool SemaMultiTarget::isDeviceViewOfHostCode(const FunctionDecl *FD) const {
  return FD && InTargetAlternativeRegion && reanalysesHostCode() &&
         SemaRef.Context.isDeviceVariant(
             SemaRef.Context.getCurrentTargetVariant()) &&
         SemaRef.CUDA().IdentifyTarget(FD) == CUDAFunctionTarget::Host;
}

bool SemaMultiTarget::inTemplateHeader() const {
  const Scope *TemplateParams =
      SemaRef.CurScope ? SemaRef.CurScope->getTemplateParamParent() : nullptr;
  return TemplateParams && !TemplateParams->decl_empty();
}

/// The type \p CTD names with \p Args.
static QualType formClassType(Sema &S, ClassTemplateDecl *CTD,
                              ArrayRef<TemplateArgument> Args,
                              SourceLocation Loc) {
  TemplateArgumentListInfo ArgInfo(Loc, Loc);
  for (const TemplateArgument &Arg : Args) {
    if (Arg.getKind() != TemplateArgument::Pack) {
      ArgInfo.addArgument(
          S.getTrivialTemplateArgumentLoc(Arg, QualType(), Loc));
      continue;
    }
    for (const TemplateArgument &Element : Arg.pack_elements())
      ArgInfo.addArgument(
          S.getTrivialTemplateArgumentLoc(Element, QualType(), Loc));
  }
  return S.CheckTemplateIdType(ElaboratedTypeKeyword::None, TemplateName(CTD),
                               Loc, ArgInfo, /*Scope=*/nullptr,
                               /*ForNestedNameSpecifier=*/false);
}

unsigned SemaMultiTarget::getWrittenArgumentCount(
    const ClassTemplateSpecializationDecl *CTSD, unsigned Target,
    SourceLocation Loc) {
  ArrayRef<TemplateArgument> Args = CTSD->getTemplateArgs().asArray();
  ClassTemplateDecl *CTD = CTSD->getSpecializedTemplate();
  TemplateParameterList *Params = CTD->getTemplateParameters();
  auto HasDefault = [](const NamedDecl *Param) {
    if (const auto *TTP = dyn_cast<TemplateTypeParmDecl>(Param))
      return TTP->hasDefaultArgument();
    if (const auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(Param))
      return NTTP->hasDefaultArgument();
    return cast<TemplateTemplateParmDecl>(Param)->hasDefaultArgument();
  };
  unsigned NumArgs = Args.size();
  if (!NumArgs || NumArgs > Params->size() ||
      !HasDefault(Params->getParam(NumArgs - 1)))
    return NumArgs;
  TargetAnalysisScope Scope(*this, Target);
  Sema::SFINAETrap Trap(SemaRef);
  while (NumArgs && HasDefault(Params->getParam(NumArgs - 1))) {
    QualType Shorter =
        formClassType(SemaRef, CTD, Args.take_front(NumArgs - 1), Loc);
    if (Trap.hasErrorOccurred() || Shorter.isNull() ||
        Shorter->getAsRecordDecl() != CTSD)
      break;
    --NumArgs;
  }
  return NumArgs;
}

QualType SemaMultiTarget::getTypeForInstantiationTarget(QualType T,
                                                        SourceLocation Loc) {
  if (T.isNull() || !SemaRef.Context.isMultiTargetCompile())
    return T;
  if (const auto *PT = T->getAs<PointerType>()) {
    QualType Pointee = getTypeForInstantiationTarget(PT->getPointeeType(), Loc);
    if (Pointee == PT->getPointeeType())
      return T;
    return SemaRef.Context.getQualifiedType(
        SemaRef.Context.getPointerType(Pointee), T.getLocalQualifiers());
  }
  if (const auto *RT = T->getAs<ReferenceType>()) {
    QualType Pointee =
        getTypeForInstantiationTarget(RT->getPointeeTypeAsWritten(), Loc);
    if (Pointee == RT->getPointeeTypeAsWritten())
      return T;
    return isa<LValueReferenceType>(RT)
               ? SemaRef.Context.getLValueReferenceType(Pointee)
               : SemaRef.Context.getRValueReferenceType(Pointee);
  }
  const auto *CTSD = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      T->getAsCXXRecordDecl());
  if (!CTSD)
    return T;
  ClassTemplateDecl *CTD = CTSD->getSpecializedTemplate();
  ArrayRef<TemplateArgument> Args = CTSD->getTemplateArgs().asArray();
  QualType Result;
  if (unsigned Key = SemaRef.Context.getMultiTarget().getTargetInstantiationKey(
          CTSD->getCanonicalDecl())) {
    unsigned TargetKey =
        SemaRef.Context.getMultiTarget().computeTargetInstantiationKey(CTD);
    if (!TargetKey || TargetKey == Key)
      return T;
    // An argument the key's default argument gave is the other target's own
    // default there.
    Args = Args.take_front(getWrittenArgumentCount(CTSD, Key, Loc));
    if (Args.size() == CTSD->getTemplateArgs().size())
      if (ClassTemplateSpecializationDecl *Sibling =
              CTD->findSpecializationForTargetKey(Args, TargetKey))
        Result = SemaRef.Context.getCanonicalTagType(Sibling);
  } else {
    // Keyed by the ambient target instead. An architecture instantiated
    // apart from the canonical device uses its own.
    unsigned Ambient = SemaRef.Context.getCurrentTargetVariant();
    unsigned Have =
        SemaRef.Context.getMultiTarget().getSpecializationKeyVariant(CTSD);
    if (!Have || Have == Ambient ||
        Ambient <= SemaRef.Context.getCanonicalDeviceVariant() ||
        !CTD->hasTargetKeyedSpecializations())
      return T;
    Args = Args.take_front(getWrittenArgumentCount(CTSD, Have, Loc));
    // A template forked per target names this target's copy.
    if (auto *Sibling = dyn_cast_or_null<ClassTemplateDecl>(
            findTargetVariantSibling(CTD, Ambient)))
      CTD = Sibling;
  }
  if (Result.isNull()) {
    Result = formClassType(SemaRef, CTD, Args, Loc);
    if (Result.isNull())
      return T;
  }
  return SemaRef.Context.getQualifiedType(Result, T.getQualifiers());
}

Expr *SemaMultiTarget::viewForInstantiationTarget(Expr *E) {
  ASTContext &Ctx = SemaRef.Context;
  const auto *DRE = dyn_cast<DeclRefExpr>(E);
  const auto *PD = DRE ? dyn_cast<ParmVarDecl>(DRE->getDecl()) : nullptr;
  auto *FD = PD ? dyn_cast<FunctionDecl>(PD->getDeclContext()) : nullptr;
  if (!Ctx.isMultiTargetCompile() || !FD || FD != SemaRef.CurContext ||
      !FD->getPrimaryTemplate() || E->isTypeDependent() ||
      !E->getType()->isRecordType() ||
      !Ctx.isDeviceVariant(getInstantiationTarget()))
    return E;
  const FunctionDecl *Pattern = FD->getTemplateInstantiationPattern();
  unsigned Index = PD->getFunctionScopeIndex();
  if (!Pattern || Index >= Pattern->getNumParams() ||
      Pattern->getParamDecl(Index)->isParameterPack())
    return E;
  QualType Written = Pattern->getParamDecl(Index)->getType();
  if (!Written->isDependentType())
    return E;
  // The parameter as the body declares it, with the types the body names for
  // this target.
  QualType Own;
  {
    Sema::SFINAETrap Trap(SemaRef);
    Own = SemaRef.SubstType(
        Written,
        SemaRef.getTemplateInstantiationArgs(FD, /*DC=*/nullptr,
                                             /*Final=*/false, std::nullopt,
                                             /*RelativeToPrimary=*/true),
        E->getExprLoc(), PD->getDeclName());
    if (Trap.hasErrorOccurred() || Own.isNull())
      return E;
  }
  QualType T = E->getType();
  Own = Ctx.getQualifiedType(Own.getNonReferenceType().getUnqualifiedType(),
                             T.getQualifiers());
  if (Ctx.hasSameType(Own, T) || !Own->isRecordType())
    return E;
  // Only where the two lay out the same, as for one class keyed by what its
  // member functions evaluate.
  if (!SemaRef.isCompleteType(E->getExprLoc(), Own) ||
      !SemaRef.isCompleteType(E->getExprLoc(), T) ||
      Ctx.getTypeSizeInChars(Own) != Ctx.getTypeSizeInChars(T) ||
      Ctx.getTypeAlignInChars(Own) != Ctx.getTypeAlignInChars(T))
    return E;
  return ImplicitCastExpr::Create(Ctx, Own, CK_LValueBitCast, E, nullptr,
                                  E->getValueKind(), FPOptionsOverride());
}

QualType SemaMultiTarget::getWrittenTypeForTarget(QualType T, unsigned Target,
                                                  SourceLocation Loc) {
  if (T.isNull() || !Target || T->isDependentType() ||
      !SemaRef.Context.isMultiTargetCompile())
    return T;
  if (const auto *FPT = T->getAs<FunctionProtoType>()) {
    QualType Result =
        getWrittenTypeForTarget(FPT->getReturnType(), Target, Loc);
    bool Changed = Result != FPT->getReturnType();
    SmallVector<QualType, 4> Params;
    for (QualType Param : FPT->getParamTypes()) {
      Params.push_back(getWrittenTypeForTarget(Param, Target, Loc));
      Changed |= Params.back() != Param;
    }
    if (!Changed)
      return T;
    return SemaRef.Context.getFunctionType(Result, Params,
                                           FPT->getExtProtoInfo());
  }
  if (const auto *PT = T->getAs<PointerType>()) {
    QualType Pointee =
        getWrittenTypeForTarget(PT->getPointeeType(), Target, Loc);
    if (Pointee == PT->getPointeeType())
      return T;
    return SemaRef.Context.getQualifiedType(
        SemaRef.Context.getPointerType(Pointee), T.getLocalQualifiers());
  }
  if (const auto *RT = T->getAs<ReferenceType>()) {
    QualType Pointee =
        getWrittenTypeForTarget(RT->getPointeeTypeAsWritten(), Target, Loc);
    if (Pointee == RT->getPointeeTypeAsWritten())
      return T;
    return isa<LValueReferenceType>(RT)
               ? SemaRef.Context.getLValueReferenceType(Pointee)
               : SemaRef.Context.getRValueReferenceType(Pointee);
  }
  const auto *TST = T->getAs<TemplateSpecializationType>();
  const auto *CTSD = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      T->getAsCXXRecordDecl());
  if (!TST || !CTSD)
    return T;
  // A type argument, e.g. M<int> in Box<M<int>>, can be one itself.
  SmallVector<TemplateArgument, 4> Args;
  bool ArgsChanged = false;
  for (const TemplateArgument &Arg : TST->template_arguments()) {
    Args.push_back(Arg);
    if (Arg.getKind() != TemplateArgument::Type)
      continue;
    QualType ArgType = getWrittenTypeForTarget(Arg.getAsType(), Target, Loc);
    if (ArgType != Arg.getAsType()) {
      Args.back() = TemplateArgument(ArgType);
      ArgsChanged = true;
    }
  }
  unsigned Have = SemaRef.Context.getMultiTarget().getTargetInstantiationKey(
      CTSD->getCanonicalDecl());
  if (!Have && !ArgsChanged)
    return T;
  llvm::SaveAndRestore Forced(ForcedInstantiationTarget, Target);
  ClassTemplateDecl *CTD = CTSD->getSpecializedTemplate();
  unsigned Want =
      Have ? SemaRef.Context.getMultiTarget().computeTargetInstantiationKey(CTD)
           : 0;
  if (!ArgsChanged && (!Want || Want == Have))
    return T;
  std::optional<ASTContext::TargetScope> Scope;
  if (Want)
    Scope.emplace(SemaRef.Context, Want);
  TemplateArgumentListInfo ArgInfo(Loc, Loc);
  for (const TemplateArgument &Arg : Args)
    ArgInfo.addArgument(
        SemaRef.getTrivialTemplateArgumentLoc(Arg, QualType(), Loc));
  QualType Result = SemaRef.CheckTemplateIdType(
      ElaboratedTypeKeyword::None, TemplateName(CTD), Loc, ArgInfo,
      /*Scope=*/nullptr, /*ForNestedNameSpecifier=*/false);
  if (Result.isNull())
    return T;
  return SemaRef.Context.getQualifiedType(Result, T.getQualifiers());
}

static Sema::PragmaClangSection *pragmaClangSection(Sema &S, unsigned Kind) {
  switch (static_cast<PragmaClangSectionKind>(Kind)) {
  case PragmaClangSectionKind::BSS:
    return &S.PragmaClangBSSSection;
  case PragmaClangSectionKind::Data:
    return &S.PragmaClangDataSection;
  case PragmaClangSectionKind::Rodata:
    return &S.PragmaClangRodataSection;
  case PragmaClangSectionKind::Relro:
    return &S.PragmaClangRelroSection;
  case PragmaClangSectionKind::Text:
    return &S.PragmaClangTextSection;
  default:
    return nullptr;
  }
}

static void applyPragmaChange(SemaMultiTarget::PragmaState &State,
                              const SemaMultiTarget::PragmaStateChange &C) {
  if (C.Part == SemaMultiTarget::PragmaStateChange::OptimizePart)
    State.OptimizeOff = C.OptimizeOff;
  else if (C.Part == SemaMultiTarget::PragmaStateChange::AssumeNonNullPart)
    State.AssumeNonNull = C.AssumeNonNull;
  else
    State.Sections[C.Part] = C.Section;
}

void SemaMultiTarget::notePragmaChange(const PragmaStateChange &Change) {
  ASTContext &Ctx = SemaRef.Context;
  if (!Ctx.isMultiTargetCompile())
    return;
  // A parse for several targets changes the state of each, which Sema's
  // already is while no target sees another.
  unsigned Tag = Ctx.getDeclTagVariant();
  if (!Tag) {
    for (PragmaState &State : PragmaStates)
      applyPragmaChange(State, Change);
    return;
  }
  if (PragmaStates.empty()) {
    // Until now every target saw Sema's state, which this change has not
    // reached yet.
    PragmaState Before;
    Before.OptimizeOff = SemaRef.getOptimizeOffPragmaLocation();
    for (unsigned K = 0; K != 5; ++K)
      if (Sema::PragmaClangSection *S = pragmaClangSection(SemaRef, K))
        Before.Sections[K] = {S->Valid, S->SectionName, S->PragmaLocation};
    Before.AssumeNonNull =
        SemaRef.getPreprocessor().getPragmaAssumeNonNullLoc();
    PragmaStates.assign(1 + Ctx.getNumAuxTargets() + 1, Before);
  }
  applyPragmaChange(PragmaStates[Tag], Change);
  PragmaChangeLog.push_back({Tag, Change});
}

static llvm::hash_code hashPragmaState(const SemaMultiTarget::PragmaState &S) {
  llvm::hash_code H = llvm::hash_value(S.OptimizeOff.getRawEncoding());
  for (const SemaMultiTarget::PragmaState::Section &Sec : S.Sections)
    H = llvm::hash_combine(H, Sec.Valid, Sec.Valid ? Sec.Name : "");
  return llvm::hash_combine(H, S.AssumeNonNull.getRawEncoding());
}

void SemaMultiTarget::usePragmaState() {
  if (PragmaStates.empty())
    return;
  ASTContext &Ctx = SemaRef.Context;
  unsigned Current = std::max(Ctx.getCurrentTargetVariant(), 1u);
  const PragmaState &State = PragmaStates[Current];
  SemaRef.OptimizeOffPragmaLocation = State.OptimizeOff;
  for (unsigned K = 0; K != 5; ++K)
    if (Sema::PragmaClangSection *S = pragmaClangSection(SemaRef, K))
      *S = {State.Sections[K].Name, State.Sections[K].Valid,
            State.Sections[K].Loc};
  SemaRef.getPreprocessor().setPragmaAssumeNonNullLoc(State.AssumeNonNull);
  // A declaration a parse for several targets makes means something
  // different to targets that see different states.
  if (Ctx.getDeclTagVariant())
    return;
  llvm::hash_code First = hashPragmaState(PragmaStates[1]);
  if (llvm::all_of(Ctx.getTargetVariants(), [&](unsigned V) {
        return hashPragmaState(PragmaStates[V]) == First;
      }))
    return;
  recordTargetInput(
      'G', Ctx.getMultiTarget().partitionTargetsByAnswer([&](unsigned V) {
        return hashPragmaState(PragmaStates[V]);
      }),
      "pragma state");
}

void SemaMultiTarget::replayPragmaChanges(size_t Begin, size_t End,
                                          unsigned Variant) {
  if (PragmaStates.empty())
    return;
  for (size_t I = Begin; I != End; ++I)
    applyPragmaChange(PragmaStates[Variant], PragmaChangeLog[I].second);
}

void SemaMultiTarget::noteSideDivergentCall(const FunctionDecl *Caller,
                                            const FunctionDecl *Callee,
                                            SourceLocation Loc) {
  SideDivergentCalls.insert({Caller, Callee, Loc});
}

bool SemaMultiTarget::isSideDivergentCall(const FunctionDecl *Caller,
                                          const FunctionDecl *Callee,
                                          SourceLocation Loc) const {
  return SideDivergentCalls.contains({Caller, Callee, Loc});
}
