// #pragma clang assume_nonnull makes the pointers declared in its region
// nonnull. A multi-target compilation preprocesses before it parses, and
// applies each target's regions where the parser reaches them, as each
// target's own compilation does.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -x hip -fsyntax-only %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx90a" \
// RUN:   2>&1 | FileCheck --check-prefix=MT --implicit-check-not=warning: %s
// The targets' own compilations agree.
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -x hip -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=HOST --implicit-check-not=warning: %s
// RUN: %clang_cc1 -std=c++17 %{hip_device} %{host_aux_triple} \
// RUN:   -target-cpu gfx90a -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck --check-prefix=DEV --implicit-check-not=warning: %s

#define __device__ __attribute__((device))
#define __host__ __attribute__((host))

// A region for every target.
#pragma clang assume_nonnull begin
__host__ __device__ void in_shared(int *p);
#pragma clang assume_nonnull end
__host__ __device__ void after_shared(int *p);

// A region for the devices only.
#if defined(__HIP_DEVICE_COMPILE__)
#pragma clang assume_nonnull begin
#endif
__host__ __device__ void in_device(int *p);
#if defined(__HIP_DEVICE_COMPILE__)
#pragma clang assume_nonnull end
#endif

// A region for the host only.
#if !defined(__HIP_DEVICE_COMPILE__)
#pragma clang assume_nonnull begin
#endif
__host__ __device__ void in_host(int *p);
#if !defined(__HIP_DEVICE_COMPILE__)
#pragma clang assume_nonnull end
#endif

__host__ __device__ void use() {
  in_shared(nullptr);
  // HOST: assume-nonnull-pragma.hip:[[@LINE-1]]:13: warning: null passed to a callee that requires a non-null argument
  // DEV: assume-nonnull-pragma.hip:[[@LINE-2]]:13: warning: null passed to a callee that requires a non-null argument
  // MT-DAG: assume-nonnull-pragma.hip:[[@LINE-3]]:13: warning: null passed to a callee that requires a non-null argument
  // MT-DAG: assume-nonnull-pragma.hip:[[@LINE-4]]:13: warning: null passed to a callee that requires a non-null argument
  after_shared(nullptr);
  in_device(nullptr);
  // DEV: assume-nonnull-pragma.hip:[[@LINE-1]]:13: warning: null passed to a callee that requires a non-null argument
  // MT-DAG: assume-nonnull-pragma.hip:[[@LINE-2]]:13: warning: null passed to a callee that requires a non-null argument
  in_host(nullptr);
  // HOST: assume-nonnull-pragma.hip:[[@LINE-1]]:11: warning: null passed to a callee that requires a non-null argument
  // MT-DAG: assume-nonnull-pragma.hip:[[@LINE-2]]:11: warning: null passed to a callee that requires a non-null argument
}

// Each side reports what its own compilation does.
// MT: 4 warnings generated when compiling for host, gfx90a.
