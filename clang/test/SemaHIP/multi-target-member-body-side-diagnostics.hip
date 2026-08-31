// A host device member whose body differs by target keeps one body per target
// in a class shared by every target. A call that is only right for one side
// is only checked for the targets whose body makes it.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -emit-llvm -o /dev/null -verify %s \
// RUN:   "-multi-target-aux-invocation=-std=c++17 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc

// expected-no-diagnostics

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#ifdef __HIP_DEVICE_COMPILE__
#define IF_ON_HOST(X)
#define IF_ON_DEVICE(X) X
#else
#define IF_ON_HOST(X) X
#define IF_ON_DEVICE(X)
#endif

int host_only(int x);
__device__ int device_only(int x);

struct Tracker {
  int v;
  __host__ __device__ ~Tracker() { IF_ON_HOST(host_only(v);) }
  __host__ __device__ int get() const {
    IF_ON_DEVICE(return device_only(v);)
    IF_ON_HOST(return host_only(v);)
  }
};

__global__ void kernel(int *o) { o[0] = Tracker{1}.get(); }
int host_use() { return Tracker{2}.get(); }
