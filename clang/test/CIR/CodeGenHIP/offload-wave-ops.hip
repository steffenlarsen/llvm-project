// Tests that AMDGPU wave/warp builtins are lowered to GPU dialect ops in CIR.
//
// The CIR emitter intercepts each builtin in CIRGenBuiltinAMDGPU.cpp and emits:
//   __builtin_amdgcn_wave_reduce_*  → gpu.subgroup_reduce
//   __builtin_amdgcn_readlane       → gpu.subgroup_broadcast ... lane
//   __builtin_amdgcn_readfirstlane  → gpu.subgroup_broadcast ... first_active_lane
//   __builtin_amdgcn_ballot_w32/64  → gpu.ballot
//
// These GPU dialect ops are lowered to ROCDL intrinsics by the existing
// GPUToROCDL conversion pass.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx906 \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

// --- Wave reduce: add -------------------------------------------------------

__device__ unsigned wave_add(unsigned val) {
  return __builtin_amdgcn_wave_reduce_add_u32(val, 0);
}

// CIR-LABEL: offload.func @_Z8wave_addj
// CIR: gpu.subgroup_reduce add

// --- Wave reduce: min/max (signed + unsigned) --------------------------------

__device__ int wave_min_i(int val) {
  return __builtin_amdgcn_wave_reduce_min_i32(val, 0);
}

// CIR-LABEL: offload.func @_Z10wave_min_ii
// CIR: gpu.subgroup_reduce minsi

__device__ unsigned wave_max_u(unsigned val) {
  return __builtin_amdgcn_wave_reduce_max_u32(val, 0);
}

// CIR-LABEL: offload.func @_Z10wave_max_uj
// CIR: gpu.subgroup_reduce maxui

// --- Wave reduce: and/or/xor ------------------------------------------------

__device__ unsigned wave_and(unsigned val) {
  return __builtin_amdgcn_wave_reduce_and_b32(val, 0);
}

// CIR-LABEL: offload.func @_Z8wave_andj
// CIR: gpu.subgroup_reduce and

// --- Readlane / readfirstlane -----------------------------------------------

__device__ unsigned readlane_fn(unsigned val, unsigned lane) {
  return __builtin_amdgcn_readlane(val, lane);
}

// CIR-LABEL: offload.func @_Z11readlane_fnjj
// CIR: gpu.subgroup_broadcast {{.*}} specific_lane

__device__ unsigned readfirstlane_fn(unsigned val) {
  return __builtin_amdgcn_readfirstlane(val);
}

// CIR-LABEL: offload.func @_Z16readfirstlane_fnj
// CIR: gpu.subgroup_broadcast {{.*}} first_active_lane

// --- Ballot -----------------------------------------------------------------

__device__ unsigned long long ballot64(bool pred) {
  return __builtin_amdgcn_ballot_w64(pred);
}

// CIR-LABEL: offload.func @_Z8ballot64b
// CIR: gpu.ballot {{.*}} : i64

__device__ unsigned ballot32(bool pred) {
  return __builtin_amdgcn_ballot_w32(pred);
}

// CIR-LABEL: offload.func @_Z8ballot32b
// CIR: gpu.ballot {{.*}} : i32
