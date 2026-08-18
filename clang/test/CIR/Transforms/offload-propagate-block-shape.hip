// End-to-end block shape propagation for HIP: real CIRGen output for host and
// device TUs, merged into a cir.offload.container by the merge tool, which runs
// the offload-container passes by default.
//
// Real CIRGen emits the launch configuration call already coerced by the target
// ABI -- each dim3 becomes an {i64, i32} pair -- which is a different operand
// layout from the one written in the source declaration. A .cir test cannot
// notice if that layout changes, so the shape the compiler actually produces is
// exercised here.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -target-sdk-version=9.2 -fhip-new-launch-api -x hip -I %S/../../CodeGenCUDA/Inputs \
// RUN:   -emit-cir %s -o %t-host.cir
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 -target-sdk-version=9.2 \
// RUN:   -fcuda-is-device -fhip-new-launch-api -x hip -I %S/../../CodeGenCUDA/Inputs -emit-cir %s -o %t-dev.cir
// RUN: cir-offload-merge -combine -input=%t-host.cir -input=%t-dev.cir \
// RUN:   -targets=host-x86_64-unknown-linux-gnu,hip-amdgcn-amd-amdhsa--gfx900 \
// RUN:   -output=%t-combined.cir
// RUN: FileCheck %s --input-file=%t-combined.cir

#include "cuda.h"

// What __clang_hip_runtime_wrapper.h declares, reduced to the part the pass
// looks at. Spelling it here rather than including the HIP headers keeps the
// test runnable under cc1 with -nostdsysteminc; the mangled name is what the
// recogniser matches, and it is written out in full below.
struct __hip_builtin_blockDim_t {
  __device__ static unsigned __get_x();
};

__device__ void use(unsigned);

__global__ void fixed_block() { use(__hip_builtin_blockDim_t::__get_x()); }

// The kernel's address escapes through the handle global CIRGen emits for it,
// so the original is left reading blockDim and a clone carries the constant.
// CHECK:      cir.func{{.*}} @_Z11fixed_blockv() cc(amdgpu_kernel)
// CHECK:        cir.call @_ZN24__hip_builtin_blockDim_t7__get_xEv()

// CHECK:      cir.func{{.*}} @_Z11fixed_blockv{{.+}}() cc(amdgpu_kernel)
// CHECK-NOT:    cir.call @_ZN24__hip_builtin_blockDim_t7__get_xEv()
// CHECK:        cir.const #cir.int<64> : !u32i

void host() { fixed_block<<<4, 64>>>(); }
