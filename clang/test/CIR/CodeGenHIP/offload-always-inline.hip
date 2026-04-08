// Tests that always_inline survives the trip to device code.
//
// HIP spells __forceinline__ as `inline __attribute__((always_inline))`, and
// device code leans on it heavily.  The attribute is inherent on
// cir.func, so the places that build a cir.offload.func by copying discardable
// attributes used to drop it silently.  A dropped always_inline is not a
// diagnostic -- the function merely becomes an ordinary inline candidate, and
// the cost model is then free to decline it once a second caller appears,
// which on AMDGPU surfaces as a register-pressure cliff rather than an error.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/../CodeGenCUDA/Inputs/ -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "cuda.h"

// The device copy must carry the attribute, not just the host one.
// CIR-DAG: cir.offload.func @_Z6helperf({{.*}}) -> !cir.float always_inline

__device__ inline __attribute__((always_inline))
float helper(float x) { return x * 2.0f; }

// Two callers: with always_inline the inliner has no choice to make.  A single
// caller would inline anyway via the last-call-to-static bonus, so it would not
// distinguish a preserved attribute from a dropped one.
__global__ void k1(float *p) { p[0] = helper(p[0]); }
__global__ void k2(float *p) { p[0] = helper(p[0]) + 1.0f; }

// A noinline device function keeps its marking too -- the mapping is not
// hardcoded to one enumerator.
// CIR-DAG: cir.offload.func @_Z7helper2f({{.*}}) -> !cir.float no_inline
__device__ __attribute__((noinline))
float helper2(float x) { return x * 3.0f; }

__global__ void k3(float *p) { p[0] = helper2(p[0]); }
