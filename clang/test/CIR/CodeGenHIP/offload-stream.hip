// Tests that HIP stream/sync runtime calls are lowered to offload dialect ops
// instead of plain cir.call ops.
//
// The CIR emitter intercepts recognized HIP runtime calls in
// CIRGenOffloadRuntime::emitHIPRuntimeCall() and emits:
//   hipDeviceSynchronize()   → offload.device_sync
//   hipStreamCreate(&s)      → offload.stream_create + store
//   hipStreamDestroy(s)      → offload.stream_destroy
//   hipStreamSynchronize(s)  → offload.stream_sync
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx906 \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

// --- Device synchronise ------------------------------------------------------

__host__ int sync_device() {
  return hipDeviceSynchronize();
}

// CIR-LABEL: offload.func @_Z11sync_devicev
// CIR: offload.device_sync

// --- Stream create / destroy / sync ------------------------------------------

__host__ void stream_ops(hipStream_t *pStream) {
  hipStreamCreate(pStream);
  hipStreamSynchronize(*pStream);
  hipStreamDestroy(*pStream);
}

// CIR-LABEL: offload.func @_Z10stream_opsPP12ihipStream_t
// CIR: offload.stream_create
// CIR: offload.stream_sync
// CIR: offload.stream_destroy
