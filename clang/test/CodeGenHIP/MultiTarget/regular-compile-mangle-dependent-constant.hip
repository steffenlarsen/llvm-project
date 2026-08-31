// A constant named in a dependent signature is mangled as written in a
// compilation for one target, which has no per-target values to look up.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o - %s | FileCheck %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))

constexpr int C = 4;
struct A {
  template <int> static __host__ __device__ int g() { return 0; }
};
template <typename T>
__host__ __device__ auto f(T) -> decltype(T::template g<C>()) {
  return 0;
}
__host__ __device__ int h() { return f(A()); }

// CHECK: define {{.*}}@_Z1fI1AEDTclsrT_1gIL_ZL1CEEEES1_(
