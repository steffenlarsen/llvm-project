// A function template forked per target calls function templates whose copies
// the widened merge folded into shared copies. Each target's copy of the
// caller was parsed while its own copies of the callees were still live, so
// its calls name copies that are now redundant.
//  - A dependent call's lookup set holds only redundant copies, which must be
//    replaced by the shared copies of the same declarations even though their
//    signatures name different copies of a class template.
//  - A non-dependent call names a redundant copy's specialization, whose body
//    calls the shared copies and so returns the shared copy of the class
//    template, so it must be replaced by the shared copy's specialization.
//  - The merge changes which specializations are keyed by target, so the
//    specialization sets keyed before the merge must be rehashed.
//
// Otherwise a dependent call finds no declaration, a call finds no function
// visible in the template definition or by argument-dependent lookup, and a
// returned value fails to convert between two copies of the same class
// template.
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
// Widens the range to cover ts.
#ifndef __HIP_DEVICE_COMPILE__
inline __host__ __device__ int arm() { return 2; }
#else
inline __host__ __device__ int arm() { return 1; }
#endif

namespace ts {
struct layout {
  unsigned long size, alignment;
};

template <class T> struct part {
  T **dest;
  layout storage_layout;
  layout get_layout() const { return storage_layout; }
};

template <class T> part<T> make_part(T **dest, layout storage_layout) {
  return part<T>{dest, storage_layout};
}

template <class T>
part<T> make_part(T **dest, unsigned long size, unsigned long alignment = 8) {
  return make_part(dest, {size, alignment});
}

template <class T> part<T> aligned(T **dest, unsigned long elements) {
  return make_part(dest, elements * sizeof(T), alignof(T));
}

template <class... Ts> struct linear {
  linear(Ts... subs) {}
  layout get_layout() const { return {0, 1}; }
};

template <class... Ts> linear<Ts...> make_linear(Ts... ts) {
  return linear<Ts...>(ts...);
}

template <class P> unsigned long partition(P p) {
  return p.get_layout().size;
}
} // namespace ts

template <class T> unsigned long use(T) {
  using namespace ts;
  void *p;
  T *q;
  auto l = make_linear(make_part(&p, 4), aligned(&q, 1));
#ifndef __HIP_DEVICE_COMPILE__
  return partition(l) + 1;
#else
  return partition(l);
#endif
}
} // namespace ns

unsigned long host() { return ns::use(0); }
