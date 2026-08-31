// Same shape as the sibling test
// multi-target-shared-caller-divergent-callee.hip, but with the divergent
// callee a plain (non-template) function instead of a function template. A
// shared, never arch-dispatched caller must still resolve the call to each
// target's own arm of the divergent plain callee, rather than baking in
// whichever target's copy ordinary lookup resolves first.
//
// REQUIRES: amdgpu-registered-target

// Ground truth: each arch compiled completely on its own.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX900 %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1100 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX1100 %s

// One primary (gfx942) cc1 job, with AuxGen configured for gfx900 and gfx1100
// and the full merge/reparse pipeline on. Each aux target's own bitcode
// output must contain its own branch's arithmetic, even though
// `kernel_plain`/`shared_caller_plain` below have exactly one (shared,
// tag-0) definition each, and `arch_specific_plain_callee` is not a
// template at all.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.primary.ll %s \
// RUN:   -mllvm -multi-target-codegen \
// RUN:   -mllvm -multi-target-aux-target=2:gfx900: \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx900.bc \
// RUN:   -mllvm -multi-target-aux-target=3:gfx1100: \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx1100.bc \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls
// RUN: FileCheck -check-prefix=GFX942 %s < %t.primary.ll
// RUN: llvm-dis -o - %t.gfx900.bc | FileCheck -check-prefix=GFX900 %s
// RUN: llvm-dis -o - %t.gfx1100.bc | FileCheck -check-prefix=GFX1100 %s

// Body-divergent, plain (non-template) callee: widening/reparse produces
// one per-target-tagged FunctionDecl for this function, each with its own
// branch's body already selected, but these copies do not chain via
// redecls() (unlike an ordinary, non-divergent redeclaration). It is
// `static` and force-inlined so it is not independently re-widened and
// re-emitted per target regardless of caller; the only body a caller's
// CodeGen sees is whichever FunctionDecl its own AST reference points at.
__attribute__((device)) static inline __attribute__((always_inline)) int
arch_specific_plain_callee(int x) {
#if defined(__gfx942__)
  return x + 942;
#elif defined(__gfx900__)
  return x + 900;
#elif defined(__gfx1100__)
  return x + 1100;
#endif
}

// A single, never-#if-gated caller template:
// Context.getCurrentTargetVariant() is always 0 throughout this template's
// whole body.
template <int Dummy>
__attribute__((device)) int shared_caller_plain(int x) {
  return arch_specific_plain_callee(x);
}

// The entry point is shared too: no arch dispatch anywhere in this TU.
__attribute__((global)) void kernel_plain(int *out, int x) {
  out[0] = shared_caller_plain<0>(x);
}

// GFX900: add nsw i32 %{{.*}}, 900
// GFX1100: add nsw i32 %{{.*}}, 1100
// GFX942: add nsw i32 %{{.*}}, 942
