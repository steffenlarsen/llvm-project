// A class template copied per target, whose copies differ only in member
// bodies, reached through a specialization the devices share. Code compiled
// for one device only, here a member of a specialization keyed by that device,
// calls a member of the copy the shared specialization named for another
// device. Each device compiles the member of its own copy, which gfx90a left
// undefined.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -std=c++17 -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1030 -O2 -std=c++17" \
// RUN:   -multi-target-aux-output=2:%t.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -O2 -std=c++17" \
// RUN:   -multi-target-aux-output=3:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1100 -O2 -std=c++17" \
// RUN:   -multi-target-aux-output=4:%t.gfx1100.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=W64 %s
// RUN: llvm-dis %t.gfx1030.bc -o - | FileCheck --check-prefix=W32 %s
// RUN: llvm-dis %t.gfx1100.bc -o - | FileCheck --check-prefix=W32 %s

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

#if defined(__GFX9__) || !defined(__HIP_DEVICE_COMPILE__)
#define WARP 64
#else
#define WARP 32
#endif

template <bool B, class X, class Y> struct cond { using type = X; };
template <class X, class Y> struct cond<false, X, Y> { using type = Y; };

template <class T, unsigned W> class dpp {
public:
  template <class F> __device__ T reduce(T x, F f) {
#if defined(__GFX9__)
    return f(x, T(W + 1000));
#else
    return f(x, T(W + 2000));
#endif
  }
};
template <class T, unsigned W> using crosslane = typename cond<true, dpp<T, W>, int>::type;

constexpr __host__ __device__ bool on_device() {
#if defined(__HIP_DEVICE_COMPILE__)
  return true;
#else
  return false;
#endif
}
template <class T, unsigned W> struct select_impl {
  using type = typename cond<on_device(), crosslane<T, W>, crosslane<T, W>>::type;
};

template <class T, unsigned W> class wr : private select_impl<T, W>::type {
  using base = typename select_impl<T, W>::type;

public:
  template <class F> __device__ T Reduce(T x, F f) {
#if defined(__GFX9__)
    return base::reduce(x, f) + 1;
#else
    return base::reduce(x, f);
#endif
  }
};

enum class target { side, device };
constexpr __host__ __device__ target get_target() {
#if defined(__HIP_DEVICE_COMPILE__)
  return target::device;
#else
  return target::side;
#endif
}
template <int BS, target X = get_target()> struct scan {
  static constexpr int w = X == target::device ? BS : 0;
};
template <class T, unsigned W = WARP> struct WarpReduce : wr<T, W> {
  static constexpr int lanes = scan<sizeof(T) * 8>::w;
};

struct Add {
  __device__ int operator()(int a, int b) const { return a + b; }
};

// W64-NOT: declare {{.*}}@_ZN3dppIiLj64EE6reduceI3AddEEiiT_(
// W64-LABEL: define {{.*}}@_Z1kILj64EEvPi(
// W64: add nsw i32 %{{.*}}, 1097
// W32-LABEL: define {{.*}}@_Z1kILj64EEvPi(
// W32: add nsw i32 %{{.*}}, 2064
template <unsigned WS> __global__ void k(int *o) {
  o[WS] = WarpReduce<int>().Reduce(o[1], Add()) + WarpReduce<int>::lanes;
}
void run(int *o) {
  k<32><<<1, 1>>>(o);
  k<64><<<1, 1>>>(o);
}
