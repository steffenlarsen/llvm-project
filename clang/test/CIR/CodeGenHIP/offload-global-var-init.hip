// Tests that device globals are emitted as cir.global ops inside
// gpu.module @offload_device_module with the correct target address space.
//
// Initializers are not emitted on the host cc1 device globals; the device
// cc1 (dcir) carries the initializer and it is merged in by MergeOffloadModules.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

// Integer __device__ variable with a non-zero initializer.
__device__ int dev_var = 42;

// Floating-point __constant__ variable with a non-trivial initializer.
__constant__ float const_var = 3.14f;

// A __device__ variable with no explicit initializer (zero-init) -- must NOT
// appear in the __offload_init_globals initializer list.
__device__ int dev_zero;

// An integer array with a constant initializer -- must appear in init list.
__device__ int dev_arr[4] = {1, 2, 3, 4};

// --- CIR checks ---

// Device globals are now nested inside gpu.module @offload_device_module.
// CIR: gpu.module @offload_device_module
// CIR-DAG: cir.global {{.*}} target_address_space(1) @dev_var : !s32i
// CIR-DAG: cir.global {{.*}} target_address_space(4) @const_var : !cir.float
// CIR-DAG: cir.global {{.*}} target_address_space(1) @dev_zero : !s32i
// CIR-DAG: cir.global {{.*}} target_address_space(1) @dev_arr : !cir.array<!s32i x 4>

