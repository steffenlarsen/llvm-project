// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// With -fwrapv-pointer, pointer arithmetic may leave the object, so no
// address computation is inbounds.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fwrapv-pointer -fclangir -emit-llvm %s -o %t-cir-wrapv.ll
// RUN: FileCheck --input-file=%t-cir-wrapv.ll %s --check-prefix=WRAPV --implicit-check-not=inbounds --implicit-check-not=nuw
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fwrapv-pointer -emit-llvm %s -o %t-wrapv.ll
// RUN: FileCheck --input-file=%t-wrapv.ll %s --check-prefix=WRAPV --implicit-check-not=inbounds --implicit-check-not=nuw

// An address computation that stays within its object is inbounds, and also
// nuw if its index is unsigned and not subtracted.

int subscript_array(int i) {
  int a[8] = {0};
  return a[i];
}

// CIR-LABEL: cir.func {{.*}} @subscript_array(
// CIR: cir.get_element inbounds %{{.*}}[%{{.*}} : !s64i]
// LLVM-LABEL: define {{.*}} @subscript_array(
// LLVM: getelementptr inbounds [8 x i32], ptr %{{.*}}, {{i32|i64}} 0, i64 %{{.*}}
// WRAPV-LABEL: define {{.*}} @subscript_array(
// WRAPV: getelementptr [8 x i32], ptr %{{.*}}, {{i32|i64}} 0, i64 %{{.*}}

int subscript_array_unsigned(unsigned i) {
  int a[8] = {0};
  return a[i];
}

// CIR-LABEL: cir.func {{.*}} @subscript_array_unsigned(
// CIR: cir.get_element inbounds nuw %{{.*}}[%{{.*}} : !u64i]
// LLVM-LABEL: define {{.*}} @subscript_array_unsigned(
// LLVM: getelementptr inbounds nuw [8 x i32], ptr %{{.*}}, {{i32|i64}} 0, i64 %{{.*}}
// WRAPV-LABEL: define {{.*}} @subscript_array_unsigned(
// WRAPV: getelementptr [8 x i32], ptr %{{.*}}, {{i32|i64}} 0, i64 %{{.*}}

int subscript_ptr(int *p, int i) { return p[i]; }

// CIR-LABEL: cir.func {{.*}} @subscript_ptr(
// CIR: cir.ptr_stride inbounds %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !s64i)
// LLVM-LABEL: define {{.*}} @subscript_ptr(
// LLVM: getelementptr inbounds i32, ptr %{{.*}}, i64 %{{.*}}
// WRAPV-LABEL: define {{.*}} @subscript_ptr(
// WRAPV: getelementptr i32, ptr %{{.*}}, i64 %{{.*}}

int subscript_ptr_unsigned(int *p, unsigned i) { return p[i]; }

// CIR-LABEL: cir.func {{.*}} @subscript_ptr_unsigned(
// CIR: cir.ptr_stride inbounds nuw %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !u64i)
// LLVM-LABEL: define {{.*}} @subscript_ptr_unsigned(
// LLVM: getelementptr inbounds nuw i32, ptr %{{.*}}, i64 %{{.*}}
// WRAPV-LABEL: define {{.*}} @subscript_ptr_unsigned(
// WRAPV: getelementptr i32, ptr %{{.*}}, i64 %{{.*}}

int *add_signed(int *p, int i) { return p + i; }

// CIR-LABEL: cir.func {{.*}} @add_signed(
// CIR: cir.ptr_stride inbounds %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !s32i)
// LLVM-LABEL: define {{.*}} @add_signed(
// LLVM: getelementptr inbounds i32, ptr %{{.*}}, i64 %{{.*}}
// WRAPV-LABEL: define {{.*}} @add_signed(
// WRAPV: getelementptr i32, ptr %{{.*}}, i64 %{{.*}}

int *add_unsigned(int *p, unsigned i) { return p + i; }

// CIR-LABEL: cir.func {{.*}} @add_unsigned(
// CIR: cir.ptr_stride inbounds nuw %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !u32i)
// LLVM-LABEL: define {{.*}} @add_unsigned(
// LLVM: getelementptr inbounds nuw i32, ptr %{{.*}}, i64 %{{.*}}
// WRAPV-LABEL: define {{.*}} @add_unsigned(
// WRAPV: getelementptr i32, ptr %{{.*}}, i64 %{{.*}}

int *sub_unsigned(int *p, unsigned i) { return p - i; }

// CIR-LABEL: cir.func {{.*}} @sub_unsigned(
// CIR: cir.ptr_stride inbounds %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !s32i)
// LLVM-LABEL: define {{.*}} @sub_unsigned(
// LLVM: getelementptr inbounds i32, ptr %{{.*}}, i64 %{{.*}}
// WRAPV-LABEL: define {{.*}} @sub_unsigned(
// WRAPV: getelementptr i32, ptr %{{.*}}, i64 %{{.*}}

void *add_void(void *p, int i) { return p + i; }

// CIR-LABEL: cir.func {{.*}} @add_void(
// CIR: cir.ptr_stride inbounds %{{.*}}, %{{.*}} : (!cir.ptr<!void>, !s32i)
// LLVM-LABEL: define {{.*}} @add_void(
// LLVM: getelementptr inbounds i8, ptr %{{.*}}, i64 %{{.*}}
// WRAPV-LABEL: define {{.*}} @add_void(
// WRAPV: getelementptr i8, ptr %{{.*}}, i64 %{{.*}}

int *pre_inc(int *p) { return ++p; }

// CIR-LABEL: cir.func {{.*}} @pre_inc(
// CIR: cir.ptr_stride inbounds nuw %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !s32i)
// LLVM-LABEL: define {{.*}} @pre_inc(
// LLVM: getelementptr inbounds nuw i32, ptr %{{.*}}, {{i32|i64}} 1
// WRAPV-LABEL: define {{.*}} @pre_inc(
// WRAPV: getelementptr i32, ptr %{{.*}}, {{i32|i64}} 1

int *pre_dec(int *p) { return --p; }

// CIR-LABEL: cir.func {{.*}} @pre_dec(
// CIR: cir.ptr_stride inbounds %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !s32i)
// LLVM-LABEL: define {{.*}} @pre_dec(
// LLVM: getelementptr inbounds i32, ptr %{{.*}}, {{i32|i64}} -1
// WRAPV-LABEL: define {{.*}} @pre_dec(
// WRAPV: getelementptr i32, ptr %{{.*}}, {{i32|i64}} -1

int *vla_add(int n, int (*p)[n], int i) { return *(p + i); }

// CIR-LABEL: cir.func {{.*}} @vla_add(
// CIR: cir.ptr_stride inbounds %{{.*}}, %{{.*}} : (!cir.ptr<!s32i>, !u64i)
// LLVM-LABEL: define {{.*}} @vla_add(
// LLVM: getelementptr inbounds i32, ptr %{{.*}}, i64 %{{.*}}
// WRAPV-LABEL: define {{.*}} @vla_add(
// WRAPV: getelementptr i32, ptr %{{.*}}, i64 %{{.*}}
