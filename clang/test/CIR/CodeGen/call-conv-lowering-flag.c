// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=ON
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - \
// RUN:     -fno-clangir-call-conv-lowering \
// RUN:   | FileCheck %s --check-prefix=OFF
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - \
// RUN:     -fclangir-call-conv-lowering -fno-clangir-call-conv-lowering \
// RUN:     -fclangir-call-conv-lowering \
// RUN:   | FileCheck %s --check-prefix=ON
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o - \
// RUN:     -fno-clangir-call-conv-lowering -fclangir-call-conv-lowering \
// RUN:     -fno-clangir-call-conv-lowering \
// RUN:   | FileCheck %s --check-prefix=OFF
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=ON

// CIR emitted without the lowering records that it was deferred, and a compile
// resuming from it runs it, as the offload merge relies on.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s \
// RUN:     -fno-clangir-call-conv-lowering -o %t.cir
// RUN: FileCheck %s --input-file=%t.cir --check-prefix=DEFERRED
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -x cir -emit-llvm \
// RUN:     %t.cir -o - | FileCheck %s --check-prefix=ON
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s \
// RUN:     -o - | FileCheck %s --check-prefix=LOWERED

typedef struct { long a, b, c, d; } Big;

Big ret_big(long a) {
  Big b = {a, a, a, a};
  return b;
}

// ON: define dso_local void @ret_big(ptr dead_on_unwind noalias writable sret(%struct.Big) align 8 %{{[^,)]+}}, i64 noundef %{{[^,)]+}})
// OFF: define dso_local %struct.Big @ret_big(i64 noundef %{{[^,)]+}})
// DEFERRED: module {{.*}}cir.call_conv_lowering_deferred
// DEFERRED: cir.func {{.*}}@ret_big(%arg0: !s64i {{.*}}) -> !rec_Big
// LOWERED-NOT: cir.call_conv_lowering_deferred
