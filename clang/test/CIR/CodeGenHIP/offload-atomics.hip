// Tests that AMDGPU atomic builtins are lowered to cir.atomic.fetch ops
// in CIR device code.
//
// The CIR emitter intercepts each builtin in CIRGenBuiltinAMDGPU.cpp and
// emits cir.atomic.fetch with the appropriate binary opcode (add/min/max for
// float variants, uinc_wrap/udec_wrap for wrapping increment/decrement).
// These ops are well-formed CIR that survive all CIR-to-CIR passes and are
// eventually lowered to the AMDGPU hardware instructions by the standard
// CIR-to-LLVM lowering path.
//
// The aux-triple / aux-target-cpu flags are required so that the AMDGPU
// builtins are recognised by the parser even though the primary triple is x86.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx906 \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

// --- Global atomic float add ------------------------------------------------

__device__ float global_fadd(float *ptr, float val) {
  return __builtin_amdgcn_global_atomic_fadd_f32(ptr, val);
}

// CIR-LABEL: offload.func @_Z11global_faddPff
// CIR: cir.atomic.fetch add seq_cst

// --- DS (shared memory) atomic float add ------------------------------------

__device__ float ds_fadd(float *ptr, float val) {
  return __builtin_amdgcn_ds_faddf(ptr, val, 0, 0, false);
}

// CIR-LABEL: offload.func @_Z7ds_faddPff
// CIR: cir.atomic.fetch add seq_cst

// --- DS atomic float min / max ----------------------------------------------

__device__ float ds_fmin(float *ptr, float val) {
  return __builtin_amdgcn_ds_fminf(ptr, val, 0, 0, false);
}

// CIR-LABEL: offload.func @_Z7ds_fminPff
// CIR: cir.atomic.fetch min seq_cst

__device__ float ds_fmax(float *ptr, float val) {
  return __builtin_amdgcn_ds_fmaxf(ptr, val, 0, 0, false);
}

// CIR-LABEL: offload.func @_Z7ds_fmaxPff
// CIR: cir.atomic.fetch max seq_cst

// --- Wrapping atomic increment / decrement ----------------------------------

__device__ unsigned int atomic_inc(unsigned int *ptr, unsigned int val) {
  return __builtin_amdgcn_atomic_inc32(ptr, val, 0, "");
}

// CIR-LABEL: offload.func @_Z10atomic_incPjj
// CIR: cir.atomic.fetch uinc_wrap seq_cst

__device__ unsigned int atomic_dec(unsigned int *ptr, unsigned int val) {
  return __builtin_amdgcn_atomic_dec32(ptr, val, 0, "");
}

// CIR-LABEL: offload.func @_Z10atomic_decPjj
// CIR: cir.atomic.fetch udec_wrap seq_cst
