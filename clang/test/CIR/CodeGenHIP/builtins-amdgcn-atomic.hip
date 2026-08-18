// REQUIRES: amdgpu-registered-target
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -x hip \
// RUN: -std=c++11 -fclangir -fcuda-is-device -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -x hip \
// RUN: -std=c++11 -fclangir -fcuda-is-device -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -x hip \
// RUN: -std=c++11 -fcuda-is-device -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

#define __device__ __attribute__((device))

typedef __attribute__((address_space(1))) float global_float;
typedef __attribute__((address_space(3))) float shared_float;

//===----------------------------------------------------------------------===//
// The AMDGPU raw atomic builtins. The LLVM checks are shared between the
// ClangIR and the classic CodeGen run lines, so they pin the two paths to the
// same IR -- including the metadata, without which the backend expands a float
// atomic add into a cmpxchg loop instead of selecting global_atomic_add_f32.
//===----------------------------------------------------------------------===//

// These take no ordering or scope argument, so they default to monotonic at
// agent scope -- what the hardware instruction actually provides.
// CIR-LABEL: @_Z17test_global_faddfPU3AS1f
// CIR: cir.atomic.fetch add relaxed syncscope(device) fetch_first
// LLVM-LABEL: @_Z17test_global_faddfPU3AS1f
// Both metadata are required; the order they are printed in is not
// significant, so the check accepts either.
// LLVM: atomicrmw fadd ptr addrspace(1) %{{.+}}, float 1.000000e+00 syncscope("agent") monotonic, align 4, !amdgpu.{{(no.fine.grained.memory|ignore.denormal.mode)}} !{{[0-9]+}}, !amdgpu.{{(no.fine.grained.memory|ignore.denormal.mode)}}
__device__ float test_global_faddf(global_float *p) {
  return __builtin_amdgcn_global_atomic_fadd_f32(p, 1.0f);
}

// A double add gets no ignore.denormal.mode -- denormal flushing only applies
// to float.
// CIR-LABEL: @_Z17test_flat_faddf64Pd
// LLVM-LABEL: @_Z17test_flat_faddf64Pd
// LLVM: atomicrmw fadd ptr %{{.+}}, double 1.000000e+00 syncscope("agent") monotonic, align 8, !amdgpu.no.fine.grained.memory
__device__ double test_flat_faddf64(double *p) {
  return __builtin_amdgcn_flat_atomic_fadd_f64(p, 1.0);
}

// An LDS atomic is always native, so it carries no metadata at all.
// CIR-LABEL: @_Z13test_ds_faddfPU3AS3f
// LLVM-LABEL: @_Z13test_ds_faddfPU3AS3f
// LLVM: atomicrmw fadd ptr addrspace(3) %{{.+}}, float 2.000000e+00 syncscope("agent") monotonic, align 4{{$}}
__device__ float test_ds_faddf(shared_float *p) {
  return __builtin_amdgcn_ds_atomic_fadd_f32(p, 2.0f);
}

// inc/dec spell out their ordering and scope, which must be honored rather
// than widened to system scope.
// CIR-LABEL: @_Z10test_inc32Pj
// CIR: cir.atomic.fetch uinc_wrap seq_cst syncscope(device) fetch_first
// LLVM-LABEL: @_Z10test_inc32Pj
// LLVM: atomicrmw uinc_wrap ptr %{{.+}}, i32 7 syncscope("agent") seq_cst
__device__ unsigned test_inc32(unsigned *p) {
  return __builtin_amdgcn_atomic_inc32(p, 7u, __ATOMIC_SEQ_CST, "agent");
}

// CIR-LABEL: @_Z10test_dec32Pj
// CIR: cir.atomic.fetch udec_wrap relaxed syncscope(workgroup) fetch_first
// LLVM-LABEL: @_Z10test_dec32Pj
// LLVM: atomicrmw udec_wrap ptr %{{.+}}, i32 7 syncscope("workgroup") monotonic
__device__ unsigned test_dec32(unsigned *p) {
  return __builtin_amdgcn_atomic_dec32(p, 7u, __ATOMIC_RELAXED, "workgroup");
}

//===----------------------------------------------------------------------===//
// Ballot and exec-mask reads.
//===----------------------------------------------------------------------===//

// llvm.amdgcn.ballot is overloaded on its result type.
// CIR-LABEL: @_Z11test_ballotb
// CIR: cir.call_llvm_intrinsic "amdgcn.ballot"
// LLVM-LABEL: @_Z11test_ballotb
// LLVM: call i64 @llvm.amdgcn.ballot.i64(i1 %{{.+}})
__device__ unsigned long test_ballot(bool p) {
  return __builtin_amdgcn_ballot_w64(p);
}

// The inverse is overloaded on its argument type, and returns a bool.
// CIR-LABEL: @_Z19test_inverse_ballotm
// LLVM-LABEL: @_Z19test_inverse_ballotm
// LLVM: call i1 @llvm.amdgcn.inverse.ballot.i64(i64 %{{.+}})
__device__ bool test_inverse_ballot(unsigned long m) {
  return __builtin_amdgcn_inverse_ballot_w64(m);
}

// The exec mask is read as a ballot over an all-true predicate.
// CIR-LABEL: @_Z14test_read_execv
// LLVM-LABEL: @_Z14test_read_execv
// LLVM: call i64 @llvm.amdgcn.ballot.i64(i1 true)
__device__ unsigned long test_read_exec() {
  return __builtin_amdgcn_read_exec();
}

// The _hi form takes the upper half of that same 64-bit ballot.
// CIR-LABEL: @_Z17test_read_exec_hiv
// LLVM-LABEL: @_Z17test_read_exec_hiv
// LLVM: call i64 @llvm.amdgcn.ballot.i64(i1 true)
// LLVM: lshr i64 %{{.+}}, 32
// LLVM: trunc i64 %{{.+}} to i32
__device__ unsigned test_read_exec_hi() {
  return __builtin_amdgcn_read_exec_hi();
}

//===----------------------------------------------------------------------===//
// A builtin declared to return `bool` whose intrinsic returns i1 must produce
// a !cir.bool, not a one-bit integer -- cir.if accepts only the former. This
// is the shape HIP's unsafeAtomicAdd uses.
//===----------------------------------------------------------------------===//

// CIR-LABEL: @_Z14test_is_sharedPv
// CIR: cir.cast int_to_bool
// CIR: cir.if
// LLVM-LABEL: @_Z14test_is_sharedPv
// LLVM: call i1 @llvm.amdgcn.is.shared(ptr %{{.+}})
// LLVM: br i1
__device__ int test_is_shared(void *p) {
  if (__builtin_amdgcn_is_shared(
          (const __attribute__((address_space(0))) void *)p))
    return 1;
  return 0;
}
