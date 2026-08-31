// A plain, never-#if-widened function whose body declares local variables
// and is only classified target-divergent transitively (it calls a
// genuinely #if-widened leaf), referenced only from inside a function
// template instantiation's local-variable initializer. Each target computes
// the value with its own leaf, and the wrapper's locals stay the wrapper's.
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
// RUN:   "-multi-target-aux-invocation=%{hip_device} -target-cpu gfx900" \
// RUN:   -multi-target-aux-output=2:%t.gfx900.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} -target-cpu gfx1100" \
// RUN:   -multi-target-aux-output=3:%t.gfx1100.bc
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
// local-variable initializer.
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
