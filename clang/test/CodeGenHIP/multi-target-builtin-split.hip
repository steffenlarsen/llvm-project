// __builtin_amdgcn_processor_is answers differently per ambient target, but
// its tokens are identical for every target, so a #if-based token-stream
// fork never sees this divergence. Sema::TouchedAmbientTargetBuiltin covers
// this case instead: it fires whenever the builtin is expanded in shared
// code and drives the same per-target re-parse that a #if-forked
// declaration gets. Checks that the primary target's own output is
// unaffected by that re-parse, and that the re-parse still applies to a
// __device__-only declaration.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.direct %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.merged %s \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -multi-target-aux-target=2:gfx900: \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -allow-target-variant-decls
// RUN: diff %t.direct %t.merged

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -fsyntax-only %s \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -multi-target-aux-target=2:gfx900: \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -allow-target-variant-decls \
// RUN:   -mllvm -trace-reparse 2>&1 | FileCheck %s
// CHECK: reparse {{[0-9]+}}t Function arch_pick

__attribute__((device)) int arch_pick(int x) {
  if (__builtin_amdgcn_processor_is("gfx900"))
    return x + 900;
  return x + 942;
}
