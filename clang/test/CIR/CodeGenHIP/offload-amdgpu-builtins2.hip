// Tests that the second wave of AMDGPU device builtins emit LLVM intrinsic
// calls (via llvm.call_intrinsic) rather than errorNYI stubs.
//
// Covered groups:
//   A — unary float→float:   fract, frexp_mant, rsq_clamp, log_clamp
//   B — float+int→float:     ldexp
//   C — ternary float:       div_fixup
//   D — float→int:           frexp_exp
//   E — integer ternary:     ubfe, sbfe, lerp, alignbit
//   F — comparisons:         uicmp, fcmp
//   G — lane permutes:       permlane64, permlane16, permlanex16
//   H — special:             fence
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx906 \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

// ── Group A: unary float → float ─────────────────────────────────────────────

// CIR-LABEL: offload.func @_Z5fractf
// CIR:         llvm.call_intrinsic "llvm.amdgcn.fract.f32"
__device__ float fract(float x) { return __builtin_amdgcn_fractf(x); }

// CIR-LABEL: offload.func @_Z10frexp_mantf
// CIR:         llvm.call_intrinsic "llvm.amdgcn.frexp.mant.f32"
__device__ float frexp_mant(float x) { return __builtin_amdgcn_frexp_mantf(x); }

// CIR-LABEL: offload.func @_Z9rsq_clampf
// CIR:         llvm.call_intrinsic "llvm.amdgcn.rsq.clamp.f32"
__device__ float rsq_clamp(float x) { return __builtin_amdgcn_rsq_clampf(x); }

// CIR-LABEL: offload.func @_Z9log_clampf
// CIR:         llvm.call_intrinsic "llvm.amdgcn.log.clamp.f32"
__device__ float log_clamp(float x) { return __builtin_amdgcn_log_clampf(x); }

// ── Group D: float → int (frexp_exp) ─────────────────────────────────────────

// CIR-LABEL: offload.func @_Z9frexp_expf
// CIR:         llvm.call_intrinsic "llvm.amdgcn.frexp.exp.i32.f32"
__device__ int frexp_exp(float x) { return __builtin_amdgcn_frexp_expf(x); }

// ── Group B: float + int → float (ldexp) ─────────────────────────────────────

// CIR-LABEL: offload.func @_Z5ldexpfi
// CIR:         llvm.call_intrinsic "llvm.ldexp.f32.i32"
__device__ float ldexp(float x, int n) { return __builtin_amdgcn_ldexpf(x, n); }

// ── Group C: ternary float (div_fixup) ───────────────────────────────────────

// CIR-LABEL: offload.func @_Z9div_fixupfff
// CIR:         llvm.call_intrinsic "llvm.amdgcn.div.fixup.f32"
__device__ float div_fixup(float a, float b, float c) {
  return __builtin_amdgcn_div_fixupf(a, b, c);
}

// ── Group E: integer ternary ──────────────────────────────────────────────────

// CIR-LABEL: offload.func @_Z4ubfejjj
// CIR:         llvm.call_intrinsic "llvm.amdgcn.ubfe.i32"
__device__ unsigned ubfe(unsigned s, unsigned o, unsigned w) {
  return __builtin_amdgcn_ubfe(s, o, w);
}

// CIR-LABEL: offload.func @_Z4sbfeijj
// CIR:         llvm.call_intrinsic "llvm.amdgcn.sbfe.i32"
__device__ int sbfe(int s, unsigned o, unsigned w) {
  return __builtin_amdgcn_sbfe(s, o, w);
}

// CIR-LABEL: offload.func @_Z4lerpjjj
// CIR:         llvm.call_intrinsic "llvm.amdgcn.lerp"
__device__ unsigned lerp(unsigned a, unsigned b, unsigned c) {
  return __builtin_amdgcn_lerp(a, b, c);
}

// CIR-LABEL: offload.func @_Z8alignbitjjj
// CIR:         llvm.call_intrinsic "llvm.amdgcn.alignbit"
__device__ unsigned alignbit(unsigned h, unsigned l, unsigned s) {
  return __builtin_amdgcn_alignbit(h, l, s);
}

// ── Group F: comparisons ──────────────────────────────────────────────────────

// CIR-LABEL: offload.func @_Z5uicmpjj
// CIR:         llvm.call_intrinsic "llvm.amdgcn.icmp.i32.i32"
__device__ unsigned long long uicmp(unsigned a, unsigned b) {
  return __builtin_amdgcn_uicmp(a, b, 32 /* EQ */);
}

// CIR-LABEL: offload.func @_Z5fcmpfff
// CIR:         llvm.call_intrinsic "llvm.amdgcn.fcmp.i32.f32"
__device__ unsigned long long fcmpf(float a, float b) {
  return __builtin_amdgcn_fcmpf(a, b, 1 /* OEQ */);
}

// ── Group G: lane permutes ────────────────────────────────────────────────────

// CIR-LABEL: offload.func @_Z10permlane64j
// CIR:         llvm.call_intrinsic "llvm.amdgcn.permlane64"
__device__ unsigned permlane64(unsigned s) {
  return __builtin_amdgcn_permlane64(s);
}

// CIR-LABEL: offload.func @_Z10permlane16jjjj
// CIR:         llvm.call_intrinsic "llvm.amdgcn.permlane16"
__device__ unsigned permlane16(unsigned o, unsigned s0, unsigned s1,
                               unsigned s2) {
  return __builtin_amdgcn_permlane16(o, s0, s1, s2, 0, 0);
}

// CIR-LABEL: offload.func @_Z11permlanex16jjjj
// CIR:         llvm.call_intrinsic "llvm.amdgcn.permlanex16"
__device__ unsigned permlanex16(unsigned o, unsigned s0, unsigned s1,
                                unsigned s2) {
  return __builtin_amdgcn_permlanex16(o, s0, s1, s2, 0, 0);
}

// ── Group H: fence ───────────────────────────────────────────────────────────

// CIR-LABEL: offload.func @_Z5fencev
// CIR:         llvm.fence
__device__ void fence() {
  __builtin_amdgcn_fence(__ATOMIC_SEQ_CST, "workgroup");
}
