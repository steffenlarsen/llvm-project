//===--- SemaTargetAudit.cpp - Audit of target-divergent inputs -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A multi-target compilation analyses a unit (a top-level declaration, an
// instantiation) once for every target, as the primary target sees it.
// That is right for every target only if the unit consumed nothing whose
// answer differs between them. This records, per unit, what it consumed that
// does; a top-level declaration that consumed any such input is parsed again
// for each other target (see TargetAlternationReconciler::Observe).
//
// -mllvm -multi-target-audit=<file> appends a report of the units still
// analysed once although they consumed such an input.
//
//===----------------------------------------------------------------------===//

#include "TargetAudit.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclFingerprint.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Sema/Scope.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaCUDA.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <map>

using namespace clang;

static llvm::cl::opt<std::string> MultiTargetAudit(
    "multi-target-audit", llvm::cl::Hidden, llvm::cl::init(""),
    llvm::cl::desc("Append to this file what each unit of a multi-target "
                   "compilation consumed that differs between targets"));

static llvm::cl::opt<std::string> MultiTargetAuditJoins(
    "multi-target-audit-joins", llvm::cl::Hidden, llvm::cl::init(""),
    llvm::cl::desc("Append to this file, for each merge of targets' copies "
                   "of a declaration, whether the inputs its parse consumed "
                   "allowed it"));

void Sema::initTargetAudit() {
  if (!Context.isMultiTargetCompile())
    return;
  TargetAuditor = std::make_unique<TargetAudit>();
  TargetAuditor->Path = MultiTargetAudit;
  Context.JoinAuditPath = MultiTargetAuditJoins;
  Context.RecordGroupInputs |= !MultiTargetAuditJoins.empty();
  SmallVector<const TargetInfo *, 4> Targets;
  for (unsigned V = 1; V <= 1 + Context.getNumAuxTargets(); ++V)
    Targets.push_back(Context.getTargetForVariant(V));
  TargetAuditor->Divergence = std::make_unique<TargetDivergence>(Targets);
  Context.setTargetAuditHook(
      [](void *S, char Kind, ASTContext::TargetPartition Targets,
         const Decl *D) {
        auto &Self = *static_cast<Sema *>(S);
        const auto *ND = dyn_cast_or_null<NamedDecl>(D);
        std::string What = ND ? ND->getQualifiedNameAsString() : "";
        // A specialization keyed by target that the shared parse names is the
        // parse's target's. That matters to the code naming it where the
        // copies of its template declare different things, or its default
        // arguments differ; member bodies are each target's own anyway.
        if (Kind == 'K') {
          ASTContext::TargetPartition Split = 0;
          if (const auto *CTD = dyn_cast_or_null<ClassTemplateDecl>(D))
            Split = Self.Context.refineTargetPartition(
                Self.Context.getClassTemplateShapePartition(CTD),
                Self.Context.getDefaultArgumentDependence(CTD));
          Self.auditTargetInput(Kind, Split, What, /*Owner=*/nullptr, Targets);
          return;
        }
        // A template's definition evaluates the copy its instantiations
        // evaluate again for their own targets.
        if (Kind == 'e') {
          if (Self.CurContext->isDependentContext())
            return;
          Kind = 'E';
        }
        Self.auditTargetInput(Kind, Targets, What);
      },
      this);
}

ASTContext::TargetPartition
Sema::getBodyTargetSplit(const FunctionDecl *FD) const {
  if (!TargetAuditor)
    return 0;
  return TargetAuditor->Split.lookup(FD->getCanonicalDecl());
}

/// Whether \p Targets tells the host apart from the canonical device.
static bool splitsSide(const ASTContext &Ctx,
                       ASTContext::TargetPartition Targets) {
  return Ctx.getHostVariant() &&
         Ctx.separatesTargets(Targets, Ctx.getHostVariant(),
                              Ctx.getCanonicalDeviceVariant());
}

/// Whether \p Targets tells device architectures apart.
static bool splitsArch(const ASTContext &Ctx,
                       ASTContext::TargetPartition Targets) {
  for (unsigned V = 1; V <= 1 + Ctx.getNumAuxTargets(); ++V)
    if (Ctx.isDeviceVariant(V) &&
        Ctx.separatesTargets(Targets, V, Ctx.getCanonicalDeviceVariant()))
      return true;
  return false;
}

/// Whether \p In needs no per-target analysis of its unit: it only decides
/// how a diagnostic is reported ('D'), the call it decides was resolved for
/// each side where it was made ('R'), or it names a specialization keyed by
/// target ('K'), which a unit analysed for every target names per target.
static bool isHandledInput(const TargetAudit::Input &In) {
  return In.Kind == 'D' || In.Kind == 'R' || In.Kind == 'K';
}

