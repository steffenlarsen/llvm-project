// Tests that HIP event runtime calls are lowered to direct cir.call ops.
//
// CIRGen passes through:
//   hipEventCreate(&e)        → cir.call @hipEventCreate
//   hipEventRecord(e, s)      → cir.call @hipEventRecord
//   hipEventSynchronize(e)    → cir.call @hipEventSynchronize
//   hipEventDestroy(e)        → cir.call @hipEventDestroy
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

__host__ void event_lifecycle(hipStream_t s) {
  hipEvent_t e;
  hipEventCreate(&e);
  hipEventRecord(e, s);
  hipEventSynchronize(e);
  hipEventDestroy(e);
}

// --- CIR checks ---

// CIR-LABEL: cir.func {{.*}}@_Z15event_lifecycleP12ihipStream_t
// CIR:         cir.call @hipEventCreate
// CIR:         cir.call @hipEventRecord
// CIR:         cir.call @hipEventSynchronize
// CIR:         cir.call @hipEventDestroy
// CIR:         cir.return
