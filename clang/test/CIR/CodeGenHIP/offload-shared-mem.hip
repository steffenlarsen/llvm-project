// Tests that HIP dynamic shared memory (`extern __shared__ T arr[]`) is
// lowered to offload.shared_mem_alloc in CIR.
//
// The CIR emitter intercepts `extern __shared__` variable declarations in
// emitVarDecl() and emits offload.shared_mem_alloc with size=0 (dynamic).
// The resulting pointer is stored in LocalDeclMap so that subsequent
// DeclRefExpr nodes resolve to this SSA value rather than a global symbol.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx906 \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

// --- Dynamic shared memory (float) -----------------------------------------

__global__ void sum_kernel(const float *in, float *out, int n) {
  extern __shared__ float sdata[];
  int tid = threadIdx.x;
  sdata[tid] = (tid < n) ? in[tid] : 0.0f;
  __syncthreads();
  if (tid == 0) {
    float s = 0.0f;
    for (int i = 0; i < n; ++i)
      s += sdata[i];
    *out = s;
  }
}

// CIR-LABEL: offload.func @_Z10sum_kernelPKfPfi
// CIR: offload.shared_mem_alloc %c0 ->

// --- Dynamic shared memory (int) --------------------------------------------

__global__ void int_kernel(int *buf) {
  extern __shared__ int smem[];
  smem[threadIdx.x] = buf[threadIdx.x];
  __syncthreads();
  buf[threadIdx.x] = smem[threadIdx.x] + 1;
}

// CIR-LABEL: offload.func @_Z10int_kernelPi
// CIR: offload.shared_mem_alloc %c0 ->
