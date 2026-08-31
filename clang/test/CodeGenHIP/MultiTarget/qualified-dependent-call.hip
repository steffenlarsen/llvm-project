// A template defined in a different #if arm for each target, called by a
// dependent qualified name from code only a kernel instantiates. The call
// names the copy lookup found while the caller was parsed. A target
// instantiating the caller finds its own copy, which is declared at another
// location.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -disable-llvm-passes -x hip -std=c++17 -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -O2 -disable-llvm-passes -std=c++17" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1030 -O2 -disable-llvm-passes -std=c++17" \
// RUN:   -multi-target-aux-output=3:%t.gfx1030.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=GFX9 %s
// RUN: llvm-dis %t.gfx1030.bc -o - | FileCheck --check-prefix=GFX10 %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

struct dim3 {
  unsigned x, y, z;
  __host__ __device__ dim3(unsigned x = 1, unsigned y = 1, unsigned z = 1)
      : x(x), y(y), z(z) {}
};
typedef struct ihipStream_t *hipStream_t;
extern "C" int hipConfigureCall(dim3, dim3, unsigned long = 0,
                                hipStream_t = 0);

// GFX9-LABEL: define {{.*}}@_ZN2ns4implIiEEiT_(
// GFX9: add nsw i32 %{{.*}}, 1001
// GFX10-LABEL: define {{.*}}@_ZN2ns4implIiEEiT_(
// GFX10: add nsw i32 %{{.*}}, 2002
namespace ns {
#if defined(__GFX9__)
template <class T> __host__ __device__ int impl(T x) { return x + 1001; }
#elif defined(__HIP_DEVICE_COMPILE__)
template <class T> __host__ __device__ int impl(T x) { return x + 2002; }
#else
template <class T> __host__ __device__ int impl(T x) { return x + 3003; }
#endif
} // namespace ns

template <class T> __host__ __device__ int explicit_args(T x) {
  return ns::impl<T>(x);
}
template <class T> __host__ __device__ int deduced(T x) {
  return ns::impl(x);
}
template <class T> __host__ __device__ int through_using(T x) {
  using ns::impl;
  return impl<T>(x);
}

template <class T> __global__ void k(T *o) {
  o[0] = explicit_args(o[1]) + deduced(o[2]) + through_using(o[3]);
}
void launch(int *o) { k<<<1, 1>>>(o); }
