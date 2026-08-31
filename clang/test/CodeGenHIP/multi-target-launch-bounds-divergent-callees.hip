// PROTOTYPE (Stage 5, Phase 5, mmvq.cu fix): a nullary function *template*,
// called with an empty-parens call (no arguments -- ADL cannot contribute any
// candidates), whose body is architecture-conditional via a single
// #if/#elif/#else chain -- exactly get_mmvq_mmid_max_batch_for_device<type>()'s
// shape in the real corpus's mmvq.cu. Widening reparses this into three
// separate, target-tagged FunctionTemplateDecl siblings with NO
// untagged/shared fallback copy, referenced (alongside a second,
// non-template, also target-divergent nullary callee, mirroring
// ggml_cuda_get_physical_warp_size) from inside a __launch_bounds__
// attribute on a shared __global__ caller that is never itself reparsed per
// target.
//
// Part 1 (isVisibleForTarget's tier-3 "last resort" fallback) is required
// just to make the attribute's call expression Sema-substitute at all --
// without it, this hard-fails with "neither visible in the template
// definition nor found by argument-dependent lookup" on every real target's
// AuxGen pass. Part 2 (a new per-target correction of the resulting
// compile-time constant) is required to make every aux target other than
// whichever one Part 1 happens to let through see its OWN arch's value,
// instead of the primary's value frozen in at the attribute's one-time
// substitution.
//
// REQUIRES: amdgpu-registered-target

// Ground truth: each arch compiled completely on its own.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX900 %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1100 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX1100 %s

// One primary (gfx942) cc1 job, with AuxGen configured for gfx900 and gfx1100
// and the full merge/reparse pipeline on -- each aux target's own bitcode
// output must see its own arch's __launch_bounds__-derived function
// attribute, not whichever arch's ambient happened to substitute the
// attribute's call expression first.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.primary.ll %s \
// RUN:   -mllvm -multi-target-codegen \
// RUN:   -mllvm -multi-target-aux-target=2:gfx900: \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx900.bc \
// RUN:   -mllvm -multi-target-aux-target=3:gfx1100: \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx1100.bc \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -merge-equivalent-variants
// RUN: FileCheck -check-prefix=GFX942 %s < %t.primary.ll
// RUN: llvm-dis -o - %t.gfx900.bc | FileCheck -check-prefix=GFX900 %s
// RUN: llvm-dis -o - %t.gfx1100.bc | FileCheck -check-prefix=GFX1100 %s

enum elem_type { TYPE_A, TYPE_B };

// Mirrors get_mmvq_mmid_max_batch_for_device<type>(): a function template,
// no untagged fallback arm, widened into 3 target-tagged siblings.
template <elem_type type>
static constexpr __attribute__((device)) int get_batch_for_device() {
#if defined(__gfx942__)
    return 100;
#elif defined(__gfx900__)
    return 200;
#elif defined(__gfx1100__)
    return 300;
#endif
}

// Mirrors ggml_cuda_get_physical_warp_size(): a plain (non-template)
// function, also target-divergent, but with a shared #else fallback arm.
static constexpr __attribute__((device)) int get_warp_size_like() {
#if defined(__gfx900__)
    return 64;
#else
    return 32;
#endif
}

// Mirrors mul_mat_vec_q_moe's
// __launch_bounds__(get_mmvq_mmid_max_batch_for_device<type>()*ggml_cuda_get_physical_warp_size(), 1):
// two independent target-divergent nullary callees multiplied together,
// attached to a shared __global__ caller never itself reparsed per target.
__attribute__((launch_bounds(get_batch_for_device<TYPE_A>() * get_warp_size_like(), 1)))
__attribute__((global)) void kernel_like(int *out) {
    out[0] = get_batch_for_device<TYPE_A>();
}

// GFX900: attributes #{{[0-9]+}} = {{.*}}"amdgpu-flat-work-group-size"="1,12800"
// GFX1100: attributes #{{[0-9]+}} = {{.*}}"amdgpu-flat-work-group-size"="1,9600"
// GFX942: attributes #{{[0-9]+}} = {{.*}}"amdgpu-flat-work-group-size"="1,3200"
