// A class template whose member function body diverges between targets is
// widened into one copy per target. A specialization instantiated from another
// copy's pattern must still find its members ("no member named 'ptr'"). Naming
// the class template is a bare type reference and must not make a declaration
// a divergent user: re-parsing such a declaration per target left the
// re-parsed part seeing one target's copy of the class while the rest saw
// another's, as in hipcub's tex_ref_input_iterator base-class mem-initializer
// ("expected '(' or '{'") and rocThrust's out-of-line definitions ("does not
// match any declaration").
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
#define __global__ __attribute__((global))

template <class T> class member_use {
public:
  T *ptr;
  __host__ __device__ T get() const {
#ifndef __HIP_DEVICE_COMPILE__
    return ptr[0];
#else
    return ptr[1];
#endif
  }
};
void use_member(member_use<float> &v) { (void)v.get(); }

namespace ns {
template <class T> class out_of_line {
public:
  __host__ __device__ T get() const {
#ifndef __HIP_DEVICE_COMPILE__
    return ptr[0];
#else
    return ptr[1];
#endif
  }
  T *ptr;
};
} // namespace ns
namespace app {
void apply(ns::out_of_line<float> &v, int n);
}
void app::apply(ns::out_of_line<float> &v, int n) { (void)v.get(); }

namespace ns {
template <class T, class D = long> class base {
public:
  __host__ __device__ T get() const {
#ifndef __HIP_DEVICE_COMPILE__
    return ptr[0];
#else
    return ptr[1];
#endif
  }
  T *ptr;
};
} // namespace ns
class derived : public ::ns::base<int, long> {
public:
  using alias = ::ns::base<int, long>;
  __host__ __device__ derived() : ::ns::base<int, long>() {}
  __host__ __device__ derived(const ::ns::base<int, long> other)
      : ::ns::base<int, long>(other) {}
  __host__ __device__ derived(const ::ns::base<int, long> other, int)
      : alias(other) {}
  __host__ __device__ int twice() const { return get() * 2; }
};
__global__ void kernel(derived d, int *out) { *out = d.twice(); }
int host_use(derived d) { return d.twice(); }
