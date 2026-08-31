//===--- SemaMultiTargetInstantiate.cpp - Instantiation for several targets
//===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Template instantiation in a compilation for several targets at once: bodies
// instantiated per group of targets, the host's view of device code, and the
// specializations other devices need of what one target instantiated.
//
//===----------------------------------------------------------------------===//

#include "SemaMultiTargetInstantiate.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DynamicRecursiveASTVisitor.h"
#include "clang/AST/MangleNumberingContext.h"
#include "clang/AST/MultiTargetContext.h"
#include "clang/AST/TargetDependence.h"
#include "clang/Sema/EnterExpressionEvaluationContext.h"
#include "clang/Sema/Lookup.h"
#include "clang/Sema/ScopeInfo.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaCUDA.h"
#include "clang/Sema/SemaMultiTarget.h"
#include "clang/Sema/Template.h"
#include "clang/Sema/TemplateDeduction.h"
#include "llvm/Support/SaveAndRestore.h"
#include <optional>

using namespace clang;
using namespace clang::multitarget;

namespace {
/// Finds references, in a function body, to a callee or member that differs
/// between targets in a way lookup does not record:
///  - Callee (isTargetDivergentCallee), a function or function template whose
///    copies differ, e.g. ggml_cuda_mma::mma.
///  - ValueMember (isTargetDivergentValueMemberAccess), a static data member
///    of a class specialization forked per target, e.g.
///    tile_like<16,16,int>::ne.
///  - MemberCallee (isTargetDivergentMemberFunctionAccess), a member function
///    of such a specialization, e.g. ggml_cuda_mma::tile<...>::get_i().
enum class DivergentReferenceKind {
  Callee,
  ValueMember,
  MemberCallee,
};

class DivergentCalleeVisitor : public DynamicRecursiveASTVisitor {
public:
  SmallVector<std::pair<Expr *, DivergentReferenceKind>, 8> Found;

  void checkDecl(Expr *E, const ValueDecl *D, NestedNameSpecifier Qualifier) {
    if (isTargetDivergentCallee(D))
      Found.emplace_back(E, DivergentReferenceKind::Callee);
    if (isTargetDivergentValueMemberAccess(D))
      Found.emplace_back(E, DivergentReferenceKind::ValueMember);
    if (isTargetDivergentMemberFunctionAccess(D))
      Found.emplace_back(E, DivergentReferenceKind::MemberCallee);
  }

  bool VisitDeclRefExpr(DeclRefExpr *E) override {
    checkDecl(E, E->getDecl(), E->getQualifier());
    return true;
  }

  bool VisitMemberExpr(MemberExpr *E) override {
    checkDecl(E, E->getMemberDecl(), E->getQualifier());
    return true;
  }

  bool VisitCXXConstructExpr(CXXConstructExpr *E) override {
    if (isTargetDivergentMemberFunctionAccess(E->getConstructor()))
      Found.emplace_back(E, DivergentReferenceKind::MemberCallee);
    return true;
  }
};

/// The specialization of \p CTD with \p Args for the target ambient now,
/// which a template keyed by target forks per target (see
/// ClassTemplateSpecializationDecl::Profile), instantiated.
static ClassTemplateSpecializationDecl *getOrCreateTargetForkedSpecialization(
    Sema &S, ClassTemplateDecl *CTD, ArrayRef<TemplateArgument> Args,
    bool HasStrictPackMatch, SourceLocation Loc) {
  ASTContext &Context = S.Context;
  llvm::FoldingSetInsertToken InsertToken;
  ClassTemplateSpecializationDecl *NewSpec =
      CTD->findSpecialization(Args, InsertToken);
  if (!NewSpec) {
    NewSpec = ClassTemplateSpecializationDecl::Create(
        Context, CTD->getTemplatedDecl()->getTagKind(), CTD->getDeclContext(),
        CTD->getTemplatedDecl()->getBeginLoc(), CTD->getLocation(), CTD, Args,
        HasStrictPackMatch, nullptr);
    CTD->AddSpecialization(NewSpec, InsertToken);
    if (CTD->isOutOfLine())
      NewSpec->setLexicalDeclContext(CTD->getLexicalDeclContext());
  }
  if (NewSpec->getSpecializationKind() == TSK_Undeclared)
    S.InstantiateClassTemplateSpecialization(
        Loc, NewSpec, TSK_ImplicitInstantiation, /*Complain=*/true,
        NewSpec->hasStrictPackMatch());
  return NewSpec;
}

} // namespace

FunctionDecl *Sema::InstantiateFunctionDeclaration(
    FunctionTemplateDecl *FTD, const TemplateArgumentList *Args,
    SourceLocation Loc, CodeSynthesisContext::SynthesisKind CSC,
    FunctionTemplateDecl *PatternSource) {
  FunctionDecl *FD = (PatternSource ? PatternSource : FTD)->getTemplatedDecl();

  InstantiatingTemplate Inst(*this, Loc, FTD, Args->asArray(), CSC);
  if (Inst.isInvalid())
    return nullptr;

  ContextRAII SavedContext(*this, FD);
  MultiLevelTemplateArgumentList MArgs(FTD, Args->asArray(),
                                       /*Final=*/false);

  return cast_or_null<FunctionDecl>(SubstDecl(FD, FD->getParent(), MArgs));
}

/// The definition \p Function is instantiated from. Of a template re-parsed
/// for each target, a specialization keyed by a device target instantiates
/// that target's copy, whose names were looked up for it.
const FunctionDecl *multitarget::getTargetPattern(Sema &S,
                                                  const FunctionDecl *Function,
                                                  const FunctionDecl *Pattern) {
  ASTContext &Context = S.Context;
  const FunctionTemplateDecl *FTD = Pattern->getDescribedFunctionTemplate();
  if (FTD && FTD->isTargetVariantReparseOrigin()) {
    unsigned Key = Context.getMultiTarget().getEffectiveTargetInstantiationKey(
        Function->getCanonicalDecl());
    if (!Context.isDeviceVariant(Key))
      return Pattern;
    if (const NamedDecl *Copy = Context.getMultiTarget().getCopyFor(FTD, Key))
      return cast<FunctionTemplateDecl>(Copy)->getTemplatedDecl();
  }
  return Pattern;
}

/// The definition of \p Pattern's template that \p Variant compiles, where
/// the template is declared once and defined per target.
const FunctionDecl *
multitarget::getDefinitionForTarget(const FunctionDecl *Pattern,
                                    unsigned Variant) {
  const FunctionDecl *Shared = Pattern->getDefinition();
  const FunctionTemplateDecl *FTD = Pattern->getDescribedFunctionTemplate();
  if (!FTD)
    return Shared;
  for (const RedeclarableTemplateDecl *R : FTD->redecls()) {
    const FunctionDecl *Def = cast<FunctionTemplateDecl>(R)->getTemplatedDecl();
    if (!Def->doesThisDeclarationHaveABody())
      continue;
    if (!R->getTargetVariant())
      Shared = Def;
    else if (!R->isRedundantTargetVariant() && R->isCopyFor(Variant))
      return Def;
  }
  return Shared;
}

/// Whether \p Function, device code, is instantiated where it is not known
/// which of the devices compile it. Its body might then not compile for some
/// of them, e.g. a wave64 specialization on a wave32 device, which only
/// report that if they emit it.
bool multitarget::isTentativeTargetInstantiation(Sema &S,
                                                 FunctionDecl *Function) {
  ASTContext &Context = S.Context;
  if (!LLVM_UNLIKELY(clang::AllowTargetVariantDecls) ||
      !Context.isMultiTargetCompile() || Context.getNumDeviceTargets() < 2 ||
      Context.getMultiTarget().isHostViewOfDeviceCode() ||
      Function->getTemplateSpecializationKind() != TSK_ImplicitInstantiation ||
      isa<CXXConstructorDecl, CXXDestructorDecl>(Function))
    return false;
  CUDAFunctionTarget FT = S.CUDA().IdentifyTarget(Function);
  return FT == CUDAFunctionTarget::Device || FT == CUDAFunctionTarget::Global;
}

Stmt *SemaMultiTarget::instantiateBodyPart(
    FunctionDecl *Function, const FunctionDecl *PatternDecl, TargetSet Part,
    unsigned View, bool Keep, DeclContext *DC,
    std::optional<ArrayRef<TemplateArgument>> Innermost,
    TargetDiagnosticCapture &Capture, Stmt *Pattern) {
  if (!Pattern)
    Pattern =
        SemaRef.Context.getMultiTarget().getBodyForTarget(PatternDecl, View);
  TargetAnalysisScope Scope(*this, View);
  MultiLevelTemplateArgumentList TemplateArgs =
      SemaRef.getTemplateInstantiationArgs(Function, DC, /*Final=*/false,
                                           Innermost, false, PatternDecl);
  LocalInstantiationScope PartScope(SemaRef);
  llvm::SaveAndRestore InPart(TargetBodyPart, Part);
  llvm::SaveAndRestore<const FunctionDecl *> PartOf(TargetBodyFunction,
                                                    Function);
  SemaRef.ActOnStartOfFunctionDef(nullptr, Function);
  Sema::ContextRAII InFunction(SemaRef, Function);
  StmtResult Body;
  // Lambdas and local entities are numbered as in the body instantiated
  // first, whose names they have.
  std::unique_ptr<MangleNumberingContext> Numbering =
      SemaRef.Context.exchangeManglingNumberContext(Function, nullptr);
  if (!SemaRef.addInstantiatedParametersToScope(Function, PatternDecl,
                                                PartScope, TemplateArgs)) {
    llvm::SaveAndRestore Capturing(CurrentTargetDiagnosticCapture, &Capture);
    Body = SemaRef.SubstStmt(Pattern, TemplateArgs);
  } else {
    Capture.ErrorOccurred = true;
  }
  SemaRef.Context.exchangeManglingNumberContext(Function, std::move(Numbering));
  SemaRef.ActOnFinishFunctionBody(
      Function, Capture.ErrorOccurred || !Keep ? nullptr : Body.get(),
      /*IsInstantiation=*/true);
  return Capture.ErrorOccurred ? nullptr : Body.get();
}

