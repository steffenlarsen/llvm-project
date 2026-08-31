// A using-declaration replayed for every target names each target's copy of a
// function template whose body diverges. Each target's shadow must stand only
// for that target's copy, whether the using-declaration is replayed by a
// re-parse (its shadows stay untagged) or inside a widened region, where
// ordinary lookup must also not return another alternative's
// using-declaration in place of a shadow it cannot see. This follows HeCBench
// mmcsf ("call to 'merge_vector4' is ambiguous", from boost::sort).
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a -nogpulib -nogpuinc \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only -Xclang -verify %s
// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpulib -nogpuinc -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -Xclang -verify %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc \
// RUN:   -mllvm -multi-target-codegen \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -merge-equivalent-variants
// RUN: FileCheck -check-prefix=HOST %s < %t.host.ll \
// RUN:   --implicit-check-not="add nsw i32 %{{.*}}, 10{{$}}"
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=DEV %s \
// RUN:   --implicit-check-not="add nsw i32 %{{.*}}, 20{{$}}"
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=DEV %s \
// RUN:   --implicit-check-not="add nsw i32 %{{.*}}, 20{{$}}"

// expected-no-diagnostics

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
#define HD __host__ __device__

namespace a {
template <class T> HD T f(T t) {
#ifdef __HIP_DEVICE_COMPILE__
  return t + 10;
#else
  return t + 20;
#endif
}
} // namespace a

// Re-parsed for every target.
namespace r {
namespace aa = a;
using aa::f;
HD int g(int v) { return f(v); }
} // namespace r

// Inside a widened region.
namespace w {
#ifdef __HIP_DEVICE_COMPILE__
constexpr int k = 1;
#else
constexpr int k = 2;
#endif
using a::f;
template <class T> HD T g(T t) { return f(t); }
} // namespace w

__global__ void kernel(int *p) { p[0] = r::g(p[1]) + w::g(p[2]); }
int host_use(int x) { return r::g(x) + w::g(x); }

// HOST-DAG: define {{.*}} @_ZN1a1fIiEET_S1_({{.*}} {
// HOST-DAG: add nsw i32 %{{.*}}, 20{{$}}
// HOST-DAG: define {{.*}} @_ZN1r1gEi({{.*}} {
// HOST-DAG: define {{.*}} @_ZN1w1gIiEET_S1_({{.*}} {

// DEV-DAG: define {{.*}} @_ZN1a1fIiEET_S1_({{.*}} {
// DEV-DAG: add nsw i32 %{{.*}}, 10{{$}}
// DEV-DAG: define {{.*}} @_ZN1r1gEi({{.*}} {
// DEV-DAG: define {{.*}} @_ZN1w1gIiEET_S1_({{.*}} {
