// Tests that the Clang driver emits the correct invocations for HIP with
// -fclangir: a host cc1 (with -clangir-offload, -emit-cir), a device cc1
// (with -fcuda-is-device, -emit-cir), and a cir-opt invocation that merges
// the two CIR modules before the backend.
//
// RUN: %clang -fclangir --offload-arch=gfx906 -x hip %s -### 2>&1 \
// RUN:   | FileCheck %s

// Host cc1: compiles to CIR with offload dialect enabled.
// CHECK: "-cc1"
// CHECK: "-triple" "x86_64-unknown-linux-gnu"
// CHECK: "-aux-triple" "amdgcn-amd-amdhsa"
// CHECK: "-fclangir"
// CHECK: "-clangir-offload"
// CHECK: "-emit-cir"
// CHECK: "-clangir-offload-arch=gfx906"

// Device cc1: compiles with -fcuda-is-device to produce TY_CIR_DEVICE.
// CHECK: "-cc1"
// CHECK: "-triple" "amdgcn-amd-amdhsa"
// CHECK: "-aux-triple" "x86_64-unknown-linux-gnu"
// CHECK: "-emit-cir"
// CHECK: "-fcuda-is-device"

// Merge step: cir-opt combines host.cir + device.dcir before the backend.
// CHECK: cir-opt
// CHECK: --offload-merge-modules

__global__ void kernel(int a) {}
int main() {
  kernel<<<1, 1>>>(42);
  return 0;
}