/// The class \p D is a member of, if it is some targets' only. A member whose
/// pattern was merged into another target's copy is its class's.
const Decl *multitarget::getTaggedEnclosingClass(const Decl *D) {
  const auto *Class = dyn_cast<CXXRecordDecl>(D->getDeclContext());
  return Class && Class->getTargetVariant() &&
                 !Class->isRedundantTargetVariant()
             ? Class
             : nullptr;
}

/// The copy of a class template copied per target that \p Function's class,
/// a specialization of it, was instantiated from, if any.
static const ClassTemplateDecl *getClassCopy(const FunctionDecl *Function) {
  const auto *Class =
      dyn_cast<ClassTemplateSpecializationDecl>(Function->getDeclContext());
  if (!Class)
    return nullptr;
  const ClassTemplateDecl *CTD = Class->getSpecializedTemplate();
  if (const CXXRecordDecl *Pattern = Class->getTemplateInstantiationPattern())
    if (const ClassTemplateDecl *Described =
            Pattern->getDescribedClassTemplate())
      CTD = Described;
  return CTD->getTargetVariant() && !CTD->isRedundantTargetVariant() ? CTD
                                                                     : nullptr;
}

/// The targets that compile \p Function. A device with its own copy of the
/// class template \p Function's class specializes compiles that copy's
/// member instead.
static TargetSet getCompilingTargets(const ASTContext &Context,
                                     const FunctionDecl *Function) {
  TargetSet Own = Function->getTargetVariant()
                      ? Function->getTargetSet()
                      : Context.getMultiTarget().getTargetInstantiationTargets(
                            Function->getCanonicalDecl());
  if (const ClassTemplateDecl *Copy = getClassCopy(Function))
    for (unsigned V : Context.getTargetVariants())
      if (Context.isDeviceVariant(V) && getCopyForTarget(Context, Copy, V))
        Own.erase(V);
  return Own;
}

/// The targets compiling \p Function's body for code that requested it, and
/// the host for host-callable code. The host's view of device code is not
/// compiled, and host code is compiled for the host only.
static TargetSet getPartTargets(Sema &S, const FunctionDecl *Function) {
  // A lambda without attributes of its own is compiled for its enclosing
  // function.
  auto Explicit = [](const Attr *A) { return A && !A->isImplicit(); };
  const FunctionDecl *Compiled = Function;
  while (isLambdaCallOperator(Compiled) &&
         !Explicit(Compiled->getAttr<CUDADeviceAttr>()) &&
         !Explicit(Compiled->getAttr<CUDAHostAttr>()))
    if (const auto *Enclosing = dyn_cast_or_null<FunctionDecl>(
            cast<CXXMethodDecl>(Compiled)->getParent()->getDeclContext()))
      Compiled = Enclosing;
    else
      break;
  CUDAFunctionTarget FT = S.CUDA().IdentifyTarget(Compiled);
  unsigned Host = S.Context.getHostVariant();
  if (FT == CUDAFunctionTarget::Host)
    return TargetSet();
  TargetSet For = S.MultiTarget().getRequestingTargets(Function);
  if (Host) {
    if (FT == CUDAFunctionTarget::Device || FT == CUDAFunctionTarget::Global)
      For.erase(Host);
    else
      For.insert(Host);
  }
  // Each other device gets a specialization of its own keyed by it, or of
  // the class it is a member of.
  const Decl *Keyed = nullptr;
  if (unsigned Key =
          S.Context.getMultiTarget().getEffectiveTargetInstantiationKey(
              Function->getCanonicalDecl(), &Keyed);
      Key && Keyed && S.Context.isDeviceVariant(Key)) {
    MultiTargetContext::TargetDependenceKind Dependence =
        MultiTargetContext::TDK_None;
    if (const auto *FD = dyn_cast<FunctionDecl>(Keyed);
        FD && FD->getPrimaryTemplate())
      Dependence = S.Context.getMultiTarget().getSpecializationTargetDependence(
          FD->getPrimaryTemplate(),
          FD->getTemplateSpecializationArgs()->asArray());
    else if (const auto *CTSD =
                 dyn_cast<ClassTemplateSpecializationDecl>(Keyed))
      Dependence = S.Context.getMultiTarget().getSpecializationTargetDependence(
          CTSD->getSpecializedTemplate(), CTSD->getTemplateArgs().asArray());
    if (Dependence == MultiTargetContext::TDK_Arch)
      for (unsigned V : S.Context.getDeviceVariants())
        if (V != Key)
          For.erase(V);
  }
  return getCompilingTargets(S.Context, Function) & For;
}

/// The member \p E, a reference DivergentCalleeVisitor found, names.
static const ValueDecl *getReferencedDecl(const Expr *E) {
  if (const auto *CE = dyn_cast<CXXConstructExpr>(E))
    return CE->getConstructor();
  if (const auto *ME = dyn_cast<MemberExpr>(E))
    return ME->getMemberDecl();
  return cast<DeclRefExpr>(E)->getDecl();
}

/// Whether \p Function's body names a callee or member that differs between
/// targets in a way lookup does not record, e.g. a member of a class
/// specialization forked per target. One the signature names is the
/// signature's in every part.
static bool hasDivergentReferences(const Sema &S,
                                   const FunctionDecl *Function) {
  if (Function->getTargetVariant() && Function->getTargetSet().isSingle())
    return false;
  Stmt *Body = Function->getBody();
  if (!Body)
    return false;
  DivergentCalleeVisitor Visitor;
  Visitor.TraverseStmt(Body);
  if (const auto *Ctor = dyn_cast<CXXConstructorDecl>(Function))
    for (const CXXCtorInitializer *Init : Ctor->inits())
      Visitor.TraverseStmt(Init->getInit());
  return llvm::any_of(Visitor.Found, [&](const auto &Entry) {
    // A callee whose own copies differ. One that only calls such a function
    // is split by its own body.
    if (Entry.second == DivergentReferenceKind::Callee) {
      const auto *FD = cast<FunctionDecl>(getReferencedDecl(Entry.first));
      if (FunctionTemplateDecl *FTD = FD->getPrimaryTemplate())
        return FTD->hasTargetTaggedRedeclaration();
      return hasTargetWidenedSiblingDecl(FD);
    }
    const auto *Owner = dyn_cast<ClassTemplateSpecializationDecl>(
        getReferencedDecl(Entry.first)->getDeclContext());
    return !Owner || !S.MultiTarget().signatureNamesTemplate(
                         Function, Owner->getSpecializedTemplate());
  });
}

void SemaMultiTarget::measureTargetParts(FunctionDecl *Function) {
  auditTargetParts(Function, getInstantiationTarget(),
                   getPartTargets(SemaRef, Function),
                   hasDivergentReferences(SemaRef, Function));
}

void SemaMultiTarget::instantiateTargetParts(FunctionDecl *Function) {
  // A constructor's member initializers are not part of its body.
  if (isa<CXXConstructorDecl, CXXDestructorDecl>(Function) ||
      SemaRef.Context.getMultiTarget().isHostViewOfDeviceCode())
    return;
  const FunctionDecl *Pattern = Function->getTemplateInstantiationPattern();
  if (!Pattern || !Function->getBody())
    return;
  unsigned Main =
      TargetPartMainTarget.try_emplace(Function, getInstantiationTarget())
          .first->second;
  // Code requesting it for more targets later splits it for those.
  RequestingTargetsUsed[Function->getCanonicalDecl()] =
      getRequestingTargets(Function);
  TargetSet Own = getPartTargets(SemaRef, Function);
  TargetPartition ByReferences =
      hasDivergentReferences(SemaRef, Function)
          ? SemaRef.Context.getMultiTarget().reconcileTargetPartition(
                0, MultiTargetContext::TDK_Arch)
          : 0;
  // The groups are those of the targets compiling the body and of the one
  // the body was instantiated for, whose group the body serves.
  SmallVector<TargetSet, 4> Parts = getInstantiationParts(
      Function, Own | TargetSet::single(Main), ByReferences);
  if (Parts.size() < 2)
    return;
  // Parts made after the body, for code requesting it later, are this
  // function's instantiation too.
  std::optional<Sema::InstantiatingTemplate> Inst;
  if (SemaRef.CodeSynthesisContexts.empty() ||
      SemaRef.CodeSynthesisContexts.back().Entity != Function) {
    Inst.emplace(SemaRef, Function->getPointOfInstantiation(), Function);
    if (Inst->isInvalid())
      return;
  }
  // The body instantiated first serves the group of Main, and each part
  // instantiated since serves its own.
  TargetSet Covered;
  for (const auto &[Targets, Body] :
       SemaRef.Context.getMultiTarget().getTargetBodies(Function))
    Covered = Covered | Targets;
  DeclContext *DC = Function->getLexicalDeclContext();
  std::optional<ArrayRef<TemplateArgument>> Innermost;
  if (FunctionTemplateDecl *Primary = Function->getPrimaryTemplate();
      Primary &&
      !isGenericLambdaCallOperatorOrStaticInvokerSpecialization(Function)) {
    if (FunctionDecl *Def = Primary->getTemplatedDecl()->getDefinition())
      DC = Def->getLexicalDeclContext();
    Innermost.emplace(Function->getTemplateSpecializationArgs()->asArray());
  }
  for (TargetSet Part : Parts) {
    TargetSet Rest;
    for (unsigned V : SemaRef.Context.getTargetVariants())
      if (Part.contains(V) && !Covered.contains(V))
        Rest.insert(V);
    if (Rest.empty() || Part.contains(Main))
      continue;
    unsigned View = SemaRef.Context.getAnalysisTarget(Rest);
    const FunctionDecl *PatternDecl = getDefinitionForTarget(
        getTargetPattern(SemaRef, Function, Pattern), View);
    if (!PatternDecl || !PatternDecl->getBody())
      continue;
    instantiatePart(Function, PatternDecl, Rest, View, DC, Innermost);
    Covered = Covered | Rest;
  }
}

