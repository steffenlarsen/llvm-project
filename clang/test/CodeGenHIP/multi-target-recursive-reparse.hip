// A function that calls a function diverging per target is re-parsed once per
// target, and a recursive call inside it names the copy being re-parsed. That
// copy is only tagged for its target once complete, so the primary's copy,
// which a re-parse falls back to, must not be taken as a second candidate.
// This follows HeCBench logic-rewrite refactor_mffc ("call to
// 'topoSortGetLevel' is ambiguous").
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

#ifdef __HIP_DEVICE_COMPILE__
HD inline int helper(int x) { return x + 10; }
#else
HD inline int helper(int x) { return x + 20; }
#endif

int separator;

HD int rec(int n) {
  if (n <= 0)
    return 0;
  return helper(n) + rec(n - 1);
}

template <class T> HD T rec_t(T n) {
  if (n <= 0)
    return 0;
  return helper(n) + rec_t(n - 1);
}

__global__ void kernel(int *p) { p[0] = rec(p[1]) + rec_t(p[2]); }
int host_use(int x) { return rec(x) + rec_t(x); }

// HOST-DAG: define {{.*}} @_Z3reci(
// HOST-DAG: call {{.*}} @_Z3reci(
// HOST-DAG: define {{.*}} @_Z5rec_tIiET_S0_(
// HOST-DAG: call {{.*}} @_Z5rec_tIiET_S0_(
// HOST-DAG: add nsw i32 %{{.*}}, 20{{$}}

// DEV-DAG: define {{.*}} @_Z3reci(
// DEV-DAG: call {{.*}} @_Z3reci(
// DEV-DAG: define {{.*}} @_Z5rec_tIiET_S0_(
// DEV-DAG: call {{.*}} @_Z5rec_tIiET_S0_(
// DEV-DAG: add nsw i32 %{{.*}}, 10{{$}}
