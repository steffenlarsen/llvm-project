// A function re-parsed for each target because it evaluates something that
// differs between targets keeps each target's copy: its copies can look alike
// and still differ, through a constant, a constexpr function or a template
// argument. Each target compiles its own copy.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o %t.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=GFX90A %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefix=GFX942 %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__gfx942__)
constexpr int W = 2;
constexpr int w() { return 2; }
enum E { V = 2 };
#elif defined(__gfx90a__)
constexpr int W = 1;
constexpr int w() { return 1; }
enum E { V = 1 };
#else
constexpr int W = 0;
constexpr int w() { return 0; }
enum E { V = 0 };
#endif
template <int N> struct I { static constexpr int v = N; };

// Copies that do not differ are still defined for every target.
__host__ __device__ inline int same(int v) {
  static_assert(W >= 0, "");
  return v + 5;
}
// GFX90A: define linkonce_odr {{.*}} @_Z4samei(
// GFX942: define linkonce_odr {{.*}} @_Z4samei(

__host__ __device__ inline int by_constant() { constexpr int a = W; return a + 5; }
// GFX90A-LABEL: define linkonce_odr {{.*}} @_Z11by_constantv(
// GFX90A: ret i32 6
// GFX942-LABEL: define linkonce_odr {{.*}} @_Z11by_constantv(
// GFX942: ret i32 7

__host__ __device__ inline int by_function() { constexpr int a = w(); return a + 5; }
// GFX90A-LABEL: define linkonce_odr {{.*}} @_Z11by_functionv(
// GFX90A: ret i32 6
// GFX942-LABEL: define linkonce_odr {{.*}} @_Z11by_functionv(
// GFX942: ret i32 7

__host__ __device__ inline int by_argument() { return I<V>::v + 5; }
// GFX90A-LABEL: define linkonce_odr {{.*}} @_Z11by_argumentv(
// GFX90A: ret i32 6
// GFX942-LABEL: define linkonce_odr {{.*}} @_Z11by_argumentv(
// GFX942: ret i32 7

__global__ void k(int *o) {
  o[0] = same(o[0]);
  o[1] = by_constant();
  o[2] = by_function();
  o[3] = by_argument();
}
