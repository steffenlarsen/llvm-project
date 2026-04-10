// Tests that -fclangir-offload emits offload.global_var ops with correct
// mem_space annotations for CUDA/HIP device-qualified global variables.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/../CodeGenCUDA/Inputs/ -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "../CodeGenCUDA/Inputs/cuda.h"

// __device__ variable → mem_space = device
__device__ int dev_counter;

// __constant__ variable with an explicit initialiser → mem_space = constant
__constant__ float const_lut[4] = {1.0f, 2.0f, 3.0f, 4.0f};

// A second __device__ variable to confirm multiple vars work.
__device__ int dev_flag;

// __managed__ variable → mem_space = managed
__managed__ int managed_var;

// A plain host variable must NOT be converted to offload.global_var.
int host_var = 42;

// CIR-DAG: offload.global_var @dev_counter : !s32i mem_space = <device>
// CIR-DAG: offload.global_var @const_lut : !cir.array<!cir.float x 4> mem_space = <constant>
// CIR-DAG: offload.global_var @dev_flag : !s32i mem_space = <device>
// CIR-DAG: offload.global_var @managed_var : !s32i mem_space = <managed>
// CIR-DAG: cir.global external @host_var
// CIR-NOT: offload.global_var @host_var
