// Tests that -fclangir-offload with an explicit --offload-arch produces LLVM IR
// with the device binary embedded as a global constant and kernel launch
// infrastructure wired via the mgpu* runtime wrappers.
//
// Milestone 3 + 4: gpu.module compiled to HSACo binary (gpu.BinaryOp), then
// translated to LLVM IR with:
//   - @offload_device_module_binary  -- embedded ELF/HSACo binary constant
//   - @offload_device_module_load    -- module ctor (calls mgpuModuleLoad)
//   - @offload_device_module_unload  -- module dtor (calls mgpuModuleUnload)
//   - mgpuLaunchKernel               -- kernel launch in host main
//   - device kernel NOT defined in host module (compiled into the binary)
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/../CodeGenCUDA/Inputs/ \
// RUN:            -internal-isystem %clang_resource_dir/include \
// RUN:            -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx906 \
// RUN:            -emit-llvm %s -o - \
// RUN:   | FileCheck --check-prefix=LLVM %s

#include "cuda.h"

__global__ void kernel_fn(int a) {}

int main() {
  kernel_fn<<<1, 1>>>(42);
  return 0;
}

// Device binary is embedded as a global constant.
// LLVM: @offload_device_module_binary = internal constant

// Module ctor/dtor load/unload the binary at startup/shutdown.
// LLVM: @llvm.global_ctors = appending global {{.*}} @offload_device_module_load
// LLVM: @llvm.global_dtors = appending global {{.*}} @offload_device_module_unload

// main launches the kernel via the mgpu* runtime wrapper.
// LLVM:      define i32 @main(
// LLVM:        call void @mgpuLaunchKernel(

// The kernel function is compiled into the device binary — NOT defined in the
// host module as a regular function.
// LLVM-NOT: define{{.*}}@_Z9kernel_fni

// The device module ctor loads the binary, and the dtor unloads it.
// LLVM: define internal void @offload_device_module_load(
// LLVM:   call ptr @mgpuModuleLoad(

// mgpu* runtime wrappers are declared (not defined) in the host module.
// LLVM: declare ptr @mgpuModuleLoad(ptr
// LLVM: declare void @mgpuLaunchKernel(ptr
