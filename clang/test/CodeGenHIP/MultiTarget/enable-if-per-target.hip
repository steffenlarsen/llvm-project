// A function template specialization chosen by one target's overload
// resolution, through enable_if on what targets evaluate differently, need not
// exist for another target. The host's view of device code and the other
// devices instantiate it in a SFINAE context and use their own choice. They
// reported the substitution failure as an error.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -std=c++17 -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx908 -O2 -std=c++17" \
// RUN:   -multi-target-aux-output=2:%t.gfx908.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -O2 -std=c++17" \
// RUN:   -multi-target-aux-output=3:%t.gfx90a.bc \
// RUN: -verify
// RUN: llvm-dis -o - %t.gfx908.bc | FileCheck -check-prefix=GFX908 %s
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=GFX90A %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

// expected-no-diagnostics

struct dim3 {
  unsigned x, y, z;
  __host__ __device__ dim3(unsigned x = 1, unsigned y = 1, unsigned z = 1)
      : x(x), y(y), z(z) {}
};
typedef struct ihipStream_t *hipStream_t;
extern "C" int hipConfigureCall(dim3, dim3, unsigned long = 0,
                                hipStream_t = 0);

template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> {
  using type = T;
};

constexpr bool is_gfx908() {
#if defined(__gfx908__)
  return true;
#else
  return false;
#endif
}

template <bool Sleep> struct State {
  static constexpr bool use_sleep = Sleep;
};
template <class S> constexpr bool runnable() {
  return is_gfx908() || !S::use_sleep;
}

template <int N, class S>
__device__ typename enable_if<!runnable<S>()>::type impl(int *p, S) {
  *p = -N;
}
template <int N, class S>
__device__ typename enable_if<runnable<S>()>::type impl(int *p, S) {
  *p = N;
}

template <int N, class S> __global__ void kernel(int *p, S s) {
  impl<N>(p, s);
}

void launch(int *p) {
  kernel<3><<<1, 1>>>(p, State<true>{});
  kernel<4><<<1, 1>>>(p, State<false>{});
}

// GFX908-LABEL: define {{.*}} @_Z6kernelILi3E5StateILb1EEEvPiT0_(
// GFX908: store i32 3,
// GFX908-LABEL: define {{.*}} @_Z6kernelILi4E5StateILb0EEEvPiT0_(
// GFX908: store i32 4,
// GFX90A-LABEL: define {{.*}} @_Z6kernelILi3E5StateILb1EEEvPiT0_(
// GFX90A: store i32 -3,
// GFX90A-LABEL: define {{.*}} @_Z6kernelILi4E5StateILb0EEEvPiT0_(
// GFX90A: store i32 4,
