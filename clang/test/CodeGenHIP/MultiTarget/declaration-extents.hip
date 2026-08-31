// Declarations whose extent the re-parse used to misjudge, and so kept shared:
// one starting with attributes, which its begin location follows, and a list
// of declarators after a braced initializer, whose `}` looked like the end of
// a declaration. A later declarator naming an earlier one was ambiguous.
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

[[maybe_unused]] __device__ int a[W];
alignas(16) __device__ int b[W];
__attribute__((used)) [[maybe_unused]] __device__ int c[W];
__device__ int d[W] = {}, *e = d;

// GFX90A-LABEL: define {{.*}} @_Z5sizesPi(
// GFX90A: store i32 360,
// GFX90A: store i32 360,
// GFX90A: store i32 360,
// GFX90A: store i32 360,
// GFX942-LABEL: define {{.*}} @_Z5sizesPi(
// GFX942: store i32 168,
// GFX942: store i32 168,
// GFX942: store i32 168,
// GFX942: store i32 168,
__global__ void sizes(int *p) {
  p[0] = sizeof(a);
  p[1] = sizeof(b);
  p[2] = sizeof(c);
  p[3] = sizeof(d) + 0 * (e != nullptr);
}
