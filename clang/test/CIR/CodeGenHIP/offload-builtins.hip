// Tests that HIP built-in dimension variables (threadIdx, blockIdx, blockDim,
// gridDim) are lowered to cir.offload.* ops rather than regular memory loads.
//
// The CIR emitter intercepts member accesses on these intrinsic variables in
// ScalarExprEmitter::VisitMemberExpr and emits cir.offload.thread_id /
// cir.offload.block_id / cir.offload.block_dim / cir.offload.grid_dim ops
// with the appropriate dimension operand.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

// A kernel that reads all six dimension variables.
__global__ void index_kernel(int *out) {
  int tx = threadIdx.x;
  int ty = threadIdx.y;
  int tz = threadIdx.z;
  int bx = blockIdx.x;
  int by = blockIdx.y;
  int bz = blockIdx.z;
  int bdx = blockDim.x;
  int bdy = blockDim.y;
  int bdz = blockDim.z;
  int gdx = gridDim.x;
  int gdy = gridDim.y;
  int gdz = gridDim.z;
  out[0] = tx + ty + tz + bx + by + bz + bdx + bdy + bdz + gdx + gdy + gdz;
}

// CIR: cir.offload.func @_Z12index_kernelPi
// CIR-SAME: kernel
// CIR:   cir.offload.thread_id <x>
// CIR:   cir.offload.thread_id <y>
// CIR:   cir.offload.thread_id <z>
// CIR:   cir.offload.block_id <x>
// CIR:   cir.offload.block_id <y>
// CIR:   cir.offload.block_id <z>
// CIR:   cir.offload.block_dim <x>
// CIR:   cir.offload.block_dim <y>
// CIR:   cir.offload.block_dim <z>
// CIR:   cir.offload.grid_dim <x>
// CIR:   cir.offload.grid_dim <y>
// CIR:   cir.offload.grid_dim <z>
