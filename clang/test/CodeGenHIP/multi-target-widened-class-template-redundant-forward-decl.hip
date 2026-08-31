// A class template forward-declared and then defined inside a widened range
// gets one copy of each per target. The copies of the forward declaration are
// identical, so the widened merge folds them into one and marks the others
// redundant, but the definitions differ and stay per target. Each definition
// is a redeclaration of its own target's forward declaration, so the
// canonical declaration of a target's copy is a redundant one. A
// specialization of the host's copy instantiated from a device's pattern must
// still be recognized as an instantiation of that pattern's copy, or its
// member typedefs stay dependent, as in rocThrust's temporary_array over
// tagged_allocator ("no matching constructor for initialization of
// 'temporary_array<...>'").
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a -nogpulib -nogpuinc \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only -Xclang -verify %s
// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpulib -nogpuinc -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -Xclang -verify %s

// expected-no-diagnostics

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))

namespace ns {
// Widens the range to cover alloc's forward declaration.
#ifndef __HIP_DEVICE_COMPILE__
inline __host__ __device__ int arm() { return 2; }
#else
inline __host__ __device__ int arm() { return 1; }
#endif

template <class T> struct alloc;

template <class T> struct alloc {
  using value_type = T;
  using pointer = value_type *;
#ifndef __HIP_DEVICE_COMPILE__
  static const int v = 2;
#else
  static const int v = 1;
#endif
};
} // namespace ns

namespace ns {
template <class T> struct storage {
  using pointer = typename alloc<T>::pointer;
  __host__ __device__ storage(pointer p) : p(p) {}
  pointer p;
};
} // namespace ns

namespace ns {
template <class T> __host__ __device__ int use(T t) {
  storage<T> s(&t);
#ifndef __HIP_DEVICE_COMPILE__
  return *s.p + 1;
#else
  return *s.p;
#endif
}
} // namespace ns

__device__ int dev() { return ns::use(0); }
int host() { return ns::use(0); }
