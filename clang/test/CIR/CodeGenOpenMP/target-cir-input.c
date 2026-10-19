// An OpenMP device module resumed from CIR with -x cir, as the offload-merge
// pipeline lowers it. The device CIR is generated before the host IR exists,
// which only the lowering is given. The lowering is not told about OpenMP
// either, the module records it.
//
// RUN: %clang_cc1 -fopenmp -triple x86_64-linux-gnu -fopenmp-targets=amdgpu9.00-amd-amdhsa \
// RUN:   -fclangir -emit-llvm-bc %s -o %t-host.bc
// RUN: %clang_cc1 -triple amdgpu9.00-amd-amdhsa -fopenmp -fopenmp-is-target-device \
// RUN:   -fclangir -emit-cir %s -o %t-device.cir
// RUN: %clang_cc1 -triple amdgpu9.00-amd-amdhsa -fopenmp-host-ir-file-path %t-host.bc \
// RUN:   -fclangir -emit-llvm -x cir %t-device.cir -o - \
// RUN:   | FileCheck %s

void use(int);
int host_helper(int);

void f(int x) {
  // Host-only computation that must not leak into the device module.
  int y = host_helper(x);
  use(y);
#pragma omp target map(tofrom : x)
  {
    x = x + 1;
  }
}

// The host operations are filtered out of the device module.
// CHECK-LABEL: define hidden void @f(
// CHECK-NOT:     call i32 @host_helper
// CHECK-NOT:     call void @use
// CHECK:         ret void

// The kernel is registered from the host offload entries.
// CHECK:       define weak_odr protected amdgpu_kernel void @__omp_offloading_{{.*}}_f_l{{[0-9]+}}({{.*}}) #[[KERNEL:[0-9]+]]
// CHECK:         call i32 @__kmpc_target_init(
// CHECK:       attributes #[[KERNEL]] = {{{.*}}"kernel"
// CHECK:       !omp_offload.info = !{
