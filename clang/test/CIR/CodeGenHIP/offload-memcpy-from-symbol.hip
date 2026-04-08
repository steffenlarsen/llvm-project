// Tests that hipMemcpyFromSymbol is intercepted and lowered to a direct
// cir.call @hipMemcpyFromSymbol in the CIR offload path.
//
// The device global is emitted inside gpu.module @offload_device_module.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx906 \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

__device__ float device_arr[64];

void copy_from_device(float *h_data) {
  hipMemcpyFromSymbol(h_data, device_arr, 64 * sizeof(float));
}

// The device global is inside gpu.module @offload_device_module.
// CIR: gpu.module @offload_device_module
// CIR: cir.global {{.*}} target_address_space(1) @device_arr
// The host function is a plain cir.func in the outer module.
// CIR-LABEL: cir.func {{.*}}@_Z16copy_from_devicePf
// CIR: cir.call @hipMemcpyFromSymbol
