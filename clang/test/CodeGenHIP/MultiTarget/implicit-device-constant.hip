// A const __device__ variable with a constant initializer is implicitly
// __constant__ in a device compilation, so it lives in the constant address
// space there (ggml's kvalues_iq4nl table). The host-primary Sema does not add
// the attribute; each device target's CodeGen must still place it there.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o %t.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

static const __device__ signed char table[4] = {-127, -10, 10, 127};

__global__ void k(int *o, int i) { o[0] = table[i & 3]; }

// CHECK: @_ZL5table = internal addrspace(4) constant [4 x i8]
