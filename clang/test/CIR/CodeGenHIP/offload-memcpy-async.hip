// Tests that hipMemcpyAsync and hipMemsetD32Async are intercepted by
// CIRGenOffloadRuntime and lowered to direct cir.call ops to the HIP runtime,
// and that ConvertCIRToLLVMPass emits the corresponding HIP API calls.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-llvm %s -o - \
// RUN:   | FileCheck --check-prefix=LLVM %s

#include "hip_runtime_minimal.h"

__host__ void async_ops(hipStream_t stream, void *dst, void *src) {
  // Async device-to-device copy with stream.
  hipMemcpyAsync(dst, src, 1024, hipMemcpyDeviceToDevice, stream);

  // Async 32-bit memset with stream.
  hipMemsetD32Async((hipDeviceptr_t)dst, 0xdeadbeef, 256, stream);
}

// --- CIR checks ---
// hipMemcpyAsync lowers to cir.call
// CIR: cir.call @hipMemcpyAsync
// hipMemsetD32Async lowers to cir.call
// CIR: cir.call @hipMemsetD32Async

// --- LLVM checks ---
// hipMemcpyAsync is called (not hipMemcpy) for the async copy.
// LLVM: call i32 @hipMemcpyAsync(
// hipMemsetD32Async is called for the async fill.
// LLVM: call i32 @hipMemsetD32Async(
// Synchronous hipMemcpy must NOT appear for the async call.
// LLVM-NOT: call i32 @hipMemcpy(
