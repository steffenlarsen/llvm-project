// REQUIRES: amdgpu-registered-target
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -x hip \
// RUN: -std=c++11 -fclangir -fcuda-is-device -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -x hip \
// RUN: -std=c++11 -fcuda-is-device -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

// Classic CodeGen states uniform-work-group-size on every kernel; ClangIR
// emitted none. Without it the backend must assume the last block may be
// partial and so cannot treat blockDim as uniform across the grid, which turns
// each read into a vector load from the dispatch packet plus a full wait.

#define __global__ __attribute__((global))

__global__ void k(int *o) { o[0] = 1; }

// LLVM: define{{.*}} amdgpu_kernel void @_Z1kPi
// LLVM-SAME: #[[ATTR:[0-9]+]]
// LLVM: attributes #[[ATTR]] = {{.*}}"uniform-work-group-size"
