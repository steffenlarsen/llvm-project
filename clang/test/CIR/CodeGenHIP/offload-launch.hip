// Tests that -fclangir-offload with an explicit --offload-arch produces LLVM IR
// with the device binary embedded as a global constant and kernel launch
// infrastructure wired via the HIP-native registration path.
//
// The CIR offload path generates:
//   - @offload_device_module_binary        -- embedded ELF/HSACo binary constant
//   - @offload_device_module_fatbin_wrapper -- HIP fat binary wrapper struct
//   - @offload_device_module_gpubin_handle  -- handle global (ptr, null-initialized)
//   - @offload_device_module_load          -- module ctor (__hipRegisterFatBinary)
//   - @offload_device_module_unload        -- module dtor (__hipUnregisterFatBinary)
//   - hipLaunchKernel                      -- kernel launch in host main
//   - device kernel NOT defined in host module (compiled into the binary)
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/../CodeGenCUDA/Inputs/ \
// RUN:            -internal-isystem %clang_resource_dir/include \
// RUN:            -aux-triple amdgcn-amd-amdhsa -clangir-offload-arch=gfx906 \
// RUN:            -emit-llvm %s -o - \
// RUN:   | FileCheck --check-prefix=LLVM %s

#include "cuda.h"

__global__ void kernel_fn(int a) {}

int main() {
  kernel_fn<<<1, 1>>>(42);
  return 0;
}

// --- Global variable checks (appear at top of module) ---

// Device binary is embedded as a global constant.
// LLVM: @offload_device_module_binary = internal constant

// The HIP fat binary wrapper struct is emitted.
// LLVM: @offload_device_module_fatbin_wrapper

// The GPU binary handle global (null-initialized, filled at ctor time).
// LLVM: @offload_device_module_gpubin_handle = internal global ptr null

// Module ctor/dtor register/unregister the binary at startup/shutdown.
// LLVM: @llvm.global_ctors = appending global {{.*}} @offload_device_module_load
// LLVM: @llvm.global_dtors = appending global {{.*}} @offload_device_module_unload

// --- Function body checks (ordered to match IR layout: main, then decls, then ctor) ---

// main launches the kernel via hipLaunchKernel (HIP-native path, no string lookup).
// LLVM:      define{{.*}}i32 @main(
// LLVM:        call {{.*}} @hipLaunchKernel(

// The kernel function is compiled into the device binary — NOT defined in the
// host module as a regular function.
// LLVM-NOT: define{{.*}}@_Z9kernel_fni{{^"}}

// HIP runtime functions are declared (not defined) in the host module.
// These declarations appear after main() in the IR.
// LLVM: declare ptr @__hipRegisterFatBinary(ptr)
// LLVM: declare i32 @__hipRegisterFunction(

// The device module ctor registers the fat binary and kernel.
// LLVM: define internal void @offload_device_module_load(
// LLVM:   call ptr @__hipRegisterFatBinary(
