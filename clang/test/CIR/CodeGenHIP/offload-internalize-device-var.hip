// Verify that __device__ and __constant__ variables get internal linkage
// in the offload path (non-RDC mode).  Without internalizeDeviceSideVar,
// these variables keep external linkage and cause undefined-reference errors.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck %s

#include "hip_runtime_minimal.h"

__device__ int dev_var;
__constant__ int const_var;

// CHECK: gpu.module @offload_device_module
// CHECK-DAG: cir.global {{.*}}internal{{.*}}@dev_var
// CHECK-DAG: cir.global {{.*}}internal{{.*}}@const_var
