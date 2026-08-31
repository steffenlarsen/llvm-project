// A macro defined differently per target (a wavefront size) is used in a
// helper's body and in a kernel's __launch_bounds__. The copies differ only in
// the macro's value, so they are spelled at the same place in every target's
// alternative: they must still not be merged, and the host's alternative of
// the kernel must find the host's alternative of the helper. This follows
// HeCBench rsmt ("call to function 'processSmallNet' that is neither visible
// in the template definition nor found by argument-dependent lookup").
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a -nogpulib -nogpuinc \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only -Xclang -verify %s
// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpulib -nogpuinc -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -Xclang -verify %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc \
// RUN:   -mllvm -multi-target-codegen \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -merge-equivalent-variants
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=DEV %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=DEV %s

// expected-no-diagnostics

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
#define __launch_bounds__(...) __attribute__((launch_bounds(__VA_ARGS__)))

#if defined(__GFX9__)
#define WS 64
#else
#define WS 32
#endif

template <int N> static __device__ inline void process(int *p) {
  p[0] = N * WS;
}

template <int N>
static __global__ __launch_bounds__(N * WS, 2) void kern(int *p) {
  process<N>(p);
}

template __global__ void kern<4>(int *);
void launch(int *p) { (void)&kern<4>; }

// DEV-LABEL: define {{.*}} @_ZL4kernILi4EEvPi(
// DEV-SAME: #[[ATTRS:[0-9]+]]
// DEV: store i32 256
// DEV: attributes #[[ATTRS]] = {{.*}}"amdgpu-flat-work-group-size"="1,256"
