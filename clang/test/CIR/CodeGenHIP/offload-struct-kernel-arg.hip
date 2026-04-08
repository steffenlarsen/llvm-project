// Tests that struct-by-value kernel arguments are correctly emitted when
// using the CIR offload path.  Previously, emitScalarExpr was used for all
// kernel args, which crashed on CXXConstructExpr (implicit copy constructor)
// for struct arguments.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/../CodeGenCUDA/Inputs/ -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/../CodeGenCUDA/Inputs/ \
// RUN:            -internal-isystem %clang_resource_dir/include \
// RUN:            -aux-triple amdgcn-amd-amdhsa -clangir-offload-arch=gfx906 \
// RUN:            -emit-llvm %s -o - \
// RUN:   | FileCheck --check-prefix=LLVM %s

#include "cuda.h"

struct Params {
  int x;
  float y;
};

__global__ void kernel_with_struct(Params p, int n) {}

int main() {
  Params p;
  p.x = 1;
  p.y = 2.0f;
  kernel_with_struct<<<1, 1>>>(p, 42);
  return 0;
}

// Verify the kernel launch emits a gpu.launch_func with the struct arg.
// CIR: gpu.launch_func @offload_device_module::@
// CIR-SAME: args(

// Verify that full lowering to LLVM IR succeeds (no crash from CXXConstructExpr).
// LLVM: define{{.*}}i32 @main(
// LLVM:   call {{.*}} @hipLaunchKernel(
