//===- TargetDependence.cpp - Target partitions and keys ---------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Partitions the targets by the answers an analysis consumed, records which
// declarations depend on the target, and keys specializations by target.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/TargetDependence.h"
#include "MultiTargetInternal.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/MultiTargetContext.h"
#include "clang/AST/RecursiveASTVisitor.h"

using namespace clang;
using namespace clang::multitarget;

/// Whether \p A and \p B, two targets' copies of a field or base type, lay
/// out alike: the same type, or the same shape where each target's copy of a
/// class names that target's copies of the classes it contains.
bool MultiTargetContext::hasSameTargetLayout(QualType A, QualType B,
                                             unsigned Depth) const {
  if (Ctx.hasSameType(A, B))
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
         layoutAttributes(Ctx, RA) == layoutAttributes(Ctx, RB);
}

TargetPartition
MultiTargetContext::internTargetPartition(ArrayRef<uint8_t> ClassOf) const {
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

TargetPartition MultiTargetContext::partitionTargetsByAnswer(
    llvm::function_ref<llvm::hash_code(unsigned Variant)> Answer) const {
  SmallVector<llvm::hash_code, 8> Answers;
  for (unsigned V : Ctx.getTargetVariants())
    Answers.push_back(Answer(V));
  return partitionTargetsBy([&](unsigned A, unsigned B) {
    return Answers[A - 1] == Answers[B - 1];
  });
}

TargetPartition MultiTargetContext::partitionTargetsBy(
    llvm::function_ref<bool(unsigned A, unsigned B)> Same) const {
  unsigned Last = 1 + Ctx.getNumAuxTargets();
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

TargetPartition
MultiTargetContext::refineTargetPartition(TargetPartition A,
                                          TargetPartition B) const {
  if (A == B || !B)
    return A;
  if (!A)
    return B;
  ArrayRef<uint8_t> CA = TargetPartitions[A], CB = TargetPartitions[B];
  return partitionTargetsBy([&](unsigned X, unsigned Y) {
    return CA[X - 1] == CA[Y - 1] && CB[X - 1] == CB[Y - 1];
  });
}

MultiTargetContext::TargetDependenceKind
MultiTargetContext::getTargetDependenceKind(TargetPartition P) const {
  if (!P)
    return TDK_None;
  ArrayRef<uint8_t> ClassOf = TargetPartitions[P];
  if (ClassOf.size() < 2)
    return TDK_None;
  // Without a host, every target is a device.
  unsigned Host = Ctx.getHostVariant();
  SmallVector<uint8_t, 8> Devices;
  for (unsigned V = 1; V <= ClassOf.size(); ++V)
    if (V != Host)
      Devices.push_back(ClassOf[V - 1]);
  if (llvm::any_of(Devices, [&](uint8_t C) { return C != Devices.front(); }))
    return TDK_Arch;
  return !Host || ClassOf[Host - 1] == Devices.front() ? TDK_None : TDK_Side;
}

TargetPartition
MultiTargetContext::reconcileTargetPartition(TargetPartition P,
                                             TargetDependenceKind Kind) const {
  if (getTargetDependenceKind(P) == Kind)
    return P;
  switch (Kind) {
  case TDK_None:
    return 0;
  case TDK_Side:
    if (!Ctx.getHostVariant())
      return 0;
    return partitionTargetsBy([&](unsigned A, unsigned B) {
      return (A == Ctx.getHostVariant()) == (B == Ctx.getHostVariant());
    });
  case TDK_Arch:
    return partitionTargetsBy([](unsigned A, unsigned B) { return A == B; });
  }
  llvm_unreachable("unknown target dependence");
}

void MultiTargetContext::noteParseInput(const Decl *D, char Kind,
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

SmallVector<MultiTargetContext::ParseInput, 4>
MultiTargetContext::getParseInputList(const Decl *D) const {
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

unsigned MultiTargetContext::getTokenClass(const Decl *D) const {
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

TargetPartition MultiTargetContext::getParseInputs(const Decl *D) const {
  TargetPartition Result = 0;
  for (const ParseInput &In : getParseInputList(D))
    Result = refineTargetPartition(Result, In.Targets);
  return Result;
}

TargetSet MultiTargetContext::getTargetClass(TargetPartition P,
                                             unsigned Variant) const {
  TargetSet Class;
  for (unsigned V : Ctx.getTargetVariants())
    if (!separatesTargets(P, Variant, V))
      Class.insert(V);
  return Class;
}

bool MultiTargetContext::separatesTargets(TargetPartition P, unsigned A,
                                          unsigned B) const {
  return P && TargetPartitions[P][A - 1] != TargetPartitions[P][B - 1];
}

bool MultiTargetContext::isFinestTargetPartition(TargetPartition P) const {
  if (!P)
    return Ctx.getNumAuxTargets() == 0;
  ArrayRef<uint8_t> ClassOf = TargetPartitions[P];
  return ClassOf.back() + 1u == ClassOf.size();
}

bool MultiTargetContext::hasTargetDivergentLayout(const NamedDecl *D) const {
  return getLayoutTargetDependence(D) != TDK_None;
}

MultiTargetContext::TargetDependenceKind
MultiTargetContext::getLayoutTargetDependence(const NamedDecl *D) const {
  return getTargetDependenceKind(getLayoutTargetPartition(D));
}

TargetPartition
MultiTargetContext::getLayoutTargetPartition(const NamedDecl *D) const {
  auto Pattern = [](const NamedDecl *ND) -> const RecordDecl * {
    if (const auto *CTD = dyn_cast<ClassTemplateDecl>(ND))
      ND = CTD->getTemplatedDecl();
    const auto *RD = dyn_cast<RecordDecl>(ND);
    return RD && RD->isCompleteDefinition() ? RD : nullptr;
  };
  // Where code analysed for every target is the primary's view, a class
  // re-parsed per target is copied per target as much as a widened one.
  bool Copied = isWidenedCopy(D) ||
                (Ctx.isMultiTargetCompile() && D->getTargetVariant() &&
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
    // One past each field's bit width, 0 for a field that is not a bit-field.
    SmallVector<unsigned, 8> BitWidths;
    // The alignments the record's and its fields' attributes evaluated to.
    SmallVector<unsigned, 4> Alignments;
    std::string Attrs;
  };
  auto AddAlignments = [](const Decl *D, SmallVectorImpl<unsigned> &Out) {
    for (const AlignedAttr *A : D->specific_attrs<AlignedAttr>())
      if (std::optional<unsigned> Value = A->getCachedAlignmentValue())
        Out.push_back(*Value);
  };
  auto Shape = [&](const RecordDecl *R) {
    LayoutShape S;
    if (const auto *CXXRD = dyn_cast<CXXRecordDecl>(R))
      for (const CXXBaseSpecifier &Base : CXXRD->bases())
        S.Types.push_back(Base.getType());
    for (const FieldDecl *FD : R->fields()) {
      S.Types.push_back(FD->getType());
      // Each copy evaluated its widths for its own targets.
      S.BitWidths.push_back(FD->isBitField() &&
                                    !FD->getBitWidth()->isValueDependent()
                                ? FD->getBitWidthValue() + 1
                                : 0);
      AddAlignments(FD, S.Alignments);
    }
    AddAlignments(R, S.Alignments);
    S.Attrs = layoutAttributes(Ctx, R);
    return S;
  };
  auto SameShape = [&](const LayoutShape &A, const LayoutShape &B) {
    return A.Types.size() == B.Types.size() && A.Attrs == B.Attrs &&
           A.BitWidths == B.BitWidths && A.Alignments == B.Alignments &&
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
    if (isHostsCopy(Ctx, Copy))
      Host = CopyShape;
    if (!isDevicesCopy(Ctx, Copy))
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
  for (unsigned V : Ctx.getTargetVariants())
    Covered += llvm::any_of(Copies, [V](const NamedDecl *Copy) {
      return Copy->getTargetVariant() && Copy->isCopyFor(V);
    });
  if (Kind != TDK_None || Covered > Ctx.getNumAuxTargets())
    TargetDivergentLayoutCache[D] = Result;
  return Result;
}

namespace {
/// Collects the strongest target dependence among what a definition names.
class TargetDependenceCollector
    : public RecursiveASTVisitor<TargetDependenceCollector> {
  const ASTContext &Ctx;

public:
  TargetPartition Result = 0;
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
  bool add(TargetPartition P) {
    Result = Ctx.getMultiTarget().refineTargetPartition(Result, P);
    return !Ctx.getMultiTarget().isFinestTargetPartition(Result);
  }
  bool done() const {
    return Ctx.getMultiTarget().isFinestTargetPartition(Result);
  }
  bool VisitDeclRefExpr(DeclRefExpr *E) { return consider(E->getDecl()); }
  bool VisitMemberExpr(MemberExpr *E) { return consider(E->getMemberDecl()); }
  bool VisitCXXConstructExpr(CXXConstructExpr *E) {
    return add(
        Ctx.getMultiTarget().getMemberTargetPartition(E->getConstructor()));
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
  TargetPartition referenceDependence(const Decl *D) const {
    if (const auto *ND = dyn_cast<NamedDecl>(D);
        ND && Ctx.getMultiTarget().isTargetDivergentSource(ND))
      return Ctx.getMultiTarget().getTargetPartition(ND);
    if (const auto *ND = dyn_cast<NamedDecl>(D);
        ND && Ctx.getMultiTarget().isArchDivergentFunction(ND))
      return Ctx.getMultiTarget().reconcileTargetPartition(
          0, MultiTargetContext::TDK_Arch);
    if (const auto *FD = dyn_cast<FunctionDecl>(D)) {
      if (!FD->isConstexpr())
        return 0;
      if (const FunctionTemplateDecl *FTD = FD->getPrimaryTemplate())
        return Ctx.getMultiTarget().getTargetPartition(FTD);
      if (const auto *CTSD =
              dyn_cast<ClassTemplateSpecializationDecl>(FD->getDeclContext()))
        return Ctx.getMultiTarget().getTargetPartition(
            CTSD->getSpecializedTemplate());
      return Ctx.getMultiTarget().refineTargetPartition(
          Ctx.getMultiTarget().getTargetPartition(FD),
          Ctx.getMultiTarget().getMemberTargetPartition(FD));
    }
    if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(D))
      return FTD->getTemplatedDecl()->isConstexpr()
                 ? Ctx.getMultiTarget().getTargetPartition(FTD)
                 : 0;
    if (const auto *VD = dyn_cast<VarDecl>(D)) {
      if (const auto *CTSD =
              dyn_cast<ClassTemplateSpecializationDecl>(VD->getDeclContext()))
        return Ctx.getMultiTarget().getTargetPartition(
            CTSD->getSpecializedTemplate());
      if (VD->getDeclContext()->isFunctionOrMethod() || !VD->getInit() ||
          !VD->isUsableInConstantExpressions(const_cast<ASTContext &>(Ctx)))
        return 0;
      return Ctx.getMultiTarget().getTargetPartition(VD);
    }
    // Naming a class template whose layout differs between the host and the
    // devices gives each its own specialization already (see
    // ClassTemplateSpecializationDecl::Profile); one that differs between
    // device architectures needs code naming it instantiated per
    // architecture.
    if (const auto *CTD = dyn_cast<ClassTemplateDecl>(D)) {
      TargetPartition Layout =
          Ctx.getMultiTarget().getLayoutTargetPartition(CTD);
      if (Ctx.getMultiTarget().getTargetDependenceKind(Layout) ==
          MultiTargetContext::TDK_Arch)
        return Layout;
      // Each target instantiates its own copy of a template redeclared per
      // target, which matters where the copies declare different things.
      return Ctx.getMultiTarget().refineTargetPartition(
          Ctx.getMultiTarget().getTargetPartition(CTD),
          Ctx.getMultiTarget().getClassTemplateShapePartition(CTD));
    }
    if (isa<TypeAliasTemplateDecl, VarTemplateDecl>(D))
      return Ctx.getMultiTarget().getTargetPartition(D);
    if (const auto *RD = dyn_cast<RecordDecl>(D))
      return Ctx.getMultiTarget().getLayoutTargetPartition(RD);
    if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(D))
      return Ctx.getMultiTarget().getTargetPartition(
          CTSD->getSpecializedTemplate());
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

void MultiTargetContext::forEachKeptSharedPart(
    const Decl *D,
    llvm::function_ref<void(const Decl *, TargetPartition, const Decl *)> Found)
    const {
  for (; D && !isa<TranslationUnitDecl>(D);
       D = dyn_cast<Decl>(D->getLexicalDeclContext())) {
    // What D was instantiated from, and the template it describes.
    const Decl *Parts[3] = {D, nullptr, nullptr};
    if (const auto *FD = dyn_cast<FunctionDecl>(D))
      Parts[1] = FD->getTemplateInstantiationPattern();
    else if (const auto *RD = dyn_cast<CXXRecordDecl>(D))
      Parts[1] = RD->getTemplateInstantiationPattern();
    else if (const auto *VD = dyn_cast<VarDecl>(D))
      Parts[1] = VD->getTemplateInstantiationPattern();
    if (const auto *FD =
            dyn_cast_or_null<FunctionDecl>(Parts[1] ? Parts[1] : D))
      Parts[2] = FD->getDescribedFunctionTemplate();
    else if (const auto *RD =
                 dyn_cast_or_null<CXXRecordDecl>(Parts[1] ? Parts[1] : D))
      Parts[2] = RD->getDescribedClassTemplate();
    for (const Decl *Part : Parts)
      if (Part)
        if (auto [P, Root] = getKeptShared(Part); P)
          Found(Part, P, Root);
  }
}

size_t
MultiTargetContext::getRecordDeclarationShape(const CXXRecordDecl *RD) const {
  auto [It, Inserted] = RecordDeclarationShapeCache.try_emplace(RD);
  if (Inserted)
    It->second = recordShape(Ctx, RD, /*StaticInitializers=*/true);
  return It->second;
}

TargetPartition
MultiTargetContext::getRecordShapePartition(const RecordDecl *RD) const {
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
        Ctx, dyn_cast_or_null<CXXRecordDecl>(copyForVariant(Copies, V)),
        /*StaticInitializers=*/false);
  });
  // Copies not parsed yet may still differ.
  unsigned Covered = 0;
  for (unsigned V : Ctx.getTargetVariants())
    Covered += llvm::any_of(Copies, [V](const NamedDecl *Copy) {
      return Copy->getTargetVariant() && Copy->isCopyFor(V);
    });
  if (Covered > Ctx.getNumAuxTargets())
    RecordShapeCache[RD] = Result;
  return Result;
}

TargetPartition MultiTargetContext::getClassTemplateShapePartition(
    const ClassTemplateDecl *CTD) const {
  SmallVector<const NamedDecl *, 4> Copies;
  getTargetCopies(CTD, Copies);
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
  return partitionTargetsByAnswer([&](unsigned V) {
    const NamedDecl *For = nullptr;
    for (const NamedDecl *Copy : Copies)
      if (Copy->isCopyFor(V) || (!For && !Copy->getTargetVariant()))
        For = Copy;
    return Shape(For);
  });
}

TargetPartition
MultiTargetContext::getDefaultArgumentDependence(const TemplateDecl *TD) const {
  TargetDependenceCollector Collector(Ctx);
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

MultiTargetContext::TargetDependenceKind
MultiTargetContext::getTargetDependence(const Decl *D) const {
  return getTargetDependenceKind(getTargetPartition(D));
}

TargetPartition MultiTargetContext::getTargetPartition(const Decl *D) const {
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

  TargetDependenceCollector Collector(Ctx);
  bool Complete = true;
  if (const auto *ND = dyn_cast<NamedDecl>(D);
      ND && isTargetDivergentSource(ND))
    Collector.Result = sourceTargetPartition(Ctx, ND);
  if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(D)) {
    Complete = FTD->getTemplatedDecl()->hasBody();
    Collector.TraverseDecl(const_cast<FunctionTemplateDecl *>(FTD));
    // Re-parsed for each device architecture, it can name in each copy
    // functions the others do not have.
    if (FTD->isTargetVariantReparseOrigin() && !Collector.done()) {
      SmallVector<const NamedDecl *, 4> Copies;
      getTargetCopies(FTD, Copies);
      for (const NamedDecl *Copy : Copies)
        if (Ctx.isCopyForDevice(Copy) || Copy->isRedundantTargetVariant())
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

unsigned MultiTargetContext::getSpecializationTarget(const Decl *Template,
                                                     unsigned Variant) const {
  // A target without the copy names it through the copy's own target.
  if (isCopyForSeveral(Template) && !Template->isCopyFor(Variant))
    return Template->getTargetSet().first();
  return Variant;
}

TargetSet MultiTargetContext::getInstantiationKeyTargets(const Decl *Template,
                                                         unsigned Key) const {
  bool Side = getTargetDependence(Template) == TDK_Side;
  TargetSet Targets;
  for (unsigned V : Ctx.getTargetVariants())
    if (Template->isCopyFor(V) &&
        (Side && Ctx.isDeviceVariant(V) ? Ctx.getCanonicalDeviceVariant()
                                        : V) == Key)
      Targets.insert(V);
  // Keyed by the target ambient where the copy is not that target's.
  return Targets == TargetSet() ? TargetSet::single(Key) : Targets;
}

TargetSet
MultiTargetContext::getMemberInstantiationTargets(const Decl *D,
                                                  unsigned Variant) const {
  if (const auto *Parent = dyn_cast<CXXRecordDecl>(D->getDeclContext());
      Parent && Parent->isCopyFor(Variant))
    return Parent->getTargetSet();
  return TargetSet::single(Variant);
}

std::optional<TargetSet>
MultiTargetContext::getRecordedSpecializationTargets(const Decl *D) const {
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

void MultiTargetContext::noteTargetDependenceDefinition(const Decl *D) {
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
  TargetDependenceCollector Collector(Ctx);
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

void MultiTargetContext::resolveTargetExplicitSpecializations() {
  if (PendingTargetExplicitSpecializations.empty())
    return;
  TargetSet All;
  for (unsigned V : Ctx.getTargetVariants())
    All.insert(V);
  // Which targets have an explicit specialization for each template and
  // arguments. One every target has, in copies or shared, is each target's
  // own already.
  llvm::DenseMap<std::pair<const Decl *, unsigned>, TargetSet> Covered;
  auto KeyOf = [&](const Decl *Template, const Decl *Spec) {
    return std::make_pair(
        Template,
        profileArgs(Ctx, explicitSpecializationArgs(Spec)).computeHash());
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

bool MultiTargetContext::hasTargetExplicitSpecializations(
    const Decl *Template) const {
  if (TargetExplicitSpecializations.empty())
    return false;
  auto It = TargetExplicitSpecializations.find(Template->getCanonicalDecl());
  return It != TargetExplicitSpecializations.end() &&
         llvm::any_of(It->second, [](const auto &Entry) {
           return Entry.second->getTargetVariant() &&
                  !Entry.second->isRedundantTargetVariant();
         });
}

Decl *MultiTargetContext::getTargetExplicitSpecialization(
    const Decl *Template, ArrayRef<TemplateArgument> Args,
    unsigned Variant) const {
  if (TargetExplicitSpecializations.empty())
    return nullptr;
  auto It = TargetExplicitSpecializations.find(Template->getCanonicalDecl());
  if (It == TargetExplicitSpecializations.end())
    return nullptr;
  llvm::FoldingSetNodeID Want = profileArgs(Ctx, Args);
  for (auto [Spec, Tagged] : It->second)
    if (Tagged->isCopyFor(Variant) &&
        profileArgs(Ctx, explicitSpecializationArgs(Spec)) == Want)
      return Spec;
  return nullptr;
}

void MultiTargetContext::noteUnkeyedSpecialization(const Decl *Template) {
  if (Ctx.isMultiTargetCompile() && !TargetKeysSuppressed)
    TemplatesWithUnkeyedSpecializations.insert(Template->getCanonicalDecl());
}

void MultiTargetContext::noteTargetDependenceEscalation() {
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

void MultiTargetContext::recheckTargetDependence() {
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

bool MultiTargetContext::hasTargetInstantiationKeys(
    const Decl *Template) const {
  return Ctx.isMultiTargetCompile() &&
         getTargetDependence(Template) != TDK_None;
}

/// The key of a class specialization keyed by architecture that \p T names,
/// itself or through its template arguments or enclosing classes, or 0.
using ClassVisitor = llvm::function_ref<bool(const CXXRecordDecl *)>;

static bool anyNamedClass(const ASTContext &Ctx, QualType T, ClassVisitor Visit,
                          unsigned Depth);

static bool anyNamedClass(const ASTContext &Ctx,
                          ArrayRef<TemplateArgument> Args, ClassVisitor Visit,
                          unsigned Depth) {
  for (const TemplateArgument &Arg : Args) {
    if (Arg.getKind() == TemplateArgument::Pack) {
      if (anyNamedClass(Ctx, Arg.pack_elements(), Visit, Depth))
        return true;
    } else if (Arg.getKind() == TemplateArgument::Type &&
               anyNamedClass(Ctx, Arg.getAsType(), Visit, Depth)) {
      return true;
    }
  }
  return false;
}

static bool anyNamedClass(const ASTContext &Ctx, QualType T, ClassVisitor Visit,
                          unsigned Depth) {
  if (T.isNull() || T->isDependentType() || Depth > 8)
    return false;
  T = T.getCanonicalType();
  if (const auto *PT = T->getAs<PointerType>())
    return anyNamedClass(Ctx, PT->getPointeeType(), Visit, Depth);
  if (const auto *RT = T->getAs<ReferenceType>())
    return anyNamedClass(Ctx, RT->getPointeeType(), Visit, Depth);
  if (const ArrayType *AT = Ctx.getAsArrayType(T))
    return anyNamedClass(Ctx, AT->getElementType(), Visit, Depth);
  if (const auto *MPT = T->getAs<MemberPointerType>()) {
    if (const CXXRecordDecl *Class = MPT->getMostRecentCXXRecordDecl())
      if (anyNamedClass(Ctx, Ctx.getCanonicalTagType(Class), Visit, Depth))
        return true;
    return anyNamedClass(Ctx, MPT->getPointeeType(), Visit, Depth);
  }
  if (const auto *FPT = T->getAs<FunctionProtoType>()) {
    if (anyNamedClass(Ctx, FPT->getReturnType(), Visit, Depth))
      return true;
    return llvm::any_of(FPT->param_types(), [&](QualType Param) {
      return anyNamedClass(Ctx, Param, Visit, Depth);
    });
  }
  for (const DeclContext *DC = T->getAsCXXRecordDecl(); DC;
       DC = DC->getParent()) {
    const auto *RD = dyn_cast<CXXRecordDecl>(DC);
    if (!RD)
      continue;
    if (Visit(RD))
      return true;
    if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RD))
      if (anyNamedClass(Ctx, CTSD->getTemplateArgs().asArray(), Visit,
                        Depth + 1))
        return true;
  }
  return false;
}

bool clang::anyNamedClass(const ASTContext &Ctx, QualType T,
                          ClassVisitor Visit) {
  return ::anyNamedClass(Ctx, T, Visit, 0);
}

bool clang::anyNamedClass(const ASTContext &Ctx,
                          ArrayRef<TemplateArgument> Args, ClassVisitor Visit) {
  return ::anyNamedClass(Ctx, Args, Visit, 0);
}

/// The target a specialization the arguments \p Args name is keyed by, where
/// that decides which device architecture's code it is.
static unsigned getArchKeyNamed(const ASTContext &Ctx,
                                ArrayRef<TemplateArgument> Args) {
  unsigned Found = 0;
  clang::anyNamedClass(Ctx, Args, [&](const CXXRecordDecl *RD) {
    const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(RD);
    if (!CTSD)
      return false;
    if (unsigned Key = Ctx.getMultiTarget().getTargetInstantiationKey(
            CTSD->getCanonicalDecl());
        Key &&
        Ctx.getMultiTarget().getTargetDependence(
            CTSD->getSpecializedTemplate()) == MultiTargetContext::TDK_Arch)
      Found = Key;
    // A specialization keyed by the target it was made for, through a static
    // member's value (see ClassTemplateSpecializationDecl::Profile), is that
    // target's.
    else if (unsigned Key =
                 Ctx.getMultiTarget().getAmbientSpecializationKey(CTSD);
             Key && CTSD->getSpecializedTemplate()->hasTargetKeyedValuesOnly())
      Found = Key;
    return Found != 0;
  });
  return Found;
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
  for (unsigned V : Ctx.getDeviceVariants())
    if (V != Canonical && Shape(V) != Expected)
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
    if (unsigned Key = getArchKeyNamed(Ctx, ArrayRef(Arg)))
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
    if (unsigned Key =
            CTSD ? Ctx.getMultiTarget().getAmbientSpecializationKey(CTSD) : 0;
        Key && signatureNamesParameterMember(FTD, Index) &&
        partialSpecializationsDifferBetweenDevices(
            Ctx, CTSD->getSpecializedTemplate()))
      return Key;
  }
  return 0;
}

MultiTargetContext::TargetDependenceKind
MultiTargetContext::getSpecializationTargetDependence(
    const Decl *Template, ArrayRef<TemplateArgument> Args) const {
  TargetDependenceKind Dependence = getTargetDependence(Template);
  if (Dependence == TDK_Arch ||
      (TargetInstantiationKeys.empty() && SpecializationKeyVariant.empty()))
    return Dependence;
  return getArchKeyNamed(Ctx, Template, Args) ? TDK_Arch : Dependence;
}

unsigned MultiTargetContext::computeTargetInstantiationKey(
    const Decl *Template, ArrayRef<TemplateArgument> Args) const {
  if (!Ctx.isMultiTargetCompile() || TargetKeysSuppressed)
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
        !(Enclosing == Ctx.getHostVariant() || isSharedByDevices(Keyed)))
      return Enclosing;
    if (unsigned Key = getArchKeyNamed(Ctx, Template, Args);
        Ctx.isDeviceVariant(Key) &&
        (Key != Ctx.getCanonicalDeviceVariant() || HostViewOfDeviceCode))
      return Key;
    if (HostViewOfDeviceCode ||
        !Ctx.isDeviceVariant(Target = getInstantiationTarget()))
      return Enclosing;
  } else if (ByArguments && HostViewOfDeviceCode) {
    // The host's view of device code naming a device's classes views that
    // device's specialization.
    if (unsigned Key = getArchKeyNamed(Ctx, Template, Args);
        Ctx.isDeviceVariant(Key))
      return Key;
  }
  if (!Target)
    Target = getInstantiationTarget();
  if (!Ctx.isDeviceVariant(Target) && !HostViewOfDeviceCode)
    if (const auto *FTD = dyn_cast<FunctionTemplateDecl>(Template)) {
      const FunctionDecl *FD = FTD->getTemplatedDecl();
      if (FD->hasAttr<CUDAGlobalAttr>()
              ? !KernelsKeyedForHost
              : FD->hasAttr<CUDADeviceAttr>() && !FD->hasAttr<CUDAHostAttr>())
        Target = Ctx.getCanonicalDeviceVariant();
    }
  // Code compiled for a target a copy standing for several is not for (e.g.
  // a host function launching a kernel) reaches it for the target ambient
  // now. Keys follow the dependence, not the copy's targets: a copy grows
  // targets after its first specializations are made.
  if (isCopyForSeveral(Template))
    if (unsigned Ambient = Ctx.getCurrentTargetVariant();
        !Template->isCopyFor(Target) && Template->isCopyFor(Ambient))
      Target = Ambient;
  if (Dependence == TDK_Side)
    return Ctx.isDeviceVariant(Target) ? Ctx.getCanonicalDeviceVariant()
                                       : Ctx.getHostVariant();
  return Target;
}

unsigned MultiTargetContext::getSpecializationKey(const Decl *D) const {
  D = D->getCanonicalDecl();
  // A class specialization's key is recorded as the variant it is keyed by,
  // whichever way it was keyed.
  if (const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(D))
    if (unsigned Key = getSpecializationKeyVariant(CTSD))
      return Key;
  return getTargetInstantiationKey(D);
}

unsigned MultiTargetContext::getAmbientSpecializationKey(
    const ClassTemplateSpecializationDecl *CTSD) const {
  if (getTargetInstantiationKey(CTSD->getCanonicalDecl()))
    return 0;
  return getSpecializationKeyVariant(CTSD);
}

unsigned MultiTargetContext::getEffectiveTargetInstantiationKey(
    const Decl *D, const Decl **Keyed) const {
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

bool MultiTargetContext::isSharedByDevices(const Decl *D) const {
  if (getTargetInstantiationKey(D) != Ctx.getCanonicalDeviceVariant())
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

TargetSet
MultiTargetContext::getTargetInstantiationTargets(const Decl *D) const {
  unsigned Key = getEffectiveTargetInstantiationKey(D);
  if (!Key)
    return TargetSet::all();
  if (std::optional<TargetSet> Recorded = getRecordedSpecializationTargets(D))
    return *Recorded;
  if (Key != Ctx.getCanonicalDeviceVariant())
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

bool clang::hasTargetWidenedSiblingDecl(const FunctionDecl *FD) {
  for (const NamedDecl *ND : FD->getDeclContext()->lookup(FD->getDeclName())) {
    const auto *RD = dyn_cast<FunctionDecl>(ND);
    if (!RD || RD == FD)
      continue;
    if (!RD->getTargetVariant() || RD->isRedundantTargetVariant())
      continue;
    if (!RD->isTargetVariantReparseOrigin())
      return true;
  }
  return false;
}

bool clang::isTargetDivergentCallee(const ValueDecl *D) {
  const auto *FD = dyn_cast_or_null<FunctionDecl>(D);
  if (!FD)
    return false;
  if (FunctionTemplateDecl *FTD = FD->getPrimaryTemplate())
    return FTD->hasTargetTaggedRedeclaration();
  // See FunctionTemplateDecl::hasTargetTaggedRedeclaration()'s comment above
  // for why a reparse-origin tag doesn't count as divergence evidence. FD's
  // own tag alone is not a reliable signal either: a caller can resolve to
  // the canonical, tag-0 "claimed primary" copy of a widened plain function
  // even though genuine #if/#elif widening has produced other, real
  // (non-reparse-origin) tagged siblings for other targets. Those siblings
  // are registered by name in the enclosing DeclContext even when FD's own
  // redecls() chain doesn't include them, so query that directly.
  if (hasTargetWidenedSiblingDecl(FD))
    return true;
  // FD's own text is never #if-widened, so the sibling-lookup check above
  // can't see any divergence -- but FD may be a thin, non-divergent wrapper
  // around a genuinely divergent callee one level down (e.g.
  // ggml_cuda_fattn_mma_get_nbatch_fa's body just returns
  // ggml_cuda_fattn_mma_get_config(...).nbatch_fa, and it's get_config whose
  // body is #if-gated). Recursing into FD's own body via
  // containsTargetDivergentCallee gives this check transitivity for free,
  // since that function already calls back into isTargetDivergentCallee for
  // every DeclRefExpr/MemberExpr it finds. Guard against infinite recursion
  // for (mutually) recursive functions with a thread-local in-progress set.
  static thread_local llvm::SmallPtrSet<const FunctionDecl *, 8> InProgress;
  const FunctionDecl *Definition = FD->getDefinition();
  if (Definition && Definition->hasBody() &&
      InProgress.insert(Definition).second) {
    bool R = containsTargetDivergentCallee(Definition->getBody());
    InProgress.erase(Definition);
    if (R)
      return true;
  }
  return false;
}

/// Shared by isTargetDivergentValueMemberAccess and
/// isTargetDivergentMemberFunctionAccess: true if \p DC is a
/// ClassTemplateSpecializationDecl whose specialized template forks its
/// FoldingSet identity per target -- the same disjunction
/// ClassTemplateSpecializationDecl::Profile itself uses, so this neither
/// over- nor under-triggers relative to what Profile can actually fork.
static bool isOwnedByTargetForkedSpecialization(const DeclContext *DC) {
  const auto *CTSD = dyn_cast_or_null<ClassTemplateSpecializationDecl>(DC);
  if (!CTSD)
    return false;
  const ClassTemplateDecl *Template = CTSD->getSpecializedTemplate();
  return Template && Template->hasTargetKeyedSpecializations();
}

bool clang::isTargetDivergentValueMemberAccess(const ValueDecl *D) {
  const auto *VD = dyn_cast_or_null<VarDecl>(D);
  if (!VD)
    return false;
  // A copy each target has of a static data member is instantiated as that
  // target's.
  if (const VarDecl *Pattern = VD->getInstantiatedFromStaticDataMember();
      Pattern && Pattern->getTargetVariant() &&
      !Pattern->isRedundantTargetVariant() &&
      isa<ClassTemplateSpecializationDecl>(VD->getDeclContext()))
    return true;
  return isOwnedByTargetForkedSpecialization(VD->getDeclContext());
}

bool clang::isTargetDivergentMemberFunctionAccess(const ValueDecl *D) {
  const auto *MD = dyn_cast_or_null<CXXMethodDecl>(D);
  return MD && isOwnedByTargetForkedSpecialization(MD->getDeclContext());
}

bool clang::isTargetDivergentTypeMemberAccess(const TypeDecl *D) {
  const auto *TND = dyn_cast_or_null<TypedefNameDecl>(D);
  return TND && isOwnedByTargetForkedSpecialization(TND->getDeclContext());
}

/// Walks a statement/expression tree looking for a DeclRefExpr/MemberExpr
/// callee reference that clang::isTargetDivergentCallee flags -- e.g. the
/// call to ggml_cuda_get_physical_warp_size() inside tile<...>::ne's
/// initializer, or inside process_tile<N>'s local `lane_count` initializer.
/// A hand-rolled Stmt::children() walk is used instead of a
/// RecursiveASTVisitor since both callers only need a cheap existence check
/// over a small tree; Stmt::children() already exposes a DeclStmt's VarDecl
/// initializers as children, so this reaches local-variable initializers
/// too without any special-casing.
bool clang::containsTargetDivergentCallee(const Stmt *S) {
  if (!S)
    return false;
  if (const auto *DRE = dyn_cast<DeclRefExpr>(S)) {
    if (isTargetDivergentCallee(DRE->getDecl()))
      return true;
  } else if (const auto *ME = dyn_cast<MemberExpr>(S)) {
    if (isTargetDivergentCallee(ME->getMemberDecl()))
      return true;
  }
  for (const Stmt *Child : S->children())
    if (containsTargetDivergentCallee(Child))
      return true;
  return false;
}
