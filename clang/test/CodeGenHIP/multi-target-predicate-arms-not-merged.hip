// An AMDGPU predicate is answered by each target for itself, so arms of a
// conditional that are the same tokens but answer differently are not
// equivalent. gfx1100's copy was merged into gfx1030's and took its answer
// (and in HIP's clock(), its s_memtime, which gfx1100 does not have).
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -std=c++17 -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip -std=c++17" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1030 -fcuda-is-device -O2 -x hip -std=c++17" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1100 -fcuda-is-device -O2 -x hip -std=c++17" \
// RUN:   -mllvm -multi-target-aux-output=4:%t.gfx1100.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=OTHER %s
// RUN: llvm-dis %t.gfx1030.bc -o - | FileCheck --check-prefix=OTHER %s
// RUN: llvm-dis %t.gfx1100.bc -o - | FileCheck --check-prefix=GFX1100 %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#ifdef __HIP_DEVICE_COMPILE__
__device__ inline int which() {
  if (__builtin_amdgcn_processor_is("gfx1100"))
    return 1100;
  return 1;
}
#else
__device__ inline int which() { return 0; }
#endif

// OTHER-LABEL: define {{.*}}@_Z1kPi(
// OTHER: store i32 1,
// GFX1100-LABEL: define {{.*}}@_Z1kPi(
// GFX1100: store i32 1100,
__global__ void k(int *p) { p[0] = which(); }
