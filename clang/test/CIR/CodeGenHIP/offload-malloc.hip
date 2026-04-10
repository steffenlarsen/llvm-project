// Tests that HIP device memory management calls are lowered to offload dialect
// ops instead of plain cir.call ops.
//
// The CIR emitter intercepts recognized HIP runtime calls in
// CIRGenOffloadRuntime::emitCUDARuntimeCall() and emits:
//   hipMalloc(&ptr, size)         → offload.malloc %size alloc_type=<device>
//   hipHostMalloc(&ptr, size, fl) → offload.malloc %size alloc_type=<host>
//   hipMallocManaged(&ptr,size,fl)→ offload.malloc %size alloc_type=<managed>
//   hipFree(ptr)                  → offload.free %ptr alloc_type=<device>
//   hipHostFree(ptr)              → offload.free %ptr alloc_type=<host>
//   hipMemcpy(dst,src,n,kind)     → offload.memcpy %dst, %src, %n dst_space=X src_space=Y
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

// CIR-LABEL: offload.func @_Z10alloc_freePPvm
// CIR: offload.malloc {{.*}} alloc_type = <device>
// CIR: offload.free {{.*}} alloc_type = <device>

// --- Host-pinned malloc / free ----------------------------------------------

__host__ void pinned_alloc_free(void **ptr, size_t n) {
  hipHostMalloc(ptr, n, 0);
  hipHostFree(*ptr);
}

// CIR-LABEL: offload.func @_Z17pinned_alloc_freePPvm
// CIR: offload.malloc {{.*}} alloc_type = <host>
// CIR: offload.free {{.*}} alloc_type = <host>

// --- Managed / unified malloc -----------------------------------------------

__host__ void managed_alloc(void **ptr, size_t n) {
  hipMallocManaged(ptr, n, 1);
}

// CIR-LABEL: offload.func @_Z13managed_allocPPvm
// CIR: offload.malloc {{.*}} alloc_type = <managed>

// --- Device memcpy ----------------------------------------------------------

__host__ void copy_h2d(void *dst, const void *src, size_t n) {
  hipMemcpy(dst, src, n, hipMemcpyHostToDevice);
}

// CIR-LABEL: offload.func @_Z8copy_h2dPvPKvm
// CIR: offload.memcpy {{.*}} dst_space = <device> src_space = <host>

__host__ void copy_d2h(void *dst, const void *src, size_t n) {
  hipMemcpy(dst, src, n, hipMemcpyDeviceToHost);
}

// CIR-LABEL: offload.func @_Z8copy_d2hPvPKvm
// CIR: offload.memcpy {{.*}} dst_space = <host> src_space = <device>

__host__ void copy_d2d(void *dst, const void *src, size_t n) {
  hipMemcpy(dst, src, n, hipMemcpyDeviceToDevice);
}

// CIR-LABEL: offload.func @_Z8copy_d2dPvPKvm
// CIR: offload.memcpy {{.*}} dst_space = <device> src_space = <device>
