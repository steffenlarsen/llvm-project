// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -x hip -fclangir -fcuda-is-device -emit-cir %s -o - | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -x hip -fclangir -fcuda-is-device -emit-llvm %s -o - | FileCheck %s --check-prefix=LLVM

// Check that casting a __shared__ (AMDGPU local/LDS, address space 3) pointer
// to a pointer type with no address space qualifier (generic/default address
// space) emits an address-space cast rather than a plain bitcast. Sema
// classifies both of these as CK_BitCast, since CUDA/HIP address spaces are
// resolved from decl attributes during CodeGen and are invisible to Sema, so
// CIRGen must detect the address-space mismatch itself, mirroring OGCG.

template <typename T>
__attribute__((device)) T *shared_cast() {
  extern __attribute__((shared)) unsigned sh[];
  return (T *)sh;
}

__attribute__((shared)) double sh_arr[8];

__attribute__((device)) int *shared_reinterpret_cast() {
  return reinterpret_cast<int *>(&sh_arr);
}

__attribute__((global)) void kernel(float *out) {
  float *p = shared_cast<float>();
  int *q = shared_reinterpret_cast();
  out[0] = p[0] + q[0];
}

// CIR-LABEL: cir.func {{.*}} @_Z23shared_reinterpret_castv
// CIR:         %[[GLOBAL:.*]] = cir.get_global @sh_arr : !cir.ptr<!cir.array<!cir.double x 8>, target_address_space(3)>
// CIR:         %[[BITCAST:.*]] = cir.cast bitcast %[[GLOBAL]] : !cir.ptr<!cir.array<!cir.double x 8>, target_address_space(3)> -> !cir.ptr<!s32i, target_address_space(3)>
// CIR:         %{{.*}} = cir.cast address_space %[[BITCAST]] : !cir.ptr<!s32i, target_address_space(3)> -> !cir.ptr<!s32i>

// CIR-LABEL: cir.func {{.*}} @_Z11shared_castIfEPT_v
// CIR:         %[[GLOBAL:.*]] = cir.get_global @sh : !cir.ptr<!cir.array<!u32i x 0>, target_address_space(3)>
// CIR:         %[[DECAY:.*]] = cir.cast array_to_ptrdecay %[[GLOBAL]] : !cir.ptr<!cir.array<!u32i x 0>, target_address_space(3)> -> !cir.ptr<!u32i, target_address_space(3)>
// CIR:         %[[BITCAST:.*]] = cir.cast bitcast %[[DECAY]] : !cir.ptr<!u32i, target_address_space(3)> -> !cir.ptr<!cir.float, target_address_space(3)>
// CIR:         %{{.*}} = cir.cast address_space %[[BITCAST]] : !cir.ptr<!cir.float, target_address_space(3)> -> !cir.ptr<!cir.float>

// LLVM-LABEL: define {{.*}} @_Z23shared_reinterpret_castv
// LLVM:         store ptr addrspacecast (ptr addrspace(3) @sh_arr to ptr), ptr addrspace(5) %{{.*}}

// LLVM-LABEL: define {{.*}} @_Z11shared_castIfEPT_v
// LLVM:         store ptr addrspacecast (ptr addrspace(3) @sh to ptr), ptr addrspace(5) %{{.*}}