void SemaMultiTarget::instantiateForOtherViews(
    FunctionDecl *Function, unsigned View,
    ArrayRef<PartialDiagnosticAt> ViewDiags, DeclContext *DC,
    std::optional<ArrayRef<TemplateArgument>> Innermost) {
  TargetSet Own = getCompilingTargets(SemaRef.Context, Function);
  Stmt *Compiled = nullptr;
  SmallVector<std::pair<unsigned, SmallVector<PartialDiagnosticAt, 2>>, 4>
      Failed;
  if (!Own.isSingle()) {
    // A device with a specialization of its own is tried too. If the body
    // compiles there, it is not broken, only not for every device.
    for (unsigned V : SemaRef.Context.getDeviceVariants()) {
      if (V == View)
        continue;
      const FunctionDecl *PatternDecl = getDefinitionForTarget(
          getTargetPattern(SemaRef, Function,
                           Function->getTemplateInstantiationPattern()),
          V);
      if (!PatternDecl || !PatternDecl->getBody())
        continue;
      TargetDiagnosticCapture Capture;
      Stmt *Body =
          instantiateBodyPart(Function, PatternDecl, TargetSet::single(V), V,
                              Own.contains(V), DC, Innermost, Capture);
      if (Body) {
        if (!Compiled)
          Compiled = Body;
      } else if (Own.contains(V)) {
        Failed.push_back({V, std::move(Capture.Diags)});
      }
    }
  }
  // A body no device compiles is an error, as it is in each device's own
  // compilation. A member of a class template copied per target is not known
  // to be compiled, as each device may compile its own copy's instead, and
  // neither is what such a member made in case a device needs it uses.
  if (!Own.isSingle() && !Compiled && !InClassCopyMember &&
      !getClassCopy(Function)) {
    for (const PartialDiagnosticAt &PD : ViewDiags)
      Diag(PD.first, PD.second);
    Function->setInvalidDecl();
    return;
  }
  // Its own body is what a target without one of its own compiles, which is
  // none of those that did not compile it.
  if (Compiled)
    Function->setBody(Compiled);
  SemaRef.Context.getMultiTarget().addFailedTargetInstantiation(
      Function, TargetSet::single(View), ViewDiags);
  for (const auto &[V, Diags] : Failed)
    SemaRef.Context.getMultiTarget().addFailedTargetInstantiation(
        Function, TargetSet::single(V), Diags);
}

void SemaMultiTarget::instantiateForOtherDefinitions(
    FunctionDecl *Function, const FunctionDecl *PatternDef, Stmt *UsedPattern,
    DeclContext *DC, std::optional<ArrayRef<TemplateArgument>> Innermost) {
  TargetSet Own =
      Function->getTargetVariant()
          ? Function->getTargetSet()
          : SemaRef.Context.getMultiTarget().getTargetInstantiationTargets(
                Function->getCanonicalDecl());
  if (Own.isSingle())
    return;
  // What each target compiles is a definition of its own or a body of the
  // definition parsed for it.
  SmallVector<std::pair<const FunctionDecl *, Stmt *>, 4> Patterns;
  SmallVector<TargetSet, 4> Targets;
  for (unsigned V : SemaRef.Context.getTargetVariants()) {
    if (!Own.contains(V))
      continue;
    const FunctionDecl *Def = getDefinitionForTarget(PatternDef, V);
    Stmt *Body = Def ? SemaRef.Context.getMultiTarget().getBodyForTarget(Def, V)
                     : nullptr;
    if (!Body || Body == UsedPattern)
      continue;
    auto It = llvm::find(Patterns, std::make_pair(Def, Body));
    if (It == Patterns.end()) {
      Patterns.push_back({Def, Body});
      Targets.push_back(TargetSet::single(V));
    } else {
      Targets[It - Patterns.begin()].insert(V);
    }
  }
  for (auto [Pattern, Part] : llvm::zip(Patterns, Targets))
    instantiatePart(Function, Pattern.first, Part,
                    SemaRef.Context.getAnalysisTarget(Part), DC, Innermost,
                    Pattern.second);
}

void SemaMultiTarget::instantiatePart(
    FunctionDecl *Function, const FunctionDecl *PatternDecl, TargetSet Part,
    unsigned View, DeclContext *DC,
    std::optional<ArrayRef<TemplateArgument>> Innermost, Stmt *Pattern) {
  TargetDiagnosticCapture Capture;
  if (!instantiateBodyPart(Function, PatternDecl, Part, View, /*Keep=*/true, DC,
                           Innermost, Capture, Pattern))
    SemaRef.Context.getMultiTarget().addFailedTargetInstantiation(
        Function, Part, Capture.Diags);
  else
    for (const PartialDiagnosticAt &PD : Capture.Diags)
      Diag(PD.first, PD.second);
}

/// The target a specialization, or the outermost specialization enclosing it,
/// was made for, or 0 if it is shared; \p Owner is set to that specialization.
unsigned multitarget::getDeviceViewOwner(const ASTContext &Context,
                                         const Decl *D, const Decl *&Owner) {
  unsigned Result = 0;
  for (; D; D = dyn_cast_or_null<Decl>(D->getDeclContext())) {
    unsigned V = Context.getMultiTarget().getSpecializationKey(D);
    if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
      const FunctionTemplateDecl *FTD = FD->getPrimaryTemplate();
      if (!V && FTD && !FTD->isRedundantTargetVariant())
        V = FTD->getTargetVariant();
    }
    if (V && V != MultiTargetContext::UnkeyedTarget) {
      Result = V;
      Owner = D;
    }
  }
  return Result;
}

/// Whether \p Args name a class specialization keyed by the host, which only
/// the host's view of device code names.
static bool argumentsNameHostKeyedClass(const ASTContext &Context,
                                        ArrayRef<TemplateArgument> Args) {
  return anyNamedClass(Context, Args, [&](const CXXRecordDecl *RD) {
    return isa<ClassTemplateSpecializationDecl>(RD) &&
           Context.getMultiTarget().getSpecializationKey(RD) ==
               Context.getHostVariant();
  });
}

/// Whether \p Function, or a class specialization it is a member of, is
/// instantiated with a class specialization keyed by the host.
bool multitarget::namesHostKeyedClass(const ASTContext &Context,
                                      const FunctionDecl *Function) {
  if (const TemplateArgumentList *Args =
          Function->getTemplateSpecializationArgs();
      Args && argumentsNameHostKeyedClass(Context, Args->asArray()))
    return true;
  for (const DeclContext *DC = Function->getDeclContext(); DC;
       DC = DC->getParent())
    if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(DC);
        CTSD &&
        argumentsNameHostKeyedClass(Context, CTSD->getTemplateArgs().asArray()))
      return true;
  return false;
}

/// Whether \p Args name something made for a device target, which the host's
/// view of the code would name its own copy of.
static bool namesDeviceView(const ASTContext &Context,
                            ArrayRef<TemplateArgument> Args,
                            unsigned Depth = 0) {
  auto IsDeviceDecl = [&](const Decl *D) {
    return D && D->getTargetVariant() &&
           !D->isCopyFor(Context.getHostVariant()) &&
           !D->isRedundantTargetVariant();
  };
  if (Depth > 8)
    return true;
  for (const TemplateArgument &Arg : Args) {
    switch (Arg.getKind()) {
    case TemplateArgument::Pack:
      if (namesDeviceView(Context, Arg.pack_elements(), Depth + 1))
        return true;
      break;
    case TemplateArgument::Declaration:
      if (IsDeviceDecl(Arg.getAsDecl()))
        return true;
      break;
    case TemplateArgument::Template:
      if (IsDeviceDecl(Arg.getAsTemplate().getAsTemplateDecl()))
        return true;
      break;
    case TemplateArgument::Type: {
      QualType T = Arg.getAsType().getCanonicalType();
      while (true) {
        if (const auto *PT = T->getAs<PointerType>())
          T = PT->getPointeeType();
        else if (const auto *RT = T->getAs<ReferenceType>())
          T = RT->getPointeeType();
        else if (const ArrayType *AT = Context.getAsArrayType(T))
          T = AT->getElementType();
        else
          break;
      }
      const auto *RD = T->getAsCXXRecordDecl();
      if (!RD)
        break;
      const Decl *Owner = nullptr;
      if (IsDeviceDecl(RD) || getDeviceViewOwner(Context, RD, Owner) > 1)
        return true;
      if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RD))
        if (namesDeviceView(Context, CTSD->getTemplateArgs().asArray(),
                            Depth + 1))
          return true;
      break;
    }
    default:
      break;
    }
  }
  return false;
}

