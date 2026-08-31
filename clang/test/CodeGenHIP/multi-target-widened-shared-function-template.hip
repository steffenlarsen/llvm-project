// PROTOTYPE (Stage 5, Phase 5 blocker #2): a 3-arm target-`#if` region whose
// arms are individually brace-unbalanced (each arm opens an `if (...) {` and
// the matching `}` only appears once, after the shared `#endif`) immediately
// followed by two or more byte-identical bare file-scope function templates.
//
// This mirrors the shape that caused the `texture_fetch_functions.h` widening
// duplication found via the ctxraii fix -- ROCm's
// hip/amd_detail/texture_fetch_functions.h itself has zero arch-#if regions,
// but sits textually downstream of a divergent, brace-unbalanced region
// earlier in the include chain; `redefinition of 'tex2DLod'` (and 9 other
// tex*/unsafeAtomicAdd templates) fired when the widened span swept the
// bare templates along with it. Root cause: the old mergeWidenedAlternative
// eagerly promoted/unclaimed a redecl on the first pairwise arm match,
// premature for 3+ real targets (2-of-3 agreement before the 3rd arm was
// even seen); the current TargetAlternationReconciler design requires
// unanimous agreement across every real target before any unclaim.
//
// This no longer reproduces on the current tree (confirmed both via this
// synthetic repro and via a real dual-arch `mtcg` build of the actual
// originally-failing file, ggml-cuda/fattn.cu, which now compiles and links
// cleanly end to end) -- fixed as a side effect of the accumulated
// Increment 6-13 widening/reparse work, not a new code change. This test
// locks that in as a regression test. See
// [[texture-fetch-functions-widening-duplication]].
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -merge-equivalent-variants \
// RUN:   %s

#include <hip/hip_runtime.h>

#if defined(__gfx90a__)
__device__ int divergent_value() { if (true) {
#elif defined(__gfx942__)
__device__ int divergent_value() { if (true) {
#else
__device__ int divergent_value() { if (true) {
#endif
    return 1;
  }
}

// Shared, non-divergent, bare file-scope function templates textually
// adjacent to the divergent region above -- mirrors
// texture_fetch_functions.h's tex1Dfetch/tex2DLod (bare, unwrapped-in-any-
// namespace function templates) being swept into a still-open widened span.
template <typename T> static __forceinline__ __device__ T identity(T x) { return x; }
template <typename T> static __forceinline__ __device__ T identity2(T x) { return x; }

__global__ void kernel_like(int *out) {
  out[0] = identity<int>(divergent_value());
  out[1] = identity2<int>(7);
}
