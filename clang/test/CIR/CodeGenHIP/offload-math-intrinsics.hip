// Verify that CIR math ops inside GPU device code compile successfully
// through the offload pipeline.  Previously, CIR lowered math ops (exp,
// floor, ceil, etc.) to LLVM intrinsics which the ROCDL pass marked illegal.
// The fix emits __ocml_* library calls directly during gpu-module lowering.

// REQUIRES: amdgpu-registered-target

// RUN: %clang -fclangir --offload-arch=gfx90a -x hip -c %s \
// RUN:   -I%S/Inputs -o /dev/null

#include "hip_runtime_minimal.h"

__global__ void test_exp64(double *out, double x) {
  *out = __builtin_exp(x);
}

__global__ void test_exp32(float *out, float x) {
  *out = __builtin_expf(x);
}

__global__ void test_floor(double *out, double x) {
  *out = __builtin_floor(x);
}

__global__ void test_ceil(float *out, float x) {
  *out = __builtin_ceilf(x);
}

__global__ void test_log2(double *out, double x) {
  *out = __builtin_log2(x);
}

__global__ void test_sin(float *out, float x) {
  *out = __builtin_sinf(x);
}

__global__ void test_cos(float *out, float x) {
  *out = __builtin_cosf(x);
}
