// A static assertion in a host function holds for the host but not for the
// devices, whose compilations analyse host functions too. The regular device
// compilations reject it, and so does the multi-target compilation.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: not %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -fsyntax-only %s -DERRORS \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx90a -DERRORS" \
// RUN:   2>&1 | FileCheck %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -fsyntax-only %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx90a"

#define __global__ __attribute__((global))

constexpr int ld_size() { return sizeof(long double); }

int host_guarded() {
#if !defined(__HIP_DEVICE_COMPILE__)
  static_assert(ld_size() == 16, "host long double");
#endif
  return ld_size();
}

#ifdef ERRORS
// CHECK: error: static assertion failed: host long double
int host() {
  static_assert(ld_size() == 16, "host long double");
  return ld_size();
}
#endif

__global__ void k(int *p) { *p = ld_size(); }
