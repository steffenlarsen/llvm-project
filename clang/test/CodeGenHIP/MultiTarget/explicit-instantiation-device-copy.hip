// A kernel template whose body calls a function the host and the devices
// define differently has a copy for the host and one the devices share. Its
// specializations are made per target. An explicit instantiation definition
// makes one for each device, as each device's own compilation does.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx1100" \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx90a" \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx942" \
// RUN:   -multi-target-aux-output=2:%t.gfx1100.bc -multi-target-aux-output=3:%t.gfx90a.bc \
// RUN:   -multi-target-aux-output=4:%t.gfx942.bc
// RUN: llvm-dis %t.gfx1100.bc -o - | FileCheck --check-prefix=DEV %s
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=DEV %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefix=DEV %s
// The devices' own compilations agree.
// RUN: %clang_cc1 -std=c++17 %{hip_device} %{host_aux_triple} \
// RUN:   -target-cpu gfx90a -emit-llvm -o - %s | FileCheck --check-prefix=DEV %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

__device__ inline int shuffle(int v, int width = 64) {
#if defined(__HIP_DEVICE_COMPILE__)
  return __builtin_amdgcn_ds_bpermute(width << 2, v);
#else
  return v;
#endif
}

template <bool Reduce, int Width> __global__ void kern(int *p) {
  if constexpr (Reduce)
    p[0] = shuffle(p[0], Width);
}
template __global__ void kern<false, 32>(int *);
template __global__ void kern<true, 64>(int *);

// DEV-DAG: define {{.*}}amdgpu_kernel void @_Z4kernILb0ELi32EEvPi(
// DEV-DAG: define {{.*}}amdgpu_kernel void @_Z4kernILb1ELi64EEvPi(
// DEV-DAG: call {{.*}} @_Z7shuffleii(