/// The host's copy of \p FTD, a template copied per target, or \p FTD itself.
static FunctionTemplateDecl *getHostCopy(const ASTContext &Context,
                                         FunctionTemplateDecl *FTD) {
  if (!FTD->getTargetVariant() || FTD->isCopyFor(Context.getHostVariant()))
    return FTD;
  return const_cast<FunctionTemplateDecl *>(
      dyn_cast_or_null<FunctionTemplateDecl>(
          Context.getMultiTarget().getCopyFor(FTD, Context.getHostVariant())));
}

void SemaMultiTarget::noteHostViewReference(FunctionDecl *Func) {
  if (!SemaRef.Context.isMultiTargetCompile() ||
      !SemaRef.Context.getHostVariant() || Func->isDefined())
    return;
  const Decl *Owner = nullptr;
  unsigned View = getDeviceViewOwner(SemaRef.Context, Func, Owner);
  if ((View && View != SemaRef.Context.getCanonicalDeviceVariant()) ||
      !isAnalysedAsHostWould())
    return;
  if (View)
    HostViewRoots.insert(Func->getCanonicalDecl());
  else
    HostViewTrustedShared.insert(Func->getCanonicalDecl());
}

/// \p FTD's specialization for \p Args, or null if it has none where it is
/// instantiated now, e.g. one enable_if'd on what this target evaluates
/// differently from the target whose code chose it.
static FunctionDecl *instantiateIfViable(Sema &S, FunctionTemplateDecl *FTD,
                                         const TemplateArgumentList *Args,
                                         SourceLocation Loc) {
  sema::TemplateDeductionInfo Info(Loc);
  Sema::SFINAETrap Trap(S, Info);
  FunctionDecl *Spec = S.InstantiateFunctionDeclaration(FTD, Args, Loc);
  return Trap.hasErrorOccurred() || (Spec && Spec->isInvalidDecl()) ? nullptr
                                                                    : Spec;
}

void SemaMultiTarget::instantiateHostView(SourceLocation Loc,
                                          FunctionDecl *Function) {
  const Decl *Owner = nullptr;
  if (getDeviceViewOwner(SemaRef.Context, Function, Owner) !=
      SemaRef.Context.getCanonicalDeviceVariant())
    return;
  if (SourceLocation Referenced = Function->getPointOfInstantiation();
      Referenced.isValid())
    Loc = Referenced;
  TargetAnalysisScope Scope(*this, SemaRef.Context.getHostVariant());
  MultiTargetContext::HostViewRAII HostView(SemaRef.Context, true);
  FunctionDecl *HostFunction = nullptr;
  if (Owner == Function) {
    FunctionTemplateDecl *FTD = Function->getPrimaryTemplate();
    const TemplateArgumentList *Args =
        Function->getTemplateSpecializationArgs();
    if (!FTD || !Args || namesDeviceView(SemaRef.Context, Args->asArray()))
      return;
    FunctionTemplateDecl *HostFTD = getHostCopy(SemaRef.Context, FTD);
    if (!HostFTD)
      return;
    // What the host's own code names is instantiated as for any other use.
    llvm::FoldingSetInsertToken InsertToken;
    if (HostFTD->findSpecialization(Args->asArray(), InsertToken))
      return;
    HostFunction = instantiateIfViable(SemaRef, HostFTD, Args, Loc);
  } else if (auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(
                 const_cast<Decl *>(Owner));
             CTSD && Function->getDeclContext() == CTSD) {
    ArrayRef<TemplateArgument> ClassArgs = CTSD->getTemplateArgs().asArray();
    if (namesDeviceView(SemaRef.Context, ClassArgs))
      return;
    ClassTemplateDecl *CTD = CTSD->getSpecializedTemplate();
    auto FormType = [&](unsigned NumArgs) {
      TemplateArgumentListInfo ArgInfo(Loc, Loc);
      for (const TemplateArgument &Arg : ClassArgs.take_front(NumArgs)) {
        if (Arg.getKind() != TemplateArgument::Pack) {
          ArgInfo.addArgument(
              SemaRef.getTrivialTemplateArgumentLoc(Arg, QualType(), Loc));
          continue;
        }
        for (const TemplateArgument &Element : Arg.pack_elements())
          ArgInfo.addArgument(
              SemaRef.getTrivialTemplateArgumentLoc(Element, QualType(), Loc));
      }
      return SemaRef.CheckTemplateIdType(ElaboratedTypeKeyword::None,
                                         TemplateName(CTD), Loc, ArgInfo,
                                         /*Scope=*/nullptr,
                                         /*ForNestedNameSpecifier=*/false);
    };
    // The host's code names the class as written, so an argument the
    // canonical device's default argument gave is the host's own default.
    unsigned NumArgs;
    {
      MultiTargetContext::HostViewRAII DeviceView(SemaRef.Context, false);
      NumArgs = getWrittenArgumentCount(
          CTSD, SemaRef.Context.getCanonicalDeviceVariant(), Loc);
    }
    QualType HostType = FormType(NumArgs);
    struct : Sema::TypeDiagnoser {
      void diagnose(Sema &, SourceLocation, QualType) override {}
    } Silent;
    if (HostType.isNull() || SemaRef.RequireCompleteType(Loc, HostType, Silent))
      return;
    auto *HostCTSD =
        dyn_cast<ClassTemplateSpecializationDecl>(HostType->getAsRecordDecl());
    if (!HostCTSD || HostCTSD == CTSD)
      return;
    // Each target's copy of the template replays the same tokens.
    FunctionTemplateDecl *MemberTemplate = Function->getPrimaryTemplate();
    const Decl *Pattern =
        MemberTemplate ? static_cast<const Decl *>(MemberTemplate) : Function;
    for (Decl *Member : HostCTSD->decls()) {
      if (Member->getKind() != Pattern->getKind() ||
          Member->getLocation() != Pattern->getLocation())
        continue;
      if (auto *MemberFTD = dyn_cast<FunctionTemplateDecl>(Member)) {
        const TemplateArgumentList *Args =
            Function->getTemplateSpecializationArgs();
        llvm::FoldingSetInsertToken InsertToken;
        if (!Args || namesDeviceView(SemaRef.Context, Args->asArray()) ||
            MemberFTD->findSpecialization(Args->asArray(), InsertToken))
          return;
        HostFunction = instantiateIfViable(SemaRef, MemberFTD, Args, Loc);
      } else if (auto *MemberFD = dyn_cast<FunctionDecl>(Member)) {
        if (MemberFD->isUsed(/*CheckUsedAttr=*/false))
          return;
        HostFunction = MemberFD;
      }
      break;
    }
  }
  if (!HostFunction || HostFunction == Function || HostFunction->isDefined())
    return;
  if (auto *MSI = HostFunction->getMemberSpecializationInfo())
    MSI->setPointOfInstantiation(Loc);
  else
    HostFunction->setTemplateSpecializationKind(TSK_ImplicitInstantiation, Loc);
  HostViewTwins.insert(HostFunction);
  SemaRef.InstantiateFunctionDefinition(Loc, HostFunction, /*Recursive=*/true);
}

/// \p Target's copy of \p D, a declaration copied per target, if \p D is not
/// its own.
const NamedDecl *multitarget::getCopyForTarget(const ASTContext &Context,
                                               const NamedDecl *D,
                                               unsigned Target) {
  if (!D->getTargetVariant() || D->isRedundantTargetVariant() ||
      D->isCopyFor(Target))
    return nullptr;
  return Context.getMultiTarget().getCopyFor(D, Target);
}

bool SemaMultiTarget::namesOtherTargetsMemberType(QualType T) const {
  unsigned Target = SemaRef.Context.getMultiTarget().getInstantiationTarget();

  if (!Target)
    return false;
  while (const auto *TT = dyn_cast<TypedefType>(T.getTypePtr())) {
    const TypedefNameDecl *TND = TT->getDecl();
    if (isa<CXXRecordDecl>(TND->getDeclContext()) &&
        getCopyForTarget(SemaRef.Context, TND, Target))
      return true;
    T = TT->desugar();
  }
  return false;
}

static TemplateArgument
getArgumentForTarget(Sema &S, const TemplateArgument &Arg, unsigned Target,
                     SourceLocation Loc, unsigned Depth);

