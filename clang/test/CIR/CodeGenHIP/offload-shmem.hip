// Tests that dynamic shared memory size from __hipPushCallConfiguration arg 2
// is propagated through offload.kernel_launch into the LLVM kernel launch call.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

__global__ void kernel_shmem(int n) {}

int main() {
  // Launch with 4096 bytes of dynamic shared memory.
  kernel_shmem<<<1, 64, 4096>>>(42);
  return 0;
}

// --- CIR checks ---
// The kernel_launch op must carry the shmem operand.
// CIR: offload.kernel_launch {{.*}} shmem
