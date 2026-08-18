// REQUIRES: amdgpu-registered-target
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -x hip \
// RUN: -std=c++11 -fclangir -fcuda-is-device -emit-cir %s -o %t.cir
// RUN: FileCheck --check-prefix=CIR --input-file=%t.cir %s

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -x hip \
// RUN: -std=c++11 -fclangir -fcuda-is-device -emit-llvm %s -o %t-cir.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t-cir.ll %s

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -x hip \
// RUN: -std=c++11 -fcuda-is-device -emit-llvm %s -o %t.ll
// RUN: FileCheck --check-prefix=LLVM --input-file=%t.ll %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
#define __managed__ __attribute__((managed))

//===----------------------------------------------------------------------===//
// A `__managed__` variable is not the storage the program names. The runtime
// allocates the storage and writes its address into a separate pointer, so the
// definition is split: the pointer keeps the original name and the storage
// becomes `<name>.managed`. Every reference loads through the pointer.
//
// Both must be externally_initialized -- the runtime writes them -- or the
// optimizer is free to fold every load of the pointer to the null it starts as.
//
// The LLVM checks are shared with the classic CodeGen run line, so they pin the
// two paths to the same globals.
//===----------------------------------------------------------------------===//

__device__ __managed__ float buffer[16];
__device__ __managed__ int counter;

// CIR-DAG: cir.global external {{.*}}@buffer.managed = #cir.zero : !cir.array<!cir.float x 16>
// CIR-DAG: cir.global external {{.*}}@buffer = #cir.ptr<null>
// CIR-DAG: cir.global external {{.*}}@counter.managed = #cir.int<0>
// CIR-DAG: cir.global external {{.*}}@counter = #cir.ptr<null>

// LLVM-DAG: @buffer.managed = addrspace(1) externally_initialized global [16 x float] zeroinitializer
// LLVM-DAG: @buffer = addrspace(1) externally_initialized global ptr addrspace(1) null
// LLVM-DAG: @counter.managed = addrspace(1) externally_initialized global i32 0
// LLVM-DAG: @counter = addrspace(1) externally_initialized global ptr addrspace(1) null

// Naming the variable means loading the pointer first.
// LLVM-LABEL: @_Z1ki
// LLVM: load ptr addrspace(1), ptr addrspace(1) @buffer
// LLVM: load ptr addrspace(1), ptr addrspace(1) @counter
__global__ void k(int n) {
  buffer[0] = (float)n;
  counter = n;
}
