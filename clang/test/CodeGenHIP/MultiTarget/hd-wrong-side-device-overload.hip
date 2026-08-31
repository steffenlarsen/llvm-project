// A host-device function calling a host function is an error for a device
// that emits it, unless the device's overload resolution picks a device
// function instead. That depends on the device's resolution, not on whether
// the devices declare a function of that name at all.
//
// REQUIRES: amdgpu-registered-target

// The device overload is not viable, so the device calls the host function.
// RUN: not %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -x hip -fsyntax-only %s -DNOT_VIABLE \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -target-cpu gfx90a -DNOT_VIABLE" \
// RUN:   2>&1 | FileCheck --check-prefix=NOT-VIABLE %s

// The devices' candidates are ambiguous, where the host picks its own.
// RUN: not %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -x hip -fsyntax-only %s -DAMBIGUOUS \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -target-cpu gfx90a -DAMBIGUOUS" \
// RUN:   2>&1 | FileCheck --check-prefix=AMBIGUOUS %s

// The device overload is viable, which the device then calls.
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -x hip -fsyntax-only %s -DVIABLE \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -target-cpu gfx90a -DVIABLE" \
// RUN:   -verify

#define __device__ __attribute__((device))
#define __host__ __attribute__((host))


int f(int x) { return x + 1; }
#if defined(NOT_VIABLE)
__device__ int f(int *p, int n) { return p[n]; }
#elif defined(AMBIGUOUS)
__device__ int f(long x) { return x + 2; }
__device__ int f(unsigned x) { return x + 3; }
#else
// expected-no-diagnostics
__device__ int f(long x) { return x + 2; }
#endif

__host__ __device__ int hd(int x) { return f(x); }
__attribute__((device, used)) int use(int x) { return hd(x); }

// NOT-VIABLE: error: reference to __host__ function 'f' in __host__ __device__ function
// NOT-VIABLE: note: 'f' declared here
// NOT-VIABLE: 1 error generated when compiling for gfx90a.

// AMBIGUOUS: error: call to 'f' is ambiguous
// AMBIGUOUS: note: candidate function
// AMBIGUOUS: note: candidate function
// AMBIGUOUS: 1 error generated when compiling for gfx90a.
