//===--- TargetAlternationReconciler.h - Widening/reparse merge state ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The definition of clang::TargetAlternationReconciler, which Parser.h only
// forward-declares so that its internal types stay out of that header's public
// interface. This header is internal and not installed.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_PARSE_TARGETALTERNATIONRECONCILER_H
#define LLVM_CLANG_LIB_PARSE_TARGETALTERNATIONRECONCILER_H

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/Parse/Parser.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include <memory>
#include <optional>
#include <vector>

namespace clang {

class ASTConsumer;

// The widening and reparse reconciliation state and logic, shared by every
// nested declaration-sequence loop (namespace bodies, extern "C" blocks, class
// bodies). Parsing is recursive descent, so a nested body is fully drained
// before control returns to the loop waiting for it. All of one declaration's
// alternates therefore return consecutively on one timeline across every loop,
// hence a single instance per translation unit rather than one per loop.
class TargetAlternationReconciler {
public:
  TargetAlternationReconciler(
      Parser &P, Sema &S, ASTConsumer *Consumer, ArrayRef<Token> MergedStream,
      const llvm::DenseMap<unsigned, unsigned> &LocIndex,
      const std::vector<bool> &Boundaries, bool Reparse)
      : P(P), S(S), Consumer(Consumer), MergedStream(MergedStream),
        LocIndex(LocIndex), Boundaries(Boundaries), Reparse(Reparse) {}

  /// Feed a just-parsed declaration group through the reconciler. \p
  /// NotifyConsumer gates every consumer notification this call would make.
  /// It is false for a nested loop, whose declarations reach the consumer as
  /// part of their enclosing declaration. Returns false if the consumer asked
  /// to stop.
  bool Observe(Parser::DeclGroupPtrTy &ADecl, bool NotifyConsumer);

  /// Flush any reconciliation left pending when a declaration-sequence loop
  /// ends. \p FinalFlush additionally reports the accumulated reparse-candidate
  /// statistics. It is only set once the whole translation unit has been
  /// parsed, so the report is not printed once per nested loop. Returns false
  /// if the consumer asked to stop.
  bool Finish(bool FinalFlush = false);

  /// A reparse alternate ended, or was \p Skipped, an earlier one standing
  /// for its target. Ends the batch after the last one expected.
  void NoteReparseAlternateEnd(bool Skipped);

private:
  /// Reconciles every arm recorded in WideningGroups against every other,
  /// pairwise, then clears WideningGroups. Runs once a widened episode is
  /// known to have closed, either because the next declaration's ambient
  /// dropped back to shared or host, or because the translation unit ended
  /// with an episode still open. Every arm's declarations must already be
  /// parsed and checked. Merging an arm as soon as it closes would mark an
  /// earlier arm's matching declarations shared, visible to every target
  /// including one whose own pass has not started, so that later arm's own
  /// declaration of the same entity would collide with the prematurely shared
  /// copy.
  void reconcileWideningGroups();
  /// Ends the current reparse batch. Merges its alternates into the primary
  /// if \p Complete (every alternate has returned), then gives each
  /// alternate, and finally the primary, its held-back HandleTopLevelDecl
  /// notification. An incomplete (abandoned) batch is not merged, since an
  /// alternate that never returned cannot be shown to agree. Returns false
  /// if the consumer asked to stop.
  bool FlushPendingReparseBatch(bool Complete);
  /// Reports where \p Decls, parsed for \p Variant, mean something else than
  /// \p StandIn's, parsed from the same tokens for \p StandInVariant.
  void verifyStandIn(ArrayRef<Decl *> StandIn, unsigned StandInVariant,
                     ArrayRef<Decl *> Decls, unsigned Variant);
  /// Notifies the consumer of \p ADecl unless \p NotifyConsumer is false or
  /// \p ADecl is null. Returns true, to keep going, whenever it is skipped.
  bool notify(Parser::DeclGroupPtrTy &ADecl, bool NotifyConsumer) {
    if (!NotifyConsumer || !ADecl)
      return true;
    return Consumer->HandleTopLevelDecl(ADecl.get());
  }

  /// When identical alternatives are parsed once, the declarations of a
  /// divergent region's alternatives, which reach the consumer once the
  /// region is parsed and they are tagged with every target they stand for.
  SmallVector<DeclGroupRef, 4> PendingAlternativeEmit;
  bool FlushPendingAlternativeEmit() {
    SmallVector<DeclGroupRef, 4> Pending = std::move(PendingAlternativeEmit);
    PendingAlternativeEmit.clear();
    for (DeclGroupRef DG : Pending)
      if (!Consumer->HandleTopLevelDecl(DG))
        return false;
    return true;
  }

  // The flush uses the NotifyConsumer value captured with the declaration,
  // not the one of the call that triggers it.
  bool FlushPendingPrimaryEmit() {
    if (!PendingPrimaryEmit)
      return true;
    ASTContext::TargetScope PrimaryAmbient(S.getASTContext(), 1);
    bool ShouldContinue = notify(PendingPrimaryEmit, PendingPrimaryEmitNotify);
    PendingPrimaryEmit = nullptr;
    return ShouldContinue;
  }

