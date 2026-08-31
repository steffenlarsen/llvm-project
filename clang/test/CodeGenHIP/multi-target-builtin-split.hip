// PROTOTYPE: __builtin_amdgcn_processor_is answers differently per ambient
// target (SemaAMDGPU.cpp's ExpandAMDGPUPredicateBuiltIn), but by itself that
// answer is only computed once, against the primary target, because
// evaluating it touches no target-tagged *entity* a lookup could flag --
// unlike a #if-driven fork, TokenStreamMerge never sees this divergence at
// all, since the tokens are identical for every target.
//
// Sema::TouchedAmbientTargetBuiltin closes that gap: it fires whenever this
// builtin is expanded in shared code, and drives the same per-target
// re-parse (-mllvm -reparse-divergent-users) that a #if-forked declaration
// already gets, reusing ASTContext::TargetScope + Decl::TargetVariant +
// redecl-merge (-mllvm -allow-target-variant-decls) as-is. This checks the
// primary target's own output is unaffected by that re-parse, and that a
// re-parse driven purely by this trigger is not rejected by the single-
// target-attribute filter meant for host/device forking (this declaration
// is __device__-only, which the entity-lookup trigger cannot re-parse).
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
