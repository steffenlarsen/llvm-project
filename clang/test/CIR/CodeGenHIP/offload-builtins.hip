// Tests that HIP built-in dimension variables (threadIdx, blockIdx, blockDim,
// gridDim) are lowered to gpu.* ops rather than regular memory loads.
//
// The CIR emitter intercepts member accesses on these intrinsic variables in
// ScalarExprEmitter::VisitMemberExpr and emits gpu.thread_id / gpu.block_id /
// gpu.block_dim / gpu.grid_dim ops with the appropriate dimension operand.
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

// CIR: offload.func @_Z12index_kernelPi
// CIR-SAME: exec_space = #offload.exec_space<global>
// CIR:   cir.gpu_builtin_var thread_idx x
// CIR:   cir.gpu_builtin_var thread_idx y
// CIR:   cir.gpu_builtin_var thread_idx z
// CIR:   cir.gpu_builtin_var block_idx  x
// CIR:   cir.gpu_builtin_var block_idx  y
// CIR:   cir.gpu_builtin_var block_idx  z
// CIR:   cir.gpu_builtin_var block_dim  x
// CIR:   cir.gpu_builtin_var block_dim  y
// CIR:   cir.gpu_builtin_var block_dim  z
// CIR:   cir.gpu_builtin_var grid_dim   x
// CIR:   cir.gpu_builtin_var grid_dim   y
// CIR:   cir.gpu_builtin_var grid_dim   z
