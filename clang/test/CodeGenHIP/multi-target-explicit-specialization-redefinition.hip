// PROTOTYPE: Sema::ActOnClassTemplateSpecialization's redefinition check looks
// up the prior specialization via ClassTemplateDecl::findSpecialization, which
// is keyed by template arguments in a FoldingSet, not by target -- so it always
// hands back the *first* target's already-complete definition, regardless of
// which target is currently being parsed. Without a target-variant guard
// (mirroring the one in CheckForFunctionRedefinition and the primary
// class-template path in CheckClassTemplate), the second target's
// identical-looking definition was flagged as a self-referential
// "redefinition of 'traits<int>'".
//
// This uses an `int` template argument specifically to isolate this bug from
// the separate isTagBrace fundamental-type-keyword gap covered by
// multi-target-explicit-specialization-fundamental-type-arg.hip: `int` was
// already tolerated by isTagBrace's backward scan before that fix, so this
// specialization's tag brace was never misclassified -- only the redefinition
// check itself was wrong.
//
// Modeled on multi-target-class-template-value-member.hip's cc1-direct
// pattern; see that file's fundamental-type-arg sibling for why this avoids
// the driver-level `%clang -x hip` invocation style.
//
// REQUIRES: amdgpu-registered-target

// Ground truth: each arch compiled completely on its own.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX90A %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX942 %s

// One primary (gfx942) cc1 job, with AuxGen configured for gfx90a and the
// full merge/reparse pipeline on -- without the redefinition-check fix, this
// job fails to parse at all ("redefinition of 'traits<int>'"), so simply
// reaching FileCheck below is itself part of what this test verifies.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o %t.primary.ll %s \
// RUN:   -mllvm -multi-target-codegen \
// RUN:   -mllvm -multi-target-aux-target=2:gfx90a: \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -merge-equivalent-variants
// RUN: FileCheck -check-prefix=GFX942 %s < %t.primary.ll
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=GFX90A %s

template <typename T> struct traits {
  static constexpr int value = 0;
};

template <> struct traits<int> {
#if defined(__gfx942__)
  static constexpr int value = 942;
#else
  static constexpr int value = 90;
#endif
};

// GFX90A: store i32 90
// GFX942: store i32 942
__attribute__((global)) void kernel_like(int *out) {
  out[0] = traits<int>::value;
}
