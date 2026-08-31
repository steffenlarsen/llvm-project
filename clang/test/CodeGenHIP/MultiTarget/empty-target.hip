// A target that sees no declarations at all, because the whole file is under
// an #if on another target, compiles nothing, and the others still compile
// what they see. Neither falls back to another target's declarations.
//
// REQUIRES: amdgpu-registered-target

// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -emit-llvm -o %t/host.ll %t/host-only.hip \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t/gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942" \
// RUN:   -multi-target-aux-output=3:%t/gfx942.bc
// RUN: FileCheck -check-prefix=HOST-ONLY-HOST %s < %t/host.ll
// RUN: llvm-dis -o - %t/gfx90a.bc | FileCheck -check-prefix=EMPTY %s
// RUN: llvm-dis -o - %t/gfx942.bc | FileCheck -check-prefix=EMPTY %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -emit-llvm -o %t/host.ll %t/device-only.hip \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t/gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942" \
// RUN:   -multi-target-aux-output=3:%t/gfx942.bc
// RUN: FileCheck -check-prefix=EMPTY %s < %t/host.ll
// RUN: llvm-dis -o - %t/gfx90a.bc | FileCheck -check-prefix=DEVICE-ONLY %s \
// RUN:   --implicit-check-not=only942
// RUN: llvm-dis -o - %t/gfx942.bc | FileCheck -check-prefixes=DEVICE-ONLY,GFX942-ONLY %s

// HOST-ONLY-HOST: define {{.*}} @_Z1hv(
// EMPTY-NOT: define
// DEVICE-ONLY: define {{.*}} @_Z7devonlyPi(
// GFX942-ONLY: define {{.*}} @_Z7only942Pi(

//--- host-only.hip
#define __global__ __attribute__((global))
#ifndef __HIP_DEVICE_COMPILE__
int h() { return 2; }
#endif

//--- device-only.hip
#define __global__ __attribute__((global))
#ifdef __HIP_DEVICE_COMPILE__
__global__ void devonly(int *o) { o[0] = 1; }
#endif
#ifdef __gfx942__
__global__ void only942(int *o) { o[0] = 2; }
#endif
