// A calling convention is checked against the targets a function runs on, the
// host's and the device's. In a multi-target compilation the target in scope
// can be a device's, which is no host for the check.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fsyntax-only %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   2>&1 | FileCheck --implicit-check-not=warning: %s
// The targets' own compilations agree.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fsyntax-only %s 2>&1 | FileCheck --implicit-check-not=warning: %s
// RUN: %clang_cc1 %{hip_device} %{host_aux_triple} -target-cpu gfx90a \
// RUN:   -fsyntax-only %s 2>&1 | FileCheck --implicit-check-not=warning: %s

#define __device__ __attribute__((device))
#define __host__ __attribute__((host))

__device__ int __attribute__((ms_abi)) d_ms(int x) { return x; }
__device__ int __attribute__((vectorcall)) d_vc(int x) { return x; }
__host__ __device__ int __attribute__((ms_abi)) hd_ms(int x) { return x; }
__host__ int __attribute__((vectorcall)) h_vc(int x) { return x; }
__host__ int __attribute__((stdcall)) h_std(int x) { return x; }
// CHECK-DAG: calling-convention-targets.hip:[[@LINE-1]]:29: warning: 'stdcall' calling convention is not supported for this target
__device__ int __attribute__((stdcall)) d_std(int x) { return x; }
// CHECK-DAG: calling-convention-targets.hip:[[@LINE-1]]:31: warning: 'stdcall' calling convention is not supported for this target

__attribute__((device, used)) int use(int x) {
  return d_ms(x) + d_vc(x) + hd_ms(x) + d_std(x);
}
int host_use(int x) { return hd_ms(x) + h_vc(x) + h_std(x); }
