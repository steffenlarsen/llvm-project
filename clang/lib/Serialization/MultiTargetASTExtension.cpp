//===- MultiTargetASTExtension.cpp - Multi-target state in AST files ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Serialization/MultiTargetASTExtension.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/DiagnosticSerialization.h"
#include "clang/Sema/Sema.h"
#include "clang/Serialization/ASTBitCodes.h"
#include "clang/Serialization/ASTReader.h"
#include "clang/Serialization/ASTWriter.h"
#include "clang/Serialization/ModuleFile.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Bitstream/BitstreamReader.h"
#include "llvm/Bitstream/BitstreamWriter.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

using namespace clang;
using namespace clang::serialization;

char MultiTargetASTExtension::ID = 0;

namespace {
/// The extension block's records. Each pairs declarations with what is known
/// about them, or lists declarations.
enum RecordKind : unsigned {
  /// (decl, ASTContext::DeclTargetVariantStorage byte)*
  DECL_TARGET_VARIANTS = FIRST_EXTENSION_RECORD_ID,
  /// (decl, key)*: ASTContext::TargetInstantiationKeys.
  INSTANTIATION_KEYS,
  /// (decl, mask)*: ASTContext::TargetInstanceReplacements.
  INSTANCE_REPLACEMENTS,
  /// (decl, variant)*: ASTContext::SpecializationKeyVariant.
  CLASS_SPECIALIZATION_VARIANTS,
  /// (decl, variant)*: ASTContext::FunctionSpecializationVariant.
  FUNCTION_SPECIALIZATION_VARIANTS,
  /// (decl, variant)*: ASTContext::TargetVariantForSpecialization.
  SPECIALIZATION_PATTERN_VARIANTS,
  /// (decl, variant)*: ASTContext::PartialSpecializationVariant.
  PARTIAL_SPECIALIZATION_VARIANTS,
  /// decl*: ASTContext::ImplicitAuxDeviceConstants.
  IMPLICIT_AUX_DEVICE_CONSTANTS,
  /// decl*: ASTContext::TemplatesWithUnkeyedSpecializations.
  TEMPLATES_WITH_UNKEYED_SPECIALIZATIONS,
  /// (decl, variant, decl)*: ASTContext::TargetVariantWrapperClones.
  WRAPPER_CLONES,
  /// (decl, variant)*: Sema::PendingInstantiationTargetVariant.
  PENDING_INSTANTIATION_VARIANTS,
  /// (decl, variant)*: Sema::PendingInstantiationProvenanceVariant.
  PENDING_INSTANTIATION_PROVENANCE,
  /// (decl, variant)*: Sema::SpecializationDeclVariant.
  SPECIALIZATION_DECL_VARIANTS,
  /// decl*: Sema::DivergentUsers.
  DIVERGENT_USERS,
  /// (decl, value)*: what each specialization folds into its identity, as
  /// specializationIdentity encodes it; see
  /// ASTContext::ImportedSpecializationIdentities.
  SPECIALIZATION_IDENTITIES,
  /// decl*: the templates whose specializations are keyed by the target
  /// instantiating them; see ASTContext::ImportedKeyedTemplates.
  KEYED_TEMPLATES,
  /// [decl]: ASTContext::createDualSideCallHolder.
  DUAL_SIDE_CALL_HOLDER,
  /// (owner decl, ordinal, host, device)*: ASTContext::CUDAAmbiguousCalls,
  /// each call as the position of a statement its owner owns (see
  /// OwnedStmtWalker), and each side as 0 for that call itself or 1 + its
  /// position in the holder's body.
  DUAL_SIDE_CALLS,
  /// (owner decl, ordinal, variant, decl)*:
  /// ASTContext::TargetVariantDeclRedirects.
  DECL_REDIRECTS,
  /// [function, attribute, argument, variant, unsigned, bit width, word*]:
  /// one of ASTContext::TargetVariantConstantValues, the argument as its
  /// position among the arguments of the function's attribute in
  /// launchBoundsArguments.
  ATTRIBUTE_CONSTANTS,
  /// [blob]: Prefix::Headers.
  PREFIX_HEADERS,
  /// [aux target index, blob]: PrefixState::Macros.
  PREFIX_MACROS,
  /// [aux target index, blob]: PrefixState::IncludeOnceFiles.
  PREFIX_INCLUDE_ONCE_FILES,
  /// [aux target index, blob]: PrefixState::ControllingMacros.
  PREFIX_CONTROLLING_MACROS,
};

/// Numbers the statements a declaration owns outside of any other
/// declaration -- a constructor's member initializers, a function's body, a
/// variable's initializer, a field's default member initializer -- in a
/// pre-order that reading the declaration back reproduces.
class OwnedStmtWalker {
  llvm::function_ref<void(const Stmt *, unsigned)> Visit;

public:
  explicit OwnedStmtWalker(
      llvm::function_ref<void(const Stmt *, unsigned)> Visit)
      : Visit(Visit) {}

  /// Whether \p D owns statements of its own.
  static bool isOwner(const Decl *D) {
    // A default argument belongs to its parameter.
    if (D->getParentFunctionOrMethod() && !isa<ParmVarDecl>(D))
      return false;
    if (const auto *FD = dyn_cast<FunctionDecl>(D))
      return FD->doesThisDeclarationHaveABody();
    if (const auto *VD = dyn_cast<VarDecl>(D))
      return VD->hasInit();
    if (const auto *Field = dyn_cast<FieldDecl>(D))
      return Field->hasInClassInitializer();
    return false;
  }

