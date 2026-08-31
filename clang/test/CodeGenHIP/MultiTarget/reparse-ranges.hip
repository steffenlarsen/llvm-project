// A declaration is parsed again over all of its tokens: one following a
// pragma or `__extension__`, a class definition followed by attributes, and a
// linkage specification without braces followed by declarators. Each target
// gets its own layout of the class.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu \
// RUN:   -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx90a -O1 -x hip \
// RUN:   -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-std=c++17 %{hip_device} %{host_aux_triple} -target-cpu gfx90a -O1" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=DEV %s
// RUN: FileCheck --check-prefix=HOST %s < %t.host.ll

#define __global__ __attribute__((global))

#pragma pack(push, 1)
struct Packed { char tag; long double value; };
#pragma pack(pop)

__extension__ struct Ext { char tag; long double value; };

struct Aligned { char tag; long double value; } __attribute__((aligned(16)));

extern "C" struct WithVar { char tag; long double value; } with_var;

__global__ void kernel(int *out) {
  out[0] = sizeof(Packed);
  out[1] = sizeof(Ext);
  out[2] = sizeof(Aligned);
  out[3] = sizeof(WithVar);
}

int on_host(int i) {
  int sizes[] = {sizeof(Packed), sizeof(Ext), sizeof(Aligned),
                 sizeof(WithVar)};
  return sizes[i];
}

// DEV-LABEL: define {{.*}}@_Z6kernelPi(
// DEV-DAG: store i32 9,
// DEV-DAG: store i32 16,
// HOST: @__const._Z7on_hosti.sizes = {{.*}}[i32 17, i32 32, i32 32, i32 32]
