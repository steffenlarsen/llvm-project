//===--- TargetAlternationReconciler.h - Widening/reparse merge state ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The full definition of clang::TargetAlternationReconciler, forward-declared
// (opaque) in clang/Parse/Parser.h so its internal types don't leak into that
// header's public interface. This internal-only header exists purely so both
// ParseAST.cpp (which defines the class's methods and drives it) and
// Parser.cpp (which merely holds a std::unique_ptr<TargetAlternationReconciler>
// member on Parser, and so needs the complete type for the implicit
// constructor/destructor to be instantiable) can see the same definition --
// it is not installed and not part of any public API.
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
#include <vector>

namespace clang {

class ASTConsumer;

// The widening/reparse reconciliation state and logic that used to live
// inline in ParseAST()'s own loop, generalized so every nested
// declaration-sequence loop (namespace bodies, extern "C" blocks, class
// bodies) can feed the same bookkeeping. Parsing is single-threaded recursive
// descent: a namespace or class body is fully drained by its own nested loop
// before control returns to whichever loop is waiting for it, so "all of one
// declaration's alternates return consecutively before the parser reaches the
// next real declaration in the file" already describes one continuous
// timeline across every loop, not one local to ParseAST()'s own -- hence a
// single, TU-wide shared instance, owned by the Parser that ParseAST()
// constructs, rather than one per loop.
class TargetAlternationReconciler {
public:
  TargetAlternationReconciler(
      Parser &P, Sema &S, ASTConsumer *Consumer, ArrayRef<Token> MergedStream,
      const llvm::DenseMap<unsigned, unsigned> &LocIndex,
      const std::vector<bool> &Boundaries, bool Reparse)
      : P(P), S(S), Consumer(Consumer), MergedStream(MergedStream),
        LocIndex(LocIndex), Boundaries(Boundaries), Reparse(Reparse) {}

  /// Feed a just-parsed declaration group through the reconciler. \p
  /// NotifyConsumer gates every Consumer->HandleTopLevelDecl call this batch
  /// of bookkeeping would otherwise make -- false for a nested loop, whose
  /// declarations are emitted as part of their enclosing declaration once
  /// that reaches the outer loop, not individually here. Returns false if the
  /// consumer asked to stop.
  bool Observe(Parser::DeclGroupPtrTy &ADecl, bool NotifyConsumer);

  /// Flush any reconciliation left pending when a declaration-sequence loop
  /// ends (a no-op if nothing is pending). \p FinalFlush additionally reports
  /// the accumulated Design 1 reparse-candidate statistics; only ParseAST()'s
  /// own final call (once the whole TU has been parsed) should pass true, so
  /// the report is not printed once per nested loop. Returns false if the
  /// consumer asked to stop.
  bool Finish(bool FinalFlush = false);

private:
  /// Reconciles every arm recorded in WideningGroups against every other,
  /// pairwise, then clears WideningGroups. Called once a widened episode is
  /// known to have closed -- either because the next declaration's ambient
  /// dropped back to shared/host (the ordinary case, from Observe), or
  /// because the TU ended while an episode was still open (the Finish
  /// safety net). Every arm's own declarations must already be fully parsed
  /// and Sema-checked before this runs: merging arm K as soon as it closes
  /// (rather than once every arm in the episode has closed) would mark an
  /// earlier arm's matching declarations "shared" -- visible to every
  /// target, including one whose own pass hasn't started yet -- so that
  /// later arm's own declaration of the same entity would collide with the
  /// prematurely-shared copy the moment it's declared.
  void reconcileWideningGroups();
  /// Calls Consumer->HandleTopLevelDecl(ADecl.get()), gated on NotifyConsumer
  /// and on ADecl being non-null (mirroring every original call site's own
  /// `if (ADecl && ...)` guard). Returns true (i.e. "keep going") whenever
  /// the call is skipped, exactly as the original sites did by falling
  /// through without a Consumer call at all.
  bool notify(Parser::DeclGroupPtrTy &ADecl, bool NotifyConsumer) {
    if (!NotifyConsumer || !ADecl)
      return true;
    return Consumer->HandleTopLevelDecl(ADecl.get());
  }

  // See PendingPrimaryEmit's doc comment below for why this is deferred, and
  // PendingPrimaryEmitNotify's for why the flush must not use whatever
  // NotifyConsumer value happens to be in effect for the call that triggers
  // it.
  bool FlushPendingPrimaryEmit() {
    if (!PendingPrimaryEmit)
      return true;
    ASTContext::TargetScope PrimaryAmbient(S.getASTContext(), 1);
    bool ShouldContinue = notify(PendingPrimaryEmit, PendingPrimaryEmitNotify);
    PendingPrimaryEmit = nullptr;
    return ShouldContinue;
  }

  Parser &P;
  Sema &S;
  ASTConsumer *Consumer;
  ArrayRef<Token> MergedStream;
  const llvm::DenseMap<unsigned, unsigned> &LocIndex;
  const std::vector<bool> &Boundaries;
  bool Reparse;

  // Re-entered token buffers must outlive the parse that reads them.
  std::vector<std::unique_ptr<std::vector<Token>>> Reparsed;
  unsigned ReparseCandidates = 0, ReparseRejected = 0, ReparseTokens = 0,
           ReparseWidest = 0;

