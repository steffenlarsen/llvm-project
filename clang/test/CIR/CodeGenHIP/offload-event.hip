// Tests that HIP event runtime calls are lowered to offload dialect ops.
//
// CIRGen intercepts:
//   hipEventCreate(&e)        → offload.event_create + store
//   hipEventRecord(e, s)      → offload.event_record (with stream)
//   hipEventSynchronize(e)    → offload.event_wait
//   hipEventDestroy(e)        → offload.event_destroy
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

// CIR-LABEL: offload.func @{{.*}}event_lifecycle
// CIR:         offload.event_create : !offload.event
// CIR:         offload.event_record {{.*}} : !offload.event, stream = {{.*}} : !offload.stream
// CIR:         offload.event_wait {{.*}} : !offload.event
// CIR:         offload.event_destroy {{.*}} : !offload.event
// CIR:         offload.return
