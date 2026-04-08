// Tests that hipStreamWaitEvent and related HIP stream/event calls are
// lowered to direct cir.call ops.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

int main() {
  hipStream_t stream;
  hipStreamCreate(&stream);

  hipEvent_t event;
  hipEventCreate(&event);

  hipEventRecord(event, stream);

  // The function under test: make stream wait for event before proceeding.
  hipStreamWaitEvent(stream, event, 0);

  hipStreamSynchronize(stream);
  hipEventDestroy(event);
  hipStreamDestroy(stream);
  return 0;
}

// --- CIR checks ---
// CIR: cir.call @hipStreamWaitEvent
