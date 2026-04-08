// Verify that AMDGPU builtins returning bool (e.g., __builtin_amdgcn_is_shared)
// produce !cir.bool in CIR, not !cir.int<u, 1>.  The LLVM intrinsic fallback
// path uses IIT descriptors that create i1 integers; correctIntegerSignedness
// must map C++ bool to !cir.bool so cir.if conditions type-check.

// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -x hip -fclangir \
// RUN:            -target-cpu gfx90a -fcuda-is-device \
// RUN:            -emit-cir %s -o - \
// RUN:   | FileCheck %s

#define __device__ __attribute__((device))

// CHECK-LABEL: cir.func {{.*}}@_Z14test_is_sharedPf
__device__ void test_is_shared(float *p) {
  // The result of __builtin_amdgcn_is_shared should be !cir.bool.
  // CHECK: cir.if
  if (__builtin_amdgcn_is_shared(p)) {
    *p = 1.0f;
  }
}