/// \p T with each class copied per target replaced by \p Target's copy.
static QualType getTypeForTargetCopies(Sema &S, QualType T, unsigned Target,
                                       SourceLocation Loc, unsigned Depth = 0) {
  if (T.isNull() || T->isDependentType() || Depth > 8)
    return T;
  ASTContext &Context = S.Context;
  QualType C = T.getCanonicalType();
  Qualifiers Quals = C.getQualifiers();
  C = C.getUnqualifiedType();
  if (const auto *PT = C->getAs<PointerType>()) {
    QualType Pointee =
        getTypeForTargetCopies(S, PT->getPointeeType(), Target, Loc, Depth + 1);
    if (Pointee == PT->getPointeeType())
      return T;
    return Context.getQualifiedType(Context.getPointerType(Pointee), Quals);
  }
  if (const auto *RT = C->getAs<ReferenceType>()) {
    QualType Pointee = getTypeForTargetCopies(S, RT->getPointeeTypeAsWritten(),
                                              Target, Loc, Depth + 1);
    if (Pointee == RT->getPointeeTypeAsWritten())
      return T;
    return isa<LValueReferenceType>(RT)
               ? Context.getLValueReferenceType(Pointee)
               : Context.getRValueReferenceType(Pointee);
  }
  if (const auto *CAT = Context.getAsConstantArrayType(C)) {
    QualType Element = getTypeForTargetCopies(S, CAT->getElementType(), Target,
                                              Loc, Depth + 1);
    if (Element == CAT->getElementType())
      return T;
    return Context.getQualifiedType(
        Context.getConstantArrayType(Element, CAT->getSize(), nullptr,
                                     CAT->getSizeModifier(),
                                     CAT->getIndexTypeCVRQualifiers()),
        Quals);
  }
  const auto *RD = C->getAsCXXRecordDecl();
  if (!RD)
    return T;
  if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RD)) {
    ClassTemplateDecl *CTD = CTSD->getSpecializedTemplate();
    const auto *CopyCTD =
        cast_or_null<ClassTemplateDecl>(getCopyForTarget(Context, CTD, Target));
    bool Changed = CopyCTD;
    TemplateArgumentListInfo ArgInfo(Loc, Loc);
    for (const TemplateArgument &Arg : CTSD->getTemplateArgs().asArray()) {
      TemplateArgument Mapped =
          getArgumentForTarget(S, Arg, Target, Loc, Depth + 1);
      Changed |= !Mapped.structurallyEquals(Arg);
      if (Mapped.getKind() != TemplateArgument::Pack) {
        ArgInfo.addArgument(
            S.getTrivialTemplateArgumentLoc(Mapped, QualType(), Loc));
        continue;
      }
      for (const TemplateArgument &Element : Mapped.pack_elements())
        ArgInfo.addArgument(
            S.getTrivialTemplateArgumentLoc(Element, QualType(), Loc));
    }
    if (!Changed)
      return T;
    ASTContext::TargetScope Scope(Context, Target);
    QualType Result = S.CheckTemplateIdType(
        ElaboratedTypeKeyword::None,
        TemplateName(const_cast<ClassTemplateDecl *>(CopyCTD ? CopyCTD : CTD)),
        Loc, ArgInfo, /*Scope=*/nullptr, /*ForNestedNameSpecifier=*/false);
    return Result.isNull() ? T : Context.getQualifiedType(Result, Quals);
  }
  if (const auto *Copy =
          dyn_cast_or_null<TagDecl>(getCopyForTarget(Context, RD, Target)))
    return Context.getQualifiedType(Context.getCanonicalTagType(Copy), Quals);
  return T;
}

static TemplateArgument
getArgumentForTarget(Sema &S, const TemplateArgument &Arg, unsigned Target,
                     SourceLocation Loc, unsigned Depth) {
  if (Arg.getKind() == TemplateArgument::Type) {
    QualType T = getTypeForTargetCopies(S, Arg.getAsType(), Target, Loc, Depth);
    return T == Arg.getAsType() ? Arg : TemplateArgument(T);
  }
  if (Arg.getKind() != TemplateArgument::Pack)
    return Arg;
  SmallVector<TemplateArgument, 4> Elements;
  bool Changed = false;
  for (const TemplateArgument &Element : Arg.pack_elements()) {
    Elements.push_back(getArgumentForTarget(S, Element, Target, Loc, Depth));
    Changed |= !Elements.back().structurallyEquals(Element);
  }
  return Changed ? TemplateArgument::CreatePackCopy(S.Context, Elements) : Arg;
}

void SemaMultiTarget::instantiateKernelForClassCopies(
    SourceLocation PointOfInstantiation, FunctionDecl *Function) {
  FunctionTemplateDecl *FTD = Function->getPrimaryTemplate();
  const TemplateArgumentList *Args = Function->getTemplateSpecializationArgs();
  // A device's own instantiation is already the copy.
  if (!FTD || !Args || Function->getTargetVariant() ||
      KernelClassCopies.count(Function))
    return;
  bool Explicit = Function->getTemplateSpecializationKind() ==
                  TSK_ExplicitInstantiationDefinition;
  for (unsigned V : SemaRef.Context.getDeviceVariants()) {
    TargetAnalysisScope Scope(*this, V);
    SmallVector<TemplateArgument, 4> Mapped;
    bool Changed = false;
    for (const TemplateArgument &Arg : Args->asArray()) {
      Mapped.push_back(
          getArgumentForTarget(SemaRef, Arg, V, PointOfInstantiation, 0));
      Changed |= !Mapped.back().structurallyEquals(Arg);
    }
    if (!Changed)
      continue;
    // The classes are this target's, complete as the call needed them.
    struct : Sema::TypeDiagnoser {
      void diagnose(Sema &, SourceLocation, QualType) override {}
    } Silent;
    if (llvm::any_of(Mapped, [&](const TemplateArgument &Arg) {
          return Arg.getKind() == TemplateArgument::Type &&
                 Arg.getAsType()->isRecordType() &&
                 SemaRef.RequireCompleteType(PointOfInstantiation,
                                             Arg.getAsType(), Silent);
        }))
      continue;
    FunctionDecl *Copy = SemaRef.InstantiateFunctionDeclaration(
        FTD, TemplateArgumentList::CreateCopy(SemaRef.Context, Mapped),
        PointOfInstantiation);
    if (!Copy || Copy == Function || Copy->isDefined())
      continue;
    KernelClassCopies.insert(Copy);
    if (Function->isReferenced())
      Copy->setReferenced();
    if (Function->isUsed(/*CheckUsedAttr=*/false))
      Copy->markUsed(SemaRef.Context);
    if (Explicit)
      Copy->setTemplateSpecializationKind(TSK_ExplicitInstantiationDefinition,
                                          PointOfInstantiation);
    SemaRef.InstantiateFunctionDefinition(PointOfInstantiation, Copy,
                                          /*Recursive=*/true);
    if (Explicit)
      SemaRef.Consumer.HandleTopLevelDecl(DeclGroupRef(Copy));
  }
}

/// The member of \p Copy declared by the same tokens as \p Member, a member
/// of a specialization of another copy of the same class template.
static FunctionDecl *findCopyMember(Sema &S, FunctionDecl *Member,
                                    CXXRecordDecl *Copy, SourceLocation Loc) {
  FunctionTemplateDecl *FTD = Member->getPrimaryTemplate();
  const NamedDecl *Pattern = FTD ? static_cast<const NamedDecl *>(FTD) : Member;
  // A copy from another #if arm is declared elsewhere, and is the member with
  // the same signature.
  auto SameSignature = [](const FunctionDecl *A, const FunctionDecl *B) {
    auto Side = [](const FunctionDecl *F) {
      return std::make_tuple(F->hasAttr<CUDAHostAttr>(),
                             F->hasAttr<CUDADeviceAttr>(),
                             F->hasAttr<CUDAGlobalAttr>());
    };
    const auto *MA = dyn_cast<CXXMethodDecl>(A);
    const auto *MB = dyn_cast<CXXMethodDecl>(B);
    if (A->getNumParams() != B->getNumParams() || Side(A) != Side(B) ||
        (MA && MB && MA->isStatic() != MB->isStatic()))
      return false;
    for (unsigned I = 0, E = A->getNumParams(); I != E; ++I)
      if (A->getParamDecl(I)->getType().getCanonicalType().getAsString() !=
          B->getParamDecl(I)->getType().getCanonicalType().getAsString())
        return false;
    return true;
  };
  // A constructor or destructor is named after its own class.
  auto SameName = [&](const NamedDecl *ND) {
    DeclarationName Name = ND->getDeclName();
    switch (Name.getNameKind()) {
    case DeclarationName::CXXConstructorName:
    case DeclarationName::CXXDestructorName:
      return Name.getNameKind() == Pattern->getDeclName().getNameKind();
    default:
      return Name == Pattern->getDeclName();
    }
  };
  NamedDecl *Found = nullptr;
  unsigned Matches = 0;
  for (Decl *D : Copy->decls()) {
    auto *ND = dyn_cast<NamedDecl>(D);
    if (!ND || ND->getKind() != Pattern->getKind() || !SameName(ND))
      continue;
    if (ND->getLocation() == Pattern->getLocation()) {
      Found = ND;
      Matches = 1;
      break;
    }
    if (SameSignature(Pattern->getAsFunction(), ND->getAsFunction())) {
      Found = ND;
      ++Matches;
    }
  }
  if (!Found || Matches != 1)
    return nullptr;
  if (!FTD)
    return cast<FunctionDecl>(Found);
  return S.InstantiateFunctionDeclaration(
      cast<FunctionTemplateDecl>(Found),
      Member->getTemplateSpecializationArgs(), Loc);
}

