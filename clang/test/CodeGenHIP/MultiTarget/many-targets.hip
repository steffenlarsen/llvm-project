// One compilation for seven device targets, more than the five a declaration
// could once be tagged for. Each target compiles what it would compile on its
// own, including the sixth and seventh aux targets.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx900 -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx900 -O2" \
// RUN:   -multi-target-aux-output=2:%t.gfx900.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx906 -O2" \
// RUN:   -multi-target-aux-output=3:%t.gfx906.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx908 -O2" \
// RUN:   -multi-target-aux-output=4:%t.gfx908.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=5:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942 -O2" \
// RUN:   -multi-target-aux-output=6:%t.gfx942.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1030 -O2" \
// RUN:   -multi-target-aux-output=7:%t.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1100 -O2" \
// RUN:   -multi-target-aux-output=8:%t.gfx1100.bc
// RUN: llvm-dis -o - %t.gfx900.bc | FileCheck -check-prefix=GFX900 %s
// RUN: llvm-dis -o - %t.gfx906.bc | FileCheck -check-prefix=GFX906 %s
// RUN: llvm-dis -o - %t.gfx908.bc | FileCheck -check-prefix=GFX908 %s
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=GFX90A %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=GFX942 %s
// RUN: llvm-dis -o - %t.gfx1030.bc | FileCheck -check-prefix=GFX1030 %s
// RUN: llvm-dis -o - %t.gfx1100.bc | FileCheck -check-prefix=GFX1100 %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__gfx900__)
#define ARCH 900
#elif defined(__gfx906__)
#define ARCH 906
#elif defined(__gfx908__)
#define ARCH 908
#elif defined(__gfx90a__)
#define ARCH 910
#elif defined(__gfx942__)
#define ARCH 942
#elif defined(__gfx1030__)
#define ARCH 1030
#elif defined(__gfx1100__)
#define ARCH 1100
#else
#define ARCH 0
#endif

__device__ int arch_value() { return ARCH; }

// A class only the last two targets lay out differently.
struct S {
#if ARCH >= 1000
  int extra[2];
#endif
  int b;
};

template <int N> __device__ int scaled() { return N * ARCH; }

__global__ void k(int *p) {
  p[0] = arch_value();
  p[1] = sizeof(S);
  p[2] = scaled<2>();
}
// GFX900-LABEL: define {{.*}} @_Z1kPi(
// GFX900: store i32 900,
// GFX900: store i32 4,
// GFX900: store i32 1800,
// GFX906-LABEL: define {{.*}} @_Z1kPi(
// GFX906: store i32 906,
// GFX906: store i32 4,
// GFX906: store i32 1812,
// GFX908-LABEL: define {{.*}} @_Z1kPi(
// GFX908: store i32 908,
// GFX908: store i32 4,
// GFX908: store i32 1816,
// GFX90A-LABEL: define {{.*}} @_Z1kPi(
// GFX90A: store i32 910,
// GFX90A: store i32 4,
// GFX90A: store i32 1820,
// GFX942-LABEL: define {{.*}} @_Z1kPi(
// GFX942: store i32 942,
// GFX942: store i32 4,
// GFX942: store i32 1884,
// GFX1030-LABEL: define {{.*}} @_Z1kPi(
// GFX1030: store i32 1030,
// GFX1030: store i32 12,
// GFX1030: store i32 2060,
// GFX1100-LABEL: define {{.*}} @_Z1kPi(
// GFX1100: store i32 1100,
// GFX1100: store i32 12,
// GFX1100: store i32 2200,
