// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -fcuda-is-device -x hip \
// RUN:   -emit-llvm -o - %s | FileCheck %s

// A dynamically sized `extern __shared__` array has incomplete type, so its
// declared alignment would otherwise only reflect its element type -- 4
// bytes for `half2`. Target::getDynamicSharedMemoryAlignment() raises it to
// the widest single LDS access (128 bits) instead.
#include "Inputs/cuda.h"

typedef _Float16 half2 __attribute__((ext_vector_type(2)));

// CHECK-DAG: @tile = external addrspace(3) global [0 x <2 x half>], align 16
extern __shared__ half2 tile[];

// A statically sized `__shared__` array already has a complete type, so its
// alignment is unaffected by the fix and must stay align 4 before and after.
// CHECK-DAG: @_ZZ10use_sharedvE10statictile = internal addrspace(3) global [8 x <2 x half>] {{.*}}, align 4
__global__ void use_shared() {
  __shared__ half2 statictile[8];
  tile[0] = statictile[0];
}
