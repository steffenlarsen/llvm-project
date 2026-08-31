// A kernel launched by the host with a class that differs between device
// architectures is specialized for each device, whose copy names its own
// class. Names mangled from both, e.g. of a lambda in the kernel passed to a
// function template, compress the two as one class, as each device's own
// compilation does. So do names mangled from two targets' copies of a class
// template. Types mangled as written are compared as written.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -std=c++17 -emit-llvm -disable-llvm-passes -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1030 -fcuda-is-device -x hip -std=c++17 -disable-llvm-passes" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip -std=c++17 -disable-llvm-passes" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx90a.bc
// RUN: llvm-dis %t.gfx1030.bc -o - | FileCheck %s
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck %s

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
constexpr __host__ __device__ target get_target() {
#if !defined(__HIP_DEVICE_COMPILE__)
  return target::dynamic;
#elif defined(__GFX9__)
  return target::size64;
#else
  return target::size32;
#endif
}
template <int BS, target W = get_target()> struct scan {
  static constexpr int w = W == target::size64 ? 64 : 32;
};
template <class T> struct state {
  using value_type = T;
  T *p;
  __device__ void init(unsigned i) { p[i] = scan<sizeof(T) * 32>::w; }
};
// CHECK-DAG: define {{.*}}@_Z6accessI5stateIiEZ9init_kernIS1_EvT_jPNS3_10value_typeEEUliE_EvS3_jT0_(
// CHECK-DAG: define {{.*}}@_ZZ9init_kernI5stateIiEEvT_jPNS2_10value_typeEENKUliE_clEi(
template <class S, class F> __device__ void access(S s, unsigned i, F f) {
  f(s.p[i]);
}
template <class S>
__global__ void init_kern(S s, unsigned n, typename S::value_type *d) {
  if (d)
    access(s, n, [&](typename S::value_type v) { *d = v; });
  s.init(n);
}
void run(int *p) {
  state<int> s{p};
  init_kern<<<1, 1>>>(s, 3, p);
}

template <class T> struct copied {
  using value_type = T;
  T *p;
  template <class F> __device__ void apply(unsigned i, F f) {
#if defined(__GFX9__)
    f(p[i] + 64);
#else
    f(p[i] + 32);
#endif
  }
};
template <class H> struct holder { H h; };
// CHECK-DAG: define {{.*}}@_ZN6copiedIiE5applyIZ8use_copyIS0_EvT_PNS3_10value_typeEEUliE_EEvjS3_(
template <class S>
__global__ void use_copy(S s, typename S::value_type *d) {
  s.apply(2, [&](typename S::value_type v) { *d = v; });
}
// CHECK-DAG: define {{.*}}@_ZN6copiedIiE5applyIZ10use_holderI6holderIS0_EEvT_PiEUliE_EEvjS5_(
template <class H> __global__ void use_holder(H h, int *d) {
  h.h.apply(3, [&](int v) { *d = v; });
}
void run_copied(int *p) {
  copied<int> c{p};
  use_copy<<<1, 1>>>(c, p);
  holder<copied<int>> h{c};
  use_holder<<<1, 1>>>(h, p);
}

typedef int ctype;
// CHECK-DAG: define {{.*}}@_Z7decayedILi4EEiPiPi(
template <int N>
__device__ int decayed(ctype *const __restrict__ x, ctype d[N]) {
  return x[0] + d[0];
}
__global__ void use_decayed(int *p) { p[0] = decayed<4>(p, p + 1); }
