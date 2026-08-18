// End-to-end covered-guard elimination for HIP: real CIRGen output for host and
// device TUs, merged into a cir.offload.container by the merge tool, which runs
// the offload-container passes by default.
//
// The .cir test drives the pass on hand-written IR; this one checks that the
// shapes it matches -- the ceiling-division grid on the host and the index
// arithmetic on the device -- are the shapes the compiler actually emits.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -target-sdk-version=9.2 -fhip-new-launch-api -x hip -I %S/../../CodeGenCUDA/Inputs \
// RUN:   -emit-cir %s -o %t-host.cir
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 -target-sdk-version=9.2 \
// RUN:   -fcuda-is-device -fhip-new-launch-api -x hip -I %S/../../CodeGenCUDA/Inputs \
// RUN:   -emit-cir %s -o %t-dev.cir
// RUN: cir-offload-merge -combine -input=%t-host.cir -input=%t-dev.cir \
// RUN:   -targets=host-x86_64-unknown-linux-gnu,hip-amdgcn-amd-amdhsa--gfx900 \
// RUN:   -output=%t-combined.cir
// RUN: FileCheck %s --input-file=%t-combined.cir

#include "cuda.h"

// Mirrors what __clang_hip_runtime_wrapper.h declares, reduced to the parts the
// pass reads. Spelling them here keeps the test runnable under cc1 with
// -nostdsysteminc; the mangled names are what the recogniser matches.
struct __hip_builtin_blockIdx_t {
  __device__ static unsigned __get_x();
};
struct __hip_builtin_blockDim_t {
  __device__ static unsigned __get_x();
};
struct __hip_builtin_threadIdx_t {
  __device__ static unsigned __get_x();
};

__global__ void fill(int *out, int n) {
  int i = __hip_builtin_blockIdx_t::__get_x() *
              __hip_builtin_blockDim_t::__get_x() +
          __hip_builtin_threadIdx_t::__get_x();
  if (i >= n)
    return;
  out[i] = i;
}

// The launch picks between two copies on n % 256. The slow side is itself a
// copy, because the later specialising passes get to the surviving launch too;
// what matters here is that it is not the guard-free one.
// CHECK-LABEL: cir.func{{.*}} @_Z4hostPii
// CHECK:         cir.rem
// CHECK:         cir.cmp eq
// CHECK:         cir.call @_Z19__device_stub__fillPii.guards
// CHECK:         cir.call @_Z19__device_stub__fillPii.blockshape

// The guard survives everywhere except in the copy the divisibility test
// dispatches to.
// CHECK:      cir.func{{.*}} @_Z4fillPii(
// CHECK:        cir.cmp ge
// CHECK:      cir.func{{.*}} @_Z4fillPii.blockshape(
// CHECK:        cir.cmp ge
// CHECK:      cir.func{{.*}} @_Z4fillPii.guards(
// CHECK-NOT:    cir.cmp ge
// CHECK:        cir.const #false

void host(int *out, int n) { fill<<<(n + 255) / 256, 256>>>(out, n); }
