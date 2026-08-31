// Same source as multi-target-builtin-split.hip. It used to check the same
// without per-target token recording, which every compilation for several
// targets now does. The Sema-level trigger (Sema::TouchedAmbientTargetBuiltin)
// forks a declaration that only diverges through
// __builtin_amdgcn_processor_is, since its tokens are identical for every
// target.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.direct %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.merged %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -target-cpu gfx900 -fcuda-is-device -x hip"
// RUN: diff %t.direct %t.merged

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -fsyntax-only %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -target-cpu gfx900 -fcuda-is-device -x hip" \
// RUN:   -mllvm -trace-reparse 2>&1 | FileCheck %s
// CHECK: reparse {{[0-9]+}}t Function arch_pick

__attribute__((device)) int arch_pick(int x) {
  if (__builtin_amdgcn_processor_is("gfx900"))
    return x + 900;
  return x + 942;
}
