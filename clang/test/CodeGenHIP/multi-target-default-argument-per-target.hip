// A call uses the default arguments of the declarations its target has. A
// declaration giving a device architecture its own default is kept apart, and
// the definition that redeclares it is analysed for each target, inheriting
// that target's default. So is a definition analysed per target for another
// reason, which inherits the default every target shares.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc
// RUN: FileCheck --check-prefix=HOST --input-file=%t.host.ll %s
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefixes=CHECK,GFX90A %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefixes=CHECK,GFX942 %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__gfx942__)
__device__ int per_target(int a = 42);
#else
__device__ int per_target(int a = 7);
#endif
__device__ int per_target(int a) { return a; }
// CHECK-LABEL: define {{.*}}@per_target_default(
// GFX90A: store i32 7,
// GFX942: store i32 42,
extern "C" __global__ void per_target_default(int *p) { *p = per_target(); }

__device__ int shared(int a = 7);
__device__ int shared(int a) {
#if defined(__gfx942__)
  return a + 1;
#else
  return a;
#endif
}
// CHECK-LABEL: define {{.*}}@shared_default(
// GFX90A: store i32 7,
// GFX942: store i32 8,
extern "C" __global__ void shared_default(int *p) { *p = shared(); }

__device__ int on_definition(int a, int b);
#if defined(__gfx942__)
__device__ int on_definition(int a, int b = 42) { return a + b; }
#else
__device__ int on_definition(int a, int b = 7) { return a - b; }
#endif
// CHECK-LABEL: define {{.*}}@default_on_definition(
// GFX90A: store i32 -6,
// GFX942: store i32 43,
extern "C" __global__ void default_on_definition(int *p) {
  *p = on_definition(1);
}

#if defined(__gfx942__)
__host__ __device__ int both(int a = 42);
#elif defined(__HIP_DEVICE_COMPILE__)
__host__ __device__ int both(int a = 7);
#else
__host__ __device__ int both(int a = 3);
#endif
__host__ __device__ int both(int a) { return a; }
// CHECK-LABEL: define {{.*}}@host_and_device_default(
// GFX90A: store i32 7,
// GFX942: store i32 42,
extern "C" __global__ void host_and_device_default(int *p) { *p = both(); }
// HOST-LABEL: define {{.*}}@host_use(
// HOST: ret i32 3
extern "C" int host_use() { return both(); }
