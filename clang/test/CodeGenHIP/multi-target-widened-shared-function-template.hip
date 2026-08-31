// A 3-arm target-`#if` region whose arms are individually brace-unbalanced
// (each arm opens an `if (...) {` and the matching `}` only appears once,
// after the shared `#endif`) immediately followed by two or more
// byte-identical bare file-scope function templates. Widening this region
// must not sweep the following unrelated templates into the divergent span
// and duplicate/redefine them.
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
// adjacent to the divergent region above.
template <typename T> static __forceinline__ __device__ T identity(T x) { return x; }
template <typename T> static __forceinline__ __device__ T identity2(T x) { return x; }

__global__ void kernel_like(int *out) {
  out[0] = identity<int>(divergent_value());
  out[1] = identity2<int>(7);
}
