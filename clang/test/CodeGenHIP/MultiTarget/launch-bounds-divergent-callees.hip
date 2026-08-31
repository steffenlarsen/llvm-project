// A nullary function template, called with empty parentheses (so ADL cannot
// contribute candidates), whose body is architecture-conditional through a
// #if/#elif/#else chain.
//
// Widening produces three target-tagged FunctionTemplateDecl siblings with
// no untagged or shared fallback. They are referenced, together with a
// second target-divergent nullary callee that is not a template, from a
// __launch_bounds__ attribute on a shared __global__ caller that is never
// itself reparsed per target. Checks that name lookup for the attribute's
// call expression succeeds for every target, and that each aux target's
// __launch_bounds__-derived attribute value reflects its own arch, not the
// primary's.
//
// A second caller below (kernel_like2/sizes_like2) reaches per-target values
// through a value member, a member-function callee and an alias-qualified
// member from inside a __launch_bounds__ attribute expression.
//
// REQUIRES: amdgpu-registered-target

// The ground truth is each arch compiled completely on its own.
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
// RUN:   "-multi-target-aux-invocation=%{hip_device} -target-cpu gfx900" \
// RUN:   -multi-target-aux-output=2:%t.gfx900.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} -target-cpu gfx1100" \
// RUN:   -multi-target-aux-output=3:%t.gfx1100.bc
// RUN: FileCheck -check-prefix=GFX942 %s < %t.primary.ll
// RUN: llvm-dis -o - %t.gfx900.bc | FileCheck -check-prefix=GFX900 %s
// RUN: llvm-dis -o - %t.gfx1100.bc | FileCheck -check-prefix=GFX1100 %s

enum elem_type { TYPE_A, TYPE_B };

// A function template with no untagged fallback arm, widened into three
// target-tagged siblings.
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

// A plain (non-template) function, also target-divergent, but with a shared
// #else fallback arm.
static constexpr __attribute__((device)) int get_warp_size_like() {
#if defined(__gfx900__)
    return 64;
#else
    return 32;
#endif
}


// Two independent target-divergent nullary callees multiplied together in a
// __launch_bounds__ attribute, attached to a shared __global__ caller that is
// never itself reparsed per target.
__attribute__((launch_bounds(get_batch_for_device<TYPE_A>() * get_warp_size_like(), 1)))
__attribute__((global)) void kernel_like(int *out) {
    out[0] = get_batch_for_device<TYPE_A>();
}

// GFX900: attributes #{{[0-9]+}} = {{.*}}"amdgpu-flat-work-group-size"="1,12800"
// GFX1100: attributes #{{[0-9]+}} = {{.*}}"amdgpu-flat-work-group-size"="1,9600"
// GFX942: attributes #{{[0-9]+}} = {{.*}}"amdgpu-flat-work-group-size"="1,3200"


// sizes_like2 below is a primary class template whose own redeclarations are
// target-tagged (no partial specialization matches sizes_like2<192>, so every
// reference falls to the primary template). It combines a value member
// (width), a member-function callee (get_val) and an alias-qualified member
// (nested::val) in a single __launch_bounds__ attribute expression on a
// second shared caller.
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
