// End-to-end launch through a kernel pointer for HIP: real CIRGen output for
// host and device TUs, merged by the merge tool. Kokkos launches every kernel
// this way, through the pointer an accessor hands out, which leaves no launch
// for the offload passes to see until the call is made direct. A launch helper
// handed the kernel, and a table of kernels, are the same problem.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -fno-clangir-call-conv-lowering -O2 \
// RUN:   -target-sdk-version=9.2 -fhip-new-launch-api -x hip -I %S/../../CodeGenCUDA/Inputs \
// RUN:   -emit-cir %s -o %t-host.cir
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 -target-sdk-version=9.2 -O2 \
// RUN:   -fcuda-is-device -fhip-new-launch-api -x hip -I %S/../../CodeGenCUDA/Inputs \
// RUN:   -emit-cir %s -o %t-dev.cir
// RUN: cir-offload-merge -combine -input=%t-host.cir -input=%t-dev.cir \
// RUN:   -targets=host-x86_64-unknown-linux-gnu,hip-amdgcn-amd-amdhsa--gfx900 \
// RUN:   -output=%t-combined.cir
// RUN: FileCheck %s --input-file=%t-combined.cir
// RUN: cir-offload-merge -combine -input=%t-host.cir -input=%t-dev.cir \
// RUN:   -targets=host-x86_64-unknown-linux-gnu,hip-amdgcn-amd-amdhsa--gfx900 \
// RUN:   -disable-cir-resolve-indirect-calls -output=%t-unresolved.cir
// RUN: FileCheck %s --input-file=%t-unresolved.cir --check-prefix=UNRESOLVED

#include "cuda.h"

__device__ void use(int);

__global__ static void kern(int n) { use(n); }

struct Launcher {
  static auto get_kernel_func() { return kern; }
};

void host() { (Launcher::get_kernel_func())<<<1, 64>>>(7); }

__global__ static void kern_param(int n) { use(n); }
__global__ static void kern_table(int n) { use(n); }

// A launch helper handed the kernel, as ggml's are.
template <typename Kernel> static void launch(Kernel kernel, int n) {
  kernel<<<1, 64>>>(n);
}
void host_param() { launch(kern_param, 8); }

// A table of kernels, read at a constant index.
typedef void (*KernelFn)(int);
static KernelFn const kernels[] = {kern_table};
void host_table() { kernels[0]<<<1, 64>>>(9); }

// Each launch calls its stub directly, so the constant it passes is seen and
// pinned in a copy of the kernel.
// CHECK-LABEL: cir.func{{.*}} @_Z4hostv
// CHECK:         cir.call @_ZL19__device_stub__kerni.constprop(
// CHECK-LABEL: cir.func{{.*}} @_Z10host_paramv
// CHECK:         cir.call @_ZL25__device_stub__kern_parami.constprop(
// CHECK-LABEL: cir.func{{.*}} @_Z10host_tablev
// CHECK:         cir.call @_ZL25__device_stub__kern_tablei.constprop(
// CHECK:       cir.func{{.*}} @_ZL4kerni.constprop(
// CHECK:         cir.const #cir.int<7>
// CHECK:       cir.func{{.*}} @_ZL10kern_parami.constprop(
// CHECK:         cir.const #cir.int<8>
// CHECK:       cir.func{{.*}} @_ZL10kern_tablei.constprop(
// CHECK:         cir.const #cir.int<9>

// Left indirect, the launches are invisible and nothing is specialised.
// UNRESOLVED-LABEL: cir.func{{.*}} @_Z4hostv
// UNRESOLVED:         cir.call %{{[0-9]+}}(
// UNRESOLVED-LABEL: cir.func{{.*}} @_Z10host_paramv
// UNRESOLVED:         cir.call %{{[0-9]+}}(
// UNRESOLVED-LABEL: cir.func{{.*}} @_Z10host_tablev
// UNRESOLVED:         cir.call %{{[0-9]+}}(
// UNRESOLVED-NOT:   .constprop
