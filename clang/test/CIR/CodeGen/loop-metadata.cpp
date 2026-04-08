// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -std=c++17 -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s

// Verify that CIR emits !llvm.loop metadata with mustprogress on loop
// back-edges for for-loops and while-loops.

// CHECK-LABEL: define {{.*}}@_Z10simple_forPii
// CHECK:       br label %{{[^ ,]+}}, !llvm.loop [[LOOP_MD:![0-9]+]]
int simple_for(int *a, int n) {
  int sum = 0;
  for (int i = 0; i < n; i++)
    sum += a[i];
  return sum;
}

// CHECK-LABEL: define {{.*}}@_Z12simple_whilePii
// CHECK:       br label %{{[^ ,]+}}, !llvm.loop [[LOOP_MD2:![0-9]+]]
int simple_while(int *a, int n) {
  int sum = 0;
  int i = 0;
  while (i < n) {
    sum += a[i];
    i++;
  }
  return sum;
}

// CHECK: !{!"llvm.loop.mustprogress"}
