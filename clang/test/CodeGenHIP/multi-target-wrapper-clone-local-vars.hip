// PROTOTYPE (Stage 5, Phase 5, Increment 14b): a plain, never-#if-widened
// function whose body declares LOCAL VARIABLES and is only classified
// target-divergent *transitively* (it calls a genuinely #if-widened leaf) --
// mirrors vec_dot_q1_0_q8_1 (ggml-cuda/vecdotq.cuh), whose loop-heavy body is
// the first real-corpus shape with local variables to go through
// getOrCreateTargetVariantWrapperClone (Increment 8). The only wrapper-clone
// shape exercised when Increment 8 first landed
// (ggml_cuda_fattn_mma_get_nbatch_fa) was a single-expression body with no
// local variables at all, so this gap was invisible until now.
//
// Reaching this shape requires the same two-hop indirection the real corpus
// uses: wrapper() is only ever referenced from inside a *function template*
// instantiation's local-variable initializer (kernel_tmpl's `local`), which is
// what routes it through DivergentValueCalleeRewriter/
// getOrCreateTargetVariantWrapperClone at all -- an ordinary direct call from
// a plain (non-template) function's body does not (that path only performs a
// sibling *lookup* under InstantiateDivergentCalleesInBody, it never
// synthesizes a wrapper clone).
//
// Root cause: getOrCreateTargetVariantWrapperClone clones each parameter
// explicitly into the new clone's DeclContext, but DivergentValueCalleeRewriter
// never cloned or reparented ordinary local variables declared via DeclStmt
// inside the body being cloned -- they kept the *original* function's
// DeclContext. Sema::tryCaptureVariable's fast path (VarDC == DC) then fails
// for every later reference to such a local inside the clone (whose CurContext
// is the clone, not the original), and the full capture-walk that follows has
// no FunctionScopeInfo for the clone to find, producing
// err_reference_to_local_in_enclosing_context. Fixed with a new
// TransformDefinition override that clones each local VarDecl into the
// clone's own DeclContext, mirroring the existing parameter-cloning loop.
//
// REQUIRES: amdgpu-registered-target

// Ground truth: each arch compiled completely on its own.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX900 %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1100 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX1100 %s

// One primary (gfx942) cc1 job, with AuxGen configured for gfx900 and gfx1100
// and the full merge/reparse pipeline on. Before the fix, this RUN line
// itself failed with err_reference_to_local_in_enclosing_context for every
// local variable in wrapper()'s cloned body, for every real target beyond the
// first.
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

#if defined(__gfx942__)
static __attribute__((device)) inline int leaf(int x) { return x + 100; }
#elif defined(__gfx900__)
static __attribute__((device)) inline int leaf(int x) { return x + 200; }
#elif defined(__gfx1100__)
static __attribute__((device)) inline int leaf(int x) { return x + 300; }
#endif

// Plain (never itself #if-widened) function with several local variables in
// its body, calling the target-divergent leaf() -- the shape that requires
// getOrCreateTargetVariantWrapperClone (no already-widened sibling of
// wrapper() itself exists to redirect to).
static __attribute__((device)) int wrapper(int x) {
  int a = x + 1;
  int b = 0;
#pragma unroll
  for (int i = 0; i < 2; ++i) {
    int c = leaf(a + i);
    b += c;
  }
  return b;
}

// wrapper() must be referenced from a function *template* instantiation's
// local-variable initializer -- that is the only path that routes it through
// DivergentValueCalleeRewriter (see file header).
template <typename T>
static __attribute__((device)) T kernel_tmpl(T x) {
  int local = wrapper((int)x);
  return (T)local;
}

__attribute__((global)) void kernel_like(int *out) {
  out[0] = kernel_tmpl<int>(10);
}

// GFX900: define{{.*}} @_ZL4leafi{{.*}}
// GFX900: add nsw i32 %{{.*}}, 200
// GFX1100: define{{.*}} @_ZL4leafi{{.*}}
// GFX1100: add nsw i32 %{{.*}}, 300
// GFX942: define{{.*}} @_ZL4leafi{{.*}}
// GFX942: add nsw i32 %{{.*}}, 100
