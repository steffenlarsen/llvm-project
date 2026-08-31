// A class declared before its definition differs between targets (rocWMMA's
// float8.hpp declares and aliases __hip_fp8_e4m3 before amd_hip_fp8.h defines
// it, with host-only members on gfx942). Every target's definition then
// redeclares the one declaration, and each must still get the implicit members
// it needs, e.g. a copy constructor, and find its own fields when defining them.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc \
// RUN: -verify
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck %s

// expected-no-diagnostics

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

// amd_hip_fp8.h's HIP_FP8_TYPE_OCP.
#if defined(__gfx942__) && __HIP_DEVICE_COMPILE__
#define OCP 0
#else
#define OCP 1
#endif

struct fp8;
using fp8_t = fp8;

struct fp8 {
  unsigned char bits;
#if OCP
  __host__ __device__ fp8(int v) : bits(v) {}
#else
  __host__ fp8(int v) : bits(v) {}
#endif
#if OCP
  __host__ __device__ fp8(double v) : bits(v) {}
#else
  __host__ fp8(double v) : bits(v) {}
#endif
  fp8() = default;
#if OCP
  __host__ __device__ operator bool() const { return bits; }
#else
  __host__ operator bool() const { return bits; }
#endif
#if OCP
  __host__ __device__ operator int() const { return bits; }
#else
  __host__ operator int() const { return bits; }
#endif
};

// A copy, not a conversion through bool or int and back.
#if OCP
#define FP8_VISIBILITY __host__ __device__
#else
#define FP8_VISIBILITY __host__
#endif
FP8_VISIBILITY constexpr fp8_t copy(const fp8_t &f) { return f; }

// CHECK-LABEL: define {{.*}} @_Z4copyRK3fp8(
// CHECK-NOT: call
// CHECK: ret
// copy is host-only on gfx942, so device code may call it only elsewhere.
__global__ void kernel(fp8 *out, const fp8 *in) {
#if OCP
  *out = copy(*in);
#endif
}
