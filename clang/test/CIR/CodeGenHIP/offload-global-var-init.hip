// Tests that device globals with constant initializers have their
// initial_value attribute preserved on offload.global_var, and that
// LowerHostRuntime emits an __offload_init_globals() constructor that
// copies each initializer to the device via mgpuModuleGetGlobal +
// hipMemcpy at module load time.  The ctor is registered at priority 200
// so it runs after the binary-load ctor at priority 123.
//
// Only scalar (integer/float) initializers are currently supported;
// aggregate types (arrays, structs) are left zero-initialized.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-llvm %s -o - \
// RUN:   | FileCheck --check-prefix=LLVM %s

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

// CIR-DAG: offload.global_var @dev_var : !s32i mem_space = <device>{{.*}}initial_value = 42 : i32
// CIR-DAG: offload.global_var @const_var : !cir.float mem_space = <constant>{{.*}}initial_value = {{.*}} : f32
// CIR-DAG: offload.global_var @dev_zero : !s32i mem_space = <device>
// CIR-DAG: offload.global_var @dev_arr {{.*}} mem_space = <device>{{.*}}initial_value
// CIR-NOT: offload.global_var @dev_zero{{.*}}initial_value

// --- LLVM IR checks ---

// The binary module handle global is referenced (defined by SelectObjectAttr).
// LLVM: @offload_device_module_module = external global ptr

// Symbol name C strings are emitted for each initialized global.
// LLVM-DAG: @__offload_sym_name.const_var = {{.*}} constant {{.*}} c"const_var\00"
// LLVM-DAG: @__offload_sym_name.dev_var = {{.*}} constant {{.*}} c"dev_var\00"

// The ctor is registered at priority 200 (after binary load at 123).
// LLVM: @llvm.global_ctors = appending global {{.*}} i32 200, ptr @__offload_init_globals

// mgpuModuleGetGlobal is declared (no definition -- comes from the mgpu runtime).
// LLVM: declare void @mgpuModuleGetGlobal(ptr, ptr, ptr, ptr)

// The init function loads the module handle, then calls mgpuModuleGetGlobal
// and hipMemcpy for each initialized global.
// LLVM: define internal void @__offload_init_globals(
// LLVM:   load ptr, ptr @offload_device_module_module
// Two mgpuModuleGetGlobal + hipMemcpy pairs -- one per initialized global.
// LLVM:   call void @mgpuModuleGetGlobal(
// LLVM:   call i32 @hipMemcpy(
// LLVM:   call void @mgpuModuleGetGlobal(
// LLVM:   call i32 @hipMemcpy(
// Three pairs total: dev_var, const_var, dev_arr (dev_zero has no initializer).
// LLVM:   call void @mgpuModuleGetGlobal(
// LLVM:   call i32 @hipMemcpy(
// LLVM-NOT: call void @mgpuModuleGetGlobal(
// hipGetSymbolAddress must NOT appear -- replaced by mgpuModuleGetGlobal.
// LLVM-NOT: call {{.*}} @hipGetSymbolAddress(
