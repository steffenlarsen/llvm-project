// Member function bodies whose meaning differs between targets, in a class
// that does not. A body consuming a per-architecture constant (an
// if constexpr condition, a template argument) is parsed again for each
// part of the targets that tells the constant apart; CodeGen emits each
// target's body and constant evaluation evaluates it. A member of a class
// template copied per target (an #if inside a member body) is instantiated
// from the copy of the targets requesting it, so one whose other copy rejects
// the specialization (a static_assert) is not instantiated from that copy.
//
// REQUIRES: amdgpu-registered-target

// Host-primary.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1030 -O2" \
// RUN:   -multi-target-aux-output=2:%t.h.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=3:%t.h.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1100 -O2" \
// RUN:   -multi-target-aux-output=4:%t.h.gfx1100.bc
// RUN: llvm-dis %t.h.gfx1030.bc -o - | FileCheck --check-prefix=W32 %s
// RUN: llvm-dis %t.h.gfx90a.bc -o - | FileCheck --check-prefix=W64 %s
// RUN: llvm-dis %t.h.gfx1100.bc -o - | FileCheck --check-prefix=W32 %s

// Device-primary, a wave32 device primary.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1030 -fcuda-is-device \
// RUN:   -O2 -x hip -emit-llvm -o %t.d.gfx1030.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=2:%t.d.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} -target-cpu gfx1100 -O2" \
// RUN:   -multi-target-aux-output=3:%t.d.gfx1100.bc
// RUN: FileCheck --check-prefix=W32 %s < %t.d.gfx1030.ll
// RUN: llvm-dis %t.d.gfx90a.bc -o - | FileCheck --check-prefix=W64 %s
// RUN: llvm-dis %t.d.gfx1100.bc -o - | FileCheck --check-prefix=W32 %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

constexpr __device__ int ws() {
#if defined(__gfx1030__) || defined(__gfx1100__)
  return 32;
#else
  return 64;
#endif
}
template <int N> struct I { static constexpr int v = N; };

struct C {
  __device__ int f() {
    if constexpr (ws() == 64)
      return 64;
    else
      return 32;
  }
  __device__ int g() { return I<ws()>::v + 1; }
  // Evaluated in a constant expression of code compiled per target.
  static constexpr __device__ int e() {
    if constexpr (ws() == 64)
      return 3064;
    else
      return 3032;
  }
};

__device__ int t() {
  if constexpr (ws() == 64)
    return 2064;
  else
    return 2032;
}

template <int W> struct T {
  __device__ int h() {
#if defined(__gfx90a__)
    return W + 1000;
#else
    static_assert(W <= 32, "narrow only");
    return W;
#endif
  }
};
template <int W> __device__ int pick() { return T<W>().h(); }

// W32-LABEL: define {{.*}}@_Z1kPi(
// W32-DAG: store i32 32,
// W32-DAG: store i32 33,
// W32-DAG: store i32 2032,
// W32-DAG: store i32 1032,
// W32-DAG: store i32 3032,
// W64-LABEL: define {{.*}}@_Z1kPi(
// W64-DAG: store i32 64,
// W64-DAG: store i32 65,
// W64-DAG: store i32 2064,
// W64-DAG: store i32 1064,
// W64-DAG: store i32 3064,
__global__ void k(int *o) {
  C c;
  o[0] = c.f();
  o[1] = c.g();
  o[2] = t();
  o[3] = pick<ws()>() + (ws() == 32 ? 1000 : 0);
  o[4] = I<C::e()>::v;
}
