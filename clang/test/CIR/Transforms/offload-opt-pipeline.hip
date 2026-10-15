// The shared offload optimization pipeline also runs for HIP. Launch-bound
// inference leaves the existing AMDGPU work-group-size attribute unchanged.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -fno-clangir-call-conv-lowering \
// RUN:   -target-sdk-version=9.2 -x hip -I %S/../CodeGenCUDA/Inputs \
// RUN:   -emit-cir %s -o %t-host.cir
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 \
// RUN:   -target-sdk-version=9.2 -fcuda-is-device -x hip \
// RUN:   -I %S/../CodeGenCUDA/Inputs -emit-cir %s -o %t-device.cir
// RUN: cir-offload-merge -combine -input=%t-host.cir -input=%t-device.cir \
// RUN:   -targets=host-x86_64-unknown-linux-gnu,hip-amdgcn-amd-amdhsa--gfx900 \
// RUN:   -output=%t-combined.cir
// RUN: FileCheck %s --input-file=%t-combined.cir \
// RUN:   --implicit-check-not=cir.nvvm.maxntid

#include "cuda.h"

__device__ int result;

static __global__ void optimized(int value) { result = value; }

void host() { optimized<<<1, 32>>>(7); }

// CHECK-LABEL: cir.func{{.*}} @_ZL9optimizedi(
// CHECK-SAME: "cir.amdgpu-flat-work-group-size" = "1,1024"