void Sema::auditTargetInput(char Kind, ASTContext::TargetPartition Targets,
                            StringRef What, const Decl *Owner,
                            ASTContext::TargetPartition JoinTargets) {
  if (Context.isTargetAuditSuspended())
    return;
  TargetAudit::Input In{Kind, Targets, What.str(),
                        JoinTargets == ~0u ? Targets : JoinTargets};
  bool ForGroups = Context.RecordGroupInputs && In.JoinTargets &&
                   (!isHandledInput(In) || In.Kind == 'K');
  if (ForGroups)
    GroupInputs = Context.refineTargetPartition(GroupInputs, In.JoinTargets);
  bool AuditsJoin = ForGroups && !Context.JoinAuditPath.empty();
  if (Owner) {
    if (AuditsJoin)
      Context.noteParseInput(Owner, In.Kind, In.JoinTargets, In.What,
                             /*FromMember=*/false);
    TargetAudit::add(Context, TargetAuditor->Units[Owner], std::move(In));
    return;
  }
  // The innermost instantiation being performed owns the input; outside one,
  // the top-level declaration being parsed does, unless it is being parsed
  // for one target only.
  for (const CodeSynthesisContext &SC : llvm::reverse(CodeSynthesisContexts)) {
    if (SC.Kind != CodeSynthesisContext::TemplateInstantiation || !SC.Entity ||
        !isa<FunctionDecl, CXXRecordDecl, VarDecl>(SC.Entity))
      continue;
    if (AuditsJoin)
      Context.noteParseInput(SC.Entity, In.Kind, In.JoinTargets, In.What,
                             /*FromMember=*/false);
    TargetAudit::add(Context, TargetAuditor->Units[SC.Entity], std::move(In));
    return;
  }
  if (AuditsJoin) {
    TargetAudit::add(Context, TargetAuditor->TargetParseInputs, In);
    // A function's body may be parsed after its class's other members, but
    // what it consumes is its own.
    if (FunctionDecl *FD = getCurFunctionDecl(/*AllowLambda=*/false)) {
      Context.noteParseInput(FD, In.Kind, In.JoinTargets, In.What,
                             /*FromMember=*/false);
      for (Decl *Unit = dyn_cast<Decl>(FD->getDeclContext());
           Unit && !isa<TranslationUnitDecl, NamespaceDecl, LinkageSpecDecl>(
                       Unit);
           Unit = dyn_cast<Decl>(Unit->getDeclContext()))
        Context.noteParseInput(Unit, In.Kind, In.JoinTargets, In.What,
                               /*FromMember=*/true);
    }
  }
  // Parsed for one target: kept only to tell whether parsing it once for
  // several would have been the same.
  if (Context.getCurrentTargetVariant())
    return;
  // A default argument is used where the function is called, and a default
  // template argument where the template is named: each use is analysed and
  // emitted for its own targets (see Sema::auditDefaultArgument).
  if (const auto &Record = currentEvaluationContext();
      Record.Context ==
          ExpressionEvaluationContext::PotentiallyEvaluatedIfUsed &&
      isa_and_nonnull<ParmVarDecl>(Record.ManglingContextDecl))
    return;
  if (getCurScope() && getCurScope()->isTemplateParamScope())
    return;
  // A function body is a unit of its own: splitting it does not change what
  // its class, or any other user, means. A template's body names a keyed
  // specialization its instantiations do not name again, and that names one
  // target's.
  if (FunctionDecl *FD = getCurFunctionDecl(/*AllowLambda=*/false);
      FD && FD->getDeclContext()->isRecord()) {
    bool BoundKey = In.Kind == 'K' && FD->isDependentContext();
    if ((!isHandledInput(In) || BoundKey) &&
        CUDA().IdentifyTarget(FD) != CUDAFunctionTarget::Host) {
      auto &Split = TargetAuditor->Split[FD->getCanonicalDecl()];
      Split = Context.refineTargetPartition(Split, BoundKey ? In.JoinTargets
                                                            : In.Targets);
    }
    if (In.Kind == 'F' &&
        CUDA().IdentifyTarget(FD) != CUDAFunctionTarget::Host) {
      auto &Body =
          TargetAuditor->SplitCounts.MemberBodies[FD->getCanonicalDecl()];
      Body = Context.refineTargetPartition(Body, In.Targets);
    }
    TargetAudit::add(Context, TargetAuditor->Units[FD], std::move(In));
    return;
  }
  if (!isHandledInput(In))
    if (FunctionDecl *FD = getCurFunctionDecl(/*AllowLambda=*/true);
        FD && isLambdaCallOperator(FD) && FD->hasAttr<CUDADeviceAttr>() &&
        !FD->getAttr<CUDADeviceAttr>()->isImplicit())
      TargetAuditor->ParseInputsInDeviceLambda = true;
  TargetAudit::add(Context, TargetAuditor->ParseInputs, std::move(In));
}

namespace {
/// What makes two targets' copies of an entity the same to a user.
enum class CopyMeaning {
  /// Only the same copy.
  Identity,
  /// Functions: the same signature and CUDA attributes.
  Signature,
  /// Variables: the same type.
  Type,
  /// Function templates whose specializations each target needs of its own
  /// copy: copies parsed from the same tokens that mean and define the same.
  Body,
};
} // namespace

/// Which targets see copies of \p D that differ by \p Meaning. Per entity: a
/// tag declared once and defined per target counts.
static ASTContext::TargetPartition
entityDivergence(ASTContext &Ctx, const NamedDecl *D, CopyMeaning Meaning) {
  // Each target emits its own copy of a function, and its own specialization
  // of a re-parsed function template, under one name.
  if (Meaning == CopyMeaning::Body &&
      (!isa<FunctionTemplateDecl>(D) || Ctx.isReparseCopy(D)))
    return 0;
  SmallVector<const NamedDecl *, 4> Copies;
  Ctx.getTargetCopies(D, Copies);
  if (const auto *TD = dyn_cast<TagDecl>(D))
    for (const TagDecl *Redecl : TD->redecls())
      if (Redecl->isThisDeclarationADefinition() && Redecl->getTargetVariant())
        Copies.push_back(Redecl);
  // So does a function declared once and defined per target.
  if (isa<FunctionDecl, FunctionTemplateDecl>(D))
    for (const Decl *Redecl : D->redecls())
      if (Redecl->getTargetVariant())
        Copies.push_back(cast<NamedDecl>(Redecl));
  unsigned Last = 1 + Ctx.getNumAuxTargets();
  SmallVector<const NamedDecl *, 8> For(Last + 1, nullptr);
  const NamedDecl *Shared = nullptr;
  for (const NamedDecl *Copy : Copies) {
    unsigned V = Copy->getTargetVariant();
    if (V == Decl::TargetVariantRedundant)
      continue;
    if (!V) {
      Shared = Copy;
      continue;
    }
    for (unsigned T = 1; T <= Last; ++T)
      if (Copy->isCopyFor(T))
        For[T] = Copy;
  }
  for (unsigned V = 1; V <= Last; ++V)
    if (!For[V])
      For[V] = Shared;
  // Copies of a function that differ only in their bodies mean the same to a
  // caller; CodeGen picks each target's body. So do copies of a class or class
  // template that lay out alike: every target names the host's copy, whose
  // members, or whose specializations' members, are each target's own.
  if (isa<RecordDecl, ClassTemplateDecl>(D) &&
      Meaning == CopyMeaning::Signature) {
    ASTContext::TargetPartition Layout = Ctx.getLayoutTargetPartition(D);
    // A layout that differs at all is taken to differ between the host and
    // the devices.
    if (Layout)
      Layout = Ctx.refineTargetPartition(
          Layout, Ctx.reconcileTargetPartition(0, ASTContext::TDK_Side));
    // So does what a class declares, member initializers included.
    if (const auto *RD = dyn_cast<RecordDecl>(D))
      Layout = Ctx.refineTargetPartition(Layout, Ctx.getRecordShapePartition(RD));
    return Layout;
  }
  auto Same = [&](const NamedDecl *A, const NamedDecl *B) {
    if (A == B)
      return true;
    if (Meaning == CopyMeaning::Identity || !A || !B)
      return false;
    if (Meaning == CopyMeaning::Body)
      return Ctx.isReparseCopy(A) || Ctx.isReparseCopy(B) ||
             (A->getLocation() == B->getLocation() &&
              copiesDefineTheSame(Ctx, const_cast<NamedDecl *>(A),
                                  const_cast<NamedDecl *>(B)));
    if (Meaning == CopyMeaning::Type) {
      const auto *VA = dyn_cast<ValueDecl>(A), *VB = dyn_cast<ValueDecl>(B);
      return VA && VB && Ctx.hasSameType(VA->getType(), VB->getType());
    }
    const FunctionDecl *FA = A->getAsFunction(), *FB = B->getAsFunction();
    if (!FA || !FB)
      return false;
    // A caller can consume a default argument, which each copy has its own of.
    auto HasDefaults = [](const NamedDecl *D, const FunctionDecl *F) {
      const auto *TD = dyn_cast<TemplateDecl>(D);
      return F->getMinRequiredArguments() < F->getNumParams() ||
             (TD && TD->getTemplateParameters()->getMinRequiredArguments() <
                        TD->getTemplateParameters()->size());
    };
    if (HasDefaults(A, FA) || HasDefaults(B, FB))
      return false;
    auto Attrs = [](const FunctionDecl *F) {
      return std::make_pair(F->hasAttr<CUDAHostAttr>(),
                            F->hasAttr<CUDADeviceAttr>() ||
                                F->hasAttr<CUDAGlobalAttr>());
    };
    return FA->getType().getCanonicalType().getAsString() ==
               FB->getType().getCanonicalType().getAsString() &&
           Attrs(FA) == Attrs(FB);
  };
  return Ctx.partitionTargetsBy(
      [&](unsigned A, unsigned B) { return Same(For[A], For[B]); });
}

