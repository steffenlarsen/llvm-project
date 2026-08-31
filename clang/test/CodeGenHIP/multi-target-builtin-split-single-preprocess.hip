// PROTOTYPE: -multi-target-record's per-target preprocessor recording and
// merge exists to catch #if/#ifdef divergence on a target macro. A
// declaration that only diverges through an ambient-target predicate builtin
// (__builtin_amdgcn_processor_is) never needs that: its tokens are identical
// for every target, and -reparse-divergent-users' Sema-level trigger
// (Sema::TouchedAmbientTargetBuiltin, see multi-target-builtin-split.hip)
// forks it correctly straight off a single, unmerged preprocessor pass.
//
// This is the same source as multi-target-builtin-split.hip, but -- unlike
// that test -- -multi-target-record is deliberately *not* passed, so
// ParseAST.cpp never records or merges a second target's tokens at all
// (recordTargetStreams never runs; the "merged" stream ParseAST builds is
// just this one pass's own tokens, verbatim). -multi-target-aux-target alone
// is enough to tell ASTContext how many targets to fork into. This checks the
// primary target's own output is unaffected by opting out of the merge this
// way, and that the aux fork still happens.
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
