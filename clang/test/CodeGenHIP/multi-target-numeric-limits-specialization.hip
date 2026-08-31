// Runs a real explicit class template specialization from libstdc++
// (std::numeric_limits<long>) through the widening/reparse pipeline
// alongside an unrelated arch-#if. std::numeric_limits<long>::min() must
// resolve to the real specialization's LONG_MIN, not to the primary
// (unspecialized) numeric_limits<_Tp> template's generic `{ return _Tp(); }`
// pattern, under either target ambient.
//
// The static_assert makes this a compile-time observable check: if the
// wrong (0-returning) body is ever used to constant-evaluate min(), this
// fails to compile for at least one of the two target archs below.
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
__device__ int marker() { return 1; }
#else
__device__ int marker() { return 2; }
#endif

#include <limits>

static_assert(std::numeric_limits<long>::min() < 0, "min should be negative");

__global__ void k(long *out) { out[0] = std::numeric_limits<long>::min(); }
