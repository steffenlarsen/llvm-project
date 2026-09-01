// REQUIRES: amdgpu-registered-target
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -x hip \
// RUN: -std=c++11 -fclangir -fcuda-is-device -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s

// An 'extern __shared__' array is emitted with its element type's natural
// alignment, so a half2 array asks for 4. That alignment is not a promise about
// an external allocator -- it is a request the backend honours when it lays LDS
// out -- so asking for less than the widest single access throws away access
// width. Raised to 16 for the dynamic case only, which is what the
// incomplete array type identifies; a static __shared__ array already has a
// sensible alignment and raising it would waste LDS.

#define __shared__ __attribute__((shared))
#define __global__ __attribute__((global))

struct half2 { _Float16 x, y; };

extern __shared__ half2 dyn[];

__global__ void k(int i, _Float16 v) { dyn[i].x = v; }

// The dynamic array is an incomplete type and gets the raised alignment.
// CHECK: @dyn = external addrspace(3) global [0 x %struct.half2], align 16
