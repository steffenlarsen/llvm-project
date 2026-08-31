// An explicit class template specialization for a fundamental-type argument
// (e.g. `template<> struct traits<double> { ... };`, the shape libstdc++'s
// `numeric_limits<long double>` takes) whose body diverges per target and
// gets widened into per-target copies. Exercises two things a fundamental
// argument type needs: recognizing `traits<double> {` as a tag body (not a
// function body) when chunking the token stream for per-target splitting,
// and treating each target's copy of the specialization as its own
// definition rather than a redefinition of another target's.
//
// Modeled on multi-target-class-template-value-member.hip's cc1-direct
// pattern: driver-level `%clang -x hip` tests route through the mandatory
// HIP runtime-wrapper preinclude, which pulls in libstdc++'s own
// numeric_limits machinery and hits a separate, pre-existing "ambiguous
// __numeric_traits_floating" issue unrelated to what this test covers.
//
// REQUIRES: amdgpu-registered-target

// Ground truth: each arch compiled completely on its own.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX90A %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 \
// RUN:   -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck -check-prefix=GFX942 %s

// One primary (gfx942) cc1 job, with AuxGen configured for gfx90a and the
// full merge/reparse pipeline on -- each aux target's own bitcode output
// must have traits<double>::value computed for its own target, not the
// primary's.
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

template <> struct traits<double> {
#if defined(__gfx942__)
  static constexpr int value = 942;
#else
  static constexpr int value = 90;
#endif
};

// GFX90A: store i32 90
// GFX942: store i32 942
__attribute__((global)) void kernel_like(int *out) {
  out[0] = traits<double>::value;
}
