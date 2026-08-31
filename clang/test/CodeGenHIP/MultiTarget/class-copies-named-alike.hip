// A class only the devices have, which each device defines differently. The
// arms that name it, here an alias and a function taking it, are each
// device's own, although they read alike, as each names its own device's
// copy of the class. So each device calls its own member.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O1 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -O1 -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -O1 -target-cpu gfx942" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=GFX90A %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefix=GFX942 %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__gfx90a__)
struct S { int a; __device__ int get() const { return a + 90; } };
#elif defined(__gfx942__)
struct S { int a; __device__ int get() const { return a + 942; } };
#endif

#if defined(__HIP_DEVICE_COMPILE__)
using S_t = S;
__device__ int use(const S_t &s) { return s.get(); }
#endif

__global__ void k(int *o) {
#if defined(__HIP_DEVICE_COMPILE__)
  S s{o[1]};
  o[0] = use(s);
#endif
}

// GFX90A-LABEL: define {{.*}}amdgpu_kernel void @_Z1kPi(
// GFX90A: add nsw i32 %{{.*}}, 90
// GFX942-LABEL: define {{.*}}amdgpu_kernel void @_Z1kPi(
// GFX942: add nsw i32 %{{.*}}, 942
