// An explicit instantiation definition whose members call a function that
// diverges per target touches a divergent entity, so it is re-parsed once per
// target. Each replay names the specialization the first one already
// instantiated: it must be accepted, not diagnosed as a duplicate, and must not
// add another declaration of it either. This follows HeCBench halo-finder
// ("duplicate explicit instantiation of 'RCBForceTree<...>'").
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
#define HD __host__ __device__

#ifdef __HIP_DEVICE_COMPILE__
HD inline int helper(int x) { return x + 10; }
#else
HD inline int helper(int x) { return x + 20; }
#endif

template <class T> struct tree {
  T v;
  HD int get() const { return helper(v); }
};

template struct tree<int>;
template struct tree<long>;

// HOST-DAG: define weak_odr {{.*}} @_ZNK4treeIiE3getEv(
// HOST-DAG: define weak_odr {{.*}} @_ZNK4treeIlE3getEv(
// HOST-DAG: add nsw i32 %{{.*}}, 20{{$}}

// DEV-DAG: define {{.*}} @_ZNK4treeIiE3getEv(
// DEV-DAG: define {{.*}} @_ZNK4treeIlE3getEv(
// DEV-DAG: add nsw i32 %{{.*}}, 10{{$}}
