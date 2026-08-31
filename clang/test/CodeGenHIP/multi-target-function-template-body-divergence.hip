// A plain (non-constexpr) function template whose body -- not its signature
// or template arguments -- is gated by target-CPU macros (the real-world
// shape is ggml_cuda_mma::mma<dl_d, dl_ab> in llama.cpp's mma.cuh). Widening
// produces one target-tagged FunctionTemplateDecl redeclaration per target,
// and a call to the same specialization (e.g. arch_specific_tpl<0>) from
// each target's own caller must resolve to that target's own instantiated
// body, not whichever target's Sema pass instantiated the specialization
// first.
//
// Each caller below is itself arch-dispatched (not a single shared caller),
// mirroring the real corpus: mma<...>'s actual callers (mul_mat_q<...>) are
// themselves arch-tagged, not shared -- a shared (tag-0) caller referencing a
// target-divergent template is a distinct, out-of-scope edge case (see
// multi-target-shared-callee-under-exclusive-caller.hip).
//
// REQUIRES: amdgpu-registered-target

// Ground truth: each arch compiled completely on its own.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX900 %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1100 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX1100 %s

// One host/primary (gfx942) cc1 job, with AuxGen configured for gfx900 and
// gfx1100 and the full merge/reparse pipeline on -- each aux target's own
// bitcode output must contain its own branch's arithmetic, not gfx942's (the
// primary's) or each other's.
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

template <int Dummy>
__attribute__((device)) int arch_specific_tpl(int x) {
#if defined(__gfx942__)
  return x + 942;
#elif defined(__gfx900__)
  return x + 900;
#elif defined(__gfx1100__)
  return x + 1100;
#endif
}

// Each kernel body below is deliberately textually distinct (not just
// wrapped in a different #if branch) -- three byte-identical kernel bodies
// living in different branches get merged by structural-equivalence into one
// shared (tag-0) declaration, which would collapse the caller into the
// out-of-scope "shared caller" case instead of the one this test exercises.
#if defined(__gfx942__)
__attribute__((global)) void kernel(int *out, int x) { out[0] = arch_specific_tpl<0>(x); }
#elif defined(__gfx900__)
__attribute__((global)) void kernel(int *out, int x) { out[1] = arch_specific_tpl<0>(x); }
#elif defined(__gfx1100__)
__attribute__((global)) void kernel(int *out, int x) { out[2] = arch_specific_tpl<0>(x); }
#endif

// GFX900: add nsw i32 %{{.*}}, 900
// GFX1100: add nsw i32 %{{.*}}, 1100
// GFX942: add nsw i32 %{{.*}}, 942
