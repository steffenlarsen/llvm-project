// A namespace block containing any divergence is widened as a whole, so each
// function template in it gets one copy per target. Copies that redeclare no
// earlier declaration are unchained siblings sharing one location, and a
// dependent call parsed inside one target's copy of a caller freezes a
// candidate set naming only that target's copy of the callee. The caller's
// copies are identical, so they are merged into one shared copy that is then
// instantiated for every target; the frozen candidate must be swapped for the
// instantiating target's sibling rather than dropped. This follows rocThrust's
// unique_by_key ("no matching function for call to 'unique_by_key'").
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a -nogpulib -nogpuinc \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only -Xclang -verify %s
// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpulib -nogpuinc -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -Xclang -verify %s

// Each target must instantiate its own copy of the two-argument ubk.
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

// expected-no-diagnostics

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
#define HD __host__ __device__

namespace hr {
template <class I, class B> HD int ubk(I a, B b) {
#ifndef __HIP_DEVICE_COMPILE__
  return a + 4;
#elif defined(__gfx90a__)
  return a + 5;
#else
  return a + 6;
#endif
}
template <class I> HD int ubk(I a) { return hr::ubk(a, 0); }
} // namespace hr

int host_use() { return hr::ubk(40); }
__global__ void kernel(int *p) { *p = hr::ubk(40); }

// HOST-LABEL: define {{.*}} @_ZN2hr3ubkIiiEEiT_T0_(
// HOST: add nsw i32 %{{.*}}, 4
// GFX90A-LABEL: define {{.*}} @_ZN2hr3ubkIiiEEiT_T0_(
// GFX90A: add nsw i32 %{{.*}}, 5
// GFX942-LABEL: define {{.*}} @_ZN2hr3ubkIiiEEiT_T0_(
// GFX942: add nsw i32 %{{.*}}, 6
