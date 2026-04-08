// Tests that HIP stream/sync runtime calls are lowered to direct cir.call ops.
// gpu.wait is not used because it cannot be translated to LLVM IR on the host
// path without the gpu-to-llvm pass.
//
// The CIR emitter intercepts recognized HIP runtime calls in
// CIRGenOffloadRuntime::emitCUDARuntimeCall() and emits:
//   hipDeviceSynchronize()   → cir.call @hipDeviceSynchronize
//   hipStreamCreate(&s)      → cir.call @hipStreamCreate
//   hipStreamDestroy(s)      → cir.call @hipStreamDestroy
//   hipStreamSynchronize(s)  → cir.call @hipStreamSynchronize
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
// CIR: cir.call @hipDeviceSynchronize

// --- Stream create / destroy / sync ------------------------------------------

__host__ void stream_ops(hipStream_t *pStream) {
  hipStreamCreate(pStream);
  hipStreamSynchronize(*pStream);
  hipStreamDestroy(*pStream);
}

// CIR-LABEL: cir.func {{.*}}@_Z10stream_opsPP12ihipStream_t
// CIR: cir.call @hipStreamCreate
// CIR: cir.call @hipStreamSynchronize
// CIR: cir.call @hipStreamDestroy
