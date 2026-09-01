// REQUIRES: amdgpu-registered-target
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -x hip \
// RUN: -std=c++11 -fclangir -fcuda-is-device -emit-cir %s -o - | FileCheck %s

// An anonymous record was named from a counter that increments as types are
// instantiated. The host and device halves of one CUDA/HIP translation unit
// instantiate types in different orders, so the same record gets different
// names in the two modules. A source location is the same in both, so the two
// agree.
//
// Only for CUDA/HIP: elsewhere a location-based name would bake source line
// numbers into every anonymous record, so an unrelated edit above the
// declaration would rename the type.

struct S {
  union { int a; float b; };
};

__attribute__((device)) S g;

// The name carries the file and the declaration's line and column rather than
// an instantiation counter.
// CHECK: !cir.union<"anon.anon-record-naming.cu.{{[0-9]+}}.{{[0-9]+}}"
// CHECK-NOT: !cir.union<"anon.{{[0-9]+}}"
