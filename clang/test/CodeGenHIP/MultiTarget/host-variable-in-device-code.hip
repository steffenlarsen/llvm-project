// A host variable a device function or kernel uses is an error for the
// devices, as in a device compile, but not where the code is the host's own
// alternative. A const variable the devices make __constant__ is a device
// variable, defined and visible on the devices whether or not it is used.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fsyntax-only -verify -DERRORS %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -DERRORS" \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942 -DERRORS"

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -O2 -fvisibility=hidden -fapply-global-visibility-to-externs" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#ifdef ERRORS
int host_var; // expected-note {{host variable declared here}}
struct T { static const int no_init; }; // expected-note {{const variable cannot be emitted on device side due to dynamic initialization}}

__global__ void kernel(int *p) {
  *p = host_var + // expected-error {{reference to __host__ variable 'host_var' in __global__ function}}
       T::no_init; // expected-error {{reference to __host__ variable 'no_init' in __global__ function}}
}

__device__ int host_arm() {
#if defined(__HIP_DEVICE_COMPILE__)
  return 1;
#else
  return host_var;
#endif
}
#else

struct S { static const int v; };
const int S::v = 2;
// CHECK: @_ZN1S1vE = protected {{.*}}addrspace(4) constant i32 2

__global__ void uses_constant(int *p) { *p = S::v; }
#endif
