// Same source as builtin-split.hip. The Sema-level trigger forks a
// declaration that only diverges through
// __builtin_amdgcn_processor_is, since its tokens are identical for every
// target. Checks that the primary target's output is the same as its
// compilation on its own, and that the declaration is re-parsed per
// target.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.direct %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.merged %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} -target-cpu gfx900"
// RUN: diff %t.direct %t.merged

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -fsyntax-only %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} -target-cpu gfx900" \
// RUN:   -mllvm -trace-reparse 2>&1 | FileCheck %s
// CHECK: reparse {{[0-9]+}}t Function arch_pick

__attribute__((device)) int arch_pick(int x) {
  if (__builtin_amdgcn_processor_is("gfx900"))
    return x + 900;
  return x + 942;
}
