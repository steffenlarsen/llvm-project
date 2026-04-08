// Tests that AMDGPU buffer load/store builtins and make_buffer_rsrc are
// lowered to cir.call_llvm_intrinsic ops, and that __amdgpu_buffer_rsrc_t
// is handled as a pointer in address space 8.
//
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -fclangir \
// RUN:            -aux-triple x86_64-unknown-linux-gnu \
// RUN:            -target-cpu gfx942 \
// RUN:            -x hip -fcuda-is-device -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

// --- __builtin_amdgcn_make_buffer_rsrc --------------------------------------

__device__ __amdgpu_buffer_rsrc_t test_make_rsrc(void *ptr) {
  return __builtin_amdgcn_make_buffer_rsrc(ptr, 0, 1024, 0);
}

// CIR-LABEL: cir.func {{.*}}@_Z14test_make_rsrcPv
// CIR: cir.call_llvm_intrinsic "amdgcn.make.buffer.rsrc"

// --- __builtin_amdgcn_raw_buffer_load_b32 -----------------------------------

__device__ unsigned test_buffer_load_b32(__amdgpu_buffer_rsrc_t rsrc) {
  return __builtin_amdgcn_raw_buffer_load_b32(rsrc, 0, 0, 0);
}

// CIR-LABEL: cir.func {{.*}}@_Z20test_buffer_load_b32
// CIR: cir.call_llvm_intrinsic "amdgcn.raw.ptr.buffer.load"

// --- __builtin_amdgcn_raw_buffer_store_b32 ----------------------------------

__device__ void test_buffer_store_b32(__amdgpu_buffer_rsrc_t rsrc, unsigned val) {
  __builtin_amdgcn_raw_buffer_store_b32(val, rsrc, 0, 0, 0);
}

// CIR-LABEL: cir.func {{.*}}@_Z21test_buffer_store_b32
// CIR: cir.call_llvm_intrinsic "amdgcn.raw.ptr.buffer.store"
