// End-to-end dead argument elimination for HIP: real CIRGen output for host and
// device TUs, merged into a cir.offload.container by the merge tool, which runs
// the offload-container passes by default.
//
// The .cir test drives the pass on a hand-written stub; this one checks that
// the argument buffer CIRGen actually emits is the shape the matcher accepts,
// which is the whole basis for the rewrite being safe.

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -target-sdk-version=9.2 -fhip-new-launch-api -x hip -I %S/../../CodeGenCUDA/Inputs \
// RUN:   -emit-cir %s -o %t-host.cir
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx900 -target-sdk-version=9.2 \
// RUN:   -fcuda-is-device -fhip-new-launch-api -x hip -I %S/../../CodeGenCUDA/Inputs \
// RUN:   -emit-cir %s -o %t-dev.cir
// RUN: cir-offload-merge -combine -input=%t-host.cir -input=%t-dev.cir \
// RUN:   -targets=host-x86_64-unknown-linux-gnu,hip-amdgcn-amd-amdhsa--gfx900 \
// RUN:   -output=%t-combined.cir
// RUN: FileCheck %s --input-file=%t-combined.cir
// RUN: FileCheck %s --input-file=%t-combined.cir --check-prefix=ATTRS

#include "cuda.h"

__device__ void use(int *);

// `unused` is spilled by CIRGen but never read, so it is the dead one; `out`
// stays live.
__global__ void k(int *out, int unused) { use(out); }

// The clone's suffix names whichever pass created it, which is not what is
// being checked here: what matters is that exactly one of the two copies lost
// the argument, and that the stub and the call lost it in step.

// The stub the visible launch reaches takes one argument and is passed one.
// CHECK-LABEL: cir.func{{.*}} @_Z16__device_stub__kPii.{{[a-z]+}}(%arg0: !cir.ptr<!s32i>
// CHECK-SAME:    ) attributes
// CHECK-LABEL: cir.func{{.*}} @_Z4hostPii
// CHECK:         cir.call @_Z16__device_stub__kPii.{{[a-z]+}}(%{{[a-z0-9]+}}) {cu.kernel_name

// The original keeps its full signature, for launches this TU cannot see.
// CHECK:      cir.func{{.*}} @_Z1kPii(%arg0: !cir.ptr<!s32i, target_address_space(1)>{{.*}}, %arg1: !s32i
// CHECK-SAME:   ) cc(amdgpu_kernel)
// CHECK:      cir.func{{.*}} @_Z1kPii.{{[a-z]+}}(%arg0: !cir.ptr<!s32i, target_address_space(1)>
// CHECK-SAME:   ) cc(amdgpu_kernel)

// `unused` is a runtime value, so constant propagation has nothing to say
// about it and this exercises dead-argument elimination on its own.
void host(int *p, int u) { k<<<1, 64>>>(p, u); }

// A dead argument ahead of one that carries a parameter attribute. `arg_attrs`
// is positional, so dropping the operand without dropping its attribute would
// rebind `zeroext` from the bool to whatever followed it -- a call the LLVM
// verifier rejects, and the reason this shape is tested rather than assumed.
__global__ void attrs(int unused, bool flag, int *out) {
  if (flag)
    use(out);
}

// ATTRS-LABEL: cir.func{{.*}} @_Z20__device_stub__attrsibPi.{{[a-z]+}}(%arg0: !cir.bool {llvm.noundef, llvm.zeroext}, %arg1: !cir.ptr<!s32i> {llvm.noundef})

// ATTRS-LABEL: cir.func{{.*}} @_Z10host_attrsPib
// ATTRS:         cir.call @_Z20__device_stub__attrsibPi.{{[a-z]+}}({{.*}}) {{.*}} : (!cir.bool {llvm.noundef, llvm.zeroext}, !cir.ptr<!s32i> {llvm.noundef})

void host_attrs(int *p, bool f) { attrs<<<1, 64>>>(7, f, p); }
