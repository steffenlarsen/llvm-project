// Code analysed once for every target that consumes something differing
// between them is analysed per target instead: a typedef, a base class, a
// member of a shared class, and a static data member that differ per target.
// Each target compiles what it would compile on its own.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942 -O2" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=GFX90A %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=GFX942 %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

// A typedef.
#if defined(__gfx942__)
typedef float T;
#else
typedef double T;
#endif
__global__ void k_typedef(int *p) { p[0] = sizeof(T); }
// GFX90A-LABEL: define {{.*}} @_Z9k_typedefPi(
// GFX90A: store i32 8,
// GFX942-LABEL: define {{.*}} @_Z9k_typedefPi(
// GFX942: store i32 4,

// A base class.
#if defined(__gfx942__)
struct Base { int x = 42; };
#else
struct Base { int x = 7; int y = 0; };
#endif
struct D : Base { int z = 1; };
__global__ void k_base(int *p) { D d; p[0] = d.x; p[1] = sizeof(D); }
// GFX90A-LABEL: define {{.*}} @_Z6k_basePi(
// GFX90A: store i32 7,
// GFX90A: store i32 12,
// GFX942-LABEL: define {{.*}} @_Z6k_basePi(
// GFX942: store i32 42,
// GFX942: store i32 8,

// A member of a class every target shares.
struct S {
#if defined(__gfx942__)
  int a;
#endif
  int b;
};
struct Outer { S s; int z; };
__device__ int use(S s) { return s.b; }
__global__ void k_member(int *p) {
  Outer o{};
  o.s.b = 9;
  p[0] = sizeof(Outer);
  p[1] = use(o.s);
}
// GFX90A-LABEL: define {{.*}} @_Z8k_memberPi(
// GFX90A: store i32 8,
// GFX90A: store i32 9,
// GFX942-LABEL: define {{.*}} @_Z8k_memberPi(
// GFX942: store i32 12,
// GFX942: store i32 9,

// A static data member that sizes a specialization.
struct Cfg {
#if defined(__gfx942__)
  static constexpr int tile = 16;
#else
  static constexpr int tile = 8;
#endif
};
template <int N> struct Buf { int d[N]; };
__global__ void k_static(int *p) { Buf<Cfg::tile> b{}; p[0] = sizeof(b); }
// GFX90A-LABEL: define {{.*}} @_Z8k_staticPi(
// GFX90A: store i32 32,
// GFX942-LABEL: define {{.*}} @_Z8k_staticPi(
// GFX942: store i32 64,
