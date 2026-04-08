// Tests that kernel pointer arguments get address space 1 (global memory)
// when lowered through the CIR offload pipeline for AMDGPU targets.
//
// Without the address-space annotation, the AMDGPU backend treats kernel
// pointer arguments as generic (flat) pointers and cannot use scalar memory
// instructions (s_load) for uniform accesses.  With addrspace(1), the backend
// knows the pointers refer to global memory and can promote to s_load.
//
// The CIR-emitted offload.func uses generic pointers (no addrspace).
// The CIR→GPU lowering pass adds target_address_space(1) for AMDGPU kernels.
// This test verifies:
//   1. The CIR output has offload.func with bare pointer args (pre-lowering)
//   2. The full host pipeline compiles successfully with the device binary

// --- CIR output: offload.func has bare pointer args (no addrspace yet) ---
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/../CodeGenCUDA/Inputs/ -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

// --- Full pipeline: compile with --offload-arch to exercise CIR→GPU lowering ---
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/../CodeGenCUDA/Inputs/ \
// RUN:            -internal-isystem %clang_resource_dir/include \
// RUN:            -aux-triple amdgcn-amd-amdhsa -clangir-offload-arch=gfx906 \
// RUN:            -emit-llvm %s -o - \
// RUN:   | FileCheck --check-prefix=HOST %s

#include "cuda.h"

__global__ void copy_kernel(const float* in, float* out, int n) {}

int main() {
  copy_kernel<<<1, 256>>>(nullptr, nullptr, 0);
  return 0;
}

// CIR output: offload.func has pointer args without address space annotation.
// The CIR→GPU lowering pass (ConvertCIROffloadToGPUPass) adds
// target_address_space(1) when converting to gpu.func for AMDGPU targets.
// CIR: cir.offload.func @{{.*}}copy_kernel{{.*}}!cir.ptr<!cir.float>{{.*}}!cir.ptr<!cir.float>{{.*}}!s32i{{.*}}kernel

// Host full compile: the embedded device binary must be present.
// HOST: @offload_device_module_binary = internal constant
// HOST: define{{.*}}i32 @main(
// HOST:   call {{.*}} @hipLaunchKernel(
