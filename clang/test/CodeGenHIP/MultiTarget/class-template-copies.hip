// A class template copied per target because a member is defined differently
// per target. Every target's code names one copy of the template, so host and
// device code agree on its specializations, and a host function analysed for
// a device (for a lambda it might contain) still names the host's. Each
// target's specialization instantiates that target's definition; the host's
// is defined once even where device code reached it through another copy.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942 -O2" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: FileCheck --check-prefix=HOST %s < %t.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=DEV %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefix=DEV %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

template <class T> struct cfg {
  int x;
  constexpr __host__ __device__ cfg(int x) : x(x) {}
  constexpr __host__ __device__ int side() const {
#if defined(__HIP_DEVICE_COMPILE__)
    return x + 100;
#else
    return x + 200;
#endif
  }
};

__host__ __device__ int by_value(cfg<int> c) { return c.side(); }
__device__ int device_only() { return cfg<int>(3).side(); }
int h() { return cfg<int>(5).side() * 1000 + by_value(cfg<int>(6)); }
__global__ void k(int *o, cfg<int> c) {
  o[0] = by_value(c);
  o[1] = by_value(cfg<int>(o[1]));
  o[2] = device_only();
}

// HOST-LABEL: define {{.*}} @_Z1hv(
// HOST: ret i32 205206

// DEV-LABEL: define {{.*}} @_Z1kPi3cfgIiE(
// DEV: add nsw i32 %c.coerce, 100
// DEV: add nsw i32 %{{.*}}, 100
// DEV: store i32 103,