  void walk(const Decl *D) {
    SmallVector<const Stmt *, 32> Roots;
    if (const auto *Ctor = dyn_cast<CXXConstructorDecl>(D))
      for (const CXXCtorInitializer *Init : Ctor->inits())
        Roots.push_back(Init->getInit());
    if (const auto *FD = dyn_cast<FunctionDecl>(D))
      Roots.push_back(FD->getBody());
    else if (const auto *VD = dyn_cast<VarDecl>(D))
      Roots.push_back(VD->getInit());
    else if (const auto *Field = dyn_cast<FieldDecl>(D))
      Roots.push_back(Field->getInClassInitializer());

    unsigned Ordinal = 0;
    SmallVector<const Stmt *, 64> Stack(llvm::reverse(Roots));
    while (!Stack.empty()) {
      const Stmt *S = Stack.pop_back_val();
      if (!S)
        continue;
      Visit(S, Ordinal++);
      size_t Mark = Stack.size();
      for (const Stmt *Child : S->children())
        Stack.push_back(Child);
      std::reverse(Stack.begin() + Mark, Stack.end());
    }
  }
};

/// The arguments ASTContext::TargetVariantConstantValues may hold values of,
/// in the order the records name them by.
SmallVector<const Expr *, 6> launchBoundsArguments(const FunctionDecl *FD) {
  SmallVector<const Expr *, 6> Arguments;
  for (const auto *A : FD->specific_attrs<CUDALaunchBoundsAttr>())
    Arguments.append(
        {A->getMaxThreads(), A->getMinBlocks(), A->getMaxBlocks()});
  return Arguments;
}

/// Strings are written as one blob, each followed by a NUL.
std::string joinStrings(const std::vector<std::string> &Strings) {
  std::string Blob;
  for (const std::string &S : Strings) {
    Blob += S;
    Blob += '\0';
  }
  return Blob;
}

std::vector<std::string> splitStrings(StringRef Blob) {
  std::vector<std::string> Strings;
  while (!Blob.empty()) {
    auto [S, Rest] = Blob.split('\0');
    Strings.push_back(S.str());
    Blob = Rest;
  }
  return Strings;
}

/// What \p Spec folds into its identity (see
/// FunctionTemplateSpecializationInfo::Profile and
/// ClassTemplateSpecializationDecl::Profile), or 0 if it is not a
/// specialization whose identity depends on a target: the target
/// instantiating it, shifted left with the low bit set, or the key it was
/// created with (see ASTContext::computeTargetInstantiationKey), shifted left.
uint64_t specializationIdentity(const ASTContext &Ctx, const Decl *Spec) {
  auto Encode = [](unsigned Value, bool Instantiating) -> uint64_t {
    return Value ? (uint64_t(Value) << 1) | Instantiating : 0;
  };
  if (const auto *FD = dyn_cast<FunctionDecl>(Spec)) {
    const FunctionTemplateDecl *Template = FD->getPrimaryTemplate();
    if (!Template)
      return 0;
    return Template->hasTargetKeyedSpecializations()
               ? Encode(Ctx.getFunctionSpecializationVariant(FD), true)
               : Encode(Ctx.getTargetInstantiationKey(FD), false);
  }
  const auto *CTSD = dyn_cast<ClassTemplateSpecializationDecl>(Spec);
  if (!CTSD || isa<ClassTemplatePartialSpecializationDecl>(CTSD))
    return 0;
  return CTSD->getSpecializedTemplate()->hasTargetKeyedSpecializations()
             ? Encode(Ctx.getSpecializationKeyVariant(CTSD), true)
             : Encode(Ctx.getTargetInstantiationKey(CTSD), false);
}

constexpr const char *BlockName = "clang.multi_target_ast";
constexpr unsigned MajorVersion = 1;
constexpr unsigned MinorVersion = 0;
} // namespace

MultiTargetASTExtension::MultiTargetASTExtension(std::vector<Target> AuxTargets)
    : AuxTargets(std::move(AuxTargets)) {}

MultiTargetASTExtension::~MultiTargetASTExtension() = default;

std::string MultiTargetASTExtension::describeTargets() const {
  std::string Result;
  llvm::raw_string_ostream OS(Result);
  llvm::ListSeparator LS("; ");
  for (const Target &T : AuxTargets)
    OS << LS << T.Triple << ':' << T.CPU << ':' << llvm::join(T.Features, ",")
       << ':' << llvm::format_hex_no_prefix(T.Predefines, 16);
  return Result;
}

std::vector<MultiTargetASTExtension::Target>
MultiTargetASTExtension::parseTargets(StringRef UserInfo) {
  std::vector<Target> Targets;
  SmallVector<StringRef, 4> Described;
  UserInfo.split(Described, "; ", -1, /*KeepEmpty=*/false);
  for (StringRef D : Described) {
    // triple:cpu:features:predefines, where only the CPU may contain a ':'.
    auto [Triple, Rest] = D.split(':');
    auto [Head, Hash] = Rest.rsplit(':');
    auto [CPU, Features] = Head.rsplit(':');
    Target T{Triple.str(), CPU.str(), {}, 0};
    SmallVector<StringRef, 8> Split;
    Features.split(Split, ',', -1, /*KeepEmpty=*/false);
    for (StringRef F : Split)
      T.Features.push_back(F.str());
    Hash.getAsInteger(16, T.Predefines);
    Targets.push_back(std::move(T));
  }
  return Targets;
}

std::optional<SmallVector<unsigned, 4>> MultiTargetASTExtension::mapTargets(
    const ModuleFileExtensionMetadata &Metadata) const {
  if (Metadata.BlockName != BlockName || Metadata.MajorVersion != MajorVersion)
    return std::nullopt;
  std::vector<Target> FileTargets = parseTargets(Metadata.UserInfo);
  SmallVector<unsigned, 4> Variants;
  for (const Target &T : AuxTargets) {
    auto It = llvm::find(FileTargets, T);
    if (It == FileTargets.end())
      return std::nullopt;
    Variants.push_back(2 + (It - FileTargets.begin()));
  }
  // The specialization the file's canonical device instantiated serves every
  // target without one of its own. Unless this compilation's canonical device
  // is the same, it can no longer say which of them that is.
  if (Variants.size() > 1 && Variants.front() != 2)
    return std::nullopt;
  return Variants;
}

ModuleFileExtensionMetadata
MultiTargetASTExtension::getExtensionMetadata() const {
  return {BlockName, MajorVersion, MinorVersion, describeTargets()};
}