void SemaMultiTarget::instantiateMemberForClassCopies(
    SourceLocation PointOfInstantiation, FunctionDecl *Function) {
  const ClassTemplateDecl *Copy = getClassCopy(Function);
  if (!Copy || ClassCopyMembers.count(Function) ||
      Function->isRedundantTargetVariant() ||
      SemaRef.CUDA().IdentifyTarget(Function) == CUDAFunctionTarget::Host)
    return;
  auto *Class =
      cast<ClassTemplateSpecializationDecl>(Function->getDeclContext());
  ClassTemplateDecl *CTD = Class->getSpecializedTemplate();
  for (unsigned V : SemaRef.Context.getDeviceVariants()) {
    if (!getCopyForTarget(SemaRef.Context, Copy, V))
      continue;
    TargetAnalysisScope Scope(*this, V);
    FunctionDecl *Member = nullptr;
    {
      Sema::SFINAETrap Trap(SemaRef);
      // Copies sharing their specializations specialize per target, and
      // others have specializations of their own.
      CXXRecordDecl *CopyClass = getOrCreateTargetForkedSpecialization(
          SemaRef, CTD, Class->getTemplateArgs().asArray(),
          Class->hasStrictPackMatch(), PointOfInstantiation);
      if (CopyClass == Class && !CTD->hasTargetKeyedSpecializations()) {
        QualType CopyType = getTypeForTargetCopies(
            SemaRef, SemaRef.Context.getCanonicalTagType(Class), V,
            PointOfInstantiation);
        CopyClass = CopyType->getAsCXXRecordDecl();
        if (CopyClass && CopyClass != Class)
          SemaRef.RequireCompleteType(PointOfInstantiation, CopyType,
                                      diag::err_incomplete_type);
      }
      if (!Trap.hasErrorOccurred() && CopyClass && CopyClass != Class &&
          CopyClass->isCompleteDefinition())
        Member =
            findCopyMember(SemaRef, Function, CopyClass, PointOfInstantiation);
      if (Trap.hasErrorOccurred())
        continue;
    }
    if (!Member || Member == Function || Member->isDefined() ||
        !ClassCopyMembers.insert(Member).second)
      continue;
    if (Function->isReferenced())
      Member->setReferenced();
    if (Function->isUsed(/*CheckUsedAttr=*/false))
      Member->markUsed(SemaRef.Context);
    llvm::SaveAndRestore Speculative(InClassCopyMember, true);
    // Made in case the device needs it, it is an error only if emitted.
    TargetDiagnosticCapture Capture;
    {
      llvm::SaveAndRestore Capturing(CurrentTargetDiagnosticCapture, &Capture);
      SemaRef.InstantiateFunctionDefinition(PointOfInstantiation, Member,
                                            /*Recursive=*/true);
    }
    if (Capture.ErrorOccurred)
      SemaRef.Context.getMultiTarget().addFailedTargetInstantiation(
          Member, TargetSet::single(V), Capture.Diags);
    else
      for (const PartialDiagnosticAt &PD : Capture.Diags)
        Diag(PD.first, PD.second);
  }
}

void SemaMultiTarget::instantiateForOtherDeviceTargets(
    SourceLocation PointOfInstantiation, FunctionDecl *Function) {
  FunctionTemplateDecl *FTD = Function->getPrimaryTemplate();
  const TemplateArgumentList *Args = Function->getTemplateSpecializationArgs();
  unsigned Canonical = SemaRef.Context.getCanonicalDeviceVariant();
  if (!FTD || !Args)
    return;
  unsigned Key = SemaRef.Context.getMultiTarget().getTargetInstantiationKey(
      Function->getCanonicalDecl());
  unsigned Host = SemaRef.Context.getHostVariant();
  unsigned First = 1, Last = 1 + SemaRef.Context.getNumAuxTargets();
  if (Key && Key == Host) {
    // An explicit instantiation for the host of a function the devices can
    // call stands for theirs too, and so does the host's specialization of a
    // kernel it launches (see AddTemplateOverloadCandidateImmediately). The
    // canonical device's instantiates the other devices'.
    if (!Function->hasAttr<CUDAGlobalAttr>() &&
        (Function->getTemplateSpecializationKind() !=
             TSK_ExplicitInstantiationDefinition ||
         !Function->hasAttr<CUDADeviceAttr>()))
      return;
    First = Last = Canonical;
  } else if (Key != Canonical || SemaRef.Context.getNumDeviceTargets() < 2 ||
             SemaRef.Context.getMultiTarget().getSpecializationTargetDependence(
                 FTD, Args->asArray()) != MultiTargetContext::TDK_Arch) {
    return;
  }
  // Instantiated for one device's copy of what needs it, the other devices
  // have copies of their own, which need their own specializations, if any.
  TargetSet Requesters = getRequestingTargets(Function);
  RequestingTargetsUsed[Function->getCanonicalDecl()] = Requesters;
  for (unsigned V = First; V <= Last; ++V) {
    if (!SemaRef.Context.isDeviceVariant(V) || !Requesters.contains(V))
      continue;
    // The host's specialization stands for one already declared for the
    // device, if any, e.g. by deduction that then chose the host's.
    FunctionDecl *Copy =
        FTD->findSpecializationForTargetKey(Args->asArray(), V);
    if (V == Key || (Copy && Key != Host))
      continue;
    // Entering V's scope keys the new declaration by V. So does forcing V: the
    // declaration is instantiated in the template's context, which a re-parse
    // claimed for the primary target.
    ASTContext::TargetScope Scope(SemaRef.Context, V);
    if (!Copy) {
      llvm::SaveAndRestore Forced(ForcedInstantiationTarget, V);
      Copy = instantiateIfViable(SemaRef, FTD, Args, PointOfInstantiation);
    }
    if (!Copy || Copy == Function)
      continue;
    if (Function->isReferenced())
      Copy->setReferenced();
    if (Function->isUsed(/*CheckUsedAttr=*/false))
      Copy->markUsed(SemaRef.Context);
    // An explicit instantiation definition is emitted whether or not it is
    // used, and so is each target's copy of it.
    bool Explicit = Function->getTemplateSpecializationKind() ==
                    TSK_ExplicitInstantiationDefinition;
    if (Explicit)
      Copy->setTemplateSpecializationKind(TSK_ExplicitInstantiationDefinition,
                                          PointOfInstantiation);
    SemaRef.InstantiateFunctionDefinition(PointOfInstantiation, Copy,
                                          /*Recursive=*/true);
    if (Explicit)
      SemaRef.Consumer.HandleTopLevelDecl(DeclGroupRef(Copy));
  }
}

TargetSet SemaMultiTarget::getRequestingTargets(const FunctionDecl *Function,
                                                bool FollowInstantiations,
                                                unsigned Depth) {
  auto It = InstantiationRequesters.find(Function->getCanonicalDecl());
  auto For = RequestedForTargets.find(Function->getCanonicalDecl());
  if ((It == InstantiationRequesters.end() &&
       For == RequestedForTargets.end()) ||
      Depth > 8)
    return TargetSet::all();
  unsigned Host = SemaRef.Context.getHostVariant();
  TargetSet Targets;
  if (For != RequestedForTargets.end())
    Targets = For->second;
  if (It == InstantiationRequesters.end())
    return Targets;
  for (const FunctionDecl *Requester : It->second) {
    // The targets whose code generation emits the requester, as CodeGen
    // decides. Host code (e.g. launching a kernel) stands for every device,
    // and so does a specialization keyed by the host.
    if (!Requester)
      return TargetSet::all();
    if (unsigned Key =
            SemaRef.Context.getMultiTarget().getEffectiveTargetInstantiationKey(
                Requester->getCanonicalDecl())) {
      if (Key == Host)
        return TargetSet::all();
      Targets = Targets |
                SemaRef.Context.getMultiTarget().getTargetInstantiationTargets(
                    Requester->getCanonicalDecl());
      continue;
    }
    if (Requester->getTargetVariant()) {
      if (Requester->isRedundantTargetVariant() || Requester->isCopyFor(Host))
        return TargetSet::all();
      Targets = Targets | Requester->getTargetSet();
      continue;
    }
    if (Requester->isTemplateInstantiation() && FollowInstantiations)
      // An instantiation for every target is for those requesting it.
      Targets = Targets | getRequestingTargets(Requester, FollowInstantiations,
                                               Depth + 1);
    else
      return TargetSet::all();
  }
  return Targets;
}

void SemaMultiTarget::noteInstantiationRequester(const FunctionDecl *Func) {
  if (!SemaRef.Context.isMultiTargetCompile())
    return;
  const FunctionDecl *Requester = nullptr;
  for (const Sema::CodeSynthesisContext &SC :
       llvm::reverse(SemaRef.CodeSynthesisContexts)) {
    const auto *Entity = dyn_cast_or_null<FunctionDecl>(SC.Entity);
    if (SC.Kind != Sema::CodeSynthesisContext::TemplateInstantiation ||
        !Entity || Entity == Func)
      continue;
    Requester = Entity;
    break;
  }
  if (!Requester)
    Requester = SemaRef.getCurFunctionDecl(/*AllowLambda=*/true);
  auto &Requesters = InstantiationRequesters[Func->getCanonicalDecl()];
  if (llvm::is_contained(Requesters, Requester))
    return;
  Requesters.push_back(Requester);
  if (Requester)
    RequestedFunctions[Requester->getCanonicalDecl()].push_back(
        Func->getCanonicalDecl());
  propagateRequestingTargets(Func);
}

void SemaMultiTarget::noteRequestedForTarget(const FunctionDecl *Func,
                                             unsigned Variant) {
  TargetSet &Targets = RequestedForTargets[Func->getCanonicalDecl()];
  if (Targets.contains(Variant))
    return;
  Targets.insert(Variant);
  propagateRequestingTargets(Func);
}

