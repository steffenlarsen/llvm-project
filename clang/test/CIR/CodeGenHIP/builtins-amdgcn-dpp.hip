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

//===----------------------------------------------------------------------===//
// Test the AMDGPU DPP builtins. The LLVM checks are shared between the ClangIR
// and the classic CodeGen run lines, so they pin the two paths to the same IR.
//===----------------------------------------------------------------------===//

// CIR-LABEL: @_Z15test_update_dppPiii
// CIR: cir.call_llvm_intrinsic "amdgcn.update.dpp" {{.*}} : (!s32i, !s32i, !u32i, !u32i, !u32i, !cir.bool) -> !u32i
// LLVM-LABEL: @_Z15test_update_dppPiii
// LLVM: call i32 @llvm.amdgcn.update.dpp.i32(i32 %{{.+}}, i32 %{{.+}}, i32 1, i32 1, i32 1, i1 false)
__device__ void test_update_dpp(int* out, int a, int b) {
  *out = __builtin_amdgcn_update_dpp(a, b, 1, 1, 1, false);
}

// mov_dpp has no "old" operand, so it maps onto update.dpp with a poison one.
// CIR-LABEL: @_Z12test_mov_dppPii
// CIR: cir.call_llvm_intrinsic "amdgcn.update.dpp" {{.*}} : (!u32i, !s32i, !u32i, !u32i, !u32i, !cir.bool) -> !u32i
// LLVM-LABEL: @_Z12test_mov_dppPii
// LLVM: call i32 @llvm.amdgcn.update.dpp.i32(i32 poison, i32 %{{.+}}, i32 1, i32 1, i32 1, i1 false)
__device__ void test_mov_dpp(int* out, int a) {
  *out = __builtin_amdgcn_mov_dpp(a, 1, 1, 1, false);
}

// The intrinsic is only defined for 32-bit and wider integers, so a narrower
// datum travels zero-extended and is truncated back on the way out.
// CIR-LABEL: @_Z21test_update_dpp_shortPsss
// CIR: cir.cast integral %{{.+}} : !s16i -> !u16i
// CIR: cir.cast integral %{{.+}} : !u16i -> !u32i
// CIR: cir.call_llvm_intrinsic "amdgcn.update.dpp" {{.*}} : (!u32i, !u32i, !u32i, !u32i, !u32i, !cir.bool) -> !u32i
// CIR: cir.cast integral %{{.+}} : !u32i -> !u16i
// LLVM-LABEL: @_Z21test_update_dpp_shortPsss
// LLVM: zext i16 %{{.+}} to i32
// LLVM: zext i16 %{{.+}} to i32
// LLVM: call i32 @llvm.amdgcn.update.dpp.i32(i32 %{{.+}}, i32 %{{.+}}, i32 1, i32 1, i32 1, i1 false)
// LLVM: trunc i32 %{{.+}} to i16
__device__ void test_update_dpp_short(short* out, short a, short b) {
  *out = __builtin_amdgcn_update_dpp(a, b, 1, 1, 1, false);
}

// A float datum is bitcast to an integer of the same width rather than
// converted.
// CIR-LABEL: @_Z20test_update_dpp_halfPDF16_DF16_DF16_
// CIR: cir.cast bitcast %{{.+}} : !cir.f16 -> !u16i
// CIR: cir.call_llvm_intrinsic "amdgcn.update.dpp" {{.*}} -> !u32i
// LLVM-LABEL: @_Z20test_update_dpp_halfPDF16_DF16_DF16_
// LLVM: bitcast half %{{.+}} to i16
// LLVM: call i32 @llvm.amdgcn.update.dpp.i32(i32 %{{.+}}, i32 %{{.+}}, i32 1, i32 1, i32 1, i1 false)
// LLVM: bitcast i16 %{{.+}} to half
__device__ void test_update_dpp_half(_Float16* out, _Float16 a, _Float16 b) {
  *out = __builtin_amdgcn_update_dpp(a, b, 1, 1, 1, false);
}
