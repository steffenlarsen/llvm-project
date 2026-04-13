// Tests that a stream-aware kernel launch uses mgpuModuleGetFunction +
// mgpuLaunchKernel (not hipLaunchKernel), and that the user-supplied stream
// is forwarded to mgpuLaunchKernel rather than a freshly created stream.
//
// The stream is passed as a function parameter so that the test exercises
// the stream-forwarding path without also exercising offload.stream_create
// (which is covered by offload-stream-create-llvm.hip).
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs \
// RUN:            -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx906 \
// RUN:            -emit-llvm %s -o - \
// RUN:   | FileCheck --check-prefix=LLVM %s

#include "hip_runtime_minimal.h"

__global__ void kernel_stream(int n) {}

// Pass stream as a parameter to avoid offload.stream_create in this function.
__host__ void launch_with_stream(hipStream_t stream) {
  kernel_stream<<<1, 64, 0, stream>>>(42);
}

// --- CIR checks ---
// The kernel_launch op carries a stream operand.
// CIR: offload.kernel_launch {{.*}} stream

// --- LLVM checks ---
// mgpuModuleGetFunction is called to look up the kernel by name.
// LLVM: call ptr @mgpuModuleGetFunction(
// mgpuLaunchKernel is called (not hipLaunchKernel), and the function's stream
// parameter (first argument, ptr %0) is forwarded as the stream arg.
// LLVM: call void @mgpuLaunchKernel({{.*}}, ptr %0,
// hipLaunchKernel must NOT appear.
// LLVM-NOT: call {{.*}} @hipLaunchKernel(
