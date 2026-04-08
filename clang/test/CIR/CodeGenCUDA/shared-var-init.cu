#include "Inputs/cuda.h"

// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -fclangir \
// RUN:            -fcuda-is-device -emit-cir -target-sdk-version=12.3 \
// RUN:            -I%S/Inputs/ %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR-DEVICE --input-file=%t.cir %s

// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda -fclangir \
// RUN:            -fcuda-is-device -emit-llvm -target-sdk-version=12.3 \
// RUN:            -I%S/Inputs/ %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM-DEVICE --input-file=%t.ll %s

// RUN: %clang_cc1 -triple nvptx64-nvidia-cuda \
// RUN:            -fcuda-is-device -emit-llvm -target-sdk-version=12.3 \
// RUN:            -I%S/Inputs/ %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG-DEVICE --input-file=%t.ll %s

// CUDA E.2.4.1: __shared__ variables cannot be initialized.  A class-typed
// __shared__ variable still gets an implicit default constructor in the AST,
// so codegen must drop that initializer and leave the global undef.  Emitting
// a real initializer (e.g. zeroinitializer) is rejected by targets such as
// AMDGPU, whose local address space cannot be initialized at all.

struct Empty {};

struct Pair {
  float x, y;
};

struct WithCtor {
  float x, y;
  __device__ WithCtor() {}
};

__global__ void fn() {
  __shared__ Empty e;
  __shared__ Pair p;
  __shared__ Pair arr[4];
  __shared__ WithCtor w;
  // Scalar and scalar-array cases have no implicit initializer at all; they
  // are checked here to pin down that both paths agree.
  __shared__ float f;
  __shared__ float farr[4];

  p.x = 0.0f;
  arr[0].y = 0.0f;
  w.x = 0.0f;
  f = 0.0f;
  farr[0] = 0.0f;
  (void)e;
}

// CIR-DEVICE-DAG: cir.global "private" internal dso_local {{.*}}target_address_space(3) @_ZZ2fnvE1e = #cir.undef : !rec_Empty
// CIR-DEVICE-DAG: cir.global "private" internal dso_local {{.*}}target_address_space(3) @_ZZ2fnvE1p = #cir.undef : !rec_Pair
// CIR-DEVICE-DAG: cir.global "private" internal dso_local {{.*}}target_address_space(3) @_ZZ2fnvE3arr = #cir.undef : !cir.array<!rec_Pair x 4>
// CIR-DEVICE-DAG: cir.global "private" internal dso_local {{.*}}target_address_space(3) @_ZZ2fnvE1w = #cir.undef : !rec_WithCtor
// CIR-DEVICE-DAG: cir.global "private" internal dso_local {{.*}}target_address_space(3) @_ZZ2fnvE1f = #cir.undef : !cir.float
// CIR-DEVICE-DAG: cir.global "private" internal dso_local {{.*}}target_address_space(3) @_ZZ2fnvE4farr = #cir.undef : !cir.array<!cir.float x 4>

// LLVM-DEVICE-DAG: @_ZZ2fnvE1e = internal addrspace(3) global %struct.Empty undef
// LLVM-DEVICE-DAG: @_ZZ2fnvE1p = internal addrspace(3) global %struct.Pair undef
// LLVM-DEVICE-DAG: @_ZZ2fnvE3arr = internal addrspace(3) global [4 x %struct.Pair] undef
// LLVM-DEVICE-DAG: @_ZZ2fnvE1w = internal addrspace(3) global %struct.WithCtor undef
// LLVM-DEVICE-DAG: @_ZZ2fnvE1f = internal addrspace(3) global float undef
// LLVM-DEVICE-DAG: @_ZZ2fnvE4farr = internal addrspace(3) global [4 x float] undef

// OGCG-DEVICE-DAG: @_ZZ2fnvE1e = internal addrspace(3) global %struct.Empty undef
// OGCG-DEVICE-DAG: @_ZZ2fnvE1p = internal addrspace(3) global %struct.Pair undef
// OGCG-DEVICE-DAG: @_ZZ2fnvE3arr = internal addrspace(3) global [4 x %struct.Pair] undef
// OGCG-DEVICE-DAG: @_ZZ2fnvE1w = internal addrspace(3) global %struct.WithCtor undef
// OGCG-DEVICE-DAG: @_ZZ2fnvE1f = internal addrspace(3) global float undef
// OGCG-DEVICE-DAG: @_ZZ2fnvE4farr = internal addrspace(3) global [4 x float] undef

// No guard variable or guarded-init call may be emitted for a __shared__
// variable whose constructor was dropped.
// CIR-DEVICE-NOT: _ZGVZ2fnvE
// LLVM-DEVICE-NOT: _ZGVZ2fnvE
// OGCG-DEVICE-NOT: _ZGVZ2fnvE
