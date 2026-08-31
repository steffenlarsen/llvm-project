// A plain, never-#if-widened function whose body declares local variables
// and is only classified target-divergent transitively (it calls a
// genuinely #if-widened leaf), referenced only from inside a function
// template instantiation's local-variable initializer. That reference path
// routes the callee through getOrCreateTargetVariantWrapperClone, whose
// clone must reparent the body's local variables into the clone's own
// DeclContext (not just its parameters) so that later references to those
// locals resolve inside the clone rather than the original function.
//
// REQUIRES: amdgpu-registered-target

// Ground truth: each arch compiled completely on its own.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX900 %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1100 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX1100 %s

// One primary (gfx942) cc1 job, with AuxGen configured for gfx900 and gfx1100
// and the full merge/reparse pipeline on.
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
static __attribute__((device)) inline int leaf(int x) { return x + 100; }
#elif defined(__gfx900__)
static __attribute__((device)) inline int leaf(int x) { return x + 200; }
#elif defined(__gfx1100__)
static __attribute__((device)) inline int leaf(int x) { return x + 300; }
#endif

// Plain (never itself #if-widened) function with several local variables in
// its body, calling the target-divergent leaf().
static __attribute__((device)) int wrapper(int x) {
  int a = x + 1;
  int b = 0;
#pragma unroll
  for (int i = 0; i < 2; ++i) {
    int c = leaf(a + i);
    b += c;
  }
  return b;
}

// wrapper() is referenced from a function template instantiation's
// local-variable initializer, which is what routes it through
// getOrCreateTargetVariantWrapperClone (see file header).
template <typename T>
static __attribute__((device)) T kernel_tmpl(T x) {
  int local = wrapper((int)x);
  return (T)local;
}

__attribute__((global)) void kernel_like(int *out) {
  out[0] = kernel_tmpl<int>(10);
}

// GFX900: define{{.*}} @_ZL4leafi{{.*}}
// GFX900: add nsw i32 %{{.*}}, 200
// GFX1100: define{{.*}} @_ZL4leafi{{.*}}
// GFX1100: add nsw i32 %{{.*}}, 300
// GFX942: define{{.*}} @_ZL4leafi{{.*}}
// GFX942: add nsw i32 %{{.*}}, 100
