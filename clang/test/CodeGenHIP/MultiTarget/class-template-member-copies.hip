// Members of a class template parsed again per target: a static data member
// and a member typedef. A member function's body names one target's copy;
// each target's instantiation uses its own. The static data member crashed
// ("Unable to find instantiation of declaration"), the typedef was the host's
// on every device.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx942 -O2" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: FileCheck -check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=GFX90A %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=GFX942 %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
#define __host__ __attribute__((host))

#if defined(__gfx942__)
constexpr int W = 42;
#else
constexpr int W = 90;
#endif

template <class T> struct Q {
  static constexpr int n = W;
  __host__ __device__ int f() { return n; }
};

template <class T> struct R {
  using A = T[W];
  __host__ __device__ int f() { return sizeof(A); }
  __host__ __device__ int g() {
    A a;
    return sizeof(a);
  }
};

// GFX90A-LABEL: define {{.*}} @_Z1kPi(
// GFX90A: store i32 90,
// GFX90A: store i32 360,
// GFX90A: store i32 360,
// GFX942-LABEL: define {{.*}} @_Z1kPi(
// GFX942: store i32 42,
// GFX942: store i32 168,
// GFX942: store i32 168,
__global__ void k(int *p) {
  p[0] = Q<int>().f();
  p[1] = R<int>().f();
  p[2] = R<int>().g();
}

// HOST-LABEL: define {{.*}} @_Z1hv(
// HOST: ret i32 810
int h() { return Q<int>().f() + R<int>().f() + R<int>().g(); }
