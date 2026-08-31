// Two overloads of one function template, both forked per target, whose device
// arms call a helper the host arm doesn't. When a caller is redirected to a
// target's copy of one overload, the copy must be that overload's own, not
// the first copy of either overload tagged for that target: the other
// overload's copy has a different signature and fails to match the call.
// This follows rocThrust's assign_value for cross_system<...> ("no type named
// 'reference' in 'thrust::system::cpp::detail::tag'", "no matching function
// for call to 'raw_pointer_cast'").
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
template <class D> struct policy {};
template <class A, class B> struct cross {};
template <class T> T *raw(T *p) { return p; }

template <class D, class P1, class P2>
__host__ __device__ void assign(policy<D> &, P1 dst, P2 src) {
#ifndef __HIP_DEVICE_COMPILE__
  *dst = *src;
#else
  *raw(dst) = *raw(src);
#endif
}

template <class S1, class S2, class P1, class P2>
__host__ __device__ void assign(cross<S1, S2> &, P1 dst, P2 src) {
#ifndef __HIP_DEVICE_COMPILE__
  *dst = *src;
#else
  *raw(dst) = *raw(src);
#endif
}
} // namespace ns

struct host_tag {};
struct device_tag {};

__host__ __device__ void copy_one(int *dst, const int *src) {
  ns::cross<host_tag, device_tag> c;
  ns::assign(c, dst, src);
}
