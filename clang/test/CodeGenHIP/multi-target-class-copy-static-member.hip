// A class copied per target whose static members differ. Code naming it names
// the primary's copy; a constant expression reading a static member reads the
// member of the copy for the target the code is compiled for, in shared code
// and inside an arm, including where CodeGen evaluates an attribute again.
//
// REQUIRES: amdgpu-registered-target

// Host-primary.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.h.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.h.gfx942.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1100 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=4:%t.h.gfx1100.bc
// RUN: llvm-dis %t.h.gfx90a.bc -o - | FileCheck --check-prefix=GFX90A %s
// RUN: llvm-dis %t.h.gfx942.bc -o - | FileCheck --check-prefix=GFX942 %s
// RUN: llvm-dis %t.h.gfx1100.bc -o - | FileCheck --check-prefix=GFX1100 %s

// Device-primary.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 -fcuda-is-device \
// RUN:   -x hip -emit-llvm -o %t.d.gfx942.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.d.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -target-cpu gfx1100 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.d.gfx1100.bc
// RUN: FileCheck --check-prefix=GFX942 %s < %t.d.gfx942.ll
// RUN: llvm-dis %t.d.gfx90a.bc -o - | FileCheck --check-prefix=GFX90A %s
// RUN: llvm-dis %t.d.gfx1100.bc -o - | FileCheck --check-prefix=GFX1100 %s

#define __global__ __attribute__((global))

#if defined(__gfx942__)
struct S { static constexpr int val = 900; };
#elif defined(__gfx90a__)
struct S { static constexpr int val = 800; };
#elif defined(__gfx1100__)
struct S { static constexpr int val = 700; };
#else
struct S { static constexpr int val = 600; };
#endif

// Shared code.
__attribute__((launch_bounds(S::val, 1))) __global__ void k1(int *o) {
  o[0] = S::val;
}

// An arm naming S through a member alias.
#if defined(__gfx942__)
template <int N> struct A { using T = S; static constexpr int w = 1; };
#elif defined(__gfx90a__)
template <int N> struct A { using T = S; static constexpr int w = 2; };
#elif defined(__gfx1100__)
template <int N> struct A { using T = S; static constexpr int w = 3; };
#else
template <int N> struct A { using T = S; static constexpr int w = 4; };
#endif

__attribute__((launch_bounds(A<1>::T::val + A<1>::w, 1))) __global__ void k2(int *o) {
  o[0] = A<1>::T::val + A<1>::w;
}

// GFX942: define {{.*}}@_Z2k1Pi({{.*}}) #[[K1:[0-9]+]]
// GFX942: store i32 900
// GFX942: define {{.*}}@_Z2k2Pi({{.*}}) #[[K2:[0-9]+]]
// GFX942: store i32 901
// GFX942-DAG: attributes #[[K1]] = {{.*}}"amdgpu-flat-work-group-size"="1,900"
// GFX942-DAG: attributes #[[K2]] = {{.*}}"amdgpu-flat-work-group-size"="1,901"

// GFX90A: define {{.*}}@_Z2k1Pi({{.*}}) #[[K1:[0-9]+]]
// GFX90A: store i32 800
// GFX90A: define {{.*}}@_Z2k2Pi({{.*}}) #[[K2:[0-9]+]]
// GFX90A: store i32 802
// GFX90A-DAG: attributes #[[K1]] = {{.*}}"amdgpu-flat-work-group-size"="1,800"
// GFX90A-DAG: attributes #[[K2]] = {{.*}}"amdgpu-flat-work-group-size"="1,802"

// GFX1100: define {{.*}}@_Z2k1Pi({{.*}}) #[[K1:[0-9]+]]
// GFX1100: store i32 700
// GFX1100: define {{.*}}@_Z2k2Pi({{.*}}) #[[K2:[0-9]+]]
// GFX1100: store i32 703
// GFX1100-DAG: attributes #[[K1]] = {{.*}}"amdgpu-flat-work-group-size"="1,700"
// GFX1100-DAG: attributes #[[K2]] = {{.*}}"amdgpu-flat-work-group-size"="1,703"
