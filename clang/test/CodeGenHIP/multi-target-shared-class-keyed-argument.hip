// A member function template of a specialization the devices share, or of
// the host's, called by device code with a class that differs between device
// architectures, is specialized for the architecture the class is for. Its
// parameter took the canonical device's or the host's class instead, so the
// call did not compile.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -std=c++17 -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1030 -fcuda-is-device -O2 -x hip -std=c++17" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip -std=c++17" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1100 -fcuda-is-device -O2 -x hip -std=c++17" \
// RUN:   -mllvm -multi-target-aux-output=4:%t.gfx1100.bc
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
constexpr __host__ __device__ target get_target() {
#if !defined(__HIP_DEVICE_COMPILE__)
  return target::dynamic;
#elif defined(__GFX9__)
  return target::size64;
#else
  return target::size32;
#endif
}
constexpr __host__ __device__ bool on_device() {
#if defined(__HIP_DEVICE_COMPILE__)
  return true;
#else
  return false;
#endif
}
// Differs between the host and the devices only.
template <class T> struct shared_scan {
  template <class P> __device__ T op(T x, P &prefix) {
    constexpr int base = on_device() ? 1 : 2;
    return prefix(x) + base;
  }
};
// Differs between device architectures.
template <class T, target W = get_target()> struct prefix_op {
  __device__ T operator()(T x) { return x + (W == target::size64 ? 64 : 32); }
};
// W32-LABEL: define {{.*}}@_Z6directIiEvPT_(
// W32: add nsw i32 %{{.*}}, 33
// W64-LABEL: define {{.*}}@_Z6directIiEvPT_(
// W64: add nsw i32 %{{.*}}, 65
template <class U> __global__ void direct(U *p) {
  prefix_op<int> po;
  shared_scan<int> s;
  p[0] = s.op(p[1], po);
}
template __global__ void direct<int>(int *);
template <int N> struct Traits { using S = shared_scan<int>; };
// W32-LABEL: define {{.*}}@_Z6traitsI6TraitsILi1EEEvPi(
// W32: add nsw i32 %{{.*}}, 33
// W64-LABEL: define {{.*}}@_Z6traitsI6TraitsILi1EEEvPi(
// W64: add nsw i32 %{{.*}}, 65
template <class K> __global__ void traits(int *p) {
  prefix_op<int> po;
  p[0] = typename K::S().op(p[1], po);
}
void run(int *p) { traits<Traits<1>><<<1, 1>>>(p); }

// The host's own copy of a class, through a member initializer that differs.
template <class T> struct copied_scan {
  static constexpr int base = on_device() ? 1 : 2;
  template <class P> __device__ T op(T x, P &prefix) {
    return prefix(x) + base;
  }
};
template <int N> struct CopiedTraits { using S = copied_scan<int>; };
// W32-LABEL: define {{.*}}@_Z6traitsI12CopiedTraitsILi1EEEvPi(
// W32: add nsw i32 %{{.*}}, 33
// W64-LABEL: define {{.*}}@_Z6traitsI12CopiedTraitsILi1EEEvPi(
// W64: add nsw i32 %{{.*}}, 65
void run_copied(int *p) { traits<CopiedTraits<1>><<<1, 1>>>(p); }
