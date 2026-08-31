// A nullary function template, called with an empty-parens call (so ADL
// cannot contribute candidates), whose body is architecture-conditional via
// a #if/#elif/#else chain -- the shape
// get_mmvq_mmid_max_batch_for_device<type>() takes in llama.cpp's mmq.cu.
// Widening produces three target-tagged FunctionTemplateDecl siblings with
// no untagged/shared fallback, referenced (alongside a second, non-template,
// also target-divergent nullary callee mirroring
// ggml_cuda_get_physical_warp_size) from a __launch_bounds__ attribute on a
// shared __global__ caller that is never itself reparsed per target. Checks
// that name lookup for the attribute's call expression succeeds for every
// target, and that each aux target's resulting __launch_bounds__-derived
// attribute value reflects its own arch, not the primary's.
//
// A second caller below (kernel_like2/sizes_like2) exercises the other three
// DivergentValueCalleeRewriter::TransformDecl kinds -- value member,
// member-function callee, and alias-qualified member -- from inside a
// __launch_bounds__ attribute expression, which this file previously did not
// cover (only the plain Callee kind, above).
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

// kernel_like above only exercises the plain Callee kind of
// DivergentValueCalleeRewriter::TransformDecl. sizes_like2 below is a
// primary class template whose own redeclarations are target-tagged (no
// partial specialization matches sizes_like2<192>, so every reference
// falls to the primary template, per multi-target-primary-template-widening.hip's
// established shape), combining the three other previously-uncovered kinds
// -- value member (width), member-function callee (get_val), and
// alias-qualified member (nested::val) -- into a single __launch_bounds__
// attribute expression on a second shared caller.
#if defined(__gfx942__)
struct nested_helper_like { static constexpr int val = 900; };
template <int N> struct sizes_like2 {
    static constexpr int width = 1;
    static constexpr __attribute__((device)) int get_val(int l) { return l + 10; }
    using nested = nested_helper_like;
};
#elif defined(__gfx900__)
struct nested_helper_like { static constexpr int val = 800; };
template <int N> struct sizes_like2 {
    static constexpr int width = 2;
    static constexpr __attribute__((device)) int get_val(int l) { return l + 20; }
    using nested = nested_helper_like;
};
#elif defined(__gfx1100__)
struct nested_helper_like { static constexpr int val = 700; };
template <int N> struct sizes_like2 {
    static constexpr int width = 3;
    static constexpr __attribute__((device)) int get_val(int l) { return l + 30; }
    using nested = nested_helper_like;
};
#endif

__attribute__((launch_bounds(sizes_like2<192>::width + sizes_like2<192>::get_val(0) + sizes_like2<192>::nested::val, 1)))
__attribute__((global)) void kernel_like2(int *out) {
    out[0] = sizes_like2<192>::get_val(0);
}

// GFX900: attributes #{{[0-9]+}} = {{.*}}"amdgpu-flat-work-group-size"="1,822"
// GFX1100: attributes #{{[0-9]+}} = {{.*}}"amdgpu-flat-work-group-size"="1,733"
// GFX942: attributes #{{[0-9]+}} = {{.*}}"amdgpu-flat-work-group-size"="1,911"
