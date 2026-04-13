// Tests that hipStreamWaitEvent is lowered through CIR to
// offload.stream_wait_event.
//
// Note: The LLVM emission path for offload.stream_create / offload.event_create
// has a preexisting crash in ConvertCIRToLLVMPass, so we only verify the CIR
// representation here (same approach as offload-event.hip and offload-stream.hip).
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
// CIR: offload.stream_wait_event {{.*}} flags = 0
