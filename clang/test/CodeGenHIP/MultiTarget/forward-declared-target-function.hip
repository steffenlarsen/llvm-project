// A constexpr function declared once and defined per target, after a class
// template whose static member calls it and after a specialization of that
// template is declared. The call names the shared declaration, whose
// redeclarations are the targets' definitions, so each target evaluates its
// own, wherever the specialization is used first.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O1 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -O1 -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc
// RUN: FileCheck --check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=DEV %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

constexpr __host__ __device__ int width();
template <class T> struct W { static constexpr int v = T(width()); };
W<int> *declared_early;

constexpr __host__ __device__ int width() {
#if defined(__HIP_DEVICE_COMPILE__)
  return 64;
#else
  return 32;
#endif
}

__global__ void k(int *o) { o[0] = W<int>::v; }
int host_value() { return W<int>::v; }

// HOST-LABEL: define {{.*}}@_Z10host_valuev(
// HOST: ret i32 32
// DEV-LABEL: define {{.*}}amdgpu_kernel void @_Z1kPi(
// DEV: store i32 64,
