// A call in a host-device function whose overload resolution depends on the
// side compiled for (here host-only and device-only overloads) is resolved
// for both sides by the host-primary Sema. The device's call must survive
// template instantiation, which does not resolve an already-resolved call
// again, or the device calls the host's function. And where such a call is
// an argument of another, the device's outer call is resolved from the
// device's inner call, or the device converts the device's float result as
// if it were the host's int. This follows HeCBench atan2 ("undefined symbol:
// std::abs(float)") and dxtc2 (`max(max(x, y), z)`, "Invalid cast").
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
// RUN:   --implicit-check-not=@_Z4pickff --implicit-check-not=@_Z2mxff
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=DEV %s \
// RUN:   --implicit-check-not=@_Z4pickii --implicit-check-not=@_Z2mxii \
// RUN:   --implicit-check-not="call {{.*}}@_Z2mxif"
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=DEV %s \
// RUN:   --implicit-check-not=@_Z4pickii --implicit-check-not=@_Z2mxii \
// RUN:   --implicit-check-not="call {{.*}}@_Z2mxif"

// expected-no-diagnostics

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
#define HD __host__ __device__

__host__ float pick(int a, int b) { return a > b ? a : b; }
__device__ float pick(float a, float b) { return a > b ? a : b; }

template <int D> HD float in_template(float x, float y) {
  return pick(x, y) * D;
}

__host__ int mx(int a, int b) { return a > b ? a : b; }
__device__ float mx(float a, float b) { return a > b ? a : b; }
__device__ double mx(int a, float b) { return a + b; }

HD float nested(float x, float y, float z) { return mx(mx(x, y), z); }

__global__ void kernel(float *p) {
  p[0] = in_template<3>(p[1], p[2]) + nested(p[3], p[4], p[5]);
}
float host_use(float x) { return in_template<3>(x, x) + nested(x, x, x); }

// HOST-LABEL: define {{.*}} @_Z6nestedfff(
// HOST: call {{.*}} i32 @_Z2mxii(
// HOST: call {{.*}} i32 @_Z2mxii(
// HOST-LABEL: define {{.*}} @_Z11in_templateILi3EEfff(
// HOST: call {{.*}} float @_Z4pickii(

// DEV-LABEL: define {{.*}} @_Z6nestedfff(
// DEV: call {{.*}} float @_Z2mxff(
// DEV: call {{.*}} float @_Z2mxff(
// DEV-LABEL: define {{.*}} @_Z11in_templateILi3EEfff(
// DEV: call {{.*}} float @_Z4pickff(
