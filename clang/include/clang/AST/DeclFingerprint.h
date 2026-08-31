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
// A combined AST carries declarations for several targets, so it is
// legitimately different from any single target's own AST -- comparing it
// byte-for-byte against an unmodified compiler's output does not work. What
// has to hold instead is that the declarations belonging to a target are the
// ones that target's own compilation would produce.
//
// Comparing -ast-dump output does not answer that: it embeds pointers and
// source locations and is ordered, while a combined parse visits a divergent
// region once per target and so produces declarations in a different order.
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

/// With -merge-equivalent-variants, also hide the copy that turned out to
/// match the original.
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
/// so a declaration identical in every arm is tagged anyway, and each tagged
/// declaration makes every shared user of it a candidate for re-parsing. This
/// checks the cheap signal (fingerprint equality) against the real one
/// (structural equivalence) before anything is merged on the strength of it.
LLVM_ABI void reportVariantEquivalence(raw_ostream &OS, ASTContext &Ctx);

/// Un-claim target-tagged declarations whose targets agree.
///
/// Everything parsed inside a divergent region, and everything a divergent
/// user's re-parse touches, is tagged for a target whether or not it depends
/// on one. Reverting the primary's copy to "every target" keeps it, and
/// everything instantiated from it, visible to the others.
///
/// Must run before pending instantiations: an instantiation inherits its
/// pattern's target, so a template left claimed takes all of its
/// specializations with it.
///
/// \returns how many declarations were un-claimed.
LLVM_ABI unsigned mergeEquivalentVariants(ASTContext &Ctx);

/// Merge one of the -reparse-divergent-users reparse alternates into
/// \p Primary, immediately, before any later declaration in the file can see
/// \p Alternative independently.
///
/// \p Primary need not be the reparse's host origin: two real device archs
/// that both re-parse the same shared declaration into the same shape
/// produce two independently-parsed copies that are never compared to each
/// other if each is only checked against the host's alternate. The caller
/// must compare each newly-returned alternate against every alternate
/// recorded so far for the current reparse, host origin included.
///
/// mergeEquivalentVariants only reconciles duplicates at end-of-translation-
/// unit, which is too late here: by then every caller in the file has already
/// resolved name lookup against whichever of the primary's and the
/// alternate's ClassTemplateDecl (or other redeclarable template/tag) it saw
/// first, and keeps that binding regardless of a later merge. \p Primary and
/// \p Alternative are positionally-paired re-parses of the identical token
/// range, so they have the same shape by construction.
///
/// Unlike mergeEquivalentVariants' kind-based gating (which excludes
/// ClassTemplateDecl/CXXRecordDecl from unconditional hiding, since an
/// already-materialized specialization reached only through the template's
/// own FoldingSet could go missing), hiding \p Alternative here is always
/// safe regardless of its kind: this runs before any caller in the file has
/// been parsed, so nothing has instantiated against either copy yet.
///
/// \p Primary's own tag is not reverted to shared here -- only appended to
/// \p PendingUnclaim. A reparse batch injects every alternate's tokens as one
/// continuous stream, so reverting \p Primary eagerly during one alternate's
/// merge would make it look shared by the time the next alternate's own live
/// redefinition check runs against it, producing a spurious diagnostic. The
/// caller must apply every batch's accumulated \p PendingUnclaim (via
/// applyPendingReparseUnclaims) only once the batch's last alternate has been
/// merged.
LLVM_ABI void mergeReparseAlternative(ASTContext &Ctx, ArrayRef<Decl *> Primary,
                                      ArrayRef<Decl *> Alternative,
                                      unsigned Variant,
                                      SmallVectorImpl<Decl *> &PendingUnclaim);

/// Revert each pending declaration's own tag back to shared, once a reparse
/// batch's merges (mergeReparseAlternative, above) have all run.
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
/// declares is simply absent from \p Arms, and every other arm's declaration
/// of it is left untouched.
///
/// Every arm's own declarations must already be fully parsed and
/// Sema-checked before this runs: merging arm K as soon as it closes would
/// mark an earlier arm's matching declarations shared -- visible to a target
/// whose own pass hasn't started yet -- so that target's later declaration
/// of the same entity would collide with the prematurely-shared copy.
///
/// An entity is promoted to shared only when *every* arm that declares it
/// agrees with every other, not merely the first arm checked: a third arm
/// may have taken a genuinely different branch of the same #if/#elif chain
/// even when the first two agree. Promoting on partial agreement would make
/// the survivor visible under the disagreeing arm's ambient too, alongside
/// that arm's own untouched declaration of the same name -- an ambiguous
/// call/reference. When agreement isn't unanimous, every arm's declaration
/// is left at its own single-target tag, not merged at all.
///
/// Two arms can contain a different number of top-level declarations (e.g.
/// one arm declares an extra typedef); a missing match for some other arm's
/// entity is treated as disagreement, not as absent-and-ignorable.
LLVM_ABI void mergeWidenedAlternatives(ASTContext &Ctx,
                                       ArrayRef<WidenedArm> Arms);

} // namespace clang

#endif
