// Host device functions are analysed as the host's code and the devices'. What
// only the devices reject is reported where a device compilation reports it:
// in functions a device compilation emits, and what they call. A call the
// devices resolve to a device overload of the same signature is not reported.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: not %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -fcxx-exceptions -x hip -emit-llvm -o %t.host.ll %s -DERRORS \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx90a -fcxx-exceptions -DERRORS" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc 2>&1 | FileCheck %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -fcxx-exceptions -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx90a -fcxx-exceptions" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
#define __host__ __attribute__((host))

int host_only(int x) { return x + 1; }
int host_var = 3;

// A device overload of the same signature, as HIP's headers declare for the
// C library.
extern "C" int lib(int x) noexcept;
extern "C" __device__ int lib(int x);
__host__ __device__ int uses_lib(int x) { return lib(x); }

__host__ __device__ inline int host_use(int x) { return host_only(x); }
int h(int x) { return host_use(x); }

__host__ __device__ int guarded(int x) {
#if !defined(__HIP_DEVICE_COMPILE__)
  return host_only(x);
#else
  return x;
#endif
}

#ifdef ERRORS
// CHECK: error: reference to __host__ function 'host_only' in __host__ __device__ function
__host__ __device__ int calls_host(int x) { return host_only(x); }
// CHECK: error: reference to __host__ variable 'host_var' in __host__ __device__ function
__host__ __device__ inline int reads_host() { return host_var; }
// CHECK: error: cannot use 'try' in __host__ __device__ function
__host__ __device__ inline int tries(int x) {
  try {
    return x;
  } catch (...) {
    return 0;
  }
}
// CHECK: error: cannot use 'throw' in __host__ __device__ function
template <class T> __host__ __device__ T throws(T x) {
  if (x < 0)
    throw x;
  return x;
}
#endif

__global__ void k(int *p) {
  p[0] = uses_lib(p[0]) + guarded(p[0]);
#ifdef ERRORS
  p[1] = reads_host() + tries(p[1]) + throws(p[1]);
#endif
}
