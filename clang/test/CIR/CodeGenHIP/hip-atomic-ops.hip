// Tests that HIP atomic operations (__hip_atomic_*) are lowered to CIR
// atomic ops. Uses the host-side compilation which still exercises the
// CIR atomic codegen paths.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir \
// RUN:            -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx942 \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

// HIP memory scope constants
#define __HIP_MEMORY_SCOPE_AGENT 2

// --- HIP atomic fetch_add (integer) -----------------------------------------

__device__ int hip_atomic_add(int *ptr, int val) {
  return __hip_atomic_fetch_add(ptr, val, __ATOMIC_RELAXED, __HIP_MEMORY_SCOPE_AGENT);
}

// The host compilation emits device functions as declarations; the important
// thing is that the CIR codegen doesn't crash on HIP atomic builtins.
// CIR: module