/// Whether \p T names a type the target-set rule would split, or one copied
/// per target.
static bool namesSplitType(const TargetAudit &Audit, QualType T) {
  while (!T.isNull()) {
    if (const auto *TT = T->getAs<TypedefType>()) {
      const TypedefNameDecl *TND = TT->getDecl();
      if (TND->getTargetVariant() || Audit.Split.count(TND->getCanonicalDecl()))
        return true;
    }
    QualType Canon = T.getCanonicalType();
    if (const TagDecl *TD = Canon->getAsTagDecl())
      return TD->getTargetVariant() ||
             Audit.Split.count(TD->getCanonicalDecl());
    if (const auto *PT = Canon->getAs<PointerType>())
      T = PT->getPointeeType();
    else if (const auto *RT = Canon->getAs<ReferenceType>())
      T = RT->getPointeeType();
    else if (const ArrayType *AT = Canon->getAsArrayTypeUnsafe())
      T = AT->getElementType();
    else
      return false;
  }
  return false;
}

/// Which targets declare \p Using, a member using-declaration of \p Spec,
/// with the same scope in the pattern their copy of \p Spec's template gives
/// it: the template's own, or that of the copy of the partial specialization
/// \p Spec was instantiated from.
static ASTContext::TargetPartition
memberUsingPartition(ASTContext &Ctx,
                     const ClassTemplateSpecializationDecl *Spec,
                     const BaseUsingDecl *Using) {
  SmallVector<const NamedDecl *, 4> Copies;
  Ctx.getTargetCopies(Spec->getSpecializedTemplate(), Copies);
  const auto *Partial =
      dyn_cast_if_present<ClassTemplatePartialSpecializationDecl *>(
          Spec->getSpecializedTemplateOrPartial());
  SmallVector<ClassTemplatePartialSpecializationDecl *, 4> Partials;
  if (Partial)
    for (const NamedDecl *Copy : Copies)
      if (const auto *CTD = dyn_cast<ClassTemplateDecl>(Copy))
        const_cast<ClassTemplateDecl *>(CTD)->getPartialSpecializations(
            Partials);
  if (Copies.size() < 2 && Partials.size() < 2)
    return 0;
  auto ArgsOf = [&](const ClassTemplatePartialSpecializationDecl *P) {
    llvm::FoldingSetNodeID ID;
    for (const TemplateArgument &Arg : P->getTemplateArgs().asArray())
      Arg.Profile(ID, Ctx);
    return ID;
  };
  PrintingPolicy Policy = Ctx.getPrintingPolicy();
  return Ctx.partitionTargetsByAnswer([&](unsigned V) {
    const CXXRecordDecl *Pattern = nullptr;
    if (Partial) {
      for (const ClassTemplatePartialSpecializationDecl *P : Partials)
        if (!P->isRedundantTargetVariant() &&
            (P->isCopyFor(V) || (!Pattern && !P->getTargetVariant())) &&
            ArgsOf(P) == ArgsOf(Partial))
          Pattern = P;
    } else {
      for (const NamedDecl *ND : Copies)
        if (ND->isCopyFor(V) || (!Pattern && !ND->getTargetVariant()))
          if (const auto *CTD = dyn_cast<ClassTemplateDecl>(ND))
            Pattern = CTD->getTemplatedDecl();
    }
    std::string Scope;
    llvm::raw_string_ostream OS(Scope);
    if (Pattern)
      for (const NamedDecl *Member : Pattern->lookup(Using->getDeclName())) {
        if (const auto *Shadow = dyn_cast<UsingShadowDecl>(Member))
          Member = Shadow->getIntroducer();
        if (const auto *U = dyn_cast_or_null<UsingDecl>(Member))
          U->getQualifier().print(OS, Policy);
        else if (const auto *U =
                     dyn_cast_or_null<UnresolvedUsingValueDecl>(Member))
          U->getQualifier().print(OS, Policy);
        else if (const auto *U =
                     dyn_cast_or_null<UnresolvedUsingTypenameDecl>(Member))
          U->getQualifier().print(OS, Policy);
      }
    return llvm::hash_value(Scope);
  });
}