void MultiTargetASTExtension::hashExtension(
    ExtensionHashBuilder &HBuilder) const {
  HBuilder.add(StringRef(BlockName), MajorVersion, MinorVersion);
  HBuilder.add(describeTargets());
}

//===----------------------------------------------------------------------===//
// Writing
//===----------------------------------------------------------------------===//

class MultiTargetASTExtension::Writer : public ModuleFileExtensionWriter {
  ASTWriter &W;

public:
  Writer(MultiTargetASTExtension *Ext, ASTWriter &W)
      : ModuleFileExtensionWriter(Ext), W(W) {}

  void writeExtensionContents(Sema &S, llvm::BitstreamWriter &Stream) override;

private:
  void writeExpressionKeyedState(Sema &S, llvm::BitstreamWriter &Stream);
  void writeAttributeConstants(Sema &S, llvm::BitstreamWriter &Stream);
};

void MultiTargetASTExtension::Writer::writeAttributeConstants(
    Sema &S, llvm::BitstreamWriter &Stream) {
  ASTContext &Ctx = S.getASTContext();
  if (Ctx.TargetVariantConstantValues.empty())
    return;
  llvm::DenseMap<const Expr *, std::pair<uint64_t, unsigned>> Where;
  struct OwnerFinder : RecursiveASTVisitor<OwnerFinder> {
    ASTWriter &W;
    const ASTContext &Ctx;
    llvm::DenseMap<const Expr *, std::pair<uint64_t, unsigned>> &Where;
    OwnerFinder(
        ASTWriter &W, const ASTContext &Ctx,
        llvm::DenseMap<const Expr *, std::pair<uint64_t, unsigned>> &Where)
        : W(W), Ctx(Ctx), Where(Where) {}
    bool shouldVisitTemplateInstantiations() const { return true; }
    bool VisitFunctionDecl(FunctionDecl *FD) {
      if (!FD->hasAttr<CUDALaunchBoundsAttr>() || !W.wasDeclEmitted(FD))
        return true;
      for (auto [I, E] : llvm::enumerate(launchBoundsArguments(FD)))
        if (E && Ctx.TargetVariantConstantValues.count(E))
          Where.try_emplace(E, W.getDeclID(FD).getRawValue(), I);
      return true;
    }
  } Finder(W, Ctx, Where);
  Finder.TraverseDecl(Ctx.getTranslationUnitDecl());
  if (Where.size() != Ctx.TargetVariantConstantValues.size()) {
    S.Diag(SourceLocation(), diag::err_multi_target_ast_unwritable)
        << "the per-target values of attribute arguments";
    return;
  }

  SmallVector<ASTWriter::RecordData, 8> Records;
  for (const auto &[E, ByVariant] : Ctx.TargetVariantConstantValues) {
    auto [Function, Argument] = Where.lookup(E);
    for (const auto &[Variant, Value] : ByVariant) {
      ASTWriter::RecordData &Record = Records.emplace_back();
      Record.append({Function, Argument, Variant, Value.isUnsigned(),
                     Value.getBitWidth()});
      Record.append(Value.getRawData(),
                    Value.getRawData() + Value.getNumWords());
    }
  }
  llvm::sort(Records);
  for (const ASTWriter::RecordData &Record : Records)
    Stream.EmitRecord(ATTRIBUTE_CONSTANTS, Record);
}

void MultiTargetASTExtension::Writer::writeExpressionKeyedState(
    Sema &S, llvm::BitstreamWriter &Stream) {
  ASTContext &Ctx = S.getASTContext();
  llvm::DenseSet<const Stmt *> Wanted;
  for (const auto &Entry : Ctx.CUDAAmbiguousCalls)
    Wanted.insert(Entry.first);
  for (const auto &Entry : Ctx.TargetVariantDeclRedirects)
    Wanted.insert(Entry.first);
  if (Wanted.empty())
    return;

  // Where each of them is: the first declaration found owning it.
  llvm::DenseMap<const Stmt *, std::pair<uint64_t, unsigned>> Where;
  struct OwnerFinder : RecursiveASTVisitor<OwnerFinder> {
    ASTWriter &W;
    llvm::DenseSet<const Stmt *> &Wanted;
    llvm::DenseMap<const Stmt *, std::pair<uint64_t, unsigned>> &Where;
    OwnerFinder(
        ASTWriter &W, llvm::DenseSet<const Stmt *> &Wanted,
        llvm::DenseMap<const Stmt *, std::pair<uint64_t, unsigned>> &Where)
        : W(W), Wanted(Wanted), Where(Where) {}
    bool shouldVisitTemplateInstantiations() const { return true; }
    bool shouldVisitImplicitCode() const { return true; }
    bool VisitDecl(Decl *D) {
      if (!OwnedStmtWalker::isOwner(D) || !W.wasDeclEmitted(D))
        return true;
      uint64_t Owner = W.getDeclID(D).getRawValue();
      OwnedStmtWalker([&](const Stmt *St, unsigned Ordinal) {
        if (Wanted.contains(St))
          Where.try_emplace(St, Owner, Ordinal);
      }).walk(D);
      return true;
    }
  } Finder(W, Wanted, Where);
  Finder.TraverseDecl(Ctx.getTranslationUnitDecl());
  if (Where.size() != Wanted.size()) {
    S.Diag(SourceLocation(), diag::err_multi_target_ast_unwritable)
        << "a call resolved per target outside of any declaration";
    return;
  }

  FunctionDecl *Holder = Ctx.DualSideCallHolder;
  llvm::DenseMap<const Stmt *, unsigned> HolderIndex;
  if (Holder && W.wasDeclEmitted(Holder)) {
    for (auto [I, St] :
         llvm::enumerate(cast<CompoundStmt>(Holder->getBody())->body()))
      HolderIndex[St] = I + 1;
    ASTWriter::RecordData Record{W.getDeclID(Holder).getRawValue()};
    Stream.EmitRecord(DUAL_SIDE_CALL_HOLDER, Record);
  }

  SmallVector<std::array<uint64_t, 4>, 16> Calls;
  for (const auto &[Call, Sides] : Ctx.CUDAAmbiguousCalls) {
    auto Side = [&](const CallExpr *E) -> std::optional<uint64_t> {
      if (E == Call)
        return 0;
      auto It = HolderIndex.find(E);
      if (It == HolderIndex.end())
        return std::nullopt;
      return It->second;
    };
    std::optional<uint64_t> Host = Side(Sides.HostCall);
    std::optional<uint64_t> Device = Side(Sides.DeviceCall);
    if (!Host || !Device) {
      S.Diag(SourceLocation(), diag::err_multi_target_ast_unwritable)
          << "a call's resolution for one of the targets";
      return;
    }
    auto [Owner, Ordinal] = Where.lookup(Call);
    Calls.push_back({Owner, Ordinal, *Host, *Device});
  }
  llvm::sort(Calls);
  ASTWriter::RecordData Record;
  for (const auto &Call : Calls)
    Record.append(Call.begin(), Call.end());
  Stream.EmitRecord(DUAL_SIDE_CALLS, Record);

  SmallVector<std::array<uint64_t, 4>, 16> Redirects;
  for (const auto &[E, ByVariant] : Ctx.TargetVariantDeclRedirects) {
    auto [Owner, Ordinal] = Where.lookup(E);
    for (const auto &[Variant, Target] : ByVariant) {
      const Decl *D =
          isa<FunctionDecl *>(Target)
              ? static_cast<const Decl *>(cast<FunctionDecl *>(Target))
              : cast<VarDecl *>(Target);
      if (!D || !W.wasDeclEmitted(D)) {
        S.Diag(SourceLocation(), diag::err_multi_target_ast_unwritable)
            << "a reference's target for one of the targets";
        return;
      }
      Redirects.push_back(
          {Owner, Ordinal, Variant, W.getDeclID(D).getRawValue()});
    }
  }
  llvm::sort(Redirects);
  Record.clear();
  for (const auto &Redirect : Redirects)
    Record.append(Redirect.begin(), Redirect.end());
  Stream.EmitRecord(DECL_REDIRECTS, Record);
}

