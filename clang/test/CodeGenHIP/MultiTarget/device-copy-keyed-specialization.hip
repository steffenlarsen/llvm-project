// A function template copied once for the host and once for every device
// keys its specializations by device. Code every target shares names the
// canonical device's, so the other devices need theirs too, or they declare
// the internal function without defining it (HeCBench addBiasQKV's
// internal::cast_from_f8).
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx1030 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1030" \
// RUN:   -multi-target-aux-output=2:%t.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1100" \
// RUN:   -multi-target-aux-output=3:%t.gfx1100.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=4:%t.gfx90a.bc
// RUN: FileCheck -check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis -o - %t.gfx1030.bc | FileCheck -check-prefix=DEV %s
// RUN: llvm-dis -o - %t.gfx1100.bc | FileCheck -check-prefix=DEV %s
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=DEV %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

namespace internal {
template <class T> __host__ __device__ static inline T widen(unsigned char x) {
#ifdef __HIP_DEVICE_COMPILE__
  return (T)x * 2;
#else
  return (T)x * 3;
#endif
}
} // namespace internal

struct fp8 {
  unsigned char x;
  __host__ __device__ operator float() const {
    return internal::widen<float>(x);
  }
};

__global__ void kernel(float *o, fp8 v) { o[0] = v; }
float host(fp8 v) { return v; }

// HOST-LABEL: define internal {{.*}} @_ZN8internalL5widenIfEET_h(
// HOST: fmul {{.*}} 3.0
// DEV-LABEL: define internal {{.*}} @_ZN8internalL5widenIfEET_h(
// DEV: fmul {{.*}} 2.0
