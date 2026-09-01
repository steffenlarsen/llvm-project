// REQUIRES: amdgpu-registered-target
// RUN: %clang_cc1 -o - -triple amdgcn-amd-amdhsa -target-cpu gfx908 -fclangir \
// RUN: -fcuda-is-device -emit-llvm %s | FileCheck %s

// RUN: %clang_cc1 -o - -triple amdgcn-amd-amdhsa -target-cpu gfx908 \
// RUN: -fcuda-is-device -emit-llvm %s | FileCheck %s

#define __device__ __attribute__((device))

// CHECK-LABEL: @_Z3b64b
// CHECK: call i64 @llvm.amdgcn.ballot.i64(i1
__device__ unsigned long long b64(bool p) { return __builtin_amdgcn_ballot_w64(p); }

// CHECK-LABEL: @_Z4ib64y
// CHECK: call i1 @llvm.amdgcn.inverse.ballot.i64(i64
__device__ bool ib64(unsigned long long m) { return __builtin_amdgcn_inverse_ballot_w64(m); }

// CHECK-LABEL: @_Z2exv
// CHECK: call i64 @llvm.amdgcn.ballot.i64(i1 true)
__device__ unsigned long long ex(void) { return __builtin_amdgcn_read_exec(); }

// CHECK-LABEL: @_Z4exlov
// CHECK: call i64 @llvm.amdgcn.ballot.i64(i1 true)
__device__ unsigned int exlo(void) { return __builtin_amdgcn_read_exec_lo(); }

// CHECK-LABEL: @_Z4exhiv
// CHECK: call i64 @llvm.amdgcn.ballot.i64(i1 true)
__device__ unsigned long long exhi(void) { return __builtin_amdgcn_read_exec_hi(); }
