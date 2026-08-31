// With -fgpu-rdc the single multi-target job packages every target's device
// bitcode unlinked (-multi-target-package-offload) and embeds the package in
// the host, as the host job embeds the packager job's output without
// -fintegrated-hip-device-codegen; the final link then links device code
// across translation units. Each target's image holds that target's own copy
// of a divergent function, and a function defined in another translation
// unit stays external.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 -nogpulib \
// RUN:   -nogpuinc -fgpu-rdc -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -Xclang -verify %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -fgpu-rdc --offload-new-driver \
// RUN:   -emit-llvm -o %t.host.ll %s -fembed-offload-object=%t.package \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -fgpu-rdc --offload-new-driver -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -fgpu-rdc --offload-new-driver -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc \
// RUN:   -mllvm -multi-target-codegen \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -merge-equivalent-variants \
// RUN:   -mllvm -multi-target-package-offload=%t.package
// RUN: FileCheck -check-prefix=HOST %s < %t.host.ll
// RUN: llvm-offload-binary %t.package \
// RUN:   --image=file=%t.img90a.bc,triple=amdgcn-amd-amdhsa,arch=gfx90a \
// RUN:   --image=file=%t.img942.bc,triple=amdgcn-amd-amdhsa,arch=gfx942
// RUN: llvm-dis -o - %t.img90a.bc | FileCheck -check-prefixes=DEV,GFX90A %s
// RUN: llvm-dis -o - %t.img942.bc | FileCheck -check-prefixes=DEV,GFX942 %s

// expected-no-diagnostics

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

__device__ int other(int x) {
#if defined(__gfx90a__)
  return x + 90;
#elif defined(__gfx942__)
  return x + 942;
#else
  return x;
#endif
}

extern __device__ int elsewhere(int);

__global__ void kernel(int *p) { p[0] = elsewhere(p[1]); }

// HOST: @.offloading.entry._Z6kernelPi = {{.*}} ptr @_Z6kernelPi
// HOST: @llvm.embedded.object = private constant {{.*}} c"\10\FF\10\AD
// HOST: !{ptr @llvm.embedded.object, !".llvm.offloading"}

// DEV-LABEL: define dso_local noundef i32 @_Z5otheri(
// GFX90A: add nsw i32 %{{.*}}, 90
// GFX942: add nsw i32 %{{.*}}, 942
// DEV: define dso_local amdgpu_kernel void @_Z6kernelPi(
// DEV: declare noundef i32 @_Z9elsewherei(
