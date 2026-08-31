// Device code right after two adjacent sets of per-target alternatives (two
// structs whose layout differs per target) is analysed per target: the
// re-parse may start after the second set as after the first.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942 -O2" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=GFX90A %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefix=GFX942 %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
struct A {
#if defined(__gfx942__)
  int a[4];
#else
  int a[2];
#endif
};
struct B {
#if defined(__HIP_DEVICE_COMPILE__)
  int b[8];
#else
  int b[1];
#endif
};
__device__ int f() { return sizeof(A) * 100 + sizeof(B); }
__global__ void k(int *o) { o[0] = f(); }

// GFX90A-LABEL: define {{.*}} @_Z1kPi(
// GFX90A: store i32 832,
// GFX942-LABEL: define {{.*}} @_Z1kPi(
// GFX942: store i32 1632,
