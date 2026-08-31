// Copies of a declaration that compare structurally equal yet mean something
// else per target: a namespace alias naming another namespace, a constructor
// explicit on one target, a member class template declaring other members, and
// a member function template with another default argument. Merged or bound
// as the host's, gfx942 used the other targets' meaning.
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

namespace A { __device__ int f() { return 1; } }
namespace B { __device__ int f() { return 2; } }
#if defined(__gfx942__)
namespace X = A;
#else
namespace X = B;
#endif

// OTHER-LABEL: define {{.*}} @_Z5aliasPi(
// OTHER: store i32 2,
// GFX942-LABEL: define {{.*}} @_Z5aliasPi(
// GFX942: store i32 1,
__global__ void alias(int *p) { *p = X::f(); }

struct E {
  int v;
#if defined(__gfx942__)
  __device__ explicit E(int) : v(1) {}
#else
  __device__ E(int) : v(1) {}
#endif
  __device__ E(long) : v(2) {}
};

// OTHER-LABEL: define {{.*}} @_Z13explicit_ctorPi(
// OTHER: store i32 1,
// GFX942-LABEL: define {{.*}} @_Z13explicit_ctorPi(
// GFX942: store i32 2,
__global__ void explicit_ctor(int *p) { E e = 5; *p = e.v; }

struct C {
#if defined(__gfx942__)
  template <class T> struct In { T a, b; };
#else
  template <class T> struct In { T a; };
#endif
};
template <class T> __device__ int size() {
  return sizeof(typename C::template In<T>);
}

// OTHER-LABEL: define {{.*}} @_Z12member_classPi(
// OTHER: store i32 4,
// GFX942-LABEL: define {{.*}} @_Z12member_classPi(
// GFX942: store i32 8,
__global__ void member_class(int *p) { *p = size<int>(); }

struct M {
#if defined(__gfx942__)
  template <int N = 9> __device__ int g() { return N; }
#else
  template <int N = 4> __device__ int g() { return N; }
#endif
};

// OTHER-LABEL: define {{.*}} @_Z11default_argPi(
// OTHER: store i32 4,
// GFX942-LABEL: define {{.*}} @_Z11default_argPi(
// GFX942: store i32 9,
__global__ void default_arg(int *p) { M m; *p = m.g(); }
