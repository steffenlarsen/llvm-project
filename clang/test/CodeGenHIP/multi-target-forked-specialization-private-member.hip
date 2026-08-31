// One partial specialization of a class template that uses a divergent callee
// is re-parsed per target, so every specialization of that template is forked
// per target -- including ones selecting the other, non-divergent partial
// specialization. A shared member function's call to its own class's private
// member function, or read of its private static data member, is redirected
// into each target's fork, which is a distinct class. That redirect must not
// be access-checked again, as in rocprim's lookback_scan_state::set_complete
// calling its private set: "'set' is a private member".
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a -nogpulib -nogpuinc \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only -Xclang -verify %s
// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpulib -nogpuinc -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -Xclang -verify %s

// expected-no-diagnostics

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if __HIP_DEVICE_COMPILE__
__device__ int divergent() { return 1; }
#else
__device__ int divergent() { return 2; }
#endif

template <class T, bool Small = (sizeof(T) <= 4)> struct S;

template <class T> struct S<T, true> {
  __device__ void set_complete(int v) { this->set(v + n); }

private:
  __device__ void set(int v) { x = v; }
  static constexpr int n = 4;
  int x;
};

template <class T> struct S<T, false> {
  __device__ int get() { return divergent(); }
};

__global__ void kernel(S<int> s) { s.set_complete(1); }
