// With several devices, a pragma that differs between two of them is each
// device's own. Where the devices agree and only the host differs, the second
// device reuses the first device's alternative, which must then still mean
// what it means to the second device.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -O1 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -O1 -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -O1 -target-cpu gfx942" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: FileCheck --check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=GFX90A %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefix=GFX942 %s

#define __device__ __attribute__((device))
#define __host__ __attribute__((host))

// Only gfx942 contracts.
__host__ __device__ float zz_gfx942(float a, float b, float c) {
#if defined(__gfx942__)
#pragma clang fp contract(fast)
#else
#pragma clang fp contract(off)
#endif
  return a * b + c;
}
// HOST-LABEL: define {{.*}} @_Z9zz_gfx942fff(
// HOST: fmul float
// GFX90A-LABEL: define {{.*}} @_Z9zz_gfx942fff(
// GFX90A: fmul float
// GFX942-LABEL: define {{.*}} @_Z9zz_gfx942fff(
// GFX942: fmul contract float

// Only gfx90a contracts, the device whose alternative comes first.
__host__ __device__ float zz_gfx90a(float a, float b, float c) {
#if defined(__gfx90a__)
#pragma clang fp contract(fast)
#else
#pragma clang fp contract(off)
#endif
  return a * b + c;
}
// HOST-LABEL: define {{.*}} @_Z9zz_gfx90afff(
// HOST: fmul float
// GFX90A-LABEL: define {{.*}} @_Z9zz_gfx90afff(
// GFX90A: fmul contract float
// GFX942-LABEL: define {{.*}} @_Z9zz_gfx90afff(
// GFX942: fmul float

// Both devices contract, the host does not.
__host__ __device__ float zz_devices(float a, float b, float c) {
#if defined(__HIP_DEVICE_COMPILE__)
#pragma clang fp contract(fast)
#else
#pragma clang fp contract(off)
#endif
  return a * b + c;
}
// HOST-LABEL: define {{.*}} @_Z10zz_devicesfff(
// HOST: fmul float
// GFX90A-LABEL: define {{.*}} @_Z10zz_devicesfff(
// GFX90A: fmul contract float
// GFX942-LABEL: define {{.*}} @_Z10zz_devicesfff(
// GFX942: fmul contract float