void MultiTargetASTExtension::Writer::writeExtensionContents(
    Sema &S, llvm::BitstreamWriter &Stream) {
  ASTContext &Ctx = S.getASTContext();
  // Only what is in this file can be looked up when it is read; every table
  // is ordered by declaration ID so that writing it is deterministic.
  auto EmitPairs = [&](unsigned Code, const auto &Map) {
    SmallVector<std::pair<uint64_t, uint64_t>, 64> Pairs;
    for (const auto &[D, Value] : Map)
      if (D && W.wasDeclEmitted(D))
        Pairs.push_back({W.getDeclID(D).getRawValue(), Value});
    llvm::sort(Pairs);
    ASTWriter::RecordData Record;
    for (const auto &[ID, Value] : Pairs)
      Record.append({ID, Value});
    Stream.EmitRecord(Code, Record);
  };
  auto EmitSet = [&](unsigned Code, const auto &Set) {
    ASTWriter::RecordData Record;
    for (const auto *D : Set)
      if (D && W.wasDeclEmitted(D))
        Record.push_back(W.getDeclID(D).getRawValue());
    llvm::sort(Record);
    Stream.EmitRecord(Code, Record);
  };

  EmitPairs(DECL_TARGET_VARIANTS, Ctx.DeclTargetVariantStorage);
  EmitPairs(INSTANTIATION_KEYS, Ctx.TargetInstantiationKeys);
  EmitPairs(INSTANCE_REPLACEMENTS, Ctx.TargetInstanceReplacements);
  EmitPairs(CLASS_SPECIALIZATION_VARIANTS, Ctx.SpecializationKeyVariant);
  EmitPairs(FUNCTION_SPECIALIZATION_VARIANTS,
            Ctx.FunctionSpecializationVariant);
  EmitPairs(SPECIALIZATION_PATTERN_VARIANTS,
            Ctx.TargetVariantForSpecialization);
  EmitPairs(PARTIAL_SPECIALIZATION_VARIANTS, Ctx.PartialSpecializationVariant);
  EmitSet(IMPLICIT_AUX_DEVICE_CONSTANTS, Ctx.ImplicitAuxDeviceConstants);
  EmitSet(TEMPLATES_WITH_UNKEYED_SPECIALIZATIONS,
          Ctx.TemplatesWithUnkeyedSpecializations);

  SmallVector<std::array<uint64_t, 3>, 4> Clones;
  for (const auto &[Key, Clone] : Ctx.TargetVariantWrapperClones)
    if (W.wasDeclEmitted(Key.first) && W.wasDeclEmitted(Clone))
      Clones.push_back({W.getDeclID(Key.first).getRawValue(), Key.second,
                        W.getDeclID(Clone).getRawValue()});
  llvm::sort(Clones);
  ASTWriter::RecordData Record;
  for (const auto &Clone : Clones)
    Record.append(Clone.begin(), Clone.end());
  Stream.EmitRecord(WRAPPER_CLONES, Record);

  EmitPairs(PENDING_INSTANTIATION_VARIANTS,
            S.PendingInstantiationTargetVariant);
  EmitPairs(PENDING_INSTANTIATION_PROVENANCE,
            S.PendingInstantiationProvenanceVariant);
  EmitPairs(SPECIALIZATION_DECL_VARIANTS, S.SpecializationDeclVariant);
  EmitSet(DIVERGENT_USERS, S.DivergentUsers);

  llvm::DenseMap<const Decl *, uint64_t> Identities;
  auto NoteIdentity = [&](const Decl *D) {
    if (uint64_t Identity = specializationIdentity(Ctx, D))
      Identities[D] = Identity;
  };
  for (const auto &Entry : Ctx.FunctionSpecializationVariant)
    NoteIdentity(Entry.first);
  for (const auto &Entry : Ctx.SpecializationKeyVariant)
    NoteIdentity(Entry.first);
  for (const auto &Entry : Ctx.TargetInstantiationKeys)
    NoteIdentity(Entry.first);
  EmitPairs(SPECIALIZATION_IDENTITIES, Identities);

  // Every template's, since a translation unit may specialize any of them.
  struct KeyedTemplateCollector : RecursiveASTVisitor<KeyedTemplateCollector> {
    SmallVector<const Decl *, 16> Keyed;
    bool shouldVisitTemplateInstantiations() const { return true; }
    bool VisitFunctionTemplateDecl(FunctionTemplateDecl *D) {
      if (D->hasTargetKeyedSpecializations())
        Keyed.push_back(D);
      return true;
    }
    bool VisitClassTemplateDecl(ClassTemplateDecl *D) {
      if (D->hasTargetKeyedSpecializations())
        Keyed.push_back(D);
      return true;
    }
  } Collector;
  Collector.TraverseDecl(Ctx.getTranslationUnitDecl());
  EmitSet(KEYED_TEMPLATES, Collector.Keyed);

  writeExpressionKeyedState(S, Stream);
  writeAttributeConstants(S, Stream);

  const Prefix &P =
      static_cast<MultiTargetASTExtension *>(getExtension())->PrefixToWrite;
  auto Abbrev = std::make_shared<llvm::BitCodeAbbrev>();
  Abbrev->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::VBR, 6)); // code
  Abbrev->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::VBR, 6)); // index
  Abbrev->Add(llvm::BitCodeAbbrevOp(llvm::BitCodeAbbrevOp::Blob));
  unsigned BlobAbbrev = Stream.EmitAbbrev(std::move(Abbrev));
  auto EmitStrings = [&](unsigned Code, unsigned Index,
                         const std::vector<std::string> &Strings) {
    uint64_t Fields[] = {Code, Index};
    Stream.EmitRecordWithBlob(BlobAbbrev, Fields, joinStrings(Strings));
  };
  EmitStrings(PREFIX_HEADERS, 0, P.Headers);
  for (auto [Index, State] : llvm::enumerate(P.States)) {
    EmitStrings(PREFIX_MACROS, Index, State.Macros);
    EmitStrings(PREFIX_INCLUDE_ONCE_FILES, Index, State.IncludeOnceFiles);
    EmitStrings(PREFIX_CONTROLLING_MACROS, Index, State.ControllingMacros);
  }
}

