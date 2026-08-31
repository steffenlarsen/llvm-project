// A device template named with a class specialized per device architecture
// through a default template argument, here the wave size. Naming the class
// for another target takes that target's default argument, not the one the
// canonical device's argument list carries. The host's view of the kernel
// names the host's class, and what that view instantiates with it is the
// host's view too: compiled for a device, a lambda in it took the device's
// class and its parameter the host's ("no matching function for call to
// object of type").
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -std=c++17 -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -std=c++17" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1030 -std=c++17" \
// RUN:   -multi-target-aux-output=3:%t.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1100 -std=c++17" \
// RUN:   -multi-target-aux-output=4:%t.gfx1100.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=W64 %s
// RUN: llvm-dis %t.gfx1030.bc -o - | FileCheck --check-prefix=W32 %s
// RUN: llvm-dis %t.gfx1100.bc -o - | FileCheck --check-prefix=W32 %s

// W64: mul nsw i32 %{{.*}}, 64
// W64-NOT: mul nsw i32 %{{.*}}, 32
// W32: mul nsw i32 %{{.*}}, 32
// W32-NOT: mul nsw i32 %{{.*}}, 64

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
enum class target { size32, size64, dynamic };
constexpr target get_target() {
#if !defined(__HIP_DEVICE_COMPILE__)
  return target::dynamic;
#elif defined(__GFX9__)
  return target::size64;
#else
  return target::size32;
#endif
}
template <int BS, target W = get_target()> struct scan {
  static constexpr int w = W == target::size64 ? 64 : W == target::size32 ? 32 : 16;
  struct Elem {
    int v;
  };
};
template <class T, class F> __device__ T apply(const T &v, F f) { return f(v); }
template <class S> __device__ typename S::Elem move(const typename S::Elem &v) {
  return apply(v, [](const typename S::Elem &x) {
    typename S::Elem r = x;
    r.v *= S::w;
    return r;
  });
}
template <class S> struct block {
  __device__ int run(int v) {
    typename S::Elem e{v};
    return move<S>(e).v;
  }
};
template <int N> __global__ void kernel(int *o) {
  block<scan<N>> b;
  o[0] = b.run(o[1]);
}
void launch(int *o) { kernel<128><<<1, 1>>>(o); }
