// Tests that -fclangir-offload emits offload.func ops with correct exec_space
// annotations instead of the legacy cir.func split-compilation path.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/../CodeGenCUDA/Inputs/ -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "../CodeGenCUDA/Inputs/cuda.h"

// A __host__ function → exec_space = host
__host__ void host_fn(int *a) {}
// CIR: offload.func @_Z7host_fnPi{{.*}}exec_space = #offload.exec_space<host>

// A __device__ function → exec_space = device
__device__ void device_fn(int *a) {}
// CIR: offload.func @_Z9device_fnPi{{.*}}exec_space = #offload.exec_space<device>

// A __host__ __device__ function → exec_space = host_device
__host__ __device__ void hd_fn(int *a) {}
// CIR: offload.func @_Z5hd_fnPi{{.*}}exec_space = #offload.exec_space<host_device>

// A __global__ kernel → exec_space = global
__global__ void kernel_fn(int a) {}
// CIR: offload.func @_Z9kernel_fni{{.*}}exec_space = #offload.exec_space<global>

// Kernel launch → offload.kernel_launch
int main() {
  kernel_fn<<<1, 1>>>(42);
}
// CIR: offload.kernel_launch @_Z9kernel_fni
