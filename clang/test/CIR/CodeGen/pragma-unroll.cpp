// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that #pragma unroll hints propagate through CIR to LLVM IR metadata.

// CHECK-LABEL: define {{.*}}@_Z11unroll_fullPii
// CHECK:       br label %{{[^ ,]+}}, !llvm.loop [[ENABLE_MD:![0-9]+]]
void unroll_full(int *a, int n) {
#pragma unroll
  for (int i = 0; i < n; i++)
    a[i] *= 2;
}

// CHECK-LABEL: define {{.*}}@_Z12unroll_countPii
// CHECK:       br label %{{[^ ,]+}}, !llvm.loop [[COUNT_MD:![0-9]+]]
void unroll_count(int *a, int n) {
#pragma unroll 4
  for (int i = 0; i < n; i++)
    a[i] *= 2;
}

// CHECK-LABEL: define {{.*}}@_Z9no_unrollPii
// CHECK:       br label %{{[^ ,]+}}, !llvm.loop [[DISABLE_MD:![0-9]+]]
void no_unroll(int *a, int n) {
#pragma nounroll
  for (int i = 0; i < n; i++)
    a[i] *= 2;
}

// CHECK-LABEL: define {{.*}}@_Z12unroll_whilePii
// CHECK:       br label %{{[^ ,]+}}, !llvm.loop [[ENABLE_MD]]
void unroll_while(int *a, int n) {
  int i = 0;
#pragma unroll
  while (i < n) {
    a[i] *= 2;
    i++;
  }
}

// CHECK-LABEL: define {{.*}}@_Z14unroll_dowhilePii
// CHECK:       !llvm.loop [[ENABLE_MD]]
void unroll_dowhile(int *a, int n) {
  int i = 0;
#pragma unroll
  do {
    a[i] *= 2;
    i++;
  } while (i < n);
}

// CHECK: !{!"llvm.loop.unroll.enable"}
// CHECK: !{!"llvm.loop.unroll.count", i32 4}
// CHECK: !{!"llvm.loop.unroll.disable"}
