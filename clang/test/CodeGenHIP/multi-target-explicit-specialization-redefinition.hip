// An explicit class template specialization (`template<> struct
// traits<int>`) whose body diverges per target and gets widened into
// per-target copies. Checks that each target's copy of the specialization is
// treated as its own definition, not as a redefinition of another target's
// already-complete one. Uses an `int` template argument to isolate this from
// the tag-brace recognition case covered by
// multi-target-explicit-specialization-fundamental-type-arg.hip.
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
// full merge/reparse pipeline on -- this job must parse successfully rather
// than reporting "redefinition of 'traits<int>'", so simply reaching
// FileCheck below is itself part of what this test verifies.
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
