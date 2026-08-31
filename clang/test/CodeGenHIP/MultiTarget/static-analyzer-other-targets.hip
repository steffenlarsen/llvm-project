// The static analyzer analyses the host's code, which a regular compilation's
// AST holds alone. A multi-target compilation's AST also holds the copies made
// for the devices. The analyzer neither analyses them nor runs them for a call.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -analyze -analyzer-checker=core %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   2>&1 | FileCheck --implicit-check-not=warning: %s
// The host's own compilation agrees.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -analyze -analyzer-checker=core %s 2>&1 \
// RUN:   | FileCheck --implicit-check-not=warning: %s

#define __device__ __attribute__((device))
#define __host__ __attribute__((host))

// Only the devices' body dereferences a null pointer.
__host__ __device__ int hd(int *q) {
#if defined(__HIP_DEVICE_COMPILE__)
  int *p = 0;
  return *p;
#else
  return *q;
#endif
}

// Called from a device function, which the host analyses too.
__host__ __device__ int called(int *q) {
#if defined(__HIP_DEVICE_COMPILE__)
  int *p = 0;
  return *p;
#else
  return *q;
#endif
}
__device__ int dev(int *q) { return called(q); }

// A device function's own bug is the host's to report too.
__device__ int devonly() {
  int *p = 0;
  return *p;
  // CHECK: static-analyzer-other-targets.hip:[[@LINE-1]]:10: warning: Dereference of null pointer
}
