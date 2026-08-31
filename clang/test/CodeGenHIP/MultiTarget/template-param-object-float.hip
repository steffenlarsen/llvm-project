// A template parameter object every target shares holds a `long double` in
// the host's format. Evaluated for a device, where `long double` is `double`,
// its value is read in the device's format.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

struct V {
  long double x;
};
template <V v> __device__ double get() { return (double)(v.x * 3 - 1); }

// CHECK-LABEL: define {{.*}} @_Z1kPd(
// CHECK: store double 0.000000e+00,
__global__ void k(double *p) { p[0] = get<V{1.0 / 4 + 1.0 / 12}>(); }
double host() { return (double)(V{1.0 / 3}.x); }
