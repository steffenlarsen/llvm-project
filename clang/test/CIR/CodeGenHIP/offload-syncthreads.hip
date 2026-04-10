// Tests that __builtin_amdgcn_s_barrier (the implementation primitive for
// HIP's __syncthreads) is lowered to gpu.barrier in CIR.
//
// The CIR emitter intercepts the builtin in CIRGenBuiltinAMDGPU.cpp and emits
// gpu.barrier, which the GPU-to-ROCDL conversion later lowers to
// rocdl.s_barrier (the s_barrier machine instruction).
//
// The aux-triple / aux-target-cpu flags are required here so that the AMDGPU
// built-in is recognized by the parser even though the primary triple is x86.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx906 \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

// A kernel that synchronises threads using the raw AMDGPU barrier builtin.
__global__ void barrier_kernel(int *a, int *b) {
  int tid = threadIdx.x;
  a[tid] = tid;
  __builtin_amdgcn_s_barrier();
  b[tid] = a[0];
}

// CIR: offload.func @_Z14barrier_kernelPiS_
// CIR-SAME: exec_space = #offload.exec_space<global>
// CIR:   gpu.thread_id x
// CIR:   gpu.barrier
