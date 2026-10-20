// An aggregate larger than 16 bytes whose initializer is at least 3/4 zero is
// first zeroed whole with a memset, padding included, and only its non-zero
// parts are then stored, as classic codegen does.

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o %t.cir
// RUN: FileCheck --input-file=%t.cir %s --check-prefix=CIR
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// 40 bytes, with padding after `c` and after `arr`.
struct Padded {
  char c;
  long l;
  int arr[3];
  void *p;
};

void use(const Padded &);

// A value-initialized temporary.
void value_init_temporary() { use(Padded{}); }

// CIR-LABEL: cir.func {{.*}} @_Z20value_init_temporaryv(
// CIR:         %[[TMP:.*]] = cir.alloca "ref.tmp0"
// CIR:         %[[VOID:.*]] = cir.cast bitcast %[[TMP]] : !cir.ptr<!rec_Padded> -> !cir.ptr<!void>
// CIR:         cir.libc.memset {{.*}} bytes at %[[VOID]] align(8)
// CIR-NOT:     cir.store
// CIR:         cir.call @_Z3useRK6Padded(%[[TMP]])

// LLVM-LABEL: define {{.*}} @_Z20value_init_temporaryv(
// LLVM:         call void @llvm.memset.p0.i64(ptr align 8 %[[TMP:.*]], i8 0, i64 40, i1 false)
// LLVM-NOT:     store
// LLVM:         call void @_Z3useRK6Padded(ptr {{.*}}%[[TMP]])

// One non-constant byte out of 40: zeroed whole, then the one member stored.
void mostly_zero(char c) { Padded s = {c}; }

// CIR-LABEL: cir.func {{.*}} @_Z11mostly_zeroc(
// CIR:         cir.libc.memset
// CIR:         %[[C:.*]] = cir.get_member %{{.*}}[0] {name = "c"}
// CIR:         cir.store {{.*}} %[[C]]
// CIR-NOT:     cir.store
// CIR:         cir.return

// LLVM-LABEL: define {{.*}} @_Z11mostly_zeroc(
// LLVM:         call void @llvm.memset.p0.i64(ptr align 8 %{{.*}}, i8 0, i64 40, i1 false)
// LLVM:         store i8 %{{.*}}, ptr %{{.*}}
// LLVM-NOT:     store
// LLVM:         ret void

// Spelled-out zeros are not stored into the zeroed object either.
void explicit_zeros(char c) { Padded s = {c, 0, {0, 0, 0}, nullptr}; }

// CIR-LABEL: cir.func {{.*}} @_Z14explicit_zerosc(
// CIR:         cir.libc.memset
// CIR:         %[[C:.*]] = cir.get_member %{{.*}}[0] {name = "c"}
// CIR:         cir.store {{.*}} %[[C]]
// CIR-NOT:     cir.store
// CIR:         cir.return

// LLVM-LABEL: define {{.*}} @_Z14explicit_zerosc(
// LLVM:         call void @llvm.memset.p0.i64(ptr align 8 %{{.*}}, i8 0, i64 40, i1 false)
// LLVM:         store i8 %{{.*}}, ptr %{{.*}}
// LLVM-NOT:     store
// LLVM:         ret void

// Mostly non-zero: stored member by member.
void mostly_nonzero(void *q) { Padded s = {1, 2, {3, 4, 5}, q}; }

// CIR-LABEL: cir.func {{.*}} @_Z14mostly_nonzeroPv(
// CIR-NOT:     cir.libc.memset
// CIR:         cir.return

// LLVM-LABEL: define {{.*}} @_Z14mostly_nonzeroPv(
// LLVM-NOT:     call void @llvm.memset
// LLVM:         ret void

// 16 bytes or less: stored member by member.
struct Small {
  char c;
  long l;
};

void small(char c) { Small s = {c}; }

// CIR-LABEL: cir.func {{.*}} @_Z5smallc(
// CIR-NOT:     cir.libc.memset
// CIR:         cir.return

// LLVM-LABEL: define {{.*}} @_Z5smallc(
// LLVM-NOT:     call void @llvm.memset
// LLVM:         ret void

// Assigning a value-initialized temporary leaves every byte zero: the
// temporary is zeroed whole and then copied whole.
struct Holder {
  Padded app;
  void reset() { app = {}; }
};

void reset(Holder &h) { h.reset(); }

// LLVM-LABEL: define {{.*}} @_ZN6Holder5resetEv(
// LLVM:         call void @llvm.memset.p0.i64(ptr align 8 %[[TMP:.*]], i8 0, i64 40, i1 false)
// LLVM:         call void @llvm.memcpy.p0.p0.i64(ptr align 8 %{{.*}}, ptr align 8 %[[TMP]], i64 40, i1 false)
