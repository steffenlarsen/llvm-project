// Tests that kernel launches with and without an explicit stream operand
// both emit gpu.launch_func in CIR (stream is dropped at the CIR level).
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

// The kernel body is inside gpu.module @offload_device_module.
// CIR: gpu.module @offload_device_module
// CIR: gpu.func @_Z9kernel_fni{{.*}} kernel

// Default-stream launch: gpu.launch_func with no stream.
// CIR-LABEL: cir.func {{.*}}@_Z21launch_default_streamv
// CIR:         gpu.launch_func @offload_device_module::@_Z9kernel_fni
// CIR:         cir.return

// Stream launch: also gpu.launch_func (stream dropped at CIR level).
// CIR-LABEL: cir.func {{.*}}@_Z18launch_with_streamP12ihipStream_t
// CIR:         gpu.launch_func @offload_device_module::@_Z9kernel_fni
// CIR:         cir.return
