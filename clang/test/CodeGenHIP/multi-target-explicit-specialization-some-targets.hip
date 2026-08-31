// An explicit specialization, or an explicit specialization of a member of a
// class specialization, that only some targets declare is theirs. The other
// targets get an implicit instantiation of the template, and code naming the
// specialization is analysed for each target. Specializations every target
// declares, one per arm, stay as they were.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc
// RUN: FileCheck --check-prefix=HOST --input-file=%t.host.ll %s
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefixes=CHECK,GFX90A %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefixes=CHECK,GFX942 %s

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

template <class T> __device__ int fn() { return 1; }
#if defined(__gfx942__)
template <> __device__ int fn<int>() { return 2; }
#endif
template <class T> __device__ int call_fn() { return fn<T>(); }
// CHECK-LABEL: define {{.*}}@function(
// GFX90A: store i32 2,
// GFX942: store i32 4,
extern "C" __global__ void function(int *p) { *p = fn<int>() + call_fn<int>(); }

template <class T> struct Cls { static constexpr int v = 1; int a; };
#if defined(__gfx942__)
template <> struct Cls<int> { static constexpr int v = 2; long a, b; };
#endif
// CHECK-LABEL: define {{.*}}@class_value_and_layout(
// GFX90A: store i32 5,
// GFX942: store i32 18,
extern "C" __global__ void class_value_and_layout(int *p) {
  Cls<int> c;
  *p = Cls<int>::v + sizeof(c);
}

template <class T> struct HostOnly { static constexpr int v = 1; };
#if !defined(__HIP_DEVICE_COMPILE__)
template <> struct HostOnly<int> { static constexpr int v = 2; };
#endif
__host__ __device__ int host_only() { return HostOnly<int>::v; }
// HOST-LABEL: define {{.*}}@host_use(
// HOST: ret i32 2
extern "C" int host_use() { return host_only(); }
// CHECK-LABEL: define {{.*}}@host_only_specialization(
// CHECK: store i32 1,
extern "C" __global__ void host_only_specialization(int *p) {
  *p = host_only();
}

template <class T> struct Member { __device__ int f() { return 1; } };
#if defined(__gfx942__)
template <> __device__ int Member<int>::f() { return 2; }
#endif
// CHECK-LABEL: define {{.*}}@member(
// GFX90A: store i32 1,
// GFX942: store i32 2,
extern "C" __global__ void member(int *p) {
  Member<int> m;
  *p = m.f();
}

template <class T> struct Both { static constexpr int v = 1; };
#if defined(__gfx942__)
template <> struct Both<int> { static constexpr int v = 2; };
#else
template <> struct Both<int> { static constexpr int v = 3; };
#endif
// CHECK-LABEL: define {{.*}}@every_target(
// GFX90A: store i32 3,
// GFX942: store i32 2,
extern "C" __global__ void every_target(int *p) { *p = Both<int>::v; }

// Each device gets its own kernel for the host's launch.
template <class T> __global__ void kern(int *p) { *p = 1; }
#if defined(__gfx942__)
template <> __global__ void kern<int>(int *p) { *p = 2; }
#endif
// CHECK-LABEL: define {{.*}}@_Z4kernIiEvPi(
// GFX90A: store i32 1,
// GFX942: store i32 2,
void launch(int *p) {
  kern<int><<<1, 1>>>(p);
}