void Sema::auditTargetLookup(const NamedDecl *D, bool Evaluated) {
  // A name found through a using-declaration each target has its own copy of
  // means what that target's copy introduces.
  if (const auto *Using = dyn_cast<BaseUsingDecl>(D)) {
    auto [It, Inserted] = TargetAuditor->EntityDivergence.try_emplace(D);
    if (Inserted) {
      SmallVector<const BaseUsingDecl *, 4> Copies;
      for (const NamedDecl *Found :
           Using->getDeclContext()->lookup(Using->getDeclName()))
        if (const auto *Shadow = dyn_cast<UsingShadowDecl>(Found))
          if (const BaseUsingDecl *Introducer = Shadow->getIntroducer();
              Introducer && Introducer->getTargetVariant() &&
              !Introducer->isRedundantTargetVariant() &&
              !llvm::is_contained(Copies, Introducer))
            Copies.push_back(Introducer);
      auto CopyFor = [&](unsigned V) -> const BaseUsingDecl * {
        for (const BaseUsingDecl *Copy : Copies)
          if (Copy->isCopyFor(V))
            return Copy;
        return nullptr;
      };
      It->second = Context.partitionTargetsBy(
          [&](unsigned A, unsigned B) { return CopyFor(A) == CopyFor(B); });
      // So does one a specialization instantiated, from its own template's
      // copy.
      if (const auto *Spec = dyn_cast<ClassTemplateSpecializationDecl>(
              Using->getDeclContext()))
        It->second = Context.refineTargetPartition(
            It->second, memberUsingPartition(Context, Spec, Using));
    }
    if (It->second)
      auditTargetInput('L', It->second, D->getQualifiedNameAsString());
    return;
  }
  // A namespace a using-directive nominates is searched for the targets that
  // have the directive.
  if (const auto *Directive = dyn_cast<UsingDirectiveDecl>(D)) {
    if (Directive->getTargetVariant() && !Directive->isRedundantTargetVariant())
      auditTargetInput(
          'L', Context.partitionTargetsBy([&](unsigned A, unsigned B) {
            return Directive->isCopyFor(A) == Directive->isCopyFor(B);
          }),
          Directive->getNominatedNamespace()->getQualifiedNameAsString());
    return;
  }
  bool Type =
      isa<TypeDecl, ClassTemplateDecl, TypeAliasTemplateDecl, VarTemplateDecl>(
          D);
  bool Function = isa<FunctionDecl, FunctionTemplateDecl>(D);
  bool Value = isa<VarDecl, EnumConstantDecl, BindingDecl>(D);
  if (!Type && !Function && !Value)
    return;
  // A function a body reaches binds that function's copy for the targets the
  // body is analysed for, even where only the copies' bodies differ.
  if (Function && CurContext->isFunctionOrMethod()) {
    auto [It, Inserted] = TargetAuditor->BodyDivergence.try_emplace(D);
    if (Inserted)
      It->second = entityDivergence(Context, D, CopyMeaning::Body);
    if (It->second)
      auditTargetInput('F', It->second, D->getQualifiedNameAsString());
  }
  // So does the value of a member of a class specialization each target has
  // its own instance of. Its member functions are each target's own by name.
  // A class the function's signature names is the one of the targets the
  // function is specialized for, where its template is copied per target or
  // keyed by the value alone.
  if (Value && CurContext->isFunctionOrMethod())
    if (const auto *VD = dyn_cast<ValueDecl>(D);
        VD && isTargetDivergentValueMemberAccess(VD)) {
      const FunctionDecl *FD = getCurFunctionDecl(/*AllowLambda=*/false);
      const ClassTemplateDecl *CTD =
          cast<ClassTemplateSpecializationDecl>(VD->getDeclContext())
              ->getSpecializedTemplate();
      if (!FD || !signatureNamesTemplate(FD, CTD) ||
          !(CTD->hasTargetTaggedPrimaryTemplate() ||
            CTD->hasTargetKeyedValuesOnly()))
        auditTargetInput(
            'F', Context.reconcileTargetPartition(0, ASTContext::TDK_Arch),
            D->getQualifiedNameAsString());
    }
  // So does a member type of one named where code is parsed. An instantiation
  // names the type its own target's specialization has.
  if (Type && CurContext->isFunctionOrMethod() && !inTemplateInstantiation())
    if (const auto *TD = dyn_cast<TypeDecl>(D);
        TD && isTargetDivergentTypeMemberAccess(TD)) {
      const FunctionDecl *FD = getCurFunctionDecl(/*AllowLambda=*/false);
      const ClassTemplateDecl *CTD =
          cast<ClassTemplateSpecializationDecl>(TD->getDeclContext())
              ->getSpecializedTemplate();
      if (!FD || !signatureNamesTemplate(FD, CTD) ||
          !(CTD->hasTargetTaggedPrimaryTemplate() ||
            CTD->hasTargetKeyedValuesOnly()))
        auditTargetInput(
            'F', Context.reconcileTargetPartition(0, ASTContext::TDK_Arch),
            D->getQualifiedNameAsString());
    }
  // A member function is reached through its class, whose copy was chosen
  // already; its copies' bodies are CodeGen's to pick.
  if (Function && D->getDeclContext()->isRecord())
    return;
  // Every target names the host's copy of a class template, whose
  // specializations, keyed by target, instantiate each target's definition: in
  // a template, the specialization named is formed where it is instantiated,
  // for the target it is instantiated for.
  bool InTemplate =
      CurContext->isDependentContext() ||
      (getCurScope() && getCurScope()->getTemplateParamParent());
  if (isa<ClassTemplateDecl>(D) && InTemplate) {
    // Parsed for one target, the name still binds that target's copy, whose
    // partial specializations are its own.
    if (Context.RecordGroupInputs && Context.getCurrentTargetVariant())
      if (ASTContext::TargetPartition Copies =
              entityDivergence(Context, D, CopyMeaning::Identity))
        auditTargetInput('L', /*Targets=*/0, D->getQualifiedNameAsString(),
                         /*Owner=*/nullptr, Copies);
    return;
  }
  // A function evaluated now means its copy's body. In a template that is
  // evaluated for each instantiation, keyed by target.
  bool EvaluatedBody = Function && Evaluated && !InTemplate;
  if (auto It = TargetAuditor->Split.find(D->getCanonicalDecl());
      It != TargetAuditor->Split.end()) {
    // A split function only matters to its callers through its signature.
    const FunctionDecl *FD = D->getAsFunction();
    bool Signature = !FD || namesSplitType(*TargetAuditor, FD->getReturnType());
    if (FD)
      for (const ParmVarDecl *P : FD->parameters())
        Signature = Signature || namesSplitType(*TargetAuditor, P->getType());
    if (Signature)
      auditTargetInput('P', It->second, D->getQualifiedNameAsString());
  }
  // A runtime read of a per-target value of one type: CodeGen emits each
  // target's.
  auto &Cache = (Value && Evaluated) || EvaluatedBody
                    ? TargetAuditor->ValueDivergence
                    : TargetAuditor->EntityDivergence;
  CopyMeaning Meaning = (Function && !EvaluatedBody) ||
                                isa<RecordDecl, ClassTemplateDecl>(D)
                            ? CopyMeaning::Signature
                        : Value && !Evaluated ? CopyMeaning::Type
                                              : CopyMeaning::Identity;
  auto [It, Inserted] = Cache.try_emplace(D);
  if (Inserted)
    It->second = entityDivergence(Context, D, Meaning);
  // Parsed for one target, a name binds that target's copy. A class's copy
  // has its own static members and member bodies, layout aside, a function
  // template's copy is instantiated from its own body, and a function's copy
  // has its own body, which constant evaluation follows.
  ASTContext::TargetPartition BoundCopies = 0;
  if (Context.RecordGroupInputs && Context.getCurrentTargetVariant()) {
    if (isa<RecordDecl, ClassTemplateDecl, FunctionTemplateDecl>(D)) {
      BoundCopies = entityDivergence(Context, D, CopyMeaning::Identity);
    } else if (Function && !EvaluatedBody &&
               Context.isTargetDivergentSource(D)) {
      const FunctionDecl *Caller = getCurFunctionDecl(/*AllowLambda=*/true);
      if (isConstantEvaluatedContext() ||
          (Caller && (Caller->isConstexpr() || Caller->isConsteval())))
        BoundCopies = Context.getTargetPartition(D);
    }
  }
  if (!It->second) {
    if (BoundCopies)
      auditTargetInput('L', /*Targets=*/0, D->getQualifiedNameAsString(),
                       /*Owner=*/nullptr, BoundCopies);
    return;
  }
  // What the declaration being parsed declares itself is parsed with it,
  // once for every target the parse is for. Copies evaluated alike mean the
  // same to the code evaluating them. A member of a target's copy of a class
  // is reached through the class, whose copy was chosen where it was named.
  ASTContext::TargetPartition JoinTargets = ~0u;
  if (Context.RecordGroupInputs) {
    const DeclContext *Unit = CurContext;
    while (Unit->getParent() && !Unit->getParent()->isFileContext())
      Unit = Unit->getParent();
    if (!Unit->isFileContext() && Unit->Encloses(D->getDeclContext()))
      JoinTargets = 0;
    else if (BoundCopies)
      JoinTargets = BoundCopies;
    else if (Meaning == CopyMeaning::Identity) {
      if (const auto *Parent = dyn_cast<RecordDecl>(D->getDeclContext());
          Parent && Parent->getTargetVariant())
        JoinTargets = 0;
      else if (Context.isTargetDivergentSource(D))
        JoinTargets = Context.getTargetPartition(D);
    }
  }
  auditTargetInput('L', It->second, D->getQualifiedNameAsString(),
                   /*Owner=*/nullptr, JoinTargets);
}

