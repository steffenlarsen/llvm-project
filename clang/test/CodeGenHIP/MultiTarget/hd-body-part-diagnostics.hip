// A member function body means something different to the host and the
// devices, as an if constexpr there tests the size of long double. Its first
// parse stands for every target, and the devices then get a body of their own.
// What the first parse found wrong for the devices is not theirs, as only the
// host takes the branch. A branch the devices take is still diagnosed for them.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -x hip -fsyntax-only -verify %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx90a"
// RUN: not %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -x hip -fsyntax-only %s -DDEVICE_TAKES \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx90a -DDEVICE_TAKES" \
// RUN:   2>&1 | FileCheck --check-prefix=DEVICE-TAKES %s

#define __device__ __attribute__((device))
#define __host__ __attribute__((host))
#define __global__ __attribute__((global))

#if defined(DEVICE_TAKES)
#define ON_THE_HOST (sizeof(long double) != 16)
#else
// expected-no-diagnostics
#define ON_THE_HOST (sizeof(long double) == 16)
#endif

void host_only();
struct Counter {
  int v;
  __host__ __device__ int bump(int x) {
    if constexpr (ON_THE_HOST)
      host_only();
    return x + v;
  }
};
__global__ void kern(int *p) {
  Counter c{1};
  *p = c.bump(*p);
}

// DEVICE-TAKES: hd-body-part-diagnostics.hip:33:7: error: reference to __host__ function 'host_only' in __host__ __device__ function
// DEVICE-TAKES: 1 error generated when compiling for gfx90a.
