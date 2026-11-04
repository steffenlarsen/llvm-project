// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fwrapv-pointer -fclangir -emit-cir %s -o %t-wrapv.cir
// RUN: FileCheck --input-file=%t-wrapv.cir %s --check-prefix=CIR-WRAPV
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fwrapv-pointer -fclangir -emit-llvm %s -o %t-cir-wrapv.ll
// RUN: FileCheck --input-file=%t-cir-wrapv.ll %s --check-prefix=WRAPV
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fwrapv-pointer -emit-llvm %s -o %t-wrapv.ll
// RUN: FileCheck --input-file=%t-wrapv.ll %s --check-prefix=WRAPV

// Member addresses through a member pointer or of a zero-size field are
// inbounds, unless pointer overflow is defined or the base is a null pointer
// constant.

struct E {};
struct S {
  int a;
  int b;
  [[no_unique_address]] E e;
  [[no_unique_address]] E f;
};

int *data_member_arrow(S *s, int S::*p) { return &(s->*p); }

// CIR-LABEL: cir.func {{.*}} @_Z17data_member_arrowP1SMS_i(
// CIR: cir.ptr_stride inbounds %{{.*}}, %{{.*}} : (!cir.ptr<!s8i>, !s64i) -> !cir.ptr<!s8i>
// CIR-WRAPV-LABEL: cir.func {{.*}} @_Z17data_member_arrowP1SMS_i(
// CIR-WRAPV: cir.ptr_stride %{{.*}}, %{{.*}} : (!cir.ptr<!s8i>, !s64i) -> !cir.ptr<!s8i>
// LLVM-LABEL: define {{.*}} @_Z17data_member_arrowP1SMS_i(
// LLVM: getelementptr inbounds i8, ptr %{{.*}}, i64 %{{.*}}
// WRAPV-LABEL: define {{.*}} @_Z17data_member_arrowP1SMS_i(
// WRAPV: getelementptr i8, ptr %{{.*}}, i64 %{{.*}}

int *data_member_dot(S &s, int S::*p) { return &(s.*p); }

// CIR-LABEL: cir.func {{.*}} @_Z15data_member_dotR1SMS_i(
// CIR: cir.ptr_stride inbounds %{{.*}}, %{{.*}} : (!cir.ptr<!s8i>, !s64i) -> !cir.ptr<!s8i>
// CIR-WRAPV-LABEL: cir.func {{.*}} @_Z15data_member_dotR1SMS_i(
// CIR-WRAPV: cir.ptr_stride %{{.*}}, %{{.*}} : (!cir.ptr<!s8i>, !s64i) -> !cir.ptr<!s8i>
// LLVM-LABEL: define {{.*}} @_Z15data_member_dotR1SMS_i(
// LLVM: getelementptr inbounds i8, ptr %{{.*}}, i64 %{{.*}}
// WRAPV-LABEL: define {{.*}} @_Z15data_member_dotR1SMS_i(
// WRAPV: getelementptr i8, ptr %{{.*}}, i64 %{{.*}}

int *data_member_null(int S::*p) { return &(((S *)0)->*p); }

// CIR-LABEL: cir.func {{.*}} @_Z16data_member_nullM1Si(
// CIR: cir.ptr_stride %{{.*}}, %{{.*}} : (!cir.ptr<!s8i>, !s64i) -> !cir.ptr<!s8i>
// LLVM-LABEL: define {{.*}} @_Z16data_member_nullM1Si(
// LLVM: getelementptr i8, ptr null, i64 %{{.*}}

E *zero_size_field(S *s) { return &s->f; }

// CIR-LABEL: cir.func {{.*}} @_Z15zero_size_fieldP1S(
// CIR: cir.ptr_stride inbounds %{{.*}}, %{{.*}} : (!cir.ptr<!u8i>, !u64i) -> !cir.ptr<!u8i>
// CIR-WRAPV-LABEL: cir.func {{.*}} @_Z15zero_size_fieldP1S(
// CIR-WRAPV: cir.ptr_stride %{{.*}}, %{{.*}} : (!cir.ptr<!u8i>, !u64i) -> !cir.ptr<!u8i>
// LLVM-LABEL: define {{.*}} @_Z15zero_size_fieldP1S(
// LLVM: getelementptr inbounds i8, ptr %{{.*}}, i64 8
// WRAPV-LABEL: define {{.*}} @_Z15zero_size_fieldP1S(
// WRAPV: getelementptr i8, ptr %{{.*}}, i64 8

struct R {
  int &r;
  R(int &v) : r(v) {}
};

void ref_field_init(int &v) { R x(v); }

// CIR-LABEL: cir.func {{.*}} @_ZN1RC2ERi(
// CIR: cir.get_member %{{.*}}[0] {name = "r"}
// CIR-WRAPV-LABEL: cir.func {{.*}} @_ZN1RC2ERi(
// CIR-WRAPV: cir.get_member no_inbounds %{{.*}}[0] {name = "r"}
// LLVM-LABEL: define {{.*}} @_ZN1RC2ERi(
// LLVM: getelementptr inbounds nuw %struct.R, ptr %{{.*}}, i32 0, i32 0
// WRAPV-LABEL: define {{.*}} @_ZN1RC2ERi(
// WRAPV: getelementptr %struct.R, ptr %{{.*}}, i32 0, i32 0
