// Verify AMDGPU builtin types lower through CIR.
//
// The AMDGPU_OPAQUE_PTR_TYPE entries (buffer_rsrc, texture) are the
// exercisable ones; AMDGPU_FEATURE_PREDICATE_TYPE is compile-time only
// and never reaches CIRGenTypes::convertType at runtime.
//
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a \
// RUN:            -fclangir -fcuda-is-device -emit-cir -x hip %s -o - \
// RUN:   | FileCheck %s

// CHECK: cir.func {{.*}}@_Z12use_buf_rsrc
// CHECK-SAME: !cir.ptr<!void, target_address_space(8)>
__attribute__((device))
void use_buf_rsrc(__amdgpu_buffer_rsrc_t rsrc) {}
// CHECK: cir.return
