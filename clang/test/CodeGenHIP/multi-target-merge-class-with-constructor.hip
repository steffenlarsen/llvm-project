// The targets' copies of a class that are the same are merged into one,
// including when the class declares a constructor: its name is the class's
// type, which each copy has its own of. Kept apart, each copy has its own
// specializations, and device code that meets two of them for the same
// arguments compares calls and definitions of two different types (rocThrust's
// transform_pair_of_input_iterators_t, whose always_inline operator+ was then
// not inlined).
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o %t.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc \
// RUN: -mllvm -dump-decl-fingerprints=0 2>&1 \
// RUN: | FileCheck %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

template <class T> struct it {
  T *p;
  __host__ __device__ it(T *p) : p(p) {}
  __host__ __device__ T operator*() const { return *p; }
#if defined(__HIP_DEVICE_COMPILE__)
  static_assert(sizeof(T) > 0, "device");
#else
  static_assert(sizeof(T) > 0, "host");
#endif
};

__global__ void k(int *o, int *a) { o[0] = *it<int>(a); }
int h(int *a) { return *it<int>(a); }

// CHECK-COUNT-1: {{^}}ClassTemplate{{[[:space:]]}}it{{[[:space:]]}}
// CHECK-NOT: {{^}}ClassTemplate{{[[:space:]]}}it{{[[:space:]]}}
// CHECK-COUNT-1: {{^}}ClassTemplateSpecialization{{[[:space:]]}}it<int>
// CHECK-NOT: {{^}}ClassTemplateSpecialization{{[[:space:]]}}it<int>
