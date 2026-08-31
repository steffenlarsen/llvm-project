// #pragma clang optimize and #pragma clang section set what the declarations
// after them get, and #pragma comment names a library to link. Each target sees its own pragmas, where its own compilation
// would, also where only some targets have them. Declarations the targets see
// different pragmas for are analysed per target, the others stay shared.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -O1 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -O1 -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -O1 -target-cpu gfx942" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc \
// RUN:   -mllvm -trace-reparse 2> %t.reparse
// RUN: FileCheck --check-prefix=HOST --implicit-check-not=devlib %s < %t.host.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=DEV %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefix=DEV %s
// RUN: FileCheck --check-prefix=REPARSE %s < %t.reparse

// The targets' own compilations agree.
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -O1 -x hip -emit-llvm -o - %s \
// RUN:   | FileCheck --check-prefix=HOST --implicit-check-not=devlib %s
// RUN: %clang_cc1 -std=c++17 %{hip_device} %{host_aux_triple} -O1 \
// RUN:   -target-cpu gfx90a -emit-llvm -o - %s | FileCheck --check-prefix=DEV %s

#define __device__ __attribute__((device))
#define __host__ __attribute__((host))

// Globals come first in the IR, the attribute groups their sections refer to
// last.
// DEV: @zz_dev_bss = {{.*}} #[[DEV_SECTIONS:[0-9]+]]
// DEV: @zz_dev_data = {{.*}} #[[DEV_SECTIONS]]
// DEV: @zz_plain = {{.*}}, align 4{{$}}
// HOST: @zz_host_bss = {{.*}} #[[HOST_SECTIONS:[0-9]+]]

// The devices turn optimizations off, the host does not. An optnone function
// keeps its parameter in an alloca at -O1.
#if defined(__HIP_DEVICE_COMPILE__)
#pragma clang optimize off
#endif
__host__ __device__ int zz_dev_off(int x) { return x * 2; }
__host__ __device__ int zz_dev_off_2(int x) { return x * 3; }
#if defined(__HIP_DEVICE_COMPILE__)
#pragma clang optimize on
#endif
__host__ __device__ int zz_after(int x) { return x * 4; }
// DEV-LABEL: define {{.*}} @_Z10zz_dev_offi(
// DEV-NEXT: entry:
// DEV-NEXT: alloca
// DEV-LABEL: define {{.*}} @_Z12zz_dev_off_2i(
// DEV-NEXT: entry:
// DEV-NEXT: alloca
// DEV-LABEL: define {{.*}} @_Z8zz_afteri(
// DEV-NOT: alloca
// DEV: ret i32
// HOST-LABEL: define {{.*}} @_Z10zz_dev_offi(
// HOST-NOT: alloca
// HOST: ret i32
// HOST-LABEL: define {{.*}} @_Z12zz_dev_off_2i(
// HOST-NOT: alloca
// HOST: ret i32
// HOST-LABEL: define {{.*}} @_Z8zz_afteri(
// HOST-NOT: alloca
// HOST: ret i32

// Only the host turns them off.
#if !defined(__HIP_DEVICE_COMPILE__)
#pragma clang optimize off
#endif
__host__ __device__ int zz_host_off(int x) { return x * 5; }
#if !defined(__HIP_DEVICE_COMPILE__)
#pragma clang optimize on
#endif
// DEV-LABEL: define {{.*}} @_Z11zz_host_offi(
// DEV-NOT: alloca
// DEV: ret i32
// HOST-LABEL: define {{.*}} @_Z11zz_host_offi(
// HOST-NEXT: entry:
// HOST-NEXT: alloca

// Every target turns them off.
#pragma clang optimize off
__host__ __device__ int zz_shared_off(int x) { return x * 6; }
#pragma clang optimize on
// DEV-LABEL: define {{.*}} @_Z13zz_shared_offi(
// DEV-NEXT: entry:
// DEV-NEXT: alloca
// HOST-LABEL: define {{.*}} @_Z13zz_shared_offi(
// HOST-NEXT: entry:
// HOST-NEXT: alloca

// The sections differ.
#if defined(__HIP_DEVICE_COMPILE__)
#pragma clang section bss = ".dev_bss" data = ".dev_data"
#else
#pragma clang section bss = ".host_bss"
#endif
__device__ int zz_dev_bss;
__device__ int zz_dev_data = 1;
int zz_host_bss;
#pragma clang section bss = "" data = ""
__device__ int zz_plain;
// DEV: attributes #[[DEV_SECTIONS]] = { "bss-section"=".dev_bss" "data-section"=".dev_data" }
// HOST: attributes #[[HOST_SECTIONS]] = { "bss-section"=".host_bss" }

// The libraries differ. Only the host records them.
#if defined(__HIP_DEVICE_COMPILE__)
#pragma comment(lib, "devlib")
#else
#pragma comment(lib, "hostlib")
#endif
// HOST: !{!"hostlib"}

__attribute__((device, used)) int use(int x) {
  return zz_dev_off(x) + zz_dev_off_2(x) + zz_after(x) + zz_host_off(x) +
         zz_shared_off(x) + zz_dev_bss + zz_dev_data + zz_plain;
}
int host_use(int x) {
  return zz_dev_off(x) + zz_dev_off_2(x) + zz_after(x) + zz_host_off(x) +
         zz_shared_off(x) + zz_host_bss;
}

// A function after a pragma only some targets have is analysed per target.
// One whose targets all see the same pragmas is not.
// REPARSE: reparse {{.*}} zz_dev_off_2
// REPARSE-NOT: zz_after
// REPARSE-NOT: zz_shared_off
