// Tests that linkonce_odr comdat symbols from C++ template instantiations
// compile correctly through the CIR offload pipeline.  Previously, the
// CIR→LLVM lowering created duplicate ComdatSelectorOp entries when the
// gpu.module pass and the host module pass both tried to create comdat
// regions for the same symbol name.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/../CodeGenCUDA/Inputs/ \
// RUN:            -internal-isystem %clang_resource_dir/include \
// RUN:            -aux-triple amdgcn-amd-amdhsa -clangir-offload-arch=gfx906 \
// RUN:            -emit-llvm %s -o - \
// RUN:   | FileCheck %s

#include "cuda.h"

// Template class with static member — generates linkonce_odr comdat global.
template <typename T>
struct Constants {
  static constexpr T value = T(42);
};

__global__ void kernel(float* out) {
  out[0] = Constants<float>::value;
}

int main() {
  float x = Constants<float>::value;
  kernel<<<1, 1>>>(nullptr);
  return (int)x;
}

// The full pipeline should compile without comdat duplication errors.
// CHECK: @offload_device_module_binary = internal constant
// CHECK: define{{.*}}i32 @main(