std::unique_ptr<ModuleFileExtensionWriter>
MultiTargetASTExtension::createExtensionWriter(ASTWriter &W) {
  return std::make_unique<Writer>(this, W);
}

//===----------------------------------------------------------------------===//
// Reading
//===----------------------------------------------------------------------===//

namespace {
/// What the extension block records about one declaration.
struct DeclState {
  enum : unsigned {
    TargetVariant = 1 << 0,
    InstantiationKey = 1 << 1,
    InstanceReplacements = 1 << 2,
    ClassSpecializationVariant = 1 << 3,
    FunctionSpecializationVariant = 1 << 4,
    SpecializationPatternVariant = 1 << 5,
    ImplicitAuxDeviceConstant = 1 << 6,
    UnkeyedSpecializations = 1 << 7,
    PendingInstantiationVariant = 1 << 8,
    PendingInstantiationProvenance = 1 << 9,
    SpecializationDeclVariant = 1 << 10,
    DivergentUser = 1 << 11,
    SpecializationIdentity = 1 << 12,
    KeyedTemplate = 1 << 13,
    PartialSpecializationVariant = 1 << 14,
    NeedsSema = PendingInstantiationVariant | PendingInstantiationProvenance |
        SpecializationDeclVariant | DivergentUser,
  };
  unsigned Has = 0;
  uint8_t Storage = 0;
  unsigned Key = 0;
  unsigned Replacements = 0;
  unsigned ClassVariant = 0;
  unsigned FunctionVariant = 0;
  unsigned PatternVariant = 0;
  unsigned PartialVariant = 0;
  unsigned PendingVariant = 0;
  unsigned PendingProvenance = 0;
  unsigned SpecDeclVariant = 0;
  uint64_t Identity = 0;
};
} // namespace

class MultiTargetASTExtension::Reader : public ModuleFileExtensionReader {
  ASTReader &R;
  ModuleFile &F;
  llvm::DenseMap<GlobalDeclID, DeclState> States;
  SmallVector<std::tuple<GlobalDeclID, unsigned, GlobalDeclID>, 4> Clones;
  GlobalDeclID DualSideCallHolder;
  SmallVector<std::tuple<GlobalDeclID, unsigned, unsigned, unsigned>, 16>
      DualSideCalls;
  SmallVector<std::tuple<GlobalDeclID, unsigned, unsigned, GlobalDeclID>, 16>
      Redirects;
  SmallVector<std::tuple<GlobalDeclID, unsigned, unsigned, llvm::APSInt>, 4>
      AttributeConstants;
  bool Started = false;
  /// Declarations read before Sema existed whose Sema state is still owed.
  SmallVector<std::pair<Decl *, const DeclState *>, 8> AwaitingSema;
  Sema *S = nullptr;

  /// The file may have been written for more targets than this compilation
  /// has: for each of this compilation's aux targets, the file's variant, and
  /// for each of the file's, this compilation's, or 0.
  SmallVector<unsigned, 4> CompileToFile;
  SmallVector<unsigned, 4> FileToCompile;

  /// What a specialization of a target this compilation lacks is keyed by: no
  /// lookup asks for it.
  static constexpr unsigned DroppedKey = ASTContext::UnkeyedTarget - 1;

  GlobalDeclID globalID(uint64_t Raw) const {
    return R.getGlobalDeclID(F, LocalDeclID::get(R, F, Raw));
  }
  void applySemaState(Decl *D, const DeclState &State);

