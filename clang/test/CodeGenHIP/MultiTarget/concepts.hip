// A concept each device architecture defines differently is satisfied as each
// target's copy defines it: in overload resolution between constrained
// function templates and member templates, and in choosing a constrained
// partial specialization. A concept every target shares, evaluated for a type
// the targets lay out differently, is satisfied per target too.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -target-cpu gfx942 -O2" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefixes=CHECK,GFX90A %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefixes=CHECK,GFX942 %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__gfx942__)
#define LIMIT 2
#else
#define LIMIT 8
#endif
template <class T> concept Big = sizeof(T) > LIMIT;

template <class T> __device__ int pick(T) requires Big<T> { return 1; }
template <class T> __device__ int pick(T) requires(!Big<T>) { return 2; }
// CHECK-LABEL: define {{.*}} @_Z8functionPi(
// GFX90A: store i32 2,
// GFX942: store i32 1,
__global__ void function(int *p) { *p = pick(1); }

template <class T> struct Sel { static constexpr int v = 2; };
template <Big T> struct Sel<T> { static constexpr int v = 1; };
// CHECK-LABEL: define {{.*}} @_Z7partialPi(
// GFX90A: store i32 2,
// GFX942: store i32 1,
__global__ void partial(int *p) { *p = Sel<int>::v; }

struct S {
  int x;
  template <Big T> __device__ int pick(T) { return 1; }
  template <class T> __device__ int pick(T) { return 2; }
};
// CHECK-LABEL: define {{.*}} @_Z6memberPi(
// GFX90A: store i32 2,
// GFX942: store i32 1,
__global__ void member(int *p) {
  S s{};
  *p = s.pick(1);
}

template <class T> concept Wide = sizeof(T) > 8;
template <class T> __device__ int width(T) requires Wide<T> { return 1; }
template <class T> __device__ int width(T) requires(!Wide<T>) { return 2; }
// CHECK-LABEL: define {{.*}} @_Z6layoutPi(
// CHECK: store i32 2,
__global__ void layout(int *p) { *p = width(1.0L); }