void SemaMultiTarget::propagateRequestingTargets(const FunctionDecl *Func) {
  // An instantiation made for the code requesting it so far is made for the
  // targets this code stands for too, and so is what it requested.
  SmallVector<const FunctionDecl *, 8> Worklist{Func->getCanonicalDecl()};
  llvm::SmallPtrSet<const FunctionDecl *, 16> Visited;
  while (!Worklist.empty()) {
    const FunctionDecl *F = Worklist.pop_back_val();
    if (!Visited.insert(F).second)
      continue;
    auto Used = RequestingTargetsUsed.find(F);
    if (Used == RequestingTargetsUsed.end())
      continue;
    TargetSet Now = getRequestingTargets(F);
    if (Now == Used->second)
      continue;
    Used->second = Now;
    const FunctionDecl *Def = nullptr;
    if (F->isDefined(Def)) {
      auto *Definition = const_cast<FunctionDecl *>(Def);
      Sema::ContextRAII InDefinition(SemaRef, Definition);
      llvm::SaveAndRestore NotForced(ForcedInstantiationTarget, 0u);
      instantiateTargetParts(Definition);
      instantiateForOtherDeviceTargets(Definition->getPointOfInstantiation(),
                                       Definition);
    }
    if (auto It = RequestedFunctions.find(F); It != RequestedFunctions.end())
      Worklist.append(It->second.begin(), It->second.end());
  }
}

void SemaMultiTarget::instantiateInOtherTargetCopies(
    SourceLocation Loc, FunctionDecl *Specialization,
    TemplateSpecializationKind TSK) {
  FunctionTemplateDecl *FTD = Specialization->getPrimaryTemplate();
  const TemplateArgumentList *Args =
      Specialization->getTemplateSpecializationArgs();
  if (!FTD || !Args || !FTD->getTargetVariant() ||
      FTD->isRedundantTargetVariant())
    return;
  SmallVector<const NamedDecl *, 4> Copies;
  SemaRef.Context.getMultiTarget().getTargetCopies(FTD, Copies);
  llvm::SmallPtrSet<FunctionDecl *, 4> Done;
  for (const NamedDecl *Copy : Copies) {
    auto *CopyFTD = dyn_cast<FunctionTemplateDecl>(Copy);
    if (!CopyFTD || CopyFTD == FTD || !Copy->getTargetVariant() ||
        Copy->isRedundantTargetVariant())
      continue;
    // Each target the copy stands for, as its specializations may be keyed by
    // target.
    for (unsigned V : SemaRef.Context.getTargetVariants()) {
      if (!Copy->isCopyFor(V))
        continue;
      TargetAnalysisScope Scope(*this, V);
      FunctionDecl *CopySpec = SemaRef.InstantiateFunctionDeclaration(
          const_cast<FunctionTemplateDecl *>(CopyFTD), Args, Loc);
      if (!CopySpec || CopySpec == Specialization ||
          !Done.insert(CopySpec).second)
        continue;
      CopySpec->setTemplateSpecializationKind(TSK, Loc);
      if (TSK != TSK_ExplicitInstantiationDefinition)
        continue;
      SemaRef.InstantiateFunctionDefinition(Loc, CopySpec);
      SemaRef.Consumer.HandleTopLevelDecl(DeclGroupRef(CopySpec));
    }
  }
}

void SemaMultiTarget::instantiateTargetCopySpecializations() {
  Sema::ContextRAII SavedContext(SemaRef,
                                 SemaRef.Context.getTranslationUnitDecl());
  for (size_t Done = 0; Done != SemaRef.Context.getMultiTarget()
                                    .getTargetCopySpecializations()
                                    .size();) {
    size_t End =
        SemaRef.Context.getMultiTarget().getTargetCopySpecializations().size();
    for (; Done != End; ++Done)
      instantiateTargetCopySpecialization(
          SemaRef.Context.getMultiTarget().getTargetCopySpecializations()[Done],
          /*Now=*/false);
    SemaRef.PerformPendingInstantiations();
  }
}

/// The member templates of the other copies of \p FTD's class that declare
/// what \p FTD does, and for a friend the friends they declare. Lookup finds
/// a member of an object's own class only, and a friend's signature names its
/// own copy of the class, but each target compiles its own copy's
/// specializations.
static void
addClassCopyMemberTemplates(const ASTContext &Context,
                            const FunctionTemplateDecl *FTD,
                            SmallVectorImpl<const NamedDecl *> &Copies) {
  bool Friend = FTD->getFriendObjectKind();
  const auto *Owner = dyn_cast<CXXRecordDecl>(
      Friend ? FTD->getLexicalDeclContext() : FTD->getDeclContext());
  if (!Owner || !Owner->getTargetVariant() || Owner->isRedundantTargetVariant())
    return;
  const FunctionDecl *FD = FTD->getTemplatedDecl();
  QualType Type = Context.getCanonicalType(FD->getType());
  unsigned NumParams = FTD->getTemplateParameters()->size();
  auto Declares = [&](const FunctionTemplateDecl *Other) {
    const FunctionDecl *OtherFD = Other->getTemplatedDecl();
    return Other->getDeclName() == FTD->getDeclName() &&
           Other->getTemplateParameters()->size() == NumParams &&
           (Friend ? OtherFD->getNumParams() == FD->getNumParams()
                   : Context.getCanonicalType(OtherFD->getType()) == Type) &&
           !llvm::is_contained(Copies, Other);
  };
  SmallVector<const NamedDecl *, 4> Owners;
  Context.getMultiTarget().getTargetCopies(Owner, Owners);
  for (const NamedDecl *OwnerCopy : Owners) {
    if (OwnerCopy == Owner)
      continue;
    const auto *Class = cast<CXXRecordDecl>(OwnerCopy);
    if (Friend) {
      for (const FriendDecl *FrD : Class->friends())
        if (const auto *Other =
                dyn_cast_or_null<FunctionTemplateDecl>(FrD->getFriendDecl());
            Other && Declares(Other))
          Copies.push_back(Other);
      continue;
    }
    for (const NamedDecl *Member : Class->lookup(FTD->getDeclName()))
      if (const auto *Other = dyn_cast<FunctionTemplateDecl>(Member);
          Other && Declares(Other))
        Copies.push_back(Other);
  }
}

void SemaMultiTarget::instantiateTargetCopySpecialization(FunctionDecl *Spec,
                                                          bool Now) {
  FunctionTemplateDecl *FTD = Spec->getPrimaryTemplate();
  const TemplateArgumentList *Args = Spec->getTemplateSpecializationArgs();
  // A host function's devices' copies only matter for the kernels it
  // launches, which are instantiated per target already.
  // Widened copies, parsed from each target's own tokens, are instantiated
  // per target by the specializations' keys already, except a kernel's,
  // which host code names for every device, and one keyed for fewer
  // devices than the code naming it is for.
  bool Widened = FTD && !SemaRef.Context.getMultiTarget().isReparseCopy(FTD);
  bool Kernel = Spec->hasAttr<CUDAGlobalAttr>();
  if (Now && !(Widened && Kernel))
    return;
  TargetSet Requesters = getRequestingTargets(Spec);
  bool ForOthers = false;
  if (Spec->getTargetVariant())
    for (unsigned V : SemaRef.Context.getTargetVariants())
      ForOthers |= SemaRef.Context.isDeviceVariant(V) &&
                   Requesters.contains(V) && !Spec->isCopyFor(V);
  if (!FTD || !Args || !Spec->isUsed(/*CheckUsedAttr=*/false) ||
      (Widened && !Kernel && !ForOthers) ||
      Spec->getTemplateSpecializationKind() == TSK_ExplicitSpecialization ||
      SemaRef.CUDA().IdentifyTarget(Spec) == CUDAFunctionTarget::Host)
    return;
  SourceLocation Loc = Spec->getPointOfInstantiation();
  SmallVector<const NamedDecl *, 4> Copies;
  SemaRef.Context.getMultiTarget().getTargetCopies(FTD, Copies);
  // Copies redeclaring one declaration are chained, and lookup finds only
  // the most recent of them.
  for (const RedeclarableTemplateDecl *R : FTD->redecls())
    if (R->getTargetVariant() && !llvm::is_contained(Copies, R))
      Copies.push_back(R);
  addClassCopyMemberTemplates(SemaRef.Context, FTD, Copies);
  for (const NamedDecl *Copy : Copies) {
    auto *CopyFTD = dyn_cast<FunctionTemplateDecl>(Copy);
    if (!CopyFTD || !Copy->getTargetVariant() ||
        Copy->isRedundantTargetVariant())
      continue;
    // Every device the copy stands for, also one whose copy was merged
    // into it, and which Spec does not stand for itself.
    for (unsigned V : SemaRef.Context.getDeviceVariants()) {
      if (!Copy->isCopyFor(V) ||
          (Spec->getTargetVariant() && Spec->isCopyFor(V)) ||
          (Widened && !Kernel && !Requesters.contains(V)))
        continue;
      TargetAnalysisScope Scope(*this, V);
      FunctionDecl *CopySpec;
      {
        // The target's copy may have no such specialization (e.g. one
        // enable_if'd on a configuration of its own).
        sema::TemplateDeductionInfo Info(Loc);
        Sema::SFINAETrap Trap(SemaRef, Info);
        CopySpec = SemaRef.InstantiateFunctionDeclaration(
            const_cast<FunctionTemplateDecl *>(CopyFTD), Args, Loc);
        if (!CopySpec || Trap.hasErrorOccurred() || CopySpec->isInvalidDecl() ||
            CopySpec->isDefined() || CopySpec->isUsed(/*CheckUsedAttr=*/false))
          continue;
      }
      CopySpec->setIsUsed();
      PendingInstantiationTargetVariant[CopySpec] = V;
      if (Now)
        SemaRef.PendingInstantiations.push_front({CopySpec, Loc});
      else
        SemaRef.PendingInstantiations.push_back({CopySpec, Loc});
    }
  }
}

