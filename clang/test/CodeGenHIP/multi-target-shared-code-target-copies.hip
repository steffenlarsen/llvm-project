// Code shared by the devices names entities each device has a copy of: a
// constexpr function (rocPRIM's wavefront::min_size()), and a class template
// with partial specializations of its own per copy. Each device compiles what
// it would compile on its own: a static_assert that only gfx90a's copy can
// reach is not instantiated for the others, a default template argument and a
// non-dependent specialization in a shared template are each device's, and a
// specialization uses its device's partial specializations, and a device emits
// the members of its own copy of a class template.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1030 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1100 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=4:%t.gfx1100.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=CDNA %s
// RUN: llvm-dis %t.gfx1030.bc -o - | FileCheck --check-prefix=RDNA %s
// RUN: llvm-dis %t.gfx1100.bc -o - | FileCheck --check-prefix=RDNA %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
constexpr __device__ unsigned min_size() {
#if defined(__gfx1030__) || defined(__gfx1100__)
  return 32u;
#else
  return 64u;
#endif
}
constexpr __device__ unsigned max_size() {
#if defined(__HIP_DEVICE_COMPILE__)
  return min_size();
#else
  return 64u;
#endif
}
template <unsigned W, class T> __device__ int load(T x) {
  static_assert(W <= max_size(), "too wide");
  return W + x;
}
template <class T> __device__ int load(T x) {
#if defined(__HIP_DEVICE_COMPILE__)
  if constexpr (min_size() == max_size())
    return load<min_size()>(x);
  else
    return 0;
#else
  return -1;
#endif
}
__global__ void k_static_assert(int *o) { o[0] = load(0); }
template <class T, int W = max_size()> struct warp { static constexpr int width = W; };
template <unsigned N> __global__ void k_template(int *o) { o[0] = warp<float>::width; }
template __global__ void k_template<1>(int *);
template <int N> struct tile { int x[N]; };
#if defined(__gfx1030__) || defined(__gfx1100__)
template <int D, int C> struct sizes { using T = tile<1>; };
template <int C> struct sizes<80, C> { using T = tile<2>; };
#else
template <int D, int C> struct sizes { using T = tile<5>; };
#endif
__global__ void k_shared(int *o) { o[0] = sizeof(sizes<80, 4>::T); }
template <int D> __global__ void k_dependent(int *o) { o[0] = sizeof(typename sizes<D, 4>::T); }
template __global__ void k_dependent<80>(int *);

// rocPRIM's warp_reduce_dpp: each device's copy of the class picks its
// implementation with its own wavefront size, and only that copy's members are
// emitted for the device.
template <bool B> struct bc { static constexpr bool value = B; };
template <class T, unsigned W> struct reducer {
  __device__ int impl(bc<false>) {
#if defined(__gfx90a__)
    return 1;
#else
    return 2;
#endif
  }
  __device__ int impl(bc<true>) { return 3; }
  __device__ int reduce() { return impl(bc<(W < min_size())>{}); }
};
template <class T> __device__ int block() { return reducer<T, 32>().reduce(); }
__global__ void k_dispatch(int *o) { o[0] = block<int>(); }
// CDNA-LABEL: define {{.*}} @_Z15k_static_assertPi(
// CDNA: store i32 64,
// CDNA-LABEL: define {{.*}} @_Z10k_templateILj1EEvPi(
// CDNA: store i32 64,
// CDNA-LABEL: define {{.*}} @_Z8k_sharedPi(
// CDNA: store i32 20,
// CDNA-LABEL: define {{.*}} @_Z11k_dependentILi80EEvPi(
// CDNA: store i32 20,
// CDNA-LABEL: define {{.*}} @_Z10k_dispatchPi(
// CDNA: store i32 3,
// RDNA-LABEL: define {{.*}} @_Z15k_static_assertPi(
// RDNA: store i32 32,
// RDNA-LABEL: define {{.*}} @_Z10k_templateILj1EEvPi(
// RDNA: store i32 32,
// RDNA-LABEL: define {{.*}} @_Z8k_sharedPi(
// RDNA: store i32 8,
// RDNA-LABEL: define {{.*}} @_Z11k_dependentILi80EEvPi(
// RDNA: store i32 8,
// RDNA-LABEL: define {{.*}} @_Z10k_dispatchPi(
// RDNA: store i32 2,
