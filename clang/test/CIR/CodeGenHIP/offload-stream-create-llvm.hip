// Tests that hipStreamCreate / hipStreamDestroy lower to correct LLVM IR.
//
// hipStream_t is a pointer type (ihipStream_t*) in the HIP ABI, so the
// lowered declarations must use 'ptr' arguments, not 'i64'.  This test
// also verifies that the offload.stream_create → CIR → LLVM path no longer
// crashes in ConvertCIRToLLVMPass (the crash occurred when !offload.stream
// was mapped to i64, leaving a !llvm.ptr → i64 gap the cast-reconciliation
// step could not auto-synthesize).
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx906 \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx906 \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-llvm %s -o - \
// RUN:   | FileCheck --check-prefix=LLVM %s

#include "hip_runtime_minimal.h"

__host__ void test_stream_create_destroy() {
  hipStream_t s;
  hipStreamCreate(&s);
  hipStreamDestroy(s);
}

// --- CIR checks ---
// CIR: offload.stream_create
// CIR: offload.stream_destroy

// --- LLVM checks ---
// Stream runtime functions must take 'ptr', not 'i64'.
// LLVM: declare i32 @hipStreamCreate(ptr)
// LLVM: declare i32 @hipStreamDestroy(ptr)
// LLVM: call i32 @hipStreamCreate(ptr
// LLVM: call i32 @hipStreamDestroy(ptr
