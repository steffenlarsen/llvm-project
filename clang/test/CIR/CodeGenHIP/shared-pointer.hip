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

#define __global__ __attribute__((global))
#define __shared__ __attribute__((shared))

struct State { int x; };

// A __shared__ variable gets no initializer -- the target cannot give LDS one
// -- so CIRGen emits #cir.undef. The array and record forms of that were
// already handled in CXXABILowering; a *pointer* in LDS was not, and lowering
// it asserted.
__global__ void k(State *g, int *o) {
  __shared__ State *p;
  __shared__ int buf[4];
  __shared__ State s;
  if (o[0]) p = g;
  __builtin_amdgcn_s_barrier();
  buf[0] = p->x + s.x;
  o[1] = buf[0];
}

// CIR-DAG: cir.global {{.*}} target_address_space(3) @_ZZ1kP5StatePiE1p = #cir.undef : !cir.ptr<!rec_State>
// CIR-DAG: cir.global {{.*}} target_address_space(3) @_ZZ1kP5StatePiE3buf = #cir.undef : !cir.array<!s32i x 4>
// CIR-DAG: cir.global {{.*}} target_address_space(3) @_ZZ1kP5StatePiE1s = #cir.undef : !rec_State

// The LLVM checks are shared with the classic CodeGen run line, pinning both
// paths to the same LDS globals.
// LLVM-DAG: @_ZZ1kP5StatePiE1p = internal addrspace(3) global ptr undef, align 8
// LLVM-DAG: @_ZZ1kP5StatePiE3buf = internal addrspace(3) global [4 x i32] undef, align 4
// LLVM-DAG: @_ZZ1kP5StatePiE1s = internal addrspace(3) global %struct.State undef, align 4
