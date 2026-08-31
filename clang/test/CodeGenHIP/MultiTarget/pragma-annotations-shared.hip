// Pragmas and #embed that mean the same to every target stay shared, so the
// code around them is parsed once.
//
// REQUIRES: amdgpu-registered-target

// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -Wno-c++26-extensions -x hip \
// RUN:   -fsyntax-only -mllvm -dump-token-stream %t/main.hip \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -Wno-c++26-extensions -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t/gfx90a.bc 2> %t/stream.txt
// RUN: FileCheck %t/main.hip < %t/stream.txt
// RUN: FileCheck --check-prefix=SHARED %t/main.hip < %t/stream.txt

//--- data.bin
DATA
//--- main.hip
// No region is divergent.
// SHARED-NOT: annot_target_alt
#define __device__ __attribute__((device))
#define __host__ __attribute__((host))
// The same name on every target.
#define ZZ_CONTRACT fast

#pragma pack(push, 2)
struct zz_S { char c; int i; };
#pragma pack(pop)
// CHECK: annot_pragma_pack
// CHECK: annot_pragma_pack

#pragma clang attribute push(__attribute__((annotate("zz"))), apply_to = function)
__host__ __device__ void zz_annotated() {}
#pragma clang attribute pop
// CHECK: annot_pragma_attribute
// CHECK: annot_pragma_attribute

__host__ __device__ float zz_fp(float a, float b, float c) {
#pragma clang fp contract(ZZ_CONTRACT)
  return a * b + c;
}
// CHECK: annot_pragma_fp

__host__ __device__ float zz_stdc(float a, float b, float c) {
#pragma STDC FP_CONTRACT ON
  return a * b + c;
}
// CHECK: annot_pragma_fp_contract

__host__ __device__ int zz_embed() {
  static const char d[] = {
#embed "data.bin"
  };
  return sizeof(d);
}
// CHECK: annot_embed
