// Tests that HIP device memory management calls are lowered to structured
// CIR offload ops instead of opaque cir.call.
//
// The CIR emitter intercepts recognized HIP runtime calls in
// CIRGenOffloadRuntime::emitCUDARuntimeCall() and emits:
//   hipMalloc(&ptr, size)         → cir.offload.malloc {kind = device}
//   hipHostMalloc(&ptr, size, fl) → cir.offload.malloc {kind = host}
//   hipMallocManaged(&ptr,size,fl)→ cir.offload.malloc {kind = managed}
//   hipFree(ptr)                  → cir.offload.free
//   hipHostFree(ptr)              → cir.offload.free
//   hipMemcpy(dst,src,n,kind)     → cir.offload.memcpy
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
// CIR: cir.offload.malloc {{.*}}{kind = <device>}
// CIR: cir.offload.free

// --- Host-pinned malloc / free ----------------------------------------------

__host__ void pinned_alloc_free(void **ptr, size_t n) {
  hipHostMalloc(ptr, n, 0);
  hipHostFree(*ptr);
}

// CIR-LABEL: cir.func {{.*}}@_Z17pinned_alloc_freePPvm
// CIR: cir.offload.malloc {{.*}}{kind = <host>}
// CIR: cir.offload.free

// --- Managed / unified malloc -----------------------------------------------

__host__ void managed_alloc(void **ptr, size_t n) {
  hipMallocManaged(ptr, n, 1);
}

// CIR-LABEL: cir.func {{.*}}@_Z13managed_allocPPvm
// CIR: cir.offload.malloc {{.*}}{kind = <managed>}

// --- Device memcpy ----------------------------------------------------------

__host__ void copy_h2d(void *dst, const void *src, size_t n) {
  hipMemcpy(dst, src, n, hipMemcpyHostToDevice);
}

// CIR-LABEL: cir.func {{.*}}@_Z8copy_h2dPvPKvm
// CIR: cir.offload.memcpy

__host__ void copy_d2h(void *dst, const void *src, size_t n) {
  hipMemcpy(dst, src, n, hipMemcpyDeviceToHost);
}

// CIR-LABEL: cir.func {{.*}}@_Z8copy_d2hPvPKvm
// CIR: cir.offload.memcpy

__host__ void copy_d2d(void *dst, const void *src, size_t n) {
  hipMemcpy(dst, src, n, hipMemcpyDeviceToDevice);
}

// CIR-LABEL: cir.func {{.*}}@_Z8copy_d2dPvPKvm
// CIR: cir.offload.memcpy
