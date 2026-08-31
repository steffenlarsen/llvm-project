// A class template whose body diverges per target has one specialization per
// target, keyed by the target ambient when it is named. A shared function
// template returning it keys its return type when its declaration is
// instantiated; its body, and the completion of the class it returns, must
// use that same target even when a device target's copy of a divergent
// caller is what first needs them, or the body returns another target's
// class. This follows rocThrust's temp_storage::make_linear_partition ("no
// viable conversion from returned value of type 'linear_partition<...>' to
// function return type 'linear_partition<...>'").
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a -nogpulib -nogpuinc \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only -Xclang -verify %s
// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpulib -nogpuinc -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -Xclang -verify %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc \
// RUN:   -mllvm -multi-target-codegen \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -merge-equivalent-variants
// RUN: FileCheck -check-prefix=HOST %s < %t.host.ll

// expected-no-diagnostics

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
#define HD __host__ __device__

template <class T> struct part {
#ifdef __HIP_DEVICE_COMPILE__
  T dev;
#endif
  T v;
  HD part(T t) : v(t) {}
};

template <class T> HD part<T> make_part(T t) { return part<T>(t); }

template <class T> HD int outer(T t) {
#ifdef __HIP_DEVICE_COMPILE__
  return make_part(t).v + 100;
#else
  return make_part(t).v;
#endif
}

using host_part = decltype(make_part(1));
__global__ void kernel(int *o) { o[0] = outer(1); }
int host_use() { return make_part(2).v; }

// HOST: %struct.part = type { i32 }
// HOST: define {{.*}} @_Z9make_partIiE4partIT_ES1_(
