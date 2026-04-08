// Tests that DPP (Data Parallel Primitives) builtins emit correct CIR
// intrinsic calls: __builtin_amdgcn_mov_dpp, __builtin_amdgcn_update_dpp,
// __builtin_amdgcn_mov_dpp8.
//
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -fclangir \
// RUN:            -aux-triple x86_64-unknown-linux-gnu \
// RUN:            -target-cpu gfx906 \
// RUN:            -x hip -fcuda-is-device -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

// CIR-LABEL: @_Z16test_mov_dpp_inti
// CIR: cir.call_llvm_intrinsic "amdgcn.update.dpp"
__device__ int test_mov_dpp_int(int x) {
  return __builtin_amdgcn_mov_dpp(x, 0x101, 0xf, 0xf, false);
}

// CIR-LABEL: @_Z19test_update_dpp_intii
// CIR: cir.call_llvm_intrinsic "amdgcn.update.dpp"
__device__ int test_update_dpp_int(int old, int x) {
  return __builtin_amdgcn_update_dpp(old, x, 0x101, 0xf, 0xf, false);
}

// CIR-LABEL: @_Z18test_mov_dpp8_uintj
// CIR: cir.call_llvm_intrinsic "amdgcn.mov.dpp8"
__device__ unsigned int test_mov_dpp8_uint(unsigned int x) {
  return __builtin_amdgcn_mov_dpp8(x, 0x00010203);
}
