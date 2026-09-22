// REQUIRES: amdgpu-registered-target
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -x hip -fclangir -fcuda-is-device \
// RUN: -emit-cir %s -o - | FileCheck --check-prefix=CIR %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -x hip -fclangir -fcuda-is-device \
// RUN: -emit-llvm %s -o - | FileCheck --check-prefix=LLVM %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -x hip -fcuda-is-device \
// RUN: -emit-llvm %s -o - | FileCheck --check-prefix=LLVM %s

#define __device__ __attribute__((device))

// CIR-LABEL: @_Z14test_inc_dec32Pj
// CIR: cir.atomic.fetch uinc_wrap seq_cst syncscope(workgroup) fetch_first %{{.+}}, %{{.+}} : (!cir.ptr<!u32i>, !u32i) -> !u32i
// CIR: cir.atomic.fetch udec_wrap seq_cst syncscope(workgroup) fetch_first %{{.+}}, %{{.+}} : (!cir.ptr<!u32i>, !u32i) -> !u32i
// LLVM-LABEL: @_Z14test_inc_dec32Pj
// LLVM: atomicrmw uinc_wrap ptr {{.*}} syncscope("workgroup") seq_cst, align 4
// LLVM: atomicrmw udec_wrap ptr {{.*}} syncscope("workgroup") seq_cst, align 4
__device__ unsigned test_inc_dec32(unsigned *p) {
  unsigned a = __builtin_amdgcn_atomic_inc32(p, 1u, __ATOMIC_SEQ_CST, "workgroup");
  unsigned b = __builtin_amdgcn_atomic_dec32(p, 1u, __ATOMIC_SEQ_CST, "workgroup");
  return a + b;
}

// CIR-LABEL: @_Z14test_inc_dec64Pm
// CIR: cir.atomic.fetch uinc_wrap seq_cst syncscope(workgroup) fetch_first %{{.+}}, %{{.+}} : (!cir.ptr<!u64i>, !u64i) -> !u64i
// CIR: cir.atomic.fetch udec_wrap seq_cst syncscope(workgroup) fetch_first %{{.+}}, %{{.+}} : (!cir.ptr<!u64i>, !u64i) -> !u64i
// LLVM-LABEL: @_Z14test_inc_dec64Pm
// LLVM: atomicrmw uinc_wrap ptr {{.*}} syncscope("workgroup") seq_cst, align 8
// LLVM: atomicrmw udec_wrap ptr {{.*}} syncscope("workgroup") seq_cst, align 8
__device__ unsigned long test_inc_dec64(unsigned long *p) {
  unsigned long a = __builtin_amdgcn_atomic_inc64(p, 1u, __ATOMIC_SEQ_CST, "workgroup");
  unsigned long b = __builtin_amdgcn_atomic_dec64(p, 1u, __ATOMIC_SEQ_CST, "workgroup");
  return a + b;
}

// CIR-LABEL: @_Z18test_inc_dec_agentPj
// CIR: cir.atomic.fetch uinc_wrap relaxed syncscope(device) fetch_first %{{.+}}, %{{.+}} : (!cir.ptr<!u32i>, !u32i) -> !u32i
// LLVM-LABEL: @_Z18test_inc_dec_agentPj
// LLVM: atomicrmw uinc_wrap ptr {{.*}} syncscope("agent") monotonic, align 4
__device__ unsigned test_inc_dec_agent(unsigned *p) {
  return __builtin_amdgcn_atomic_inc32(p, 1u, __ATOMIC_RELAXED, "agent");
}

// CIR-LABEL: @_Z19test_inc_dec_systemPj
// CIR: cir.atomic.fetch uinc_wrap relaxed syncscope(system) fetch_first %{{.+}}, %{{.+}} : (!cir.ptr<!u32i>, !u32i) -> !u32i
// LLVM-LABEL: @_Z19test_inc_dec_systemPj
// LLVM: atomicrmw uinc_wrap ptr {{.*}} monotonic, align 4
__device__ unsigned test_inc_dec_system(unsigned *p) {
  return __builtin_amdgcn_atomic_inc32(p, 1u, __ATOMIC_RELAXED, "");
}

// CIR-LABEL: @_Z21test_inc_dec_volatilePVj
// CIR: cir.atomic.fetch uinc_wrap seq_cst syncscope(workgroup) fetch_first %{{.+}}, %{{.+}} volatile : (!cir.ptr<!u32i>, !u32i) -> !u32i
// LLVM-LABEL: @_Z21test_inc_dec_volatilePVj
// LLVM: atomicrmw volatile uinc_wrap ptr {{.*}} syncscope("workgroup") seq_cst, align 4
__device__ unsigned test_inc_dec_volatile(volatile unsigned *p) {
  return __builtin_amdgcn_atomic_inc32(p, 1u, __ATOMIC_SEQ_CST, "workgroup");
}
