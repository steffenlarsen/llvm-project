// Tests that kernel launches through a function pointer are recovered into
// cir.offload.kernel_launch, so the offload optimization passes (which all
// match on that op) can see them.
//
// Two transformations cooperate:
//   * OffloadSpecializeLaunchWrappers clones a wrapper that takes the kernel
//     as a parameter, once per kernel it is called with, making the callee a
//     constant inside the clone.
//   * OffloadDevirtualizeLaunches rewrites a constant-callee launch sequence
//     into cir.offload.kernel_launch.
//
// A launch whose callee cannot be pinned to exactly one kernel must be left
// alone: missing the optimization is fine, rewriting to the wrong kernel is
// not.
//
// The offload passes only ever clone kernels they can see a launch for, so a
// "$"-suffixed clone is the observable proof that a launch became visible.

// REQUIRES: amdgpu-registered-target
// RUN: %clang -x hip --offload-arch=gfx90a -fclangir -O2 -Wno-everything \
// RUN:   -I%S/Inputs -c -o %t.o %s
// RUN: rm -f %t.o.*
// RUN: llvm-objdump --offloading %t.o
// RUN: llvm-nm %t.o.*amdgcn* | FileCheck %s \
// RUN:   --implicit-check-not=ambiguousPf$ \
// RUN:   --implicit-check-not=runtime_choicePf$

#include "hip_runtime_minimal.h"

__global__ void traceable(float *o) { o[0] = 1; }
__global__ void ambiguous(float *o) { o[0] = 2; }
__global__ void runtime_choice(float *o) { o[0] = 3; }
__global__ void only_via_wrapper(float *o) { o[0] = 4; }

typedef void (*KFn)(float *);

// Single reaching definition -- devirtualized.
void launch_traceable(float *d) {
  KFn f;
  f = traceable;
  f<<<dim3(1), dim3(64), 0, 0>>>(d);
}

// Two stores into the slot.  Both name the same kernel, but the pass requires
// a single reaching definition and declines.
void launch_ambiguous(float *d, bool c) {
  KFn f;
  f = ambiguous;
  if (c)
    f = ambiguous;
  f<<<dim3(1), dim3(64), 0, 0>>>(d);
}

// Genuinely two candidates -- must decline.
void launch_runtime_choice(float *d, bool c) {
  KFn f = c ? runtime_choice : ambiguous;
  f<<<dim3(1), dim3(64), 0, 0>>>(d);
}

// The kernel is a parameter of the wrapper, so it is only constant after the
// wrapper is specialized for this call site.
template <typename K, typename... A>
static void wrapper(K k, dim3 g, dim3 b, A &&...a) {
  k<<<g, b, 0, 0>>>(static_cast<A &&>(a)...);
}

void launch_via_wrapper(float *d) {
  wrapper(only_via_wrapper, dim3(1), dim3(64), d);
}

// Both recovered launches gained a specialized clone.
// CHECK-DAG: _Z9traceablePf$max64
// CHECK-DAG: _Z16only_via_wrapperPf$ptrfacts

// The kernels themselves are all still emitted.  The two untraceable ones
// have no clone, which the --implicit-check-not options above enforce.
// CHECK-DAG: _Z9ambiguousPf
// CHECK-DAG: _Z14runtime_choicePf
