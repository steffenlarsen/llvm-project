// A constant evaluated for one target is not another target's value. The
// devices lay out and compute `long double` as `double`, the host does not,
// so each target folds these with its own format and size. Reusing the
// host's value made the devices fold with x87 values ("Should only call on
// two APFloats with the same semantics").
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: FileCheck -check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=DEV %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=DEV %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

constexpr long double third = 1.0 / 3;
constexpr int ld_size = sizeof(long double);
struct Consts {
  static constexpr long double third = 1.0 / 3;
};

// DEV-LABEL: define {{.*}} double @_Z9var_errord(
// DEV: fmul contract double %{{.*}}, 0.000000e+00
__device__ double var_error(double y) { return y * (double)(third * 3 - 1); }

// DEV-LABEL: define {{.*}} i32 @_Z8size_tenv(
// DEV: ret i32 80
__device__ int size_ten() { return ld_size * 10; }

// DEV-LABEL: define {{.*}} double @_Z12member_errord(
// DEV: fmul contract double %{{.*}}, 0.000000e+00
__device__ double member_error(double y) {
  return y * (double)(Consts::third * 3 - 1);
}

// DEV-LABEL: define {{.*}} void @_Z6kernelPd(
// DEV: fadd contract double 0.000000e+00,
__global__ void kernel(double *p) { p[0] = (double)(third * 3 - 1) + p[1]; }

// HOST-LABEL: define {{.*}} i32 @_Z13host_size_tenv(
// HOST: ret i32 160
int host_size_ten() { return ld_size * 10; }

// HOST-LABEL: define {{.*}} double @_Z10host_errord(
// HOST-NOT: fmul contract double %{{.*}}, 0.000000e+00
// HOST: ret double
double host_error(double y) { return y * (double)(third * 3 - 1); }
