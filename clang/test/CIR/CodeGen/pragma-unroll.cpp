// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir --check-prefix=CIR %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll --check-prefix=LLVM %s

// #pragma unroll and friends must survive to LLVM as !llvm.loop metadata;
// dropping them silently costs a fully-unrolled loop.

void enable(int *a, int n) {
#pragma unroll
  for (int i = 0; i < n; ++i)
    a[i] = i;
}
// CIR-LABEL: cir.func {{.*}}@_Z6enablePii
// CIR: } {loop_hint = #cir.loop_hint<unroll_mode = 1 : i32, must_progress = true>}

// LLVM-LABEL: @_Z6enablePii
// LLVM: br label %{{.*}}, !llvm.loop ![[ENABLE:[0-9]+]]

void count(int *a, int n) {
#pragma unroll 4
  for (int i = 0; i < n; ++i)
    a[i] = i;
}
// CIR-LABEL: cir.func {{.*}}@_Z5countPii
// CIR: } {loop_hint = #cir.loop_hint<unroll_count = 4, must_progress = true>}

void disable(int *a, int n) {
#pragma nounroll
  for (int i = 0; i < n; ++i)
    a[i] = i;
}
// CIR-LABEL: cir.func {{.*}}@_Z7disablePii
// CIR: } {loop_hint = #cir.loop_hint<unroll_mode = 2 : i32, must_progress = true>}

void whileLoop(int *a, int n) {
  int i = 0;
#pragma unroll
  while (i < n) {
    a[i] = i;
    ++i;
  }
}
// CIR-LABEL: cir.func {{.*}}@_Z9whileLoopPii
// CIR: } {loop_hint = #cir.loop_hint<unroll_mode = 1 : i32, must_progress = true>}

void doLoop(int *a, int n) {
  int i = 0;
#pragma unroll
  do {
    a[i] = i;
    ++i;
  } while (i < n);
}
// CIR-LABEL: cir.func {{.*}}@_Z6doLoopPii
// CIR: } {loop_hint = #cir.loop_hint<unroll_mode = 1 : i32, must_progress = true>}

// An unannotated loop must not gain a hint.
void plain(int *a, int n) {
  for (int i = 0; i < n; ++i)
    a[i] = i;
}
// CIR-LABEL: cir.func {{.*}}@_Z5plainPii
// CIR-NOT: loop_hint


// The back-edge metadata must name the pragma that produced it.
// LLVM-DAG: ![[ENABLE]] = distinct !{![[ENABLE]], !{{[0-9]+}}, ![[UE:[0-9]+]]}
// LLVM-DAG: ![[UE]] = !{!"llvm.loop.unroll.enable"}
// LLVM-DAG: !{!"llvm.loop.unroll.count", i32 4}
// LLVM-DAG: !{!"llvm.loop.unroll.disable"}
