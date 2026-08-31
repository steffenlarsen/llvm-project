// A specialization made for the canonical device of a template that depends on
// the architecture is instantiated for the other devices only where the code
// requesting it stands for them. Here load<64> is requested only by the wave64
// device's load<int>, from a deferred (not constexpr) instantiation, and must
// not be instantiated for the wave32 devices, whose static_assert rejects it.
//
// REQUIRES: amdgpu-registered-target

// Host-primary.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.h.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1030 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.h.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1100 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=4:%t.h.gfx1100.bc
// RUN: llvm-dis %t.h.gfx90a.bc -o - | FileCheck --check-prefix=W64 %s
// RUN: llvm-dis %t.h.gfx1030.bc -o - | FileCheck --check-prefix=W32 %s
// RUN: llvm-dis %t.h.gfx1100.bc -o - | FileCheck --check-prefix=W32 %s

// Device-primary.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -fcuda-is-device \
// RUN:   -O2 -x hip -emit-llvm -o %t.d.gfx90a.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -target-cpu gfx1030 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.d.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -target-cpu gfx1100 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.d.gfx1100.bc
// RUN: FileCheck --check-prefix=W64 %s < %t.d.gfx90a.ll
// RUN: llvm-dis %t.d.gfx1030.bc -o - | FileCheck --check-prefix=W32 %s
// RUN: llvm-dis %t.d.gfx1100.bc -o - | FileCheck --check-prefix=W32 %s

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
  if constexpr (min_size() == max_size())
    return load<min_size()>(x);
  else
    return 0;
}

// W64-LABEL: define {{.*}}@_Z1kPi(
// W64: store i32 64
// W32-LABEL: define {{.*}}@_Z1kPi(
// W32: store i32 32
__global__ void k(int *o) { o[0] = load(0); }
