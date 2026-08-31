// A class template's static member is initialized by a call to a function
// each device architecture has its own copy of. Its copies are grouped by what
// the function's copies declare, so one copy of the member stands for
// architectures whose function copies differ only in their bodies. Each
// target's code generation evaluates the member with its own copy of the
// function.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx1030 -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1030 -O2" \
// RUN:   -multi-target-aux-output=2:%t.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=3:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942 -O2" \
// RUN:   -multi-target-aux-output=4:%t.gfx942.bc
// RUN: llvm-dis -o - %t.gfx1030.bc | FileCheck -check-prefix=GFX1030 %s
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=GFX90A %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=GFX942 %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__gfx942__)
__host__ __device__ constexpr int wave() { return 2; }
#elif defined(__gfx90a__)
__host__ __device__ constexpr int wave() { return 1; }
#else
__host__ __device__ constexpr int wave() { return 3; }
#endif

template <class T> struct Box {
  static constexpr int W = wave();
  __device__ int get(T x) { return x * 10 + W; }
};

// GFX1030-LABEL: define {{.*}} @_Z1kPi(
// GFX1030: {{add nsw|or disjoint}} i32 %{{.*}}, 3
// GFX90A-LABEL: define {{.*}} @_Z1kPi(
// GFX90A: {{add nsw|or disjoint}} i32 %{{.*}}, 1
// GFX942-LABEL: define {{.*}} @_Z1kPi(
// GFX942: {{add nsw|or disjoint}} i32 %{{.*}}, 2
__global__ void k(int *p) {
  Box<int> b;
  p[0] = b.get(p[1]);
}
