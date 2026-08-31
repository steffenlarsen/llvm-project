// An error is attributed to the targets whose analysis raises it, also where
// code analysed once for every target is analysed again for some of them.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: rm -rf %t && split-file %s %t
// RUN: not %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx90a -x hip \
// RUN:   -fsyntax-only %t/host.hip \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx942" \
// RUN:   2>&1 | FileCheck -check-prefix=HOST %s
// HOST: 1 error generated when compiling for host.

// RUN: not %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx90a -x hip \
// RUN:   -fsyntax-only %t/devices.hip \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx942" \
// RUN:   2>&1 | FileCheck -check-prefix=DEVICES %s
// DEVICES: 1 error generated when compiling for gfx90a, gfx942.

// RUN: not %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx90a -x hip \
// RUN:   -fsyntax-only %t/arch.hip \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx942" \
// RUN:   2>&1 | FileCheck -check-prefix=ARCH %s
// ARCH: 1 error generated when compiling for gfx942.

// RUN: not %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx90a -x hip \
// RUN:   -fsyntax-only %t/all.hip \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx942" \
// RUN:   2>&1 | FileCheck -check-prefix=ALL %s
// ALL: 2 errors generated when compiling for host, gfx90a, gfx942.

// Device-only code is also analysed as the host sees it, as in a compilation
// for the host alone.
// RUN: not %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx90a -x hip \
// RUN:   -fsyntax-only %t/kernel.hip \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx942" \
// RUN:   2>&1 | FileCheck -check-prefix=KERNEL %s
// KERNEL: kernel.hip:4:17: error: static assertion failed due to requirement 'width() == 64': fails for the host
// KERNEL: 1 error generated when compiling for host.

// A wrong-side call in device-only code is an error for the devices, as in
// their own compilations.
// RUN: not %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx90a -x hip \
// RUN:   -fsyntax-only %t/wrong-side.hip \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx942" \
// RUN:   2>&1 | FileCheck -check-prefix=WRONG-SIDE %s
// WRONG-SIDE: wrong-side.hip:3:53: error: reference to __host__ function 'f' in __global__ function
// WRONG-SIDE: 1 error generated when compiling for gfx90a, gfx942.

// An error generating the host's code is the host's.
// RUN: not %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx90a -x hip \
// RUN:   -fhip-new-launch-api -emit-llvm -o /dev/null %t/launch.hip \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx90a -fhip-new-launch-api" \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx942 -fhip-new-launch-api" \
// RUN:   2>&1 | FileCheck -check-prefix=LAUNCH %s
// LAUNCH: error: Can't find declaration for hipLaunchKernel
// LAUNCH: 1 error generated when compiling for host.

//--- width.h
#if defined(__HIP_DEVICE_COMPILE__)
__attribute__((host, device)) constexpr int width() { return 64; }
#else
__attribute__((host, device)) constexpr int width() { return 1; }
#endif
#if defined(__gfx942__)
constexpr int lanes() { return 32; }
#else
constexpr int lanes() { return 64; }
#endif

//--- host.hip
#include "width.h"
static_assert(width() == 64, "fails for the host");

//--- devices.hip
#include "width.h"
static_assert(width() == 1, "fails for the devices");

//--- arch.hip
#include "width.h"
static_assert(lanes() == 64, "fails for gfx942");

//--- all.hip
#include "width.h"
static_assert(width() == 0, "fails for every target");

//--- kernel.hip
#include "width.h"
__attribute__((global)) void kernel(int *out) {
  out[0] = width();
  static_assert(width() == 64, "fails for the host");
}

//--- wrong-side.hip
#define __global__ __attribute__((global))
struct MF { int f() const { return 4; } };
__global__ void kernel(int *out) { MF m; out[0] = m.f(); }

//--- launch.hip
#define __global__ __attribute__((global))
__global__ void kernel(int *out) { out[0] = 1; }
