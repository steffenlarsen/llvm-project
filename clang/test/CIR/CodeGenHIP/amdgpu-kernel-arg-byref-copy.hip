// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -x hip \
// RUN:   -fcuda-is-device -fhip-new-launch-api -fclangir -emit-cir %s -o - \
// RUN:   | FileCheck %s --check-prefix=CIR
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -x hip \
// RUN:   -fcuda-is-device -fhip-new-launch-api -fclangir -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=LLVM
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx90a -x hip \
// RUN:   -fcuda-is-device -fhip-new-launch-api -emit-llvm %s -o - \
// RUN:   | FileCheck %s --check-prefix=OGCG

// A record kernel argument is passed byref in the constant address space.
// Its spill slot must be filled by a memcpy from that pointer, as classic
// codegen does, not by loading the whole record and storing it back: the
// backend promotes pointers read from the kernel argument to the global
// address space only when it sees scalar loads of them, so an aggregate
// load leaves every pointer member flat.

struct Functor {
  int *out;
  const int *in;
  int n;
};

__attribute__((global)) void kernel(const Functor f, int i) {
  f.out[i] = f.in[i] + f.n;
}

// CIR: cir.func {{.*}} @_Z6kernel7Functori(%[[ARG:[a-z0-9]+]]: !cir.ptr<!rec_Functor, target_address_space(4)> {{{.*}}llvm.byref = !rec_Functor{{.*}}cc(amdgpu_kernel)
// CIR:   %[[F:.*]] = cir.alloca "f" align(8) init const : !cir.ptr<!rec_Functor, target_address_space(5)>
// CIR-NOT: cir.load {{.*}}%[[ARG]]
// CIR:   cir.copy %[[ARG]] align(8) to %[[F]] align(8) : !cir.ptr<!rec_Functor, target_address_space(4)>, !cir.ptr<!rec_Functor, target_address_space(5)>

// LLVM: define dso_local amdgpu_kernel void @_Z6kernel7Functori(ptr addrspace(4) noundef byref(%struct.Functor) align 8 %[[ARG:.*]], i32 noundef %{{.*}})
// LLVM:   %[[F:.*]] = alloca %struct.Functor, align 8, addrspace(5)
// LLVM-NOT: load %struct.Functor
// LLVM:   call void @llvm.memcpy.p5.p4.i64(ptr addrspace(5) align 8 %[[F]], ptr addrspace(4) align 8 %[[ARG]], i64 24, i1 false)

// OGCG: define dso_local amdgpu_kernel void @_Z6kernel7Functori(ptr addrspace(4) noundef byref(%struct.Functor) align 8 %[[ARG:.*]], i32 noundef %{{.*}})
// OGCG:   %[[F:.*]] = alloca %struct.Functor, align 8, addrspace(5)
// OGCG:   call void @llvm.memcpy.p5.p4.i64(ptr addrspace(5) align 8 %[[F]], ptr addrspace(4) align 8 %[[ARG]], i64 24, i1 false)
