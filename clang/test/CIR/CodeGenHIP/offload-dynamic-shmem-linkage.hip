// Verify that dynamic shared memory globals emitted by the CIR offload
// pipeline have external linkage.  Internal linkage on a zero-sized [0 x i8]
// global accessed through addrspacecast ConstantExprs can cause LLVM's
// optimizer to incorrectly forward stores across aliased shared memory
// accesses when loops are fully unrolled with constant-propagated indices.
//
// We dump the device LLVM IR just before amdgpu-lower-module-lds and check
// that the dynamic shared memory sentinel has "external" linkage.
//
// REQUIRES: amdgpu-registered-target
// RUN: %clang -x hip --offload-arch=gfx90a -fclangir -O1 -Wno-everything \
// RUN:   -I%S/Inputs -Xclang -mllvm -Xclang -print-before=amdgpu-lower-module-lds \
// RUN:   -c -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s

#include "hip_runtime_minimal.h"

__global__ void kern(float *out) {
  extern __shared__ float s[];
  s[threadIdx.x] = 1.0f;
  __syncthreads();
  out[threadIdx.x] = s[threadIdx.x];
}

int main() {
  float *d;
  hipMalloc((void**)&d, 256 * sizeof(float));
  kern<<<1, 64, 256>>>(d);
  hipFree(d);
  return 0;
}

// The dynamic shared memory global must have external linkage (not internal).
// CHECK: @__dynamic_shmem_{{[0-9]+}} = external
// CHECK-SAME: addrspace(3) global [0 x i8]
