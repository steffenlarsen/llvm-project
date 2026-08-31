// A class whose targets' definitions differ only in a member's CUDA target,
// so its copies are one family and then merge into one shared definition.
// Shared code assigning through the class finds that definition's members for
// every target.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O1 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -O1 -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -O1 -target-cpu gfx942" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -O1 -target-cpu gfx1100" \
// RUN:   -multi-target-aux-output=4:%t.gfx1100.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck %s
// RUN: llvm-dis %t.gfx1100.bc -o - | FileCheck %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__gfx942__)
#define CONV __host__ __device__
#else
#define CONV __host__
#endif

struct base {
  unsigned char x;
  __host__ __device__ base(float f) : x(f) {}
  base() = default;
};

struct num : base {
  using base::base;
  CONV operator int() const { return x; }
  __host__ __device__ num &operator=(const num &o) {
    x = o.x + 1;
    return *this;
  }
};

__host__ __device__ num &add(num &a, num b) {
  return a = num(float(a.x) + float(b.x));
}

__global__ void k(int *o) {
  num a(1.0f), b(2.0f);
  add(a, b);
  o[0] = a.x;
}

// CHECK-LABEL: define {{.*}}amdgpu_kernel void @_Z1kPi(
// CHECK: store i32 4,
