// Tests that hipMemcpyFromSymbol is intercepted and lowered to
// offload.memcpy_from_symbol in the CIR offload path.
//
// The CIRGenOffloadRuntime intercepts the hipMemcpyFromSymbol call, extracts
// the source symbol name from the second argument (a DeclRefExpr to a device
// global), and emits an offload.memcpy_from_symbol op with a FlatSymbolRefAttr,
// the host destination pointer, and the byte count.
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

// CIR-LABEL: offload.func @_Z16copy_from_devicePf
// CIR: offload.memcpy_from_symbol {{.*}} : !cir.ptr<!void>, @device_arr count = {{.*}}
