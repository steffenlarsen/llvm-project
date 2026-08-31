// An explicit class template specialization for a fundamental-type argument
// (e.g. `template<> struct traits<double> { ... };`, the shape libstdc++'s
// `numeric_limits<long double>` takes) whose body diverges per target and
// gets widened into per-target copies. Exercises two things a fundamental
// argument type needs. One is recognizing `traits<double> {` as a tag body
// (not a function body) when chunking the token stream for per-target
// splitting. The other is treating each target's copy of the specialization
// as its own definition rather than a redefinition of another target's.
//
// The test runs cc1 directly. A driver-level `%clang -x hip` test would
// preinclude the HIP runtime wrapper, which pulls in libstdc++'s own
// numeric_limits machinery and hits an unrelated ambiguity in
// __numeric_traits_floating.
//
//
// REQUIRES: amdgpu-registered-target

// The ground truth is each arch compiled completely on its own.
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
// RUN:   "-multi-target-aux-invocation=%{hip_device} -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc
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
