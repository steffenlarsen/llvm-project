// Tests that HIP device memory management calls are lowered to direct
// cir.call to the HIP runtime (gpu.alloc is not used on the host path
// because it cannot be translated to LLVM IR without the gpu-to-llvm pass).
//
// The CIR emitter intercepts recognized HIP runtime calls in
// CIRGenOffloadRuntime::emitCUDARuntimeCall() and emits:
//   hipMalloc(&ptr, size)         → cir.call @hipMalloc
//   hipHostMalloc(&ptr, size, fl) → cir.call @hipHostMalloc
//   hipMallocManaged(&ptr,size,fl)→ cir.call @hipMallocManaged
//   hipFree(ptr)                  → cir.call @hipFree
//   hipHostFree(ptr)              → cir.call @hipHostFree
//   hipMemcpy(dst,src,n,kind)     → cir.call @hipMemcpy
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx906 \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

// --- Device malloc / free ---------------------------------------------------

__host__ void alloc_free(void **ptr, size_t n) {
  hipMalloc(ptr, n);
  hipFree(*ptr);
}

// CIR-LABEL: cir.func {{.*}}@_Z10alloc_freePPvm
// CIR: cir.call @hipMalloc
// CIR: cir.call @hipFree

// --- Host-pinned malloc / free ----------------------------------------------

__host__ void pinned_alloc_free(void **ptr, size_t n) {
  hipHostMalloc(ptr, n, 0);
  hipHostFree(*ptr);
}

// CIR-LABEL: cir.func {{.*}}@_Z17pinned_alloc_freePPvm
// CIR: cir.call @hipHostMalloc
// CIR: cir.call @hipHostFree

// --- Managed / unified malloc -----------------------------------------------

__host__ void managed_alloc(void **ptr, size_t n) {
  hipMallocManaged(ptr, n, 1);
}

// CIR-LABEL: cir.func {{.*}}@_Z13managed_allocPPvm
// CIR: cir.call @hipMallocManaged

// --- Device memcpy ----------------------------------------------------------

__host__ void copy_h2d(void *dst, const void *src, size_t n) {
  hipMemcpy(dst, src, n, hipMemcpyHostToDevice);
}

// CIR-LABEL: cir.func {{.*}}@_Z8copy_h2dPvPKvm
// CIR: cir.call @hipMemcpy

__host__ void copy_d2h(void *dst, const void *src, size_t n) {
  hipMemcpy(dst, src, n, hipMemcpyDeviceToHost);
}

// CIR-LABEL: cir.func {{.*}}@_Z8copy_d2hPvPKvm
// CIR: cir.call @hipMemcpy

__host__ void copy_d2d(void *dst, const void *src, size_t n) {
  hipMemcpy(dst, src, n, hipMemcpyDeviceToDevice);
}

// CIR-LABEL: cir.func {{.*}}@_Z8copy_d2dPvPKvm
// CIR: cir.call @hipMemcpy
