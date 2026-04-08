// Tests that -fclangir-offload emits cir.global ops with correct
// target_address_space annotations for CUDA/HIP device-qualified global variables.
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

// A plain host variable must NOT be placed inside gpu.module.
int host_var = 42;

// Host globals remain in the outer module (appear before the gpu.module).
// CIR: cir.global external @host_var
// Device globals are nested inside gpu.module @offload_device_module.
// CIR: gpu.module @offload_device_module
// CIR-DAG: cir.global {{.*}} target_address_space(1) @dev_counter : !s32i
// CIR-DAG: cir.global {{.*}} target_address_space(4) @const_lut : !cir.array<!cir.float x 4>
// CIR-DAG: cir.global {{.*}} target_address_space(1) @dev_flag : !s32i
// CIR-DAG: cir.global {{.*}} target_address_space(0) @managed_var : !s32i
// CIR-NOT: cir.global {{.*}}@host_var{{.*}}target_address_space
