// A declaration of more than 20000 tokens whose meaning differs per target is
// still analysed per target. It used to be kept shared without a diagnostic.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx942 -O2" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=GFX90A %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=GFX942 %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__gfx942__)
constexpr int W = 42;
#else
constexpr int W = 90;
#endif
template <int N> struct Box { static constexpr int v = N; };

#define R4(x) x x x x
#define R10(x) x x x x x x x x x x

// GFX90A-LABEL: define {{.*}} @_Z4kernPi(
// GFX90A: store i32 360,
// GFX90A: store i32 90,
// GFX942-LABEL: define {{.*}} @_Z4kernPi(
// GFX942: store i32 168,
// GFX942: store i32 42,
__global__ void kern(int *p) {
  int buf[W];
  p[0] = sizeof(buf);
  p[1] = Box<W>::v;
  R4(R10(R10(R10((void)0;))))
}