  // The declarations a returning reparse alternate should be compared
  // against, and how many alternates are still expected back before the next
  // reparse can start: all of one declaration's alternates are injected as a
  // single continuous token stream, so they always return through Observe
  // consecutively, before the parser ever reaches the next real top-level
  // declaration in the file. Merging as each alternate returns -- rather than
  // waiting for mergeEquivalentVariants' end-of-TU pass -- reconciles a
  // duplicate before any later caller in the file can bind to it
  // independently.
  //
  // Two real device archs can both fail to diverge from each other while
  // both diverging from the host (e.g. both instantiate a shared, macro-
  // gated member the same way, differently from host's ambient): comparing
  // every returning alternate only against the host origin would leave those
  // two alternates as two independent, never-reconciled copies. So, exactly
  // like WideningGroups below, every alternate seen so far for the current
  // reparse (the host origin included) is kept as its own comparison
  // candidate, and a newly-returned alternate is checked against all of
  // them, not just the host origin.
  struct ReparseGroup {
    SmallVector<Decl *, 4> Decls;
    unsigned Variant;
  };
  SmallVector<ReparseGroup, 4> PendingReparseGroups;
  unsigned PendingMergeAlternatesRemaining = 0;
  // Set when the current reparse batch's origin was swept in solely (or
  // partly) because it touched an ambient-target predicate builtin (e.g.
  // __builtin_amdgcn_processor_is), as opposed to a divergent entity
  // reference. That builtin's whole reason for existing is that it answers
  // differently per target for byte-identical source tokens -- so unlike
  // every other reparse trigger, AST-shape equivalence between this batch's
  // alternates is never grounds to merge them, no matter what
  // isInterchangeable concludes: merging would destroy the very
  // target-distinctness the reparse was triggered to preserve. See
  // multi-target-builtin-split.hip.
  bool PendingBatchAmbientBuiltinTriggered = false;
  // The primary's own HandleTopLevelDecl notification, held back until its
  // reparse batch's merges complete: notifying the Consumer as soon as this
  // declaration is parsed (the ordinary timing, used for every other
  // declaration) lets CodeGen bake in this target's resolution of a
  // divergent value -- e.g. a class-template static data member computed
  // from a target-tagged callee -- before the other targets' alternates are
  // even parsed, let alone merged into ASTContext's per-target value table
  // (see recordDivergentValueRefs/TargetVariantValueDecls). Flushed at each
  // point below where a batch is known to be complete or abandoned.
  Parser::DeclGroupPtrTy PendingPrimaryEmit;
  // The NotifyConsumer value in effect when PendingPrimaryEmit was captured,
  // not whatever value happens to be passed to whichever later Observe/
  // Finish call triggers its flush: a batch never spans a change in calling-
  // loop context (it always resolves before the parser advances past the
  // declaration that started it), but the flush call site can differ from
  // the batch-origin call site (e.g. a namespace loop's last member starts a
  // batch that flushes only once the namespace loop's Finish() runs).
  bool PendingPrimaryEmitNotify = false;
  // Every primary this batch's merges found equivalent to some alternate,
  // recorded but not yet unclaimed: reverting a primary's tag as soon as its
  // first alternate merges would make it look shared by the time a *later*
  // alternate's own live redefinition check runs against it, since every
  // alternate in the batch returns through Observe consecutively before the
  // parser reaches the next real top-level declaration. See
  // mergeReparseAlternative's doc comment. Applied and cleared once this
  // batch's last alternate has been merged, below.
  SmallVector<Decl *, 4> PendingUnclaim;

  // Genuine #if/#elif widening (as opposed to Design 1's reparse just
  // above): unlike a reparse alternate, an ordinary widened arm's alternate
  // count isn't known in advance and an arm can itself span more than one
  // top-level declaration, so its declarations are accumulated across calls
  // and recorded into WideningGroups as soon as the next alternation marker
  // closes the arm (detected as a change in P.getTargetAlternative()).
  // Reconciliation itself (mergeWidenedAlternatives) is deferred past that
  // point, to whenever the whole episode closes -- see
  // reconcileWideningGroups's doc comment for why.
  //
  // Two real device archs can both fail to diverge from each other while
  // both diverging from the host (e.g. two AMDGPU targets that both take
  // the same #elif branch): comparing every closed arm only against the
  // host would leave those two arms' declarations as two independent,
  // never-reconciled copies. So every arm closed so far for the current
  // top-level declaration -- host included -- is kept as its own comparison
  // candidate in WideningGroups, and a newly-closed arm is checked against
  // all of them, not just the first (host) one.
  struct WideningGroup {
    SmallVector<Decl *, 4> Decls;
    unsigned Variant;
  };
  SmallVector<WideningGroup, 4> WideningGroups;
  SmallVector<Decl *, 4> WideningCurDecls;
  unsigned WideningCurVariant = 0;
  unsigned LastTargetAlternative = 0;
};

} // namespace clang

#endif // LLVM_CLANG_LIB_PARSE_TARGETALTERNATIONRECONCILER_H