/// \p Sibling's counterpart of \p Member, a member of another specialization
/// of the same class template.
static CXXMethodDecl *
findSiblingMember(Sema &S, ClassTemplateSpecializationDecl *Sibling,
                  CXXMethodDecl *Member) {
  if (!Member->isImplicit()) {
    FunctionDecl *Pattern = Member->getInstantiatedFromMemberFunction();
    for (Decl *D : Sibling->decls())
      if (auto *MD = dyn_cast<CXXMethodDecl>(D);
          MD && Pattern && MD->getInstantiatedFromMemberFunction() == Pattern)
        return MD;
    return nullptr;
  }
  if (isa<CXXDestructorDecl>(Member))
    return S.LookupDestructor(Sibling);
  if (auto *Ctor = dyn_cast<CXXConstructorDecl>(Member)) {
    if (Ctor->isDefaultConstructor())
      return S.LookupDefaultConstructor(Sibling);
    unsigned Quals = 0;
    if (Ctor->isCopyOrMoveConstructor(Quals))
      return Ctor->isMoveConstructor()
                 ? S.LookupMovingConstructor(Sibling, Quals)
                 : S.LookupCopyingConstructor(Sibling, Quals);
  }
  return nullptr;
}

void SemaMultiTarget::keySpecializationsMadeTooEarly() {
  if (!SemaRef.Context.getMultiTarget().hasTargetDependenceEscalations())
    return;
  // This runs as a definition is finished, inside a template perhaps, but
  // what it instantiates is used from wherever the specializations were.
  Sema::ContextRAII SavedContext(SemaRef,
                                 SemaRef.Context.getTranslationUnitDecl());
  EnterExpressionEvaluationContext Evaluated(
      SemaRef, Sema::ExpressionEvaluationContext::PotentiallyEvaluated);
  unsigned Canonical = SemaRef.Context.getCanonicalDeviceVariant();
  unsigned HostKey = SemaRef.Context.getHostVariant();
  unsigned Last = 1 + SemaRef.Context.getNumAuxTargets();
  // Whether code for target \p Key may call \p FD.
  auto IsCallableFor = [&](const FunctionDecl *FD, unsigned Key) {
    CUDAFunctionTarget FT = SemaRef.CUDA().IdentifyTarget(FD);
    if (!SemaRef.Context.isDeviceVariant(Key))
      return FT != CUDAFunctionTarget::Device &&
             FT != CUDAFunctionTarget::Global;
    return FT != CUDAFunctionTarget::Host;
  };
  // A specialization made before its template was known to depend on the
  // target stands for the devices, or the host if it only has host code, and
  // every other target that needs its own gets one, using what it did.
  for (const Decl *D :
       SemaRef.Context.getMultiTarget().takeTargetDependenceEscalations()) {
    bool Arch = SemaRef.Context.getMultiTarget().getTargetDependence(D) ==
                MultiTargetContext::TDK_Arch;
    auto OtherKeys = [&](bool Host, bool Device) {
      SmallVector<unsigned, 4> Keys;
      if (Host && Device)
        Keys.push_back(HostKey);
      if (Device && Arch)
        for (unsigned V = 1; V <= Last; ++V)
          if (SemaRef.Context.isDeviceVariant(V) && V != Canonical)
            Keys.push_back(V);
      return Keys;
    };

    if (auto *FTD = dyn_cast<FunctionTemplateDecl>(const_cast<Decl *>(D))) {
      if (FTD->hasTargetKeyedSpecializations())
        continue;
      SmallVector<FunctionDecl *, 4> Specs;
      for (FunctionDecl *Spec : FTD->specializations())
        if (Spec->getTemplateSpecializationKind() ==
                TSK_ImplicitInstantiation &&
            !SemaRef.Context.getMultiTarget().getTargetInstantiationKey(
                Spec->getCanonicalDecl()))
          Specs.push_back(Spec->getCanonicalDecl());
      if (Specs.empty())
        continue;
      bool Host = HostKey && IsCallableFor(FTD->getTemplatedDecl(), HostKey);
      bool Device = IsCallableFor(FTD->getTemplatedDecl(), Canonical);
      for (FunctionDecl *Spec : Specs)
        SemaRef.Context.getMultiTarget().setTargetInstantiationKey(
            Spec, Device || !Host ? Canonical : HostKey);
      FTD->rehashSpecializations();
      for (FunctionDecl *Spec : Specs) {
        const TemplateArgumentList *Args =
            Spec->getTemplateSpecializationArgs();
        for (unsigned Key : OtherKeys(Host, Device)) {
          if (FTD->findSpecializationForTargetKey(Args->asArray(), Key))
            continue;
          TargetAnalysisScope Scope(*this, Key);
          FunctionDecl *Copy = SemaRef.InstantiateFunctionDeclaration(
              FTD, Args, Spec->getLocation());
          if (Copy && Copy != Spec && Spec->isUsed(/*CheckUsedAttr=*/false))
            SemaRef.MarkFunctionReferenced(Spec->getLocation(), Copy);
        }
      }
      continue;
    }

    auto *CTD = cast<ClassTemplateDecl>(const_cast<Decl *>(D));
    if (CTD->hasTargetKeyedSpecializations())
      continue;
    SmallVector<ClassTemplateSpecializationDecl *, 4> Specs;
    for (ClassTemplateSpecializationDecl *Spec : CTD->specializations())
      if (Spec->getSpecializationKind() == TSK_ImplicitInstantiation &&
          !SemaRef.Context.getMultiTarget().getTargetInstantiationKey(
              Spec->getCanonicalDecl()))
        Specs.push_back(Spec);
    if (Specs.empty())
      continue;
    for (ClassTemplateSpecializationDecl *Spec : Specs) {
      SemaRef.Context.getMultiTarget().setTargetInstantiationKey(
          Spec->getCanonicalDecl(), Canonical);
      SemaRef.Context.getMultiTarget().setSpecializationKeyVariant(Spec,
                                                                   Canonical);
    }
    CTD->rehashSpecializations();
    for (ClassTemplateSpecializationDecl *Spec : Specs) {
      ArrayRef<TemplateArgument> Args = Spec->getTemplateArgs().asArray();
      SourceLocation Loc = Spec->getPointOfInstantiation().isValid()
                               ? Spec->getPointOfInstantiation()
                               : Spec->getLocation();
      for (unsigned Key : OtherKeys(/*Host=*/HostKey != 0, /*Device=*/true)) {
        if (CTD->findSpecializationForTargetKey(Args, Key))
          continue;
        TargetAnalysisScope Scope(*this, Key);
        TemplateArgumentListInfo ArgInfo(Loc, Loc);
        for (const TemplateArgument &Arg : Args) {
          if (Arg.getKind() != TemplateArgument::Pack) {
            ArgInfo.addArgument(
                SemaRef.getTrivialTemplateArgumentLoc(Arg, QualType(), Loc));
            continue;
          }
          for (const TemplateArgument &Element : Arg.pack_elements())
            ArgInfo.addArgument(SemaRef.getTrivialTemplateArgumentLoc(
                Element, QualType(), Loc));
        }
        QualType T = SemaRef.CheckTemplateIdType(
            ElaboratedTypeKeyword::None, TemplateName(CTD), Loc, ArgInfo,
            /*Scope=*/nullptr,
            /*ForNestedNameSpecifier=*/false);
        auto *Sibling = T.isNull()
                            ? nullptr
                            : dyn_cast_or_null<ClassTemplateSpecializationDecl>(
                                  T->getAsCXXRecordDecl());
        if (!Sibling || Sibling == Spec || !Spec->hasDefinition() ||
            SemaRef.RequireCompleteType(Loc, T, diag::err_incomplete_type))
          continue;
        for (Decl *Member : Spec->decls()) {
          if (auto *MD = dyn_cast<CXXMethodDecl>(Member)) {
            if (!MD->isUsed(/*CheckUsedAttr=*/false))
              continue;
            if (CXXMethodDecl *SMD = findSiblingMember(SemaRef, Sibling, MD);
                SMD && IsCallableFor(SMD, Key))
              SemaRef.MarkFunctionReferenced(Loc, SMD);
          } else if (auto *MemberFTD = dyn_cast<FunctionTemplateDecl>(Member)) {
            FunctionTemplateDecl *Pattern =
                MemberFTD->getInstantiatedFromMemberTemplate();
            FunctionTemplateDecl *SiblingFTD = nullptr;
            for (Decl *SD : Sibling->decls())
              if (auto *F = dyn_cast<FunctionTemplateDecl>(SD);
                  F && Pattern &&
                  F->getInstantiatedFromMemberTemplate() == Pattern)
                SiblingFTD = F;
            if (!SiblingFTD)
              continue;
            SmallVector<FunctionDecl *, 4> Used;
            for (FunctionDecl *FS : MemberFTD->specializations())
              if (FS->isUsed(/*CheckUsedAttr=*/false))
                Used.push_back(FS);
            for (FunctionDecl *FS : Used)
              if (FunctionDecl *Copy = SemaRef.InstantiateFunctionDeclaration(
                      SiblingFTD, FS->getTemplateSpecializationArgs(), Loc);
                  Copy && IsCallableFor(Copy, Key))
                SemaRef.MarkFunctionReferenced(Loc, Copy);
          }
        }
      }
    }
  }
}
