// A static device method declared directly on a primary class template
// whose own redeclarations are target-tagged, called through ordinary
// object syntax (s.get_val(l)) from a shared caller that is never itself
// reparsed per target. Each target must dispatch to its own arm's method
// body.
//
// REQUIRES: amdgpu-registered-target

// Ground truth: each arch compiled completely on its own.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX900 %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1100 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX1100 %s

// One primary (gfx942) cc1 job, with AuxGen configured for gfx900 and gfx1100
// and the full merge/reparse pipeline on. Each aux target's own bitcode
// output must call its own arm's get_val().
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

#if defined(__gfx942__)
template <int N> struct sizes_like {
  static __attribute__((device)) __attribute__((always_inline)) int get_val(int l) { return l + 100; }
};
#elif defined(__gfx900__)
template <int N> struct sizes_like {
  static __attribute__((device)) __attribute__((always_inline)) int get_val(int l) { return l + 200; }
};
#elif defined(__gfx1100__)
template <int N> struct sizes_like {
  static __attribute__((device)) __attribute__((always_inline)) int get_val(int l) { return l + 300; }
};
#endif

// sizes_like<192> matches no partial specialization (there is none), so this
// always falls to the primary template.
__attribute__((global)) void kernel_like(int *out) {
  sizes_like<192> s;
  out[0] = s.get_val(5);
}

// GFX900: add nsw i32 %{{.*}}, 200
// GFX1100: add nsw i32 %{{.*}}, 300
// GFX942: add nsw i32 %{{.*}}, 100
