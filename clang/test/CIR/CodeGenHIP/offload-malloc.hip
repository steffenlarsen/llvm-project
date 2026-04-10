// Tests that HIP device memory management calls are lowered to offload dialect
// ops instead of plain cir.call ops.
//
// The CIR emitter intercepts recognized HIP runtime calls in
// CIRGenOffloadRuntime::emitHIPRuntimeCall() and emits:
//   hipMalloc(&ptr, size)    → offload.malloc %size : <ptr_type>
//   hipFree(ptr)             → offload.free %ptr
//   hipMemcpy(dst,src,n,k)  → offload.memcpy %dst, %src, %n, <kind>
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
// CIR: offload.malloc
// CIR: offload.free

// --- Device memcpy ----------------------------------------------------------

__host__ void copy_h2d(void *dst, const void *src, size_t n) {
  hipMemcpy(dst, src, n, hipMemcpyHostToDevice);
}

// CIR-LABEL: offload.func @_Z8copy_h2dPvPKvm
// CIR: offload.memcpy {{.*}} <host_to_device>
