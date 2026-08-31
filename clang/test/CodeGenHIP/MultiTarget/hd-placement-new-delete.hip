// A placement new in a host-device function looks up the matching placement
// operator delete, and a delete expression the usual operator delete. The
// host and the devices each have one of these. The host picks its own and the
// devices theirs, so the host's is no wrong-side call for the devices.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -x hip -fcxx-exceptions -fexceptions \
// RUN:   -fsyntax-only -verify %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -fcxx-exceptions -fexceptions -target-cpu gfx90a"

// expected-no-diagnostics

#define __device__ __attribute__((device))
#define __host__ __attribute__((host))
#define __global__ __attribute__((global))

typedef __SIZE_TYPE__ size_t;
inline void *operator new(size_t, void *p) noexcept { return p; }
inline void operator delete(void *, void *) noexcept {}
__device__ inline void *operator new(size_t, void *p) noexcept { return p; }
__device__ inline void operator delete(void *, void *) noexcept {}

struct T { __host__ __device__ T(); };
__host__ __device__ T::T() {}

template <int N> struct Few {
  T a[N];
  __host__ __device__ Few() {
    for (int i = 0; i < N; ++i)
      new (a + i) T();
  }
};

void operator delete(void *) noexcept;
__device__ void operator delete(void *) noexcept;
struct D { int x; };
__host__ __device__ void destroy(D *p) { delete p; }

__global__ void kernel(D *p) { Few<3> f; destroy(p); }
void host_use(D *p) { Few<3> f; destroy(p); }
