// Tests that hipMemcpyToSymbol is intercepted and lowered to
// offload.memcpy_to_symbol in the CIR offload path.
//
// The CIRGenOffloadRuntime intercepts the hipMemcpyToSymbol call, extracts the
// target symbol name from the first argument (a DeclRefExpr to a device global),
// and emits an offload.memcpy_to_symbol op with a FlatSymbolRefAttr, the host
// source pointer, and the byte count.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx906 \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

__device__ float device_arr[64];

void init_device(const float *h_data, int n) {
  hipMemcpyToSymbol(device_arr, h_data, n * sizeof(float));
}

// CIR-LABEL: offload.func @_Z11init_devicePKfi
// CIR: offload.memcpy_to_symbol @device_arr src = {{.*}} count = {{.*}}
