// Tests that initializing a vector from another vector of the same type
// (common in GPU code with ext_vector_type) doesn't crash with a
// "cir.vec.create operand must be scalar" error.
//
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -fclangir \
// RUN:            -aux-triple x86_64-unknown-linux-gnu \
// RUN:            -target-cpu gfx942 \
// RUN:            -x hip -fcuda-is-device -fhip-new-launch-api \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=CIR %s

#include "hip_runtime_minimal.h"

typedef float float4 __attribute__((ext_vector_type(4)));

struct vec_holder {
    float4 data;
    __device__ vec_holder(float4 v) : data{v} {}
};

__device__ void test_vec_init() {
    float4 v = {1.0f, 2.0f, 3.0f, 4.0f};
    vec_holder h(v);
}

// CIR-LABEL: cir.func {{.*}}@_Z13test_vec_initv
// CIR-NOT: cir.vec.create {{.*}}!cir.vector