bool Sema::measuresTargetParts() const {
  return TargetAuditor && !TargetAuditor->Path.empty();
}

/// The classes of \p P among \p Own.
static SmallVector<TargetSet, 4>
partsOf(const ASTContext &Ctx, ASTContext::TargetPartition P, TargetSet Own) {
  SmallVector<TargetSet, 4> Parts;
  for (unsigned V = 1, E = 1 + Ctx.getNumAuxTargets(); V <= E; ++V)
    if (Own.contains(V))
      if (TargetSet Part = Ctx.getTargetClass(P, V) & Own;
          !llvm::is_contained(Parts, Part))
        Parts.push_back(Part);
  return Parts;
}

/// What \p Inputs tell apart as a body splits by them: the inputs a unit
/// analysed for several targets must not share, keyed specializations by the
/// targets naming different ones, and functions by their copies' bodies.
static ASTContext::TargetPartition
bodySplit(const ASTContext &Ctx, ArrayRef<TargetAudit::Input> Inputs,
          std::string *Kinds = nullptr) {
  ASTContext::TargetPartition Result = 0;
  for (const TargetAudit::Input &In : Inputs) {
    ASTContext::TargetPartition P = In.Kind == 'K' ? In.JoinTargets
                                    : !isHandledInput(In) || In.Kind == 'F'
                                        ? In.Targets
                                        : 0;
    if (!P)
      continue;
    ASTContext::TargetPartition Refined = Ctx.refineTargetPartition(Result, P);
    if (Kinds && Refined != Result)
      *Kinds += (" " + Twine(In.Kind) + ":" + In.What).str();
    Result = Refined;
  }
  return Result;
}

/// Adds to \p Names the class templates \p T names specializations of,
/// itself or through the classes enclosing it.
static void collectNamedTemplates(QualType T, llvm::StringSet<> &Names,
                                  unsigned Depth = 0) {
  if (T.isNull() || Depth > 8)
    return;
  T = T.getCanonicalType();
  if (const auto *PT = T->getAs<PointerType>())
    return collectNamedTemplates(PT->getPointeeType(), Names, Depth + 1);
  if (const auto *RT = T->getAs<ReferenceType>())
    return collectNamedTemplates(RT->getPointeeType(), Names, Depth + 1);
  for (const DeclContext *DC = T->getAsCXXRecordDecl(); DC;
       DC = DC->getParent()) {
    const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(DC);
    if (!CTSD)
      continue;
    Names.insert(CTSD->getSpecializedTemplate()->getQualifiedNameAsString());
    for (const TemplateArgument &Arg : CTSD->getTemplateArgs().asArray())
      if (Arg.getKind() == TemplateArgument::Type)
        collectNamedTemplates(Arg.getAsType(), Names, Depth + 1);
  }
}

/// The class templates \p Function's signature names specializations of.
static llvm::StringSet<> signatureTemplates(const FunctionDecl *Function) {
  llvm::StringSet<> Names;
  collectNamedTemplates(Function->getReturnType(), Names);
  for (const ParmVarDecl *P : Function->parameters())
    collectNamedTemplates(P->getType(), Names);
  return Names;
}

bool Sema::signatureNamesTemplate(const FunctionDecl *Function,
                                  const NamedDecl *Template) const {
  return signatureTemplates(Function).contains(
      Template->getQualifiedNameAsString());
}

