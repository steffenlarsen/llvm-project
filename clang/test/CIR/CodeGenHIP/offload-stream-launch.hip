// Tests that a kernel launch with an explicit stream operand emits
// offload.kernel_launch with a stream operand in CIR.
//
// A kernel launched with stream=0 (the default) should produce
// offload.kernel_launch WITHOUT a stream operand and therefore maps to
// gpu.launch_func.  A non-null stream produces a stream operand and is
// left for LowerHostRuntime to lower to hipLaunchKernel.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

__global__ void kernel_fn(int a) {}

// A kernel launched into the default stream — no stream operand in CIR.
__host__ void launch_default_stream() {
  kernel_fn<<<1, 1>>>(42);
}

// A kernel launched into an explicit (non-null) stream — stream operand present.
__host__ void launch_with_stream(hipStream_t s) {
  kernel_fn<<<1, 1, 0, s>>>(42);
}

// --- CIR checks ---

// Default-stream launch: no stream operand.
// CIR-LABEL: offload.func @{{.*}}launch_default_stream
// CIR:         offload.kernel_launch @_Z9kernel_fni
// CIR-NOT:     stream =
// CIR:         offload.return

// Stream launch: stream operand present.
// CIR-LABEL: offload.func @{{.*}}launch_with_stream
// CIR:         offload.kernel_launch @_Z9kernel_fni
// CIR-SAME:    stream =
// CIR:         offload.return
