// REQUIRES: amdgpu-registered-target
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -fclangir \
// RUN:   -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --input-file=%t-cir.ll %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a \
// RUN:   -emit-llvm %s -o %t.ll
// RUN: FileCheck --input-file=%t.ll %s --check-prefix=LLVM

// Every sync scope other than singlethread used to fall through to the empty
// string -- system scope -- so device, workgroup and wavefront scopes were
// silently discarded, and workgroup emitted NVPTX's "block" regardless of
// target. The checks are shared with the classic CodeGen run line.

// LLVM-LABEL: @wg
// LLVM: atomicrmw add ptr {{.*}} syncscope("workgroup") monotonic
int wg(int *p, int v) {
  return __scoped_atomic_fetch_add(p, v, __ATOMIC_RELAXED, __MEMORY_SCOPE_WRKGRP);
}

// LLVM-LABEL: @wave
// LLVM: atomicrmw add ptr {{.*}} syncscope("wavefront") monotonic
int wave(int *p, int v) {
  return __scoped_atomic_fetch_add(p, v, __ATOMIC_RELAXED, __MEMORY_SCOPE_WVFRNT);
}

// LLVM-LABEL: @dev
// LLVM: atomicrmw add ptr {{.*}} syncscope("agent") monotonic
int dev(int *p, int v) {
  return __scoped_atomic_fetch_add(p, v, __ATOMIC_RELAXED, __MEMORY_SCOPE_DEVICE);
}

// System scope is the unnamed default.
// LLVM-LABEL: @sys
// LLVM: atomicrmw add ptr {{.*}} monotonic
int sys(int *p, int v) {
  return __scoped_atomic_fetch_add(p, v, __ATOMIC_RELAXED, __MEMORY_SCOPE_SYSTEM);
}

// LLVM-LABEL: @sing
// LLVM: atomicrmw add ptr {{.*}} syncscope("singlethread") monotonic
int sing(int *p, int v) {
  return __scoped_atomic_fetch_add(p, v, __ATOMIC_RELAXED, __MEMORY_SCOPE_SINGLE);
}
