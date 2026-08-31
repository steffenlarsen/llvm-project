// Friend functions defined in a class whose definition differs per target,
// found by argument-dependent lookup. Each target calls its own friend, by its
// body, by its constant value, and by overload resolution. Each call was
// rejected with "no matching function".
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx1030 -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx1030 -O2" \
// RUN:   -multi-target-aux-output=2:%t.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=3:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx942 -O2" \
// RUN:   -multi-target-aux-output=4:%t.gfx942.bc
// RUN: llvm-dis -o - %t.gfx1030.bc | FileCheck -check-prefix=OTHER %s
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=OTHER %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=GFX942 %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

struct F {
  int v = 3;
#if defined(__gfx942__)
  __device__ friend int peek(const F &f) { return f.v * 10; }
#else
  __device__ friend int peek(const F &f) { return f.v; }
#endif
};

// OTHER-LABEL: define {{.*}} @_Z4bodyPi(
// OTHER: store i32 3,
// GFX942-LABEL: define {{.*}} @_Z4bodyPi(
// GFX942: store i32 30,
__global__ void body(int *p) { F f; *p = peek(f); }

template <int N> struct I { static constexpr int v = N; };
struct G {
  int v = 3;
#if defined(__gfx942__)
  __device__ friend constexpr int value(const G &g) { return g.v * 10; }
#else
  __device__ friend constexpr int value(const G &g) { return g.v; }
#endif
};

// OTHER-LABEL: define {{.*}} @_Z8constantPi(
// OTHER: store i32 3,
// GFX942-LABEL: define {{.*}} @_Z8constantPi(
// GFX942: store i32 30,
__global__ void constant(int *p) { *p = I<value(G{})>::v; }

struct H {
#if defined(__gfx942__)
  __device__ friend int pick(const H &, long) { return 1; }
#endif
  __device__ friend int pick(const H &, int) { return 2; }
};

// OTHER-LABEL: define {{.*}} @_Z8overloadPi(
// OTHER: store i32 2,
// GFX942-LABEL: define {{.*}} @_Z8overloadPi(
// GFX942: store i32 1,
__global__ void overload(int *p) { H h; *p = pick(h, 5L); }
