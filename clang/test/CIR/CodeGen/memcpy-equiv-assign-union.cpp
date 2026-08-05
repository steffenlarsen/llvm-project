// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s

// CIR should emit a cir.copy to perform the data movement for defaulted union
// copy/move assignment.

union U {
  int i;
  float f;
};

void copy_union(U &dst, const U &src) { dst = src; }

// CHECK: cir.func {{.*}}@_ZN1UaSERKS_
// CHECK:   cir.copy {{.*}} : !cir.ptr<!rec_U>

union Large {
  double d;
  char buf[32];
};

void copy_large_union(Large &dst, const Large &src) { dst = src; }

// CHECK: cir.func {{.*}}@_ZN5LargeaSERKS_
// CHECK:   cir.copy {{.*}} : !cir.ptr<!rec_Large>
