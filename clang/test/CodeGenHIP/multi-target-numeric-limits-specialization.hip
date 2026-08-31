// PROTOTYPE (Stage 5, Phase 5, Increment 6 verification): a real explicit
// class template specialization from libstdc++ (std::numeric_limits<long>)
// read through the widening/reparse pipeline alongside an unrelated
// arch-#if. This was flagged as an open regression while verifying
// Increment 6 -- std::numeric_limits<long>::min() was observed to silently
// resolve to the *primary* (unspecialized) numeric_limits<_Tp> template's
// generic `{ return _Tp(); }` pattern (i.e. 0) instead of the real
// specialization's LONG_MIN under one target ambient, with no diagnostic.
//
// A static_assert makes this a compile-time observable regression rather
// than one requiring a codegen/IR inspection: if the wrong (0-returning)
// body is ever used to constant-evaluate min(), this fails to compile for
// at least one of the two real target archs below.
//
// This no longer reproduces on the current tree (confirmed empirically
// across several structurally distinct repro shapes -- arch-#if before and
// after the <limits> include, a duplicate #include <limits> in both #if
// arms, and a namespace-std reopening) -- most likely fixed as a side
// effect of Increment 6's hasLiveDuplicateInPattern fix and/or Increment
// 7's eager-instantiation fix, both of which touch the same
// duplicate-declaration/stale-specialization machinery this bug lived in.
// This test locks that in as a regression test.
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
