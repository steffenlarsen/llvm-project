// PROTOTYPE (Stage 5, Phase 5 Increment 5): a class template whose static
// data member is computed by calling a plain (non-template) function whose
// body -- not its signature -- is gated by raw target-CPU macros, mirroring
// llama.cpp's ggml_cuda_mma::tile<>: `static constexpr int ne = I_ * J_ /
// get_lane_count(); T x[ne];`. The real ggml_cuda_get_physical_warp_size()
// returns 32 for RDNA and 64 for CDNA, so `ne` (and therefore the array
// bound of the dependent member `x`) was silently baked in by whichever
// target's Sema pass instantiated the specialization first, with no
// diagnostic, for every other target.
//
// Also mirrors two subtler parts of the real bug, both confirmed empirically
// (2026-09-14) against the real ggml_cuda_mma::tile<> and NOT representative
// in this test's earlier revision:
//
// 1. tile<>'s primary template has zero members at all -- every real member
//    (including `ne`) lives only in a partial specialization -- so the
//    fix's detection predicate, ClassTemplateDecl::hasTargetTaggedValueMember(),
//    must walk each partial specialization's members, not just the primary's.
//
// 2. Direct replay of the real corpus (a real dual-arch fattn.cu build)
//    confirmed that ClassTemplateDecl::hasTargetTaggedPartialSpecialization()
//    -- the OLDER, pre-existing predicate -- is *already* true for the real
//    tile<>, independent of hasTargetTaggedValueMember(): tile<>'s
//    DATA_LAYOUT_I_MAJOR partial specialization is a single, unconditionally-
//    declared partial spec whose *body* is entirely gated by raw target-CPU
//    macros (`#if defined(AMD_MFMA_AVAILABLE) ... #elif defined(VOLTA_MMA_AVAILABLE)
//    ...`), so target widening produces multiple target-tagged redeclarations
//    of *that* partial spec -- which is what actually makes
//    hasTargetTaggedPartialSpecialization() true for the whole
//    ClassTemplateDecl, not the SCRAMBLED-style partial spec containing `ne`.
//    An earlier revision of this test gave tile_like exactly one, deliberately
//    untagged partial specialization so that only hasTargetTaggedValueMember()
//    could make Profile() fork -- but with no other partial spec ever tagged,
//    Context.getCurrentTargetVariant() never actually varies for a shared,
//    never-#if-dispatched caller's references, so no fork could ever happen
//    regardless of the new predicate's own correctness. A later attempt at
//    fixing this added a second partial specialization
//    (`tile_like<I_,J_,float>`) whose body was raw-macro-#if-gated, mirroring
//    DATA_LAYOUT_I_MAJOR, to make hasTargetTaggedPartialSpecialization()
//    naturally true -- but that construction instead triggered the
//    separately-tracked, still-unfixed "tile partial-spec reparse-
//    duplication" bug (see the plan's "Phase 5, newly-found blocker" note):
//    Design 1's reparse net duplicated `tile_like`'s ClassTemplateDecl itself,
//    producing "ambiguous partial specializations of 'tile_like<16, 16, int>'"
//    -- the exact reason the real tile<>'s `ne` bug is itself currently
//    masked in the real corpus. That construction was abandoned; this
//    revision instead gets real ambient variance the same way Increment 4's
//    own regression test does: `kernel_like` directly calls the divergent
//    plain callee, which alone is sufficient to sweep it into Design 1's
//    reparse net (already proven in
//    multi-target-shared-caller-divergent-plain-callee.hip) -- no second,
//    tagged partial specialization is needed, and none is added here.
//
// Root cause: ClassTemplateSpecializationDecl::Profile only folds
// Context.getCurrentTargetVariant() into its FoldingSet cache key when the
// template has a target-tagged partial specialization -- not when a single,
// untagged specialization's own static data member happens to call a
// target-divergent plain function. Fixed by adding
// ClassTemplateDecl::hasTargetTaggedValueMember() (ORed into the existing
// Profile condition) plus a rewrite hook,
// rewriteDivergentCalleesInMemberInitializer, called from
// Sema::InstantiateVariableInitializer right after SubstInitializer, which
// retargets the initializer's callee reference to the sibling FunctionDecl
// tagged for the specialization's own (now correctly forked) target.
//
// Second root cause, found once the above was in place: kernel_like itself
// is swept into Design 1's reparse net (it calls get_lane_count() directly),
// so mergeReparseAlternative compares its per-target reparse alternates --
// which are re-parses of the identical token range, so isInterchangeable's
// SameBody/bodyShape (a source-range/statement-shape hash) judged them
// equivalent and discarded every alternate but the primary's, including
// local variable `t`'s own VarDecl -- invisible to that shape-only check
// since it never resolves what a type or reference actually names. This
// permanently baked the primary's tile_like<16,16,int> specialization (and
// its 64-wide array bound) into every target's CodeGen. Fixed by
// isInterchangeable's new sameLocalDeclTypes check, which walks A/B's bodies
// in lockstep (also special-casing DeclStmt, whose children() yields a
// decl's initializer expression rather than the decl itself, so a
// no-initializer local like `t` is otherwise invisible to any Stmt-tree
// walk) and reports non-equivalence when a local variable's type resolves
// to a different per-target-forked specialization in each alternate --
// falling back to the same per-target-tagged, unmerged dispatch that
// already handles genuinely #if-divergent alternates.
//
// REQUIRES: amdgpu-registered-target

