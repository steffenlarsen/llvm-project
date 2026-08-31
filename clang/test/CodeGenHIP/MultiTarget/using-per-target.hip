// A using-declaration or using-directive one device architecture has its own
// of means what that target's names. Copies naming different entities or
// namespaces are kept apart, a namespace is searched for the targets whose
// directive nominates it, directly or through another namespace's, and code
// whose lookup that changes is analysed for each target.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942 -O2" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefixes=CHECK,GFX90A %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefixes=CHECK,GFX942 %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

namespace A {
__device__ long f(long) { return 1; }
__device__ int g() { return 1; }
}
namespace B {
__device__ int f(int) { return 2; }
__device__ int g() { return 2; }
}

#if defined(__gfx942__)
using A::f;
#else
using B::f;
#endif
// CHECK-LABEL: define {{.*}}@using_declaration(
// GFX90A: store i32 6,
// GFX942: store i32 9,
extern "C" __global__ void using_declaration(int *p) {
  *p = f(3) + sizeof(f(3));
}

namespace Directive {
#if defined(__gfx942__)
using namespace A;
#else
using namespace B;
#endif
__device__ int use() { return g(); }
}
// CHECK-LABEL: define {{.*}}@using_directive(
// GFX90A: store i32 2,
// GFX942: store i32 1,
extern "C" __global__ void using_directive(int *p) { *p = Directive::use(); }

namespace OneTarget {
namespace C { __device__ long h(long) { return 1; } }
#if defined(__gfx942__)
using namespace C;
#endif
__device__ int h(int) { return 2; }
__device__ int use() { return h(3L) + sizeof(h(3L)); }
}
// CHECK-LABEL: define {{.*}}@directive_one_target(
// GFX90A: store i32 6,
// GFX942: store i32 9,
extern "C" __global__ void directive_one_target(int *p) {
  *p = OneTarget::use();
}

namespace C2 { __device__ int q() { return 1; } }
namespace C3 { __device__ int q() { return 2; } }
namespace Qualified {
#if defined(__gfx942__)
using namespace C2;
#else
using namespace C3;
#endif
}
// CHECK-LABEL: define {{.*}}@qualified_through_directive(
// GFX90A: store i32 2,
// GFX942: store i32 1,
extern "C" __global__ void qualified_through_directive(int *p) {
  *p = Qualified::q();
}

namespace Transitive {
namespace D { __device__ long t(long) { return 1; } }
namespace M { using namespace D; }
#if defined(__gfx942__)
using namespace M;
#endif
__device__ int t(int) { return 2; }
__device__ int use() { return t(3L) + sizeof(t(3L)); }
}
// CHECK-LABEL: define {{.*}}@transitive_directive(
// GFX90A: store i32 6,
// GFX942: store i32 9,
extern "C" __global__ void transitive_directive(int *p) {
  *p = Transitive::use();
}

namespace E { __device__ long b(long) { return 1; } }
__device__ int b(int) { return 2; }
// CHECK-LABEL: define {{.*}}@block_scope_directive(
// GFX90A: store i32 6,
// GFX942: store i32 9,
extern "C" __global__ void block_scope_directive(int *p) {
#if defined(__gfx942__)
  using namespace E;
#endif
  *p = b(3L) + sizeof(b(3L));
}

namespace Inline {
#if defined(__gfx942__)
inline namespace v2 { __device__ int i() { return 42; } }
#else
inline namespace v1 { __device__ int i() { return 7; } }
#endif
}
// CHECK-LABEL: define {{.*}}@inline_namespace(
// GFX90A: store i32 7,
// GFX942: store i32 42,
extern "C" __global__ void inline_namespace(int *p) { *p = Inline::i(); }

// A specialization instantiated from a partial specialization has its
// member using-declarations from that partial specialization's copy.
template <class T> struct TA { __device__ long f(long) { return 1; } };
template <class T> struct TB { __device__ int f(int) { return 2; } };
template <class T, class U> struct Partial {};
#if defined(__gfx942__)
template <class T> struct Partial<T, int> : TA<T>, TB<T> { using TA<T>::f; };
#else
template <class T> struct Partial<T, int> : TA<T>, TB<T> { using TB<T>::f; };
#endif
// CHECK-LABEL: define {{.*}}@partial_specialization_using(
// GFX90A: store i32 6,
// GFX942: store i32 9,
extern "C" __global__ void partial_specialization_using(int *p) {
  Partial<int, int> d;
  *p = d.f(3) + sizeof(d.f(3));
}

template <class T, class U> struct PartialInner {};
template <class T> struct PartialInner<T, int> : TA<T>, TB<T> {
#if defined(__gfx942__)
  using TA<T>::f;
#else
  using TB<T>::f;
#endif
};
// CHECK-LABEL: define {{.*}}@partial_specialization_using_inner(
// GFX90A: store i32 6,
// GFX942: store i32 9,
extern "C" __global__ void partial_specialization_using_inner(int *p) {
  PartialInner<int, int> d;
  *p = d.f(3) + sizeof(d.f(3));
}
