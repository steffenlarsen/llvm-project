// PROTOTYPE (Stage 5, Phase 5 Increment 4): the same shape as the sibling
// test multi-target-shared-caller-divergent-callee.hip, but with the
// divergent callee made a plain (non-template) function instead of a
// function template -- the real corpus's actual shape:
// mul_mat_q<...> (llama.cpp's mmq.cuh, a *shared*, never arch-dispatched
// function template) calls ggml_cuda_ue4m3_to_fp32 (common.cuh, a plain
// __device__ function whose entire body -- not its signature -- is gated by
// raw target-CPU macros).
//
// Design 1's per-declaration reparse mechanism still produces one
// per-target-tagged FunctionDecl for this plain function (confirmed via
// -mllvm -trace-tagged-decl= on the real corpus), but unlike the template
// case, these tagged copies do not merge into one FunctionTemplateDecl
// redeclaration chain to hang hasTargetTaggedRedeclaration() off of --
// Increment 3's DivergentCalleeVisitor::isDivergentCallee only recognized a
// callee that is itself a function-template specialization
// (FD->getPrimaryTemplate() non-null), so a plain-function callee like this
// one was invisible to it: the shared caller's one Sema-time resolution
// permanently baked in whichever target's copy ordinary lookup resolved
// first, producing an undefined symbol at link time for every other target
// (ld.lld: undefined symbol: ggml_cuda_ue4m3_to_fp32(unsigned char), from
// mul_mat_q<...> in the real corpus).
//
// Fixed by generalizing isDivergentCallee to also match a plain FunctionDecl
// with its own nonzero getTargetVariant(), and generalizing
// Sema::InstantiateDivergentCalleesInBody to, for such a callee, look up the
// sibling FunctionDecl tagged variant V directly (via LookupQualifiedName
// under a pushed TargetScope, exactly like the template path, since
// redecls() cannot be trusted here either) instead of re-instantiating a
// template pattern -- Design 1's reparse already fully parsed and defined
// each per-target copy, so no InstantiateFunctionDeclaration/Definition call
// is needed for this path.
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
// primary's) or each other's, even though `kernel_plain`/`shared_caller_plain`
// below have exactly one (shared, tag-0) definition each, and
// `arch_specific_plain_callee` is not a template at all.
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

// Body-divergent, plain (non-template) callee -- mirrors
// ggml_cuda_ue4m3_to_fp32's exact shape: widening/reparse produces one
// per-target-tagged FunctionDecl for this function, each with its own
// branch's body already selected, but these copies do not chain via
// redecls() (unlike an ordinary, non-divergent redeclaration). Must be
// `static` + force-inlined, exactly like the real ggml_cuda_ue4m3_to_fp32:
// an ordinary (non-static) top-level __device__ function gets independently
// re-widened and re-emitted per target regardless of any caller, which
// would mask this bug entirely -- a `static`, always-inlined callee has no
// such independent per-target re-emission, so the only body a caller's
// CodeGen ever sees is whichever FunctionDecl its own AST reference points
// at (which is exactly the reference this fix corrects).
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

// A single, never-#if-gated caller template -- exactly the shape that
// required a shared/tag-0 caller for Increment 3's original bug too:
// Context.getCurrentTargetVariant() is provably always 0 throughout this
// template's whole body.
template <int Dummy>
__attribute__((device)) int shared_caller_plain(int x) {
  return arch_specific_plain_callee(x);
}

// The entry point is shared too -- no arch dispatch anywhere in this TU.
__attribute__((global)) void kernel_plain(int *out, int x) {
  out[0] = shared_caller_plain<0>(x);
}

// GFX900: add nsw i32 %{{.*}}, 900
// GFX1100: add nsw i32 %{{.*}}, 1100
// GFX942: add nsw i32 %{{.*}}, 942
