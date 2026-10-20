// Only the kernels whose stubs are defined in this translation unit are in
// its device binary, so only those are registered with it. A kernel that is
// only declared here, such as an extern template instantiation, is registered
// by the translation unit that defines it. Registering it here as well would
// tie its host handle to a device binary that does not contain it.

// RUN: echo -n "GPU binary would be here." > %t
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm %s -x hip \
// RUN:   -fhip-new-launch-api -fcuda-include-gpubinary %t -o %t-hip-cir.ll
// RUN: FileCheck --input-file=%t-hip-cir.ll %s --check-prefix=HIP
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm %s -x hip \
// RUN:   -fhip-new-launch-api -fcuda-include-gpubinary %t -o %t-hip-ogcg.ll
// RUN: FileCheck --input-file=%t-hip-ogcg.ll %s --check-prefix=HIP
// RUN: %clang_cc1 -triple x86_64-linux-gnu -fclangir -emit-llvm %s -x cuda \
// RUN:   -target-sdk-version=12.3 -fcuda-include-gpubinary %t -o %t-cuda-cir.ll
// RUN: FileCheck --input-file=%t-cuda-cir.ll %s --check-prefix=CUDA
// RUN: %clang_cc1 -triple x86_64-linux-gnu -emit-llvm %s -x cuda \
// RUN:   -target-sdk-version=12.3 -fcuda-include-gpubinary %t -o %t-cuda-ogcg.ll
// RUN: FileCheck --input-file=%t-cuda-ogcg.ll %s --check-prefix=CUDA

#include "Inputs/cuda.h"

template <int N> __global__ void kern(int *p) { p[0] = N; }

// Instantiated in another translation unit.
extern template __global__ void kern<1>(int *);
// Instantiated here.
template __global__ void kern<2>(int *);

void launch(int *p) {
  kern<1><<<1, 1>>>(p);
  kern<2><<<1, 1>>>(p);
}

// HIP-LABEL: define internal void @__hip_register_globals(
// HIP-NOT:     @_Z4kernILi1EEvPi
// HIP:         call {{.*}}@__hipRegisterFunction({{.*}}@_Z4kernILi2EEvPi
// HIP-NOT:     @_Z4kernILi1EEvPi
// HIP:         ret void

// CUDA-LABEL: define internal void @__cuda_register_globals(
// CUDA-NOT:     @_Z19__device_stub__kernILi1EEvPi
// CUDA:         call {{.*}}@__cudaRegisterFunction({{.*}}@_Z19__device_stub__kernILi2EEvPi
// CUDA-NOT:     @_Z19__device_stub__kernILi1EEvPi
// CUDA:         ret void
