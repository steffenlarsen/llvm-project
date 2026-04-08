// Tests that -clangir-offload emits the correct module structure.
// The host cc1 emits host functions as cir.func in the outer module, and
// device/global functions inside cir.offload.module @offload_device_module.
// The device cc1 emits device and global functions as plain cir.func.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/../CodeGenCUDA/Inputs/ -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=HOST %s
//
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -fclangir \
// RUN:            -x hip -fcuda-is-device -fhip-new-launch-api \
// RUN:            -I%S/../CodeGenCUDA/Inputs/ -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=DEVICE %s

#include "../CodeGenCUDA/Inputs/cuda.h"

// A __host__ function → emitted as cir.func in the outer module (host cc1)
__host__ void host_fn(int *a) {}
// HOST: cir.func {{.*}}@_Z7host_fnPi

// A __device__ function → private declaration on host cc1; device cc1 emits cir.func
__device__ void device_fn(int *a) {}
// HOST: cir.func {{.*}}private {{.*}}@_Z9device_fnPi
// DEVICE: cir.func {{.*}}@_Z9device_fnPi

// A __host__ __device__ function → device body inside cir.offload.module (host cc1)
__host__ __device__ void hd_fn(int *a) {}
// HOST: cir.offload.module @offload_device_module
// Host+device functions stay as cir.func (not moved to cir.offload.module).
// HOST-NOT: cir.offload.func @_Z5hd_fnPi

// A __global__ kernel → body inside cir.offload.module on host cc1; plain cir.func on device cc1
__global__ void kernel_fn(int a) {}
// HOST: cir.offload.func @_Z9kernel_fni{{.*}}kernel
// DEVICE: cir.func {{.*}}@_Z9kernel_fni

// Kernel launch → cir.offload.kernel_launch (host cc1)
int main() {
  kernel_fn<<<1, 1>>>(42);
}
// HOST: cir.offload.kernel_launch @offload_device_module::@_Z9kernel_fni
