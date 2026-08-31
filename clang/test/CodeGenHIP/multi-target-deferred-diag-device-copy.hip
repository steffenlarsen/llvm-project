// A shared host+device caller first instantiated while a device target is
// ambient binds that target's copy of a divergent callee, whose device arm
// calls a device-only function; the host is redirected to its own copy. The
// host's deferred-diagnostic walk must follow the host's copy, not report the
// device target's call to the device-only function as a host error. This
// follows rocThrust's cross_system_copy_n ("reference to __device__ function
// 'seq' in __host__ __device__ function"). A device-only reference the host
// really reaches is still diagnosed.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a -nogpulib -nogpuinc \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only -Xclang -verify %s
// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpulib -nogpuinc -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -Xclang -verify %s
// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpulib -nogpuinc -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -Xclang -verify=genuine -DGENUINE %s

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
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=DEV %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=DEV %s

// expected-no-diagnostics

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
#define HD __host__ __device__

__device__ int device_only() { return 5; } // genuine-note {{'device_only' declared here}}

namespace hr {
template <class D> struct pol {};
struct tag : pol<tag> {};
template <class D> HD int sync(pol<D> &) {
#ifndef __HIP_DEVICE_COMPILE__
  return 4;
#else
  return device_only();
#endif
}
template <class D> HD int sync_opt(pol<D> &p) { return sync(p); }
} // namespace hr

template <class D> HD int call_outer(hr::pol<D> &p) {
#ifdef __HIP_DEVICE_COMPILE__
  return hr::sync_opt(p) + 100;
#else
  return hr::sync_opt(p);
#endif
}

__global__ void kernel(int *o) {
  hr::tag t;
  o[0] = call_outer(t);
}
int host_use() {
  hr::tag t;
  return hr::sync_opt(t);
}

#ifdef GENUINE
template <class T> HD int shared_hd(T t) {
  return device_only() + t; // genuine-error {{reference to __device__ function 'device_only' in __host__ __device__ function}}
}
int genuine_use() { return shared_hd(1); } // genuine-note {{called by 'genuine_use'}}
#endif

// HOST-LABEL: define {{.*}} @_ZN2hr4syncINS_3tagEEEiRNS_3polIT_EE(
// HOST: ret i32 4
// DEV-LABEL: define {{.*}} @_ZN2hr4syncINS_3tagEEEiRNS_3polIT_EE(
// DEV: call {{.*}} @_Z11device_onlyv()