SmallVector<TargetSet, 4>
Sema::getInstantiationParts(const FunctionDecl *Function, TargetSet Own,
                            ASTContext::TargetPartition Extra) const {
  ASTContext::TargetPartition Split = Extra;
  if (auto It = TargetAuditor->Units.find(Function);
      It != TargetAuditor->Units.end()) {
    // A part has the function's one signature: a keyed specialization the
    // signature names is the signature's in every part.
    llvm::StringSet<> Signature = signatureTemplates(Function);
    SmallVector<TargetAudit::Input, 4> Inputs;
    for (const TargetAudit::Input &In : It->second)
      if (In.Kind != 'K' || !Signature.contains(In.What))
        Inputs.push_back(In);
    Split = Context.refineTargetPartition(Split, bodySplit(Context, Inputs));
  }
  return partsOf(Context, Split, Own);
}

void Sema::auditTargetParts(const FunctionDecl *Function, unsigned View,
                            TargetSet Own, bool Redirected) {
  auto &Counts = TargetAuditor->SplitCounts;
  bool Instantiation = Function->isTemplateInstantiation();
  Counts.Instantiations += Instantiation;
  std::string Kinds;
  ASTContext::TargetPartition Split = 0;
  if (auto It = TargetAuditor->Units.find(Function);
      It != TargetAuditor->Units.end())
    Split = bodySplit(Context, It->second, &Kinds);
  // A function parsed once is split by what its declaration's parse consumed.
  if (!Instantiation)
    Split = Context.refineTargetPartition(
        Split, bodySplit(Context, TargetAuditor->ParseInputs, &Kinds));
  SmallVector<TargetSet, 4> Parts = partsOf(Context, Split, Own);
  std::string Name;
  llvm::raw_string_ostream OS(Name);
  Function->getNameForDiagnostic(OS, getPrintingPolicy(), /*Qualified=*/true);
  if (!Instantiation) {
    if (!Redirected)
      return;
    ++Counts.ParseRedirected;
    if (Parts.size() < 2) {
      ++Counts.ParseUncovered;
      Counts.Lines.push_back("uncovered parse " + Name);
    }
    return;
  }
  if (Parts.size() > 1) {
    ++Counts.Split;
    Counts.Parts += Parts.size() - 1;
    Counts.Lines.push_back(("split " + Twine(Parts.size()) + " view " +
                            Twine(View) + " " + Name + ":" + Kinds)
                               .str());
  }
  if (!Redirected)
    return;
  ++Counts.Redirected;
  if (Parts.size() > 1)
    return;
  // Splitting every target apart by the divergent references.
  if (!Own.empty() && !Own.isSingle()) {
    SmallVector<TargetSet, 4> ByRedirect =
        partsOf(Context,
                Context.reconcileTargetPartition(0, ASTContext::TDK_Arch), Own);
    if (ByRedirect.size() > 1) {
      ++Counts.RedirectSplit;
      Counts.RedirectParts += ByRedirect.size() - 1;
    }
  }
  // A body re-parsed per target is instantiated per target already.
  for (const Decl *D = Function->getTemplateInstantiationPattern(); D;
       D = dyn_cast<Decl>(D->getDeclContext())) {
    for (const Decl *Unit :
         {D, static_cast<const Decl *>(D->getDescribedTemplate())})
      if (auto It = Unit ? TargetAuditor->Units.find(Unit)
                         : TargetAuditor->Units.end();
          It != TargetAuditor->Units.end() &&
          partsOf(Context, bodySplit(Context, It->second), Own).size() > 1)
        return;
    if (isa<TranslationUnitDecl, NamespaceDecl, LinkageSpecDecl>(D))
      break;
  }
  ++Counts.Uncovered;
  Counts.Lines.push_back(("uncovered view " + Twine(View) + " " + Name).str());
}

/// Which targets lay \p T out alike, as \p Div tells builtin types apart. A
/// record whose definition is not known yet has no layout to differ.
static ASTContext::TargetPartition
layoutPartition(const ASTContext &Ctx, const TargetDivergence &Div, QualType T,
                llvm::DenseMap<const Type *, ASTContext::TargetPartition> &Cache) {
  if (!Div.any())
    return 0;
  T = T.getCanonicalType();
  if (const auto *BT = T->getAs<BuiltinType>()) {
    BuiltinType::Kind K = BT->getKind();
    if (!Div.isDivergent(K))
      return 0;
    return Ctx.partitionTargetsByAnswer([&](unsigned V) {
      std::optional<std::pair<unsigned, unsigned>> Layout =
          TargetDivergence::getLayout(*Ctx.getTargetForVariant(V), K);
      return Layout ? llvm::hash_combine(Layout->first, Layout->second)
                    : llvm::hash_code(0);
    });
  }
  if (const auto *AT = T->getAsArrayTypeUnsafe())
    return layoutPartition(Ctx, Div, AT->getElementType(), Cache);
  const auto *RD = T->getAsRecordDecl();
  const RecordDecl *Def = RD ? RD->getDefinition() : nullptr;
  if (!Def)
    return 0;
  auto [It, Inserted] = Cache.try_emplace(T.getTypePtr(), 0);
  if (!Inserted)
    return It->second;
  ASTContext::TargetPartition Result = 0;
  if (const auto *CXXRD = dyn_cast<CXXRecordDecl>(Def))
    for (const CXXBaseSpecifier &Base : CXXRD->bases())
      Result = Ctx.refineTargetPartition(
          Result, layoutPartition(Ctx, Div, Base.getType(), Cache));
  for (const FieldDecl *F : Def->fields())
    Result = Ctx.refineTargetPartition(
        Result, layoutPartition(Ctx, Div, F->getType(), Cache));
  Cache[T.getTypePtr()] = Result;
  return Result;
}

void Sema::auditTargetType(QualType T) {
  if (T.isNull() || T->isDependentType())
    return;
  if (ASTContext::TargetPartition Layout = layoutPartition(
          Context, *TargetAuditor->Divergence, T, TargetAuditor->Layout))
    auditTargetInput('T', Layout, T.getAsString());

  // A specialization whose instantiation consumed a divergent input is
  // instantiated per target by the target-set rule, and so is what uses it.
  const auto *CTSD = dyn_cast_or_null<ClassTemplateSpecializationDecl>(
      T->getAsCXXRecordDecl());
  if (!CTSD || CTSD->getTargetVariant())
    return;
  bool Keyed = Context.getSpecializationKeyVariant(CTSD);
  auto It = TargetAuditor->Units.find(CTSD);
  if (It == TargetAuditor->Units.end())
    return;
  // Shared code naming a specialization keyed by target names one target's,
  // and so consumes what that one's definition consumed.
  ASTContext::TargetPartition Split = 0;
  for (const TargetAudit::Input &In : It->second)
    if (!isHandledInput(In) || (Keyed && In.Kind == 'K'))
      Split = Context.refineTargetPartition(Split, In.Targets);
  if (Split)
    auditTargetInput('S', Split, T.getCanonicalType().getAsString());
}

