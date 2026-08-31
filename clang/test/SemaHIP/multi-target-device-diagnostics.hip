// In a compilation for several targets, device code's inline asm is checked
// against every device target, as each device's own compilation would. The
// summary names the targets each error was raised for.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fsyntax-only -verify %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip"

// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fsyntax-only %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   2>&1 | FileCheck --check-prefix=DEVICE %s
// DEVICE: 3 errors generated when compiling for gfx90a, gfx942.

// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fsyntax-only %s -DHOST_ERROR \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   2>&1 | FileCheck --check-prefix=HOST %s
// HOST: 4 errors generated when compiling for host, gfx90a, gfx942.

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

__global__ void k(int *p) {
  int x;
  asm volatile("mov %0, 1" : "=q"(x)); // expected-error {{invalid output constraint '=q' in asm}}
  *p = x;
}

__device__ void clobbers() {
  asm volatile("" ::: "rax"); // expected-error {{unknown register name 'rax' in asm}}
}

__device__ void fine() {
  int x;
  asm volatile("v_mov_b32 %0, 1" : "=v"(x));
}

void host_asm() {
  int x;
  asm volatile("mov $1, %0" : "=q"(x));
}

__device__ int throws(int x) {
  if (x)
    throw 1; // expected-error {{cannot use 'throw' with exceptions disabled}}
  return 0;
}

#ifdef HOST_ERROR
void host_error() { undeclared_in_host(); }
#endif