  /// The variant this compilation has for the file's \p V, or
  /// Decl::TargetVariantRedundant if it lacks that target.
  unsigned variant(unsigned V) const {
    if (V <= 1 || V == Decl::TargetVariantRedundant)
      return V;
    unsigned C = V - 2 < FileToCompile.size() ? FileToCompile[V - 2] : 0;
    return C ? C : Decl::TargetVariantRedundant;
  }
  /// The same, for the target a specialization was instantiated for.
  unsigned instantiatingVariant(unsigned V) const {
    unsigned C = variant(V);
    return C == Decl::TargetVariantRedundant ? DroppedKey : C;
  }
  /// A specialization's key (see ASTContext::computeTargetInstantiationKey)
  /// and, if it is keyed by the canonical device, the targets with one of
  /// their own (see ASTContext::markTargetInstanceReplaced), for this
  /// compilation.
  std::pair<unsigned, unsigned> key(unsigned Key, unsigned Replaced) const;

public:
  Reader(MultiTargetASTExtension *Ext, ASTReader &R, ModuleFile &F,
         llvm::BitstreamCursor Stream, ArrayRef<unsigned> CompileToFile,
         unsigned NumFileTargets);

  void readDecl(GlobalDeclID ID, Decl *D) override;
  void startTranslationUnit() override;
  void initializeSema(Sema &SemaRef) override;
};

std::pair<unsigned, unsigned>
MultiTargetASTExtension::Reader::key(unsigned Key, unsigned Replaced) const {
  if (Key <= 1 || Key == ASTContext::UnkeyedTarget)
    return {Key, 0};
  const unsigned Canonical = 2;
  const unsigned Host = 1u << 1;
  if (Key == Canonical) {
    // The specialization serves this compilation's canonical device, which is
    // the file's, and every other target the file had none of its own for.
    if (FileToCompile[0]) {
      unsigned Own = Replaced & Host;
      for (unsigned C = Canonical + 1; C < CompileToFile.size() + 2; ++C)
        if (Replaced >> CompileToFile[C - 2] & 1)
          Own |= 1u << C;
      return {Canonical, Own};
    }
    // This compilation's only device is another; see mapTargets. If the file
    // had one for it, this one keeps serving the host, if it did.
    if (!(Replaced >> CompileToFile[0] & 1))
      return {Canonical, Replaced & Host};
    return {Replaced & Host ? DroppedKey : 1, 0};
  }
  unsigned C = variant(Key);
  if (C == Decl::TargetVariantRedundant)
    return {DroppedKey, 0};
  // This compilation's only device is one the file had its own for, which
  // never served the host.
  return {C, C == Canonical ? Host : 0};
}

