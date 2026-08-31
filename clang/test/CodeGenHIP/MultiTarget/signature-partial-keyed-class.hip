// A function template whose signature names a member of a class that each
// device architecture declares through a partial specialization of its own is
// specialized per device. Splitting one shared body per device instead bound
// the body's types to each device's class but the parameters to the
// signature's, and a lambda taking the device's member could not be called
// with the parameter ("no matching function for call to object of type").
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx1030 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1030" \
// RUN:   -multi-target-aux-output=2:%t.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1100" \
// RUN:   -multi-target-aux-output=3:%t.gfx1100.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=4:%t.gfx90a.bc
// RUN: llvm-dis -o - %t.gfx1030.bc | FileCheck -check-prefix=W32 %s
// RUN: llvm-dis -o - %t.gfx1100.bc | FileCheck -check-prefix=W32 %s
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=W64 %s

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

#if defined(__gfx90a__)
__host__ __device__ constexpr int wave() { return 64; }
#elif defined(__HIP_DEVICE_COMPILE__)
__host__ __device__ constexpr int wave() { return 32; }
#else
__host__ __device__ constexpr int wave() { return 16; }
#endif

// Specializations keyed by the target through a partial specialization
// each target declares differently.
template <int N, class T = void> struct Partial;
#if defined(__gfx90a__)
template <int N> struct Partial<N, void> {
  static constexpr int W = 64;
  struct Elem { int v; };
};
#elif defined(__HIP_DEVICE_COMPILE__)
template <int N> struct Partial<N, void> {
  static constexpr int W = 32;
  struct Elem { int v; };
};
#else
template <int N> struct Partial<N, void> {
  static constexpr int W = 16;
  struct Elem { int v; };
};
#endif

template <class Tr> __device__ typename Tr::Elem scale(typename Tr::Elem v) {
  typename Tr::Elem x = v;
  x.v *= Tr::W;
  return x;
}

template <class T, class F> __device__ T apply(T v, F f) { return f(v); }
template <class Tr> __device__ typename Tr::Elem twice(typename Tr::Elem v) {
  return apply(v, [](typename Tr::Elem x) {
    x.v *= Tr::W * 2;
    return x;
  });
}

template <class Tr> __global__ void kernel(int *o) {
  typename Tr::Elem e{o[1]};
  o[0] = scale<Tr>(e).v + twice<Tr>(e).v;
}

void launch(int *o) {
  kernel<Partial<1>><<<1, 1>>>(o);
}

// W32-DAG: mul nsw i32 %{{.*}}, 32
// W32-DAG: mul nsw i32 %{{.*}}, 64
// W32-NOT: mul nsw i32 %{{.*}}, 128
// W64-DAG: mul nsw i32 %{{.*}}, 64
// W64-DAG: mul nsw i32 %{{.*}}, 128
// W64-NOT: mul nsw i32 %{{.*}}, 32
