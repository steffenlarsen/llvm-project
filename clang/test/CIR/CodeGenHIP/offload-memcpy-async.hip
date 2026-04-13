// Tests that hipMemcpyAsync and hipMemsetD32Async are intercepted by
// CIRGenOffloadRuntime and lowered to offload.memcpy/offload.memset with a
// stream operand, and that LowerHostRuntime emits the async HIP variants.
//
// NOTE on hipMemset async limitations:
// HIP does NOT provide hipMemsetAsync (byte pattern) or hipMemsetD16Async.
// When offload.memset has a stream operand and an i8 or i16 pattern,
// LowerHostRuntime silently falls back to the synchronous hipMemset /
// hipMemsetD16 variants, discarding the stream.  Only hipMemsetD32Async is
// available for true async behavior.  This is correct per the HIP API but may
// surprise callers expecting async semantics for byte/i16 fills with a stream.
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
// offload.memcpy with stream operand
// CIR: offload.memcpy {{.*}} dst_space = {{.*}}device{{.*}} src_space = {{.*}}device{{.*}} stream
// offload.memset with stream operand (i32 pattern → D32)
// CIR: offload.memset {{.*}} stream

// --- LLVM checks ---
// hipMemcpyAsync is called (not hipMemcpy) for the async copy.
// LLVM: call i32 @hipMemcpyAsync(
// hipMemsetD32Async is called for the async fill.
// LLVM: call i32 @hipMemsetD32Async(
// Synchronous hipMemcpy must NOT appear for the async call.
// LLVM-NOT: call i32 @hipMemcpy(
