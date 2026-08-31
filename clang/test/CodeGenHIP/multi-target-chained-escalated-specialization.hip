// A function template declared once before a definition whose body diverges
// per target, so every target's copy redeclares the shared declaration and
// the copies share one set of specializations. A kernel's call, parsed under
// the host's ambient, reaches a device target's copy through the canonical
// device fallback; that specialization must be filed under the device target,
// not the host, or the host's own call later reuses it and the host is left
// with only a declaration. This follows rocThrust's undefined host symbols
// ("undefined reference to `hipError_t
// thrust::hip_rocprim::synchronize_stream<...>(...)'").
//
// REQUIRES: amdgpu-registered-target

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
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=GFX90A %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=GFX942 %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
#define HD __host__ __device__

namespace sq {
template <class I> HD int ssk(I);
template <class I> HD int ssk(I i) {
#ifndef __HIP_DEVICE_COMPILE__
  return i + 1;
#elif defined(__gfx90a__)
  return i + 2;
#else
  return i + 3;
#endif
}
} // namespace sq

__global__ void kernel(int *p) { *p = sq::ssk(40); }
int host_use() { return sq::ssk(40); }

// HOST-LABEL: define {{.*}} @_ZN2sq3sskIiEEiT_(
// HOST: add nsw i32 %{{.*}}, 1
// GFX90A-LABEL: define {{.*}} @_ZN2sq3sskIiEEiT_(
// GFX90A: add nsw i32 %{{.*}}, 2
// GFX942-LABEL: define {{.*}} @_ZN2sq3sskIiEEiT_(
// GFX942: add nsw i32 %{{.*}}, 3
