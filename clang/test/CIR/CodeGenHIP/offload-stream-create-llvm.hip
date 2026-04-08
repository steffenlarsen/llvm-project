// Tests that hipStreamCreate / hipStreamDestroy lower to correct LLVM IR.
//
// hipStream_t is a pointer type (ihipStream_t*) in the HIP ABI, so the
// lowered declarations must use 'ptr' arguments, not 'i64'.  This test
// verifies that the hipStreamCreate / hipStreamDestroy → cir.call → LLVM
// path emits correct 'ptr' argument types.
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
// CIR: cir.call @hipStreamCreate
// CIR: cir.call @hipStreamDestroy

// --- LLVM checks ---
// Stream runtime functions must take 'ptr', not 'i64'.
// LLVM: declare {{.*}}i32 @hipStreamCreate(ptr)
// LLVM: declare {{.*}}i32 @hipStreamDestroy(ptr)
// LLVM: call i32 @hipStreamCreate(ptr
// LLVM: call i32 @hipStreamDestroy(ptr