  /// Class templates a field of which differs between targets. Only
  /// re-parsing the template gives each target its own layout.
  llvm::SmallPtrSet<const Decl *, 4> TemplatesWithTargetFields;

  Parser &P;
  Sema &S;
  ASTConsumer *Consumer;
  ArrayRef<Token> MergedStream;
  const llvm::DenseMap<unsigned, unsigned> &LocIndex;
  const std::vector<bool> &Boundaries;
  bool Reparse;

  // Re-entered token buffers must outlive the parse that reads them.
  std::vector<std::unique_ptr<std::vector<Token>>> Reparsed;
  unsigned ReparseCandidates = 0, ReparseRejected = 0, ReparseTokens = 0;

  // The declarations of the current reparse batch, the primary first and then
  // each alternate as it returns, and how many alternates are still expected
  // back before the next reparse can start. All of one declaration's
  // alternates are injected as a single continuous token stream, so they
  // return consecutively, before the parser reaches the next top-level
  // declaration. Merging once the last alternate returns, rather than at the
  // end of the translation unit, reconciles a duplicate before any later code
  // can bind to it independently.
  //
  // An alternate's own consumer notification (Emit) is held back until then
  // too. Whether it is hidden depends on every other alternate in the batch
  // agreeing with the primary, and code generation only skips a redundant
  // decl, or finds a merged constant, if the merge has happened by the time
  // the alternate reaches it.
  struct ReparseGroup {
    SmallVector<Decl *, 4> Decls;
    unsigned Variant;
    Parser::DeclGroupPtrTy Emit;
    bool EmitNotify = false;
    /// With -verify-identical-alternatives, the earlier alternative that
    /// would have stood for this one.
    unsigned StandIn = 0;
  };
  SmallVector<ReparseGroup, 4> PendingReparseGroups;
  unsigned PendingMergeAlternatesRemaining = 0;
  /// The declaration the alternate being parsed returned last.
  std::optional<ReparseGroup> CurrentReparseAlternate;
  /// The consumer asked to stop while a batch ended between declarations.
  bool ConsumerStopped = false;
  // Set when the current reparse batch's origin was swept in at least partly
  // because it touched an ambient-target predicate builtin, as opposed to a
  // divergent entity reference. Such a builtin answers differently per target
  // for identical source tokens, so AST-shape equivalence between this batch's
  // alternates is never grounds to merge them. Merging would destroy the very
  // target-distinctness the reparse preserves. The same holds for a batch
  // triggered by what the declaration consumed.
  bool PendingBatchAmbientBuiltinTriggered = false;
  // Set when the current batch re-parses a device-only declaration for the
  // host too, whose re-parse replaces the first parse as the primary's.
  bool PendingBatchHostView = false;
  // The primary's own consumer notification, held back until its reparse
  // batch's merges complete. Notifying as soon as this declaration is parsed
  // would let code generation bake in this target's resolution of a divergent
  // value, e.g. a class template static data member computed from a
  // target-tagged callee, before the other targets' alternates are parsed.
  Parser::DeclGroupPtrTy PendingPrimaryEmit;
  // The NotifyConsumer value in effect when PendingPrimaryEmit was captured.
  // A batch never spans a change in the calling loop's context, but the flush
  // can come from a different call, e.g. a namespace loop's last member starts
  // a batch that is flushed only when that loop finishes.
  bool PendingPrimaryEmitNotify = false;

  // Genuine #if/#elif widening, as opposed to reparsing. Unlike a reparse
  // alternate, a widened arm's alternate count is not known in advance and an
  // arm can span more than one top-level declaration. Its declarations are
  // accumulated across calls and recorded into WideningGroups once the next
  // alternation marker closes the arm, seen as a change in the target
  // alternative. Reconciliation is deferred until the whole episode closes.
  //
  // Two device targets can agree with each other while both diverge from the
  // host, e.g. when both take the same #elif branch. Comparing each closed arm
  // only against the host would leave their declarations as two independent,
  // never-reconciled copies. So every arm closed so far for the current
  // top-level declaration, host included, is kept in WideningGroups, and a
  // newly closed arm is checked against all of them.
  struct WideningGroup {
    SmallVector<Decl *, 4> Decls;
    unsigned Variant;
    /// With -verify-identical-alternatives, the earlier alternative that
    /// would have stood for this one.
    unsigned StandIn = 0;
    /// The divergent region the declarations were parsed in.
    unsigned Region = 0;
  };
  SmallVector<WideningGroup, 4> WideningGroups;
  SmallVector<Decl *, 4> WideningCurDecls;
  unsigned WideningCurVariant = 0;
  unsigned WideningCurStandIn = 0;
  unsigned WideningCurRegion = 0;
  unsigned LastTargetAlternative = 0;
};

} // namespace clang

#endif // LLVM_CLANG_LIB_PARSE_TARGETALTERNATIONRECONCILER_H
