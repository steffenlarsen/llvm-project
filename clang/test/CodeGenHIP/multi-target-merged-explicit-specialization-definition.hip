// Explicit specializations whose members carry target-dependent CUDA
// attributes, so they are forked per target, but whose copies are otherwise
// equivalent, so the widened merge folds them into one shared copy. The
// class's definition data still names whichever copy was defined last, which
// is now redundant; member lookup through the specialization must find the
// shared copy's members instead. This follows rocThrust's merge_sort dispatch
// ("no member named 'doit' in
// 'thrust::hip_rocprim::__merge_sort::dispatch<detail::true_type>'").
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

#ifndef __HIP_DEVICE_COMPILE__
#define RUNTIME_FUNCTION __host__ __device__
#else
#define RUNTIME_FUNCTION __host__
#endif

struct false_type {};
struct true_type {};

template <class S> struct dispatch;
template <> struct dispatch<false_type> {
  template <class K> static RUNTIME_FUNCTION int doit(K k) { return 1; }
};
template <> struct dispatch<true_type> {
  template <class K> static RUNTIME_FUNCTION int doit(K k) { return 2; }
};
template <> struct dispatch<int> {
  static RUNTIME_FUNCTION int doit(int k) { return 3; }
};

template <class S, class K> RUNTIME_FUNCTION int merge_sort(K k) {
  return dispatch<S>::doit(k);
}

int sort_all() {
  return merge_sort<false_type>(1) + merge_sort<true_type>(1.0) +
         dispatch<int>::doit(1);
}