void Sema::auditCUDAModeRead(char Kind, StringRef Site, const Decl *Owner) {
  auditTargetInput(Kind,
                   Context.reconcileTargetPartition(0, ASTContext::TDK_Side),
                   Site, Owner);
}

void Sema::auditLangOptRead(StringRef Name, const Decl *Owner) {
  if (auto It = TargetLangOptDivergence.find(Name);
      It != TargetLangOptDivergence.end())
    auditTargetInput('O',
                     Context.partitionTargetsByAnswer([&](unsigned V) {
                       return llvm::hash_value(It->second[V - 1]);
                     }),
                     Name, Owner);
}

void Sema::auditTargetQuery(StringRef What,
                            llvm::function_ref<bool(const TargetInfo &)> Query,
                            const Decl *Owner) {
  if (ASTContext::TargetPartition Targets =
          Context.partitionTargetsByAnswer([&](unsigned V) {
            return llvm::hash_value(Query(*Context.getTargetForVariant(V)));
          }))
    auditTargetInput('T', Targets, What, Owner);
}

bool Sema::auditFlushParseUnit(ArrayRef<Decl *> Decls, bool TargetSpecific) {
  SmallVector<TargetAudit::Input, 4> Inputs =
      std::move(TargetAuditor->ParseInputs);
  TargetAuditor->ParseInputs.clear();
  bool InDeviceLambda =
      std::exchange(TargetAuditor->ParseInputsInDeviceLambda, false);
  if (!Context.JoinAuditPath.empty()) {
    SmallVector<TargetAudit::Input, 4> Consumed =
        std::move(TargetAuditor->TargetParseInputs);
    TargetAuditor->TargetParseInputs.clear();
    for (Decl *D : Decls) {
      Context.noteParseUnit(D);
      // A template's parameters, whose defaults it parsed, are its own.
      if (const auto *TD = dyn_cast<TemplateDecl>(D))
        if (const TemplateParameterList *TPL = TD->getTemplateParameters())
          for (const NamedDecl *Param : *TPL)
            for (const TargetAudit::Input &In : Consumed)
              Context.noteParseInput(Param, In.Kind, In.JoinTargets, In.What,
                                     /*FromMember=*/false);
      // A class means what its members do.
      for (Decl *Unit = D; Unit && !isa<TranslationUnitDecl, NamespaceDecl,
                                         LinkageSpecDecl>(Unit);
           Unit = dyn_cast<Decl>(Unit->getDeclContext()))
        for (const TargetAudit::Input &In : Consumed)
          Context.noteParseInput(Unit, In.Kind, In.JoinTargets, In.What,
                                 /*FromMember=*/Unit != D);
    }
  }
  if (TargetSpecific)
    return false;
  ++TargetAuditor->ParseUnits;
  ASTContext::TargetPartition Targets = 0;
  // A specialization keyed by target that the shared parse named was made for
  // the target it was parsed for, once, not for each target that uses it.
  for (const TargetAudit::Input &In : Inputs)
    if (!isHandledInput(In) || In.Kind == 'K')
      Targets = Context.refineTargetPartition(Targets, In.Targets);
  // A class whose member declarations consumed a divergent input means
  // something different per target too.
  for (Decl *D : Decls)
    if (auto It = TargetAuditor->MemberInputs.find(
            isa<ClassTemplateDecl>(D) ? cast<ClassTemplateDecl>(D)->getTemplatedDecl()
                                      : D);
        It != TargetAuditor->MemberInputs.end()) {
      Targets = Context.refineTargetPartition(Targets, It->second);
      if (!TargetAuditor->Path.empty())
        TargetAudit::add(Context, TargetAuditor->Units[D],
                         {'M', It->second, "member declaration", It->second});
    }
  if (!TargetAuditor->Path.empty()) {
    ASTContext::TargetPartition WithBodies = Targets;
    for (const TargetAudit::Input &In : Inputs)
      if (In.Kind == 'F')
        WithBodies = Context.refineTargetPartition(WithBodies, In.Targets);
    const FunctionDecl *FD =
        Decls.empty() ? nullptr : Decls[0]->getAsFunction();
    if (WithBodies != Targets &&
        !(FD && CUDA().IdentifyTarget(FD) == CUDAFunctionTarget::Host &&
          !InDeviceLambda))
      ++TargetAuditor->SplitCounts.ParseUnits;
  }
  bool Touched = false;
  for (Decl *D : Decls) {
    if (!TargetAuditor->Path.empty())
      for (const TargetAudit::Input &In : Inputs)
        TargetAudit::add(Context, TargetAuditor->Units[D], In);
    // A host function is only ever analysed for the host.
    const auto *FD = D->getAsFunction();
    if (Targets &&
        !(FD && CUDA().IdentifyTarget(FD) == CUDAFunctionTarget::Host &&
          !InDeviceLambda)) {
      auto &Split = TargetAuditor->Split[D->getCanonicalDecl()];
      Split = Context.refineTargetPartition(Split, Targets);
      Touched = true;
      if (const auto *Parent = dyn_cast<CXXRecordDecl>(D->getDeclContext())) {
        auto &ParentTargets = TargetAuditor->MemberInputs[Parent];
        ParentTargets = Context.refineTargetPartition(ParentTargets, Targets);
      }
    }
  }
  return Touched;
}

void Sema::auditKeptShared(DeclGroupRef DG) {
  for (Decl *D : DG)
    TargetAuditor->Split.erase(D->getCanonicalDecl());
}

namespace {
/// Replays the references a default template argument makes.
class DefaultArgumentReferences
    : public RecursiveASTVisitor<DefaultArgumentReferences> {
public:
  Sema &S;
  bool Evaluated;
  DefaultArgumentReferences(Sema &S, bool Evaluated)
      : S(S), Evaluated(Evaluated) {}
  bool VisitDeclRefExpr(DeclRefExpr *E) {
    S.auditTargetLookup(E->getDecl(), Evaluated);
    return true;
  }
  bool VisitTagType(TagType *T) {
    S.auditTargetLookup(T->getDecl(), /*Evaluated=*/false);
    return true;
  }
  bool VisitTypedefType(TypedefType *T) {
    S.auditTargetLookup(T->getDecl(), /*Evaluated=*/false);
    return true;
  }
};
} // namespace

