// A kernel template that does not itself depend on the device architecture is
// specialized per architecture when its template arguments name classes that
// do. Every other device got the canonical device's specialization (silent),
// and a host launch passing such a class by value did not compile.
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
// RUN: FileCheck --check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis %t.gfx1030.bc -o - | FileCheck --check-prefix=W32 %s
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=W64 %s
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
  static constexpr int w = W == target::size64 ? 64 : W == target::size32 ? 32 : 0;
};

template <int N> struct Traits { using S = scan<N>; };

// W32-LABEL: define {{.*}}@_Z4kernI6TraitsILi128EEEvPi(
// W32: store i32 32,
// W64-LABEL: define {{.*}}@_Z4kernI6TraitsILi128EEEvPi(
// W64: store i32 64,
template <class K> __global__ void kern(int *p) { p[0] = K::S::w; }

template <class T> struct state {
  T *p;
  __device__ void init(unsigned i) { p[i] = scan<sizeof(T) * 32>::w; }
};
template <class S> __device__ void init_state(S s, unsigned i) { s.init(i); }

// HOST: define {{.*}}@_Z24__device_stub__init_kernI5stateIiEEvT_j(
// W32-LABEL: define {{.*}}@_Z9init_kernI5stateIiEEvT_j(
// W32: store i32 32,
// W64-LABEL: define {{.*}}@_Z9init_kernI5stateIiEEvT_j(
// W64: store i32 64,
template <class S> __global__ void init_kern(S s, unsigned n) {
  init_state(s, n);
}

// HOST: define {{.*}}@_Z24__device_stub__init_kernI5stateIlEEvT_j(
// W32-LABEL: define {{.*}}@_Z9init_kernI5stateIlEEvT_j(
// W32: store i64 32,
// W64-LABEL: define {{.*}}@_Z9init_kernI5stateIlEEvT_j(
// W64: store i64 64,
void run(int *p, long *q) {
  kern<Traits<128>><<<1, 1>>>(p);
  state<int> s{p};
  init_kern<<<1, 1>>>(s, 3);
  state<long> t{q};
  init_kern<decltype(t)><<<1, 1>>>(t, 3);
}
