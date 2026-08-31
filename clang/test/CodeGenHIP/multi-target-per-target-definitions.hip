// Definitions that differ per target where only part of the definition is
// conditional: a constructor's member initializer, and a class whose shared
// forward declaration precedes a definition per target. Each target compiles
// its own definition, as it would on its own.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc
// RUN: FileCheck -check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=GFX90A %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=GFX942 %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

// A constructor whose member initializer differs per architecture.
struct C {
  int a, b;
  __host__ __device__ C() : a(1)
#if defined(__gfx942__)
  , b(42)
#else
  , b(7)
#endif
  {}
};
__global__ void k_ctor_init(int *p) { C c; *p = c.b; }
// GFX90A-LABEL: define {{.*}} @_Z11k_ctor_initPi(
// GFX90A: store i32 7,
// GFX942-LABEL: define {{.*}} @_Z11k_ctor_initPi(
// GFX942: store i32 42,

// A class forward-declared once and defined per target.
struct F;
__device__ int size_through(F *f);
#if defined(__gfx90a__)
struct F { int a, b, c; };
#elif defined(__gfx942__)
struct F { int a, b; };
#else
struct F { int a; };
#endif
__global__ void k_fwd_sizeof(int *p) { p[0] = sizeof(F); }
// GFX90A-LABEL: define {{.*}} @_Z12k_fwd_sizeofPi(
// GFX90A: store i32 12,
// GFX942-LABEL: define {{.*}} @_Z12k_fwd_sizeofPi(
// GFX942: store i32 8,
__device__ int size_through(F *f) { return f->a + sizeof(*f); }
__global__ void k_fwd_pointer(int *p) { F f{}; p[0] = size_through(&f); }
// GFX90A-LABEL: define {{.*}} @_Z13k_fwd_pointerPi(
// GFX90A: store i32 12,
// GFX942-LABEL: define {{.*}} @_Z13k_fwd_pointerPi(
// GFX942: store i32 8,
int host_size() { return sizeof(F); }
// HOST-LABEL: define {{.*}} @_Z9host_sizev(
// HOST: ret i32 4
