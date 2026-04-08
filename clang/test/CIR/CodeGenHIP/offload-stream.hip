// Tests that HIP stream/sync runtime calls are lowered to structured offload ops.
//
// The CIR emitter intercepts recognized HIP runtime calls in
// CIRGenOffloadRuntime::emitCUDARuntimeCall() and emits:
//   hipDeviceSynchronize()   → cir.offload.device_sync
//   hipStreamSynchronize(s)  → cir.offload.stream_sync
//   hipStreamCreate(&s)      → cir.call @hipStreamCreate (stays as call)
//   hipStreamDestroy(s)      → cir.call @hipStreamDestroy (stays as call)
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

// CIR-LABEL: cir.func {{.*}}@_Z11sync_devicev
// CIR: cir.offload.device_sync

// --- Stream create / destroy / sync ------------------------------------------

__host__ void stream_ops(hipStream_t *pStream) {
  hipStreamCreate(pStream);
  hipStreamSynchronize(*pStream);
  hipStreamDestroy(*pStream);
}

// CIR-LABEL: cir.func {{.*}}@_Z10stream_opsPP12ihipStream_t
// CIR: cir.call @hipStreamCreate
// CIR: cir.offload.stream_sync
// CIR: cir.call @hipStreamDestroy