// Ground truth: each arch compiled completely on its own.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX900 %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1100 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX1100 %s

// One primary (gfx942) cc1 job, with AuxGen configured for gfx900 and gfx1100
// and the full merge/reparse pipeline on -- each aux target's own bitcode
// output must have `ne` (and the array it sizes) computed for its own
// target, not gfx942's (the primary's) or each other's.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.primary.ll %s \
// RUN:   -mllvm -multi-target-codegen \
// RUN:   -mllvm -multi-target-aux-target=2:gfx900: \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx900.bc \
// RUN:   -mllvm -multi-target-aux-target=3:gfx1100: \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx1100.bc \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -merge-equivalent-variants
// RUN: FileCheck -check-prefix=GFX942 %s < %t.primary.ll
// RUN: llvm-dis -o - %t.gfx900.bc | FileCheck -check-prefix=GFX900 %s
// RUN: llvm-dis -o - %t.gfx1100.bc | FileCheck -check-prefix=GFX1100 %s

// Body-divergent, plain (non-template) callee -- mirrors
// ggml_cuda_get_physical_warp_size's exact shape.
__attribute__((device)) static constexpr int get_lane_count() {
#if defined(__gfx942__)
  return 4;
#elif defined(__gfx900__)
  return 8;
#elif defined(__gfx1100__)
  return 16;
#endif
}

// Empty primary -- exactly ggml_cuda_mma::tile<>'s own shape: no members at
// all live here, they only live in the partial specialization below.
template <int I_, int J_, typename T> struct tile_like {};

// The one and only partial specialization, with a target-divergent value
// member: not itself target-#if-tagged (a single, unconditional definition),
// and tile_like has no other, sibling-tagged partial specialization either --
// hasTargetTaggedPartialSpecialization() is false for the whole
// ClassTemplateDecl. This fix's hasTargetTaggedValueMember() predicate is
// what makes Profile() fork this specialization, and
// rewriteDivergentCalleesInMemberInitializer is what then corrects the stale
// callee reference inside each fork's `ne` initializer.
template <int I_, int J_>
struct tile_like<I_, J_, int> {
  static constexpr int ne = I_ * J_ / get_lane_count();
  int x[ne];
};

// A single, never-#if-gated entry point -- no arch dispatch anywhere in this
// TU, exactly like the real mul_mat_q-style caller shape this bug depends on.
// Also directly calls the divergent plain callee (mirroring Increment 4's
// already-proven shared-caller shape) so this function is genuinely swept
// into Design 1's reparse net, giving Context.getCurrentTargetVariant() real
// per-target variance at the point it references tile_like<16,16,int>.
__attribute__((global)) void kernel_like(int *out) {
  tile_like<16, 16, int> t;
  t.x[0] = get_lane_count();
  out[0] = tile_like<16, 16, int>::ne;
}

// Array bound of `x` (sized by `ne`) must also be forked per target, not
// just the value stored into out[0] -- the array-bound-caching bug this test
// also guards against: kernel_like's own reparse alternates were judged
// "interchangeable" and each alternate's local variable `t` (and therefore
// its own, correctly-forked tile_like<16,16,int>) was discarded, leaving
// every target's CodeGen convert the same (primary's) 64-wide specialization.
// GFX900: %struct.tile_like{{[.0-9]*}} = type { [32 x i32] }
// GFX900: store i32 32
// GFX1100: %struct.tile_like{{[.0-9]*}} = type { [16 x i32] }
// GFX1100: store i32 16
// GFX942: %struct.tile_like{{[.0-9]*}} = type { [64 x i32] }
// GFX942: store i32 64
