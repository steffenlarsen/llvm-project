// Same source as multi-target-builtin-split.hip, but without
// -multi-target-record: no per-target token recording or merge happens, so
// the "merged" token stream is just this one preprocessor pass's own
// tokens. Checks that -reparse-divergent-users' Sema-level trigger
// (Sema::TouchedAmbientTargetBuiltin) still forks a declaration that only
// diverges through __builtin_amdgcn_processor_is, since its tokens are
// identical for every target and -multi-target-aux-target alone is enough
// to tell ASTContext how many targets to fork into.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.direct %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.merged %s \
// RUN:   -mllvm -multi-target-aux-target=2:gfx900: \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -allow-target-variant-decls
// RUN: diff %t.direct %t.merged

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -fsyntax-only %s \
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
