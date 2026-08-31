// Copies of an invalid declaration made per target leave a lookup with no
// declaration once the other targets' and the invalid ones are dropped.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -fsyntax-only -verify %s \
// RUN:   "-multi-target-aux-invocation=-std=c++17 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   "-multi-target-aux-invocation=-std=c++17 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip"

#define __global__ __attribute__((global))

#if defined(__gfx942__)
#define LIMIT 2
#else
#define LIMIT 8
#endif
template <class T> concept Big = sizeof(T) > LIMIT; // expected-error 3 {{unknown type name 'concept'}}
__global__ void k(int *p) { p[0] = Big<int> ? 1 : 2; } // expected-error {{use of undeclared identifier 'Big'}}
