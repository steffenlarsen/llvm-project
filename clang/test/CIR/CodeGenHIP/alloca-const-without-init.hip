// Tests that a const-qualified local variable without explicit initializer
// (e.g., const char arrays with brace init) produces a valid cir.alloca
// that can round-trip through CIR serialization.
//
// Previously, const-without-init allocas serialized as ["name", const]
// which couldn't be parsed because the format requires "init" before "const".
//
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -fclangir \
// RUN:            -aux-triple x86_64-unknown-linux-gnu \
// RUN:            -target-cpu gfx942 \
// RUN:            -x hip -fcuda-is-device -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

__device__ void test_const_local() {
    const int arr[] = {1, 2, 3};
    volatile int x = arr[0];
}

// Verify the alloca has both "init" and "const" (in that order).
// CIR-LABEL: cir.func {{.*}}@_Z16test_const_localv
// CIR: cir.alloca "arr" {{.*}} init const :
