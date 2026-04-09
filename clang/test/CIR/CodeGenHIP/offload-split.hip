// Tests that -fclangir-offload splits a unified host+device module and
// produces clean host-side LLVM IR.  The split pass converts:
//   offload.func (host/host_device) → llvm.func (via func.func)
//   offload.func (device/global)    → gpu.func  (inside gpu.module)
//   offload.kernel_launch           → gpu.launch_func
//
// Without --offload-arch / -aux-target-cpu no ROCDL target is attached and the
// gpu.module is dropped; device compilation and kernel launch wiring require
// an explicit GPU target (see offload-launch.hip).
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/../CodeGenCUDA/Inputs/ -emit-llvm %s -o - \
// RUN:   | FileCheck --check-prefix=LLVM %s

#include "../CodeGenCUDA/Inputs/cuda.h"

// A host function — must appear in LLVM IR as a regular function definition.
__host__ void host_fn(int *a) {}

// A device-only function — must NOT appear in host LLVM IR.
__device__ void device_fn(int *a) {}

// A kernel — must NOT appear in host LLVM IR.
__global__ void kernel_fn(int a) {}

// main launches the kernel; the launch is dropped (no GPU target attached).
int main() {
  kernel_fn<<<1, 1>>>(42);
  return 0;
}

// Host functions are emitted as regular LLVM function definitions.
// LLVM: define void @_Z7host_fnPi(
// LLVM: ret void

// Device/kernel functions do not appear in the host module.
// LLVM-NOT: @_Z9device_fnPi
// LLVM-NOT: @_Z9kernel_fni

// main is present (it's a host function).
// LLVM: define i32 @main(
// LLVM: ret i32
