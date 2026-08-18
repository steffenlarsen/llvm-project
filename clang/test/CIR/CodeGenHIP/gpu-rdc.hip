// REQUIRES: amdgpu-registered-target

// -fgpu-rdc used to be rejected outright by ClangIR, in three places: the
// mangler, internalizeDeviceSideVar, and the CUDA module constructor. The LLVM
// checks below are shared between the ClangIR and the classic CodeGen run
// lines, so they pin the two paths to the same output.

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -x hip \
// RUN: -std=c++11 -fclangir -fcuda-is-device -fgpu-rdc -cuid=abc123 \
// RUN: -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=RDC --input-file=%t-cir.ll %s

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -x hip \
// RUN: -std=c++11 -fcuda-is-device -fgpu-rdc -cuid=abc123 \
// RUN: -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=RDC --input-file=%t.ll %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

static __device__ int s_counter;
__device__ int g_counter;

__global__ void k(int *o) {
  s_counter = 1;
  g_counter = 2;
  o[0] = s_counter + g_counter;
}

// A static device variable stays internal here. It only takes the CUID
// postfix once it is externalized, and ASTContext::shouldExternalize requires
// it to have been ODR-used by *host* code -- a set built during host Sema, so
// a device-only compile like this one cannot reach that path. Both compilers
// agree on that, which is what these shared checks assert.
// RDC-DAG: @_ZL9s_counter = internal addrspace(1) global i32 0
// RDC-DAG: @g_counter = {{.*}}addrspace(1) externally_initialized global i32 0

// The CUID marker is emitted under -fgpu-rdc so the linker wrapper can tell
// translation units apart.
// RDC-DAG: @__hip_cuid_{{[0-9a-f]+}} = addrspace(1) global i8 0

// The kernel body is deliberately not checked here: ClangIR stores straight to
// addrspace(1) where classic CodeGen stores through an addrspacecast to
// generic. That difference is pre-existing and unrelated to -fgpu-rdc.
// RDC-LABEL: @_Z1kPi