void Sema::auditDefaultArgument(const NamedDecl *Param) {
  if (const auto *NTTP = dyn_cast<NonTypeTemplateParmDecl>(Param)) {
    if (NTTP->hasDefaultArgument())
      if (Expr *E = NTTP->getDefaultArgument().getArgument().getAsExpr())
        DefaultArgumentReferences(*this, /*Evaluated=*/true).TraverseStmt(E);
  } else if (const auto *TTP = dyn_cast<TemplateTypeParmDecl>(Param)) {
    if (TTP->hasDefaultArgument())
      DefaultArgumentReferences(*this, /*Evaluated=*/false)
          .TraverseType(TTP->getDefaultArgument().getArgument().getAsType());
  }
}

void Sema::writeTargetAudit() {
  std::error_code EC;
  llvm::raw_fd_ostream OS(TargetAuditor->Path, EC,
                          llvm::sys::fs::OF_Append | llvm::sys::fs::OF_Text);
  if (EC)
    return;
  const SourceManager &SM = getSourceManager();
  OS << "== " << SM.getFileEntryRefForID(SM.getMainFileID())->getName() << "\n";
  unsigned Canonical = Context.getCanonicalDeviceVariant();
  unsigned Units = 0, CodeGen = 0, DiagOnly = 0, ParseBytes = 0;
  unsigned Instantiations = 0, PropagatedOnly = 0;
  std::map<char, unsigned> ByKind;
  for (auto &[D, Inputs] : TargetAuditor->Units) {
    ++Units;
    if (D->isRedundantTargetVariant() || D->isInvalidDecl())
      continue;
    // The targets this unit is analysed for today.
    bool Host = true, Devices = true, Arch = true;
    unsigned Key = Context.getEffectiveTargetInstantiationKey(D);
    if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(D))
      if (unsigned V = Context.getSpecializationKeyVariant(CTSD))
        Key = V;
    if (D->getTargetVariant() || (Key && Key != Canonical))
      continue;
    if (Key == Canonical)
      Host = false;
    // Only the host's meaning of a host function matters.
    if (const auto *FD = dyn_cast<FunctionDecl>(D);
        FD && CUDA().IdentifyTarget(FD) == CUDAFunctionTarget::Host)
      continue;
    std::string Line;
    llvm::raw_string_ostream LS(Line);
    bool AffectsCode = false, Any = false, Direct = false;
    for (const TargetAudit::Input &In : Inputs) {
      bool SideMatters = Host && Devices && splitsSide(Context, In.Targets);
      bool ArchMatters = Arch && splitsArch(Context, In.Targets);
      if (!SideMatters && !ArchMatters)
        continue;
      Any = true;
      Direct |= In.Kind != 'P';
      // Device code is analysed with the host's answers today, so a host/device
      // difference changes its code, not just its diagnostics.
      AffectsCode |= ArchMatters || (SideMatters && !isHandledInput(In));
      ++ByKind[In.Kind];
      LS << " " << In.Kind << ":" << In.What << "("
         << (SideMatters ? "side" : "")
         << (SideMatters && ArchMatters ? "," : "")
         << (ArchMatters ? "arch" : "") << ")";
    }
    if (!Any)
      continue;
    bool Instantiation = isa<FunctionDecl>(D)
                             ? cast<FunctionDecl>(D)->isTemplateInstantiation()
                             : isa<ClassTemplateSpecializationDecl>(D);
    if (Instantiation)
      ++Instantiations;
    else if (SourceRange R = D->getSourceRange(); R.isValid()) {
      std::pair<FileID, unsigned> B =
          SM.getDecomposedExpansionLoc(R.getBegin());
      std::pair<FileID, unsigned> E = SM.getDecomposedExpansionLoc(R.getEnd());
      if (B.first == E.first && E.second >= B.second)
        ParseBytes += E.second - B.second;
    }
    (AffectsCode ? CodeGen : DiagOnly) += 1;
    if (!Direct)
      ++PropagatedOnly;
    PresumedLoc PL = SM.getPresumedLoc(D->getLocation());
    OS << (AffectsCode ? "code " : "diag ")
       << (Instantiation ? "inst " : "parse ")
       << (Host ? "shared " : "devices ")
       << (isa<NamedDecl>(D) ? cast<NamedDecl>(D)->getQualifiedNameAsString()
                             : D->getDeclKindName())
       << " @ "
       << (PL.isValid() ? (llvm::Twine(PL.getFilename()) + ":" +
                           llvm::Twine(PL.getLine()))
                              .str()
                        : std::string("?"))
       << ":" << Line << "\n";
  }
  auto &Counts = TargetAuditor->SplitCounts;
  unsigned MemberBodies = 0;
  for (auto [FD, Bodies] : Counts.MemberBodies) {
    ASTContext::TargetPartition Now = TargetAuditor->Split.lookup(FD);
    MemberBodies += Context.refineTargetPartition(Now, Bodies) != Now;
  }
  for (const std::string &Line : Counts.Lines)
    OS << Line << "\n";
  OS << "parts instantiations=" << Counts.Instantiations
     << " split=" << Counts.Split << " extra-bodies=" << Counts.Parts
     << " redirected=" << Counts.Redirected << " uncovered=" << Counts.Uncovered
     << " redirect-split=" << Counts.RedirectSplit
     << " redirect-extra-bodies=" << Counts.RedirectParts
     << " parse-redirected=" << Counts.ParseRedirected
     << " parse-uncovered=" << Counts.ParseUncovered
     << " parse-units=" << Counts.ParseUnits
     << " member-bodies=" << MemberBodies << "\n";
  OS << "summary parse-units=" << TargetAuditor->ParseUnits
     << " recorded-units=" << Units << " code=" << CodeGen
     << " diag-only=" << DiagOnly << " instantiations=" << Instantiations
     << " parse-bytes=" << ParseBytes << " propagated-only=" << PropagatedOnly;
  for (auto [Kind, N] : ByKind)
    OS << " " << Kind << "=" << N;
  // The frontend warns about those it does not expect to differ.
  SmallVector<StringRef, 4> LangOpts;
  for (const auto &Entry : TargetLangOptDivergence)
    LangOpts.push_back(Entry.getKey());
  llvm::sort(LangOpts);
  OS << " lang-opts=" << llvm::join(LangOpts, ",") << "\n";
}