MultiTargetASTExtension::Reader::Reader(MultiTargetASTExtension *Ext,
                                        ASTReader &R, ModuleFile &F,
                                        llvm::BitstreamCursor Stream,
                                        ArrayRef<unsigned> CompileToFile,
                                        unsigned NumFileTargets)
    : ModuleFileExtensionReader(Ext), R(R), F(F),
      CompileToFile(CompileToFile.begin(), CompileToFile.end()),
      FileToCompile(NumFileTargets, 0) {
  for (auto [I, V] : llvm::enumerate(CompileToFile))
    FileToCompile[V - 2] = 2 + I;
  std::vector<PrefixState> FileStates;
  SmallVector<uint64_t, 64> Record;
  while (true) {
    llvm::Expected<llvm::BitstreamEntry> Entry =
        Stream.advanceSkippingSubblocks();
    if (!Entry) {
      llvm::consumeError(Entry.takeError());
      break;
    }
    if (Entry->Kind != llvm::BitstreamEntry::Record)
      break;
    Record.clear();
    StringRef Blob;
    llvm::Expected<unsigned> Code = Stream.readRecord(Entry->ID, Record, &Blob);
    if (!Code) {
      llvm::consumeError(Code.takeError());
      break;
    }

    auto ForPairs = [&](auto Apply) {
      for (size_t I = 0; I + 1 < Record.size(); I += 2)
        Apply(States[globalID(Record[I])], Record[I + 1]);
    };
    auto ForEach = [&](unsigned Flag) {
      for (uint64_t Raw : Record)
        States[globalID(Raw)].Has |= Flag;
    };
    switch (*Code) {
    case DECL_TARGET_VARIANTS:
      ForPairs([](DeclState &S, uint64_t V) {
        S.Has |= DeclState::TargetVariant;
        S.Storage = V;
      });
      break;
    case INSTANTIATION_KEYS:
      ForPairs([](DeclState &S, uint64_t V) {
        S.Has |= DeclState::InstantiationKey;
        S.Key = V;
      });
      break;
    case INSTANCE_REPLACEMENTS:
      ForPairs([](DeclState &S, uint64_t V) {
        S.Has |= DeclState::InstanceReplacements;
        S.Replacements = V;
      });
      break;
    case CLASS_SPECIALIZATION_VARIANTS:
      ForPairs([](DeclState &S, uint64_t V) {
        S.Has |= DeclState::ClassSpecializationVariant;
        S.ClassVariant = V;
      });
      break;
    case FUNCTION_SPECIALIZATION_VARIANTS:
      ForPairs([](DeclState &S, uint64_t V) {
        S.Has |= DeclState::FunctionSpecializationVariant;
        S.FunctionVariant = V;
      });
      break;
    case SPECIALIZATION_PATTERN_VARIANTS:
      ForPairs([](DeclState &S, uint64_t V) {
        S.Has |= DeclState::SpecializationPatternVariant;
        S.PatternVariant = V;
      });
      break;
    case PARTIAL_SPECIALIZATION_VARIANTS:
      ForPairs([](DeclState &S, uint64_t V) {
        S.Has |= DeclState::PartialSpecializationVariant;
        S.PartialVariant = V;
      });
      break;
    case IMPLICIT_AUX_DEVICE_CONSTANTS:
      ForEach(DeclState::ImplicitAuxDeviceConstant);
      break;
    case TEMPLATES_WITH_UNKEYED_SPECIALIZATIONS:
      ForEach(DeclState::UnkeyedSpecializations);
      break;
    case WRAPPER_CLONES:
      for (size_t I = 0; I + 2 < Record.size(); I += 3)
        Clones.emplace_back(globalID(Record[I]), Record[I + 1],
                            globalID(Record[I + 2]));
      break;
    case PENDING_INSTANTIATION_VARIANTS:
      ForPairs([](DeclState &S, uint64_t V) {
        S.Has |= DeclState::PendingInstantiationVariant;
        S.PendingVariant = V;
      });
      break;
    case PENDING_INSTANTIATION_PROVENANCE:
      ForPairs([](DeclState &S, uint64_t V) {
        S.Has |= DeclState::PendingInstantiationProvenance;
        S.PendingProvenance = V;
      });
      break;
    case SPECIALIZATION_DECL_VARIANTS:
      ForPairs([](DeclState &S, uint64_t V) {
        S.Has |= DeclState::SpecializationDeclVariant;
        S.SpecDeclVariant = V;
      });
      break;
    case DIVERGENT_USERS:
      ForEach(DeclState::DivergentUser);
      break;
    case SPECIALIZATION_IDENTITIES:
      ForPairs([](DeclState &S, uint64_t V) {
        S.Has |= DeclState::SpecializationIdentity;
        S.Identity = V;
      });
      break;
    case KEYED_TEMPLATES:
      ForEach(DeclState::KeyedTemplate);
      break;
    case DUAL_SIDE_CALL_HOLDER:
      if (!Record.empty())
        DualSideCallHolder = globalID(Record[0]);
      break;
    case DUAL_SIDE_CALLS:
      for (size_t I = 0; I + 3 < Record.size(); I += 4)
        DualSideCalls.emplace_back(globalID(Record[I]), Record[I + 1],
                                   Record[I + 2], Record[I + 3]);
      break;
    case DECL_REDIRECTS:
      for (size_t I = 0; I + 3 < Record.size(); I += 4)
        Redirects.emplace_back(globalID(Record[I]), Record[I + 1],
                               Record[I + 2], globalID(Record[I + 3]));
      break;
    case ATTRIBUTE_CONSTANTS: {
      if (Record.size() < 5)
        break;
      unsigned BitWidth = Record[4];
      ArrayRef<uint64_t> Words = ArrayRef(Record).drop_front(5);
      AttributeConstants.emplace_back(
          globalID(Record[0]), Record[1], Record[2],
          llvm::APSInt(llvm::APInt(BitWidth, Words), Record[3]));
      break;
    }
    case PREFIX_HEADERS:
      Ext->LoadedPrefix.Headers = splitStrings(Blob);
      break;
    case PREFIX_MACROS:
    case PREFIX_INCLUDE_ONCE_FILES:
    case PREFIX_CONTROLLING_MACROS: {
      if (Record.empty())
        break;
      if (FileStates.size() <= Record[0])
        FileStates.resize(Record[0] + 1);
      PrefixState &State = FileStates[Record[0]];
      (*Code == PREFIX_MACROS               ? State.Macros
       : *Code == PREFIX_INCLUDE_ONCE_FILES ? State.IncludeOnceFiles
                                            : State.ControllingMacros) =
          splitStrings(Blob);
      break;
    }
    }
  }

  // Each of this compilation's aux targets resumes from its own state.
  Ext->LoadedPrefix.States.clear();
  for (unsigned V : CompileToFile)
    if (V - 2 < FileStates.size())
      Ext->LoadedPrefix.States.push_back(std::move(FileStates[V - 2]));
}

void MultiTargetASTExtension::Reader::readDecl(GlobalDeclID ID, Decl *D) {
  auto It = States.find(ID);
  if (It == States.end())
    return;
  const DeclState &State = It->second;
  ASTContext &Ctx = R.getContext();
  unsigned Storage = State.Storage;
  bool Dropped = false;
  if (State.Has & DeclState::InstantiationKey) {
    auto [Key, Replaced] = key(State.Key, State.Replacements);
    Ctx.TargetInstantiationKeys[D] = Key;
    if (Replaced)
      Ctx.TargetInstanceReplacements[D] = Replaced;
    Dropped |= Key == DroppedKey;
    if (State.Has & DeclState::ClassSpecializationVariant)
      Ctx.SpecializationKeyVariant[cast<ClassTemplateSpecializationDecl>(D)] =
          Key;
  } else if (State.Has & DeclState::ClassSpecializationVariant) {
    Ctx.SpecializationKeyVariant[cast<ClassTemplateSpecializationDecl>(D)] =
        instantiatingVariant(State.ClassVariant);
  }
  if (State.Has & DeclState::FunctionSpecializationVariant)
    Ctx.FunctionSpecializationVariant[cast<FunctionDecl>(D)] =
        instantiatingVariant(State.FunctionVariant);
  if (State.Has & DeclState::SpecializationPatternVariant)
    Ctx.TargetVariantForSpecialization[cast<ClassTemplateSpecializationDecl>(
        D)] = variant(State.PatternVariant);
  // Before the reader adds it to its template's partial specializations.
  if (State.Has & DeclState::PartialSpecializationVariant)
    Ctx.PartialSpecializationVariant[D] = variant(State.PartialVariant);
  if (State.Has & DeclState::ImplicitAuxDeviceConstant)
    Ctx.ImplicitAuxDeviceConstants.insert(cast<VarDecl>(D));
  if (State.Has & DeclState::UnkeyedSpecializations)
    Ctx.TemplatesWithUnkeyedSpecializations.insert(D);
  if (State.Has & DeclState::SpecializationIdentity) {
    unsigned Value = State.Identity >> 1;
    unsigned Identity = State.Identity & 1
                            ? instantiatingVariant(Value)
                            : key(Value, State.Replacements).first;
    Ctx.ImportedSpecializationIdentities[D] = Identity;
    Dropped |= Identity == DroppedKey;
  }
  // A specialization for no target this compilation has is emitted for none.
  if (Dropped)
    Storage = (Storage & 0x8) | Decl::TargetVariantRedundant;
  if ((State.Has & DeclState::TargetVariant) || Dropped)
    D->setTargetVariantStorage(Ctx, (Storage & 0x8) | variant(Storage & 0x7));
  if (State.Has & DeclState::KeyedTemplate)
    Ctx.ImportedKeyedTemplates.insert(D);
  if (State.Has & DeclState::NeedsSema) {
    if (S)
      applySemaState(D, State);
    else
      AwaitingSema.push_back({D, &State});
  }
}

