// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefixes=LLVM,LLVM-CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefixes=LLVM,OGCG

// The decay of an array to a pointer to its first element and the adjustment
// of a derived class pointer to one of its bases stay within their object, so
// both are inbounds, as in classic codegen.

void use(int *);

void decay() {
  int a[4];
  use(a);
}

// CIR-LABEL: cir.func {{.*}} @_Z5decayv(
// CIR: cir.cast array_to_ptrdecay %{{.*}} : !cir.ptr<!cir.array<!s32i x 4>> -> !cir.ptr<!s32i>
// LLVM-LABEL: define {{.*}} @_Z5decayv(
// LLVM-CIR: getelementptr inbounds i32, ptr %{{.*}}, i32 0
// OGCG: getelementptr inbounds [4 x i32], ptr %{{.*}}, i64 0, i64 0

struct A { int a; };
struct B { int b; };
struct C : A, B { int c; };

B *base_ptr(C *c) { return c; }

// CIR-LABEL: cir.func {{.*}} @_Z8base_ptrP1C(
// CIR: cir.base_class_addr %{{.*}} [4] : !cir.ptr<!rec_C> -> !cir.ptr<!rec_B>
// LLVM-LABEL: define {{.*}} @_Z8base_ptrP1C(
// LLVM: icmp eq ptr %{{.*}}, null
// LLVM: getelementptr inbounds i8, ptr %{{.*}}, {{i32|i64}} 4

B &base_ref(C &c) { return c; }

// CIR-LABEL: cir.func {{.*}} @_Z8base_refR1C(
// CIR: cir.base_class_addr nonnull %{{.*}} [4] : !cir.ptr<!rec_C> -> !cir.ptr<!rec_B>
// LLVM-LABEL: define {{.*}} @_Z8base_refR1C(
// LLVM: getelementptr inbounds i8, ptr %{{.*}}, {{i32|i64}} 4
