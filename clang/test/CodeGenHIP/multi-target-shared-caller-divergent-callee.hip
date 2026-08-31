// PROTOTYPE (Stage 5, Phase 5 Increment 3): a *shared* (never arch-#if-
// dispatched) caller template whose body references a callee whose body --
// not its signature or template arguments -- is target-CPU-macro-gated. This
// is the real corpus's actual shape: mul_mat_q<...> (llama.cpp's mmq.cuh,
// itself never arch-dispatched) calls ggml_cuda_mma::mma<10,0> (mma.cuh,
// gated by raw CDNA macros). Because a shared caller's own body is
// Sema-instantiated exactly once, its one-time resolution of the divergent
// callee would otherwise permanently bake in whichever target's ambient
// happened to resolve it first -- confirmed on the real corpus via an
// ld.lld ISel abort (a wrong-arch AMDGPU intrinsic selected for the *other*
// arch) and via -dump-decl-fingerprints showing the divergent callee's
// specialization living as a single FunctionDecl shared across every
// CodeGenModule.
//
// Fixed by Sema::InstantiateDivergentCalleesInBody: right after a shared
// specialization's body is substituted, eagerly re-instantiate each direct
// reference to a hasTargetTaggedRedeclaration() callee once per real target
// (from that target's own tagged redeclaration), and record the results in
// ASTContext::TargetVariantCallees for CodeGenFunction::EmitCallee to consume
// passively at CodeGen time.
//
// This is multi-target-function-template-body-divergence.hip's structure
// with the *caller* made shared instead of arch-dispatched -- see that
// file's header comment for how the callee-side divergence itself is set up.
//
// REQUIRES: amdgpu-registered-target

// Ground truth: each arch compiled completely on its own.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX900 %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1100 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX1100 %s

// One primary (gfx942) cc1 job, with AuxGen configured for gfx900 and gfx1100
// and the full merge/reparse pipeline on -- each aux target's own bitcode
// output must contain its own branch's arithmetic, not gfx942's (the
// primary's) or each other's, even though `kernel`/`shared_caller` below have
// exactly one (shared, tag-0) definition each.
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

// Body-divergent callee, exactly mirroring arch_specific_tpl in the sibling
// test -- widening/reparse produces one per-target-tagged redeclaration for
// this template, each with its own branch's body already selected.
template <int Dummy>
__attribute__((device)) int arch_specific_callee(int x) {
#if defined(__gfx942__)
  return x + 942;
#elif defined(__gfx900__)
  return x + 900;
#elif defined(__gfx1100__)
  return x + 1100;
#endif
}

// A single, never-#if-gated definition -- exactly the shape that defeated
// the prior (Sema-FoldingSet-only) increment: Context.getCurrentTargetVariant()
// is provably always 0 throughout this template's whole body.
template <int Dummy>
__attribute__((device)) int shared_caller(int x) {
  return arch_specific_callee<Dummy>(x);
}

// The entry point is shared too -- no arch dispatch anywhere in this TU.
__attribute__((global)) void kernel(int *out, int x) {
  out[0] = shared_caller<0>(x);
}

// GFX900: add nsw i32 %{{.*}}, 900
// GFX1100: add nsw i32 %{{.*}}, 1100
// GFX942: add nsw i32 %{{.*}}, 942