void MultiTargetASTExtension::Reader::applySemaState(Decl *D,
                                                     const DeclState &State) {
  if (State.Has & DeclState::PendingInstantiationVariant)
    S->PendingInstantiationTargetVariant[cast<ValueDecl>(D)] =
        variant(State.PendingVariant);
  if (State.Has & DeclState::PendingInstantiationProvenance)
    S->PendingInstantiationProvenanceVariant[cast<ValueDecl>(D)] =
        variant(State.PendingProvenance);
  if (State.Has & DeclState::SpecializationDeclVariant)
    S->SpecializationDeclVariant[cast<FunctionDecl>(D)] =
        variant(State.SpecDeclVariant);
  if (State.Has & DeclState::DivergentUser)
    S->DivergentUsers.insert(cast<NamedDecl>(D));
}

void MultiTargetASTExtension::Reader::initializeSema(Sema &SemaRef) {
  S = &SemaRef;
  for (const auto &[D, State] : AwaitingSema)
    applySemaState(D, *State);
  AwaitingSema.clear();
}

void MultiTargetASTExtension::Reader::startTranslationUnit() {
  if (Started)
    return;
  Started = true;
  ASTContext &Ctx = R.getContext();

  // Expression-keyed state names statements in declarations' bodies, which
  // can only now be read. Code generation consults it for the declarations
  // passed to it next.
  llvm::DenseMap<GlobalDeclID, std::vector<const Stmt *>> Owned;
  auto OwnedStmt = [&](GlobalDeclID Owner, unsigned Ordinal) -> const Stmt * {
    auto [It, Inserted] = Owned.try_emplace(Owner);
    if (Inserted)
      OwnedStmtWalker([&](const Stmt *St, unsigned) {
        It->second.push_back(St);
      }).walk(R.GetDecl(Owner));
    return Ordinal < It->second.size() ? It->second[Ordinal] : nullptr;
  };
  ArrayRef<Stmt *> Holder;
  if (DualSideCallHolder.isValid())
    if (auto *FD =
            dyn_cast_or_null<FunctionDecl>(R.GetDecl(DualSideCallHolder)))
      if (auto *Body = dyn_cast_or_null<CompoundStmt>(FD->getBody()))
        Holder = Body->body();
  for (const auto &[Owner, Ordinal, Host, Device] : DualSideCalls) {
    const auto *Call = dyn_cast_or_null<CallExpr>(OwnedStmt(Owner, Ordinal));
    auto Side = [&](unsigned Index) -> const CallExpr * {
      if (!Index)
        return Call;
      return Index <= Holder.size() ? dyn_cast<CallExpr>(Holder[Index - 1])
                                    : nullptr;
    };
    if (Call)
      Ctx.CUDAAmbiguousCalls[Call] = {Side(Host), Side(Device)};
  }
  for (const auto &[Owner, Ordinal, FileVariant, Target] : Redirects) {
    unsigned Variant = variant(FileVariant);
    if (Variant == Decl::TargetVariantRedundant)
      continue;
    const auto *E = dyn_cast_or_null<Expr>(OwnedStmt(Owner, Ordinal));
    if (!E)
      continue;
    Decl *D = R.GetDecl(Target);
    if (auto *FD = dyn_cast_or_null<FunctionDecl>(D))
      Ctx.setTargetVariantCallee(E, Variant, FD);
    else if (auto *VD = dyn_cast_or_null<VarDecl>(D))
      Ctx.setTargetVariantValueDecl(E, Variant, VD);
  }

  for (const auto &[Function, Argument, FileVariant, Value] :
       AttributeConstants) {
    unsigned Variant = variant(FileVariant);
    if (Variant == Decl::TargetVariantRedundant)
      continue;
    auto *FD = dyn_cast_or_null<FunctionDecl>(R.GetDecl(Function));
    if (!FD)
      continue;
    SmallVector<const Expr *, 6> Arguments = launchBoundsArguments(FD);
    if (Argument < Arguments.size() && Arguments[Argument])
      Ctx.setTargetVariantConstantValue(Arguments[Argument], Variant, Value);
  }

  for (const auto &[Original, FileVariant, Clone] : Clones) {
    unsigned Variant = variant(FileVariant);
    if (Variant != Decl::TargetVariantRedundant)
      Ctx.TargetVariantWrapperClones[{cast<FunctionDecl>(R.GetDecl(Original)),
                                      Variant}] =
          cast<FunctionDecl>(R.GetDecl(Clone));
  }
}

std::unique_ptr<ModuleFileExtensionReader>
MultiTargetASTExtension::createExtensionReader(
    const ModuleFileExtensionMetadata &Metadata, ASTReader &R, ModuleFile &Mod,
    const llvm::BitstreamCursor &Stream) {
  std::optional<SmallVector<unsigned, 4>> CompileToFile = mapTargets(Metadata);
  if (!CompileToFile) {
    R.getDiags().Report(Mod.ImportLoc, diag::err_multi_target_ast_file_targets)
        << Mod.FileName << Metadata.UserInfo << describeTargets();
    R.getDiags().Report(Mod.ImportLoc,
                        diag::note_multi_target_ast_file_targets);
    return nullptr;
  }
  LoadedPrefix = Prefix();
  auto Result =
      std::make_unique<Reader>(this, R, Mod, Stream, *CompileToFile,
                               parseTargets(Metadata.UserInfo).size());
  HasLoadedPrefix = LoadedPrefix.States.size() == AuxTargets.size();
  return Result;
}
