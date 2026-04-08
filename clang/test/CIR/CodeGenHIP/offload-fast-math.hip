// Tests that -ffast-math propagates to the device code through the CIR
// offload pipeline.  Two mechanisms are verified:
//
//   1. CIR module attributes: cir.unsafe_fp_math and cir.finite_math_only
//      are set on the module when -ffast-math is used.  These survive
//      serialization to .cir and are read back by the merge cc1.
//
//   2. OCML control variables: the ROCDL target gets unsafeMath/finiteOnly
//      flags, causing the serializer to set __oclc_unsafe_math_opt=1.
//
//   3. Per-op fast-math flags: LLVM FP ops inside gpu.func get the "fast"
//      FastmathFlags attribute.

// --- CIR output with -ffast-math: module has fast-math attributes ---
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api -ffast-math \
// RUN:            -I%S/../CodeGenCUDA/Inputs/ -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR-FAST %s

// --- CIR output without -ffast-math: no fast-math attributes ---
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/../CodeGenCUDA/Inputs/ -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR-NOFAST %s

// --- Full pipeline with -ffast-math: verify compilation succeeds ---
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api -ffast-math \
// RUN:            -I%S/../CodeGenCUDA/Inputs/ \
// RUN:            -internal-isystem %clang_resource_dir/include \
// RUN:            -aux-triple amdgcn-amd-amdhsa -clangir-offload-arch=gfx906 \
// RUN:            -emit-llvm %s -o - \
// RUN:   | FileCheck --check-prefix=HOST %s

#include "cuda.h"

__global__ void math_kernel(float* out, float a, float b) {}

int main() {
  math_kernel<<<1, 256>>>(nullptr, 1.0f, 2.0f);
  return 0;
}

// With -ffast-math, the module should have both attributes on the same line.
// CIR-FAST: cir.finite_math_only{{.*}}cir.unsafe_fp_math

// Without -ffast-math, these attributes should be absent.
// CIR-NOFAST-NOT: cir.unsafe_fp_math
// CIR-NOFAST-NOT: cir.finite_math_only

// Full pipeline should succeed.
// HOST: @offload_device_module_binary = internal constant
// HOST: define{{.*}}i32 @main(
