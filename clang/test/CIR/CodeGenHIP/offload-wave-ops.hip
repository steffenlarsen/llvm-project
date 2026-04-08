// Tests that AMDGPU wave/warp builtins are lowered to CIR offload ops.
//
// The CIR emitter intercepts each builtin in CIRGenBuiltinAMDGPU.cpp and emits:
//   __builtin_amdgcn_wave_reduce_*  -> cir.offload.subgroup_reduce
//   __builtin_amdgcn_readlane       -> cir.call_llvm_intrinsic "amdgcn.readlane"
//   __builtin_amdgcn_readfirstlane  -> cir.call_llvm_intrinsic "amdgcn.readfirstlane"
//   __builtin_amdgcn_ballot_w32/64  -> cir.offload.ballot
//
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -fclangir \
// RUN:            -aux-triple x86_64-unknown-linux-gnu \
// RUN:            -target-cpu gfx906 \
// RUN:            -x hip -fcuda-is-device -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

// --- Wave reduce: add -------------------------------------------------------

__device__ unsigned wave_add(unsigned val) {
  return __builtin_amdgcn_wave_reduce_add_u32(val, 0);
}

// CIR-LABEL: cir.func {{.*}}@_Z8wave_addj
// CIR: cir.offload.subgroup_reduce <add>

// --- Wave reduce: min/max (signed + unsigned) --------------------------------

__device__ int wave_min_i(int val) {
  return __builtin_amdgcn_wave_reduce_min_i32(val, 0);
}

// CIR-LABEL: cir.func {{.*}}@_Z10wave_min_ii
// CIR: cir.offload.subgroup_reduce <minsi>

__device__ unsigned wave_max_u(unsigned val) {
  return __builtin_amdgcn_wave_reduce_max_u32(val, 0);
}

// CIR-LABEL: cir.func {{.*}}@_Z10wave_max_uj
// CIR: cir.offload.subgroup_reduce <maxui>

// --- Wave reduce: and/or/xor ------------------------------------------------

__device__ unsigned wave_and(unsigned val) {
  return __builtin_amdgcn_wave_reduce_and_b32(val, 0);
}

// CIR-LABEL: cir.func {{.*}}@_Z8wave_andj
// CIR: cir.offload.subgroup_reduce <and>

// --- Readlane / readfirstlane -----------------------------------------------

__device__ unsigned readlane_fn(unsigned val, unsigned lane) {
  return __builtin_amdgcn_readlane(val, lane);
}

// CIR-LABEL: cir.func {{.*}}@_Z11readlane_fnjj
// CIR: cir.call_llvm_intrinsic "amdgcn.readlane"

__device__ unsigned readfirstlane_fn(unsigned val) {
  return __builtin_amdgcn_readfirstlane(val);
}

// CIR-LABEL: cir.func {{.*}}@_Z16readfirstlane_fnj
// CIR: cir.call_llvm_intrinsic "amdgcn.readfirstlane"

// --- Ballot -----------------------------------------------------------------

__device__ unsigned long long ballot64(bool pred) {
  return __builtin_amdgcn_ballot_w64(pred);
}

// CIR-LABEL: cir.func {{.*}}@_Z8ballot64b
// CIR: cir.offload.ballot {{.*}} : !u64i

__device__ unsigned ballot32(bool pred) {
  return __builtin_amdgcn_ballot_w32(pred);
}

// CIR-LABEL: cir.func {{.*}}@_Z8ballot32b
// CIR: cir.offload.ballot {{.*}} : !u32i
