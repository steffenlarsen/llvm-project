//===- DeclFingerprint.h - Order-independent AST comparison -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Prints one normalised line per declaration, so two compilations' ASTs can be
// compared as sets.
//
// Every increment of the combined-frontend work so far could be checked by
// byte-identical output against an unmodified compiler. Once one AST carries
// declarations for several targets that stops being possible: the combined AST
// is legitimately different from either target's. What has to hold instead is
// that the declarations belonging to a target are the ones that target's own
// compilation produces.
//
// Comparing -ast-dump output does not answer that. It embeds pointers and
// source locations, and it is ordered -- and a combined parse visits a
// divergent region once per target, so declaration order legitimately differs.
// A fingerprint carries only identity and shape, and is compared sorted.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_DECLFINGERPRINT_H
#define LLVM_CLANG_AST_DECLFINGERPRINT_H

#include "clang/Basic/LLVM.h"
#include "clang/Support/Compiler.h"
#include "llvm/Support/CommandLine.h"
#include <string>

namespace clang {

class ASTContext;
class Decl;

/// PROTOTYPE: see DeclFingerprint.cpp. With -merge-equivalent-variants, also
/// hide the copy that turned out to match the original.
LLVM_ABI extern llvm::cl::opt<bool> HideRedundantVariants;

/// Print a fingerprint line for every declaration in \p Ctx's translation unit.
///
/// \param Variant Which target's declarations to print. Declarations marked as
/// belonging to every target are always printed; those marked for a specific
/// target are printed only when it matches. 0 prints everything, which is what
/// an ordinary single-target compilation contains.
///
/// The variant itself is deliberately *not* part of a line: a separate
/// per-target compilation marks nothing, so including it would make every line
/// differ. It selects which lines appear, not what they say.
LLVM_ABI void printDeclFingerprints(raw_ostream &OS, ASTContext &Ctx,
                                    unsigned Variant);

/// Report how many target-tagged declarations are structurally the same.
///
/// Everything parsed inside a divergent region inherits that region's target,
/// so a declaration identical in every arm is tagged anyway. Measured on
/// ggml-cuda, 23 declarations are tagged for every one that actually differs,
/// and each tagged declaration makes every shared user of it a candidate for
/// re-parsing. This checks the cheap signal (fingerprint equality) against the
/// real one (structural equivalence) before anything is merged on the strength
/// of it.
LLVM_ABI void reportVariantEquivalence(raw_ostream &OS, ASTContext &Ctx);

/// Un-claim target-tagged declarations whose targets agree.
///
/// Everything parsed inside a divergent region, and everything a divergent
/// user's re-parse touches, is tagged for a target whether or not it depends on
/// one. Measured on ggml-cuda, 996 of 1,011 pairs are structurally equivalent
/// and the 15 that are not are the same 15 on every translation unit --
/// `numeric_limits<long double>`, `hardware_destructive_interference_size`, and
/// two locals in HIP intrinsics. Reverting the primary's copy to "every target"
/// keeps it, and everything instantiated from it, visible to the others.
///
/// Must run before pending instantiations: an instantiation inherits its
/// pattern's target, so a template left claimed takes all of its
/// specializations with it -- 6,232 locals of `launch_bin_bcast_pack` alone.
///
/// \returns how many declarations were un-claimed.
LLVM_ABI unsigned mergeEquivalentVariants(ASTContext &Ctx);

/// Merge one of Design 1's (-reparse-divergent-users) reparse alternates into
/// \p Primary, immediately, before any later declaration in the file can see
/// \p Alternative independently.
///
/// \p Primary need not be the reparse's host origin: two real device archs
/// that both re-parse the same shared declaration into the same shape (e.g.
/// both instantiate a macro-gated member identically, differently from the
/// host's ambient) produce two independently-parsed, never-compared-to-each-
/// other copies if each is only ever checked against the host's genuinely
/// different alternate. The caller is expected to compare each newly-returned
/// alternate against every alternate recorded so far for the current reparse,
/// host origin included, not just the host.
///
/// mergeEquivalentVariants only reconciles duplicates at end-of-translation-
/// unit, which is too late here: by then every caller in the file has already
/// done its own name lookup against whichever of the primary's and the
/// alternate's ClassTemplateDecl (or other redeclarable template/tag) it saw
/// first, and a caller that resolved to the now-redundant alternate keeps that
/// binding regardless of a later merge. \p Primary and \p Alternative are
/// positionally-paired re-parses of the identical token range (Design 1 always
/// re-parses one declaration's tokens whole), so they have the same shape by
/// construction.
///
/// Unlike mergeEquivalentVariants' kind-based gating (which excludes
/// ClassTemplateDecl/CXXRecordDecl from unconditional hiding, since one of
/// their already-materialized specializations can only be reached through the
/// template's own FoldingSet and could go missing if some other caller
/// instantiated against the hidden copy first), hiding \p Alternative here is
/// always safe regardless of its kind: this runs before any caller anywhere in
/// the file has been parsed, so nothing has instantiated against either copy
/// yet.
///
/// \p Primary's own tag is not reverted to shared here -- only appended to
/// \p PendingUnclaim. A reparse batch injects every alternate's tokens as one
/// continuous stream (ParseAST.cpp), so alternate 2 returns, is merged, and
/// only then does alternate 3 begin parsing; reverting \p Primary eagerly
/// during alternate 2's merge would make it look shared (tag 0) by the time
/// alternate 3's own live redefinition check runs against it, producing a
/// spurious redefinition diagnostic alternate 2 itself never triggered. The
/// caller must apply every batch's accumulated \p PendingUnclaim (via
/// applyPendingReparseUnclaims) only once the batch's last alternate has been
/// merged.
LLVM_ABI void mergeReparseAlternative(ASTContext &Ctx, ArrayRef<Decl *> Primary,
                                      ArrayRef<Decl *> Alternative,
                                      unsigned Variant,
                                      SmallVectorImpl<Decl *> &PendingUnclaim);

/// Revert each pending declaration's own tag back to shared, once a Design 1
/// reparse batch's merges (mergeReparseAlternative, above) have all run.
/// \p PendingUnclaim may contain the same declaration more than once (a
/// primary compared equivalent against more than one alternate in the same
/// batch); duplicates are reconciled once each.
LLVM_ABI void applyPendingReparseUnclaims(ArrayRef<Decl *> PendingUnclaim);

/// One arm of a genuine #if/#elif-widened alternative, as recorded by the
/// caller once that arm's declaration-sequence loop has fully finished
/// parsing and Sema-checking it (see mergeWidenedAlternatives' doc comment
/// for why every arm must be complete before any of them are merged).
struct WidenedArm {
  ArrayRef<Decl *> Decls;
  unsigned Variant;
};

/// Reconcile every arm of a genuine #if/#elif-widened alternative against
/// every other arm, all at once, promoting an entity to shared (variant 0)
/// only when every arm that declares it agrees.
///
/// \p Arms need not include the host: an entity the host's own branch never
/// declares (e.g. the host takes a branch of the chain none of the real
/// device targets do) is simply absent from \p Arms, and every other arm's
/// declaration of it is left untouched -- nothing to collide with.
///
/// Every arm's own declarations must already be fully parsed and
/// Sema-checked before this runs: merging arm K as soon as it closes (rather
/// than once every arm in the episode has closed) would mark an earlier
/// arm's matching declarations "shared" -- visible to every target,
/// including one whose own pass hasn't started yet -- so that later arm's
/// own declaration of the same entity would collide with the
/// prematurely-shared copy the moment it's declared.
///
/// An entity is promoted to shared only when *every* arm that declares it
/// structurally agrees with every other -- not merely the first arm checked
/// against it. Two arms agreeing with each other is not, by itself, enough:
/// a third arm may have taken a genuinely different branch of the same
/// #if/#elif chain (e.g. two AMDGPU targets that both take the CDNA #elif
/// while the host's own #else branch legitimately differs, or conversely the
/// host and one device target happen to agree while a second, not-yet-
/// checked device target diverges). Promoting to shared on partial agreement
/// would make the survivor visible under the disagreeing arm's ambient too,
/// alongside that arm's own untouched declaration of the same name -- two
/// live candidates, "ambiguous call"/"ambiguous reference". When agreement
/// isn't unanimous, every arm's declaration of that entity is left at its
/// own, individually correct, single-target tag -- not merged at all, even
/// pairwise.
///
/// Two arms can each contain more than one top-level declaration with no
/// guarantee they agree on how many (e.g. one arm declares an extra
/// typedef); when a given arm has no matching declaration for some other
/// arm's entity, that entity is treated the same as outright disagreement
/// (conservatively left unmerged) rather than assumed absent-and-ignorable.
LLVM_ABI void mergeWidenedAlternatives(ASTContext &Ctx,
                                       ArrayRef<WidenedArm> Arms);

} // namespace clang

#endif
