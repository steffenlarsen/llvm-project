// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=OGCG --input-file=%t.ll %s
// RUN: %clang_cc1 -triple i386-unknown-linux-gnu -fclangir -emit-llvm %s -o %t-i386.ll
// RUN: FileCheck --check-prefix=I386 --input-file=%t-i386.ll %s

// A union is lowered to its most-aligned member, here long double. x86_fp80
// stores only 10 of the bytes it occupies, but a union value loaded or stored
// whole has to carry the bytes of every member, so the storage is lowered as
// integers spanning the slot instead.

union Bytes {
  long double ld;
  char buf[16];
};

// LLVM: %union.Bytes = type { i128 }
// I386: %union.Bytes = type { [3 x i32] }

#ifdef __SIZEOF_INT128__
union Wide {
  __int128 i;
  long double ld;
};
#endif

// A long double initializer occupies the whole 16-byte slot, so no padding
// follows it, whichever member the union is stored as.

// LLVM: @bytes = global { x86_fp80 } { x86_fp80 1.000000e+00 }, align 16
// LLVM: @wide = global { x86_fp80 } { x86_fp80 2.000000e+00 }, align 16
// OGCG: @bytes = global %union.Bytes { x86_fp80 1.000000e+00 }, align 16
// OGCG: @wide = global { x86_fp80 } { x86_fp80 2.000000e+00 }, align 16
Bytes bytes = {.ld = 1.0L};
#ifdef __SIZEOF_INT128__
Wide wide = {.ld = 2.0L};
#endif

Bytes make();
void take(Bytes);

// The returned union passes through the coercion slot as a whole value.

// LLVM-LABEL: define {{.*}} void @_Z7forwardv()
// LLVM:         %[[RET:.*]] = call { i64, i64 } @_Z4makev()
// LLVM:         store { i64, i64 } %[[RET]], ptr %[[SLOT:.*]], align
// LLVM:         load %union.Bytes, ptr %[[SLOT]], align 16
void forward() { take(make()); }
