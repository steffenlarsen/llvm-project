// Tests that a stream-aware kernel launch uses the HIP-native registration
// path (hipLaunchKernel with compile-time kernel handle, not mgpuLaunchKernel).
//
// The stream is passed as a function parameter to verify that even
// stream-annotated launches go through the HIP-native launch path.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs \
// RUN:            -aux-triple amdgcn-amd-amdhsa -clangir-offload-arch=gfx906 \
// RUN:            -emit-llvm %s -o - \
// RUN:   | FileCheck --check-prefix=LLVM %s

#include "hip_runtime_minimal.h"

__global__ void kernel_stream(int n) {}

// Pass stream as a parameter to avoid offload.stream_create in this function.
__host__ void launch_with_stream(hipStream_t stream) {
  kernel_stream<<<1, 64, 0, stream>>>(42);
}

// --- CIR checks ---
// The kernel launch emits gpu.launch_func.
// CIR: gpu.launch_func @offload_device_module::@_Z13kernel_streami

// --- LLVM checks ---
// The stream is passed directly as the last argument to hipLaunchKernel.
// LLVM: call {{.*}} @hipLaunchKernel({{.*}}, ptr %
// The kernel handle is a compile-time constant global (no string lookup).
// LLVM: @"offload_device_module__Z13kernel_streami{{[^"]*}}_handle"
// mgpuModuleGetFunction must NOT appear (no per-launch string lookup).
// LLVM-NOT: call {{.*}} @mgpuModuleGetFunction(
// mgpuLaunchKernel must NOT appear.
// LLVM-NOT: call {{.*}} @mgpuLaunchKernel(
