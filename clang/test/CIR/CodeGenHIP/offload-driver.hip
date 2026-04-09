// Tests that the Clang driver emits a single cc1 invocation with the correct
// flags when compiling HIP with -fclangir. The CIR single-source offload path
// handles both host and device in one cc1 pass, so no device cc1 should appear.
//
// RUN: %clang -fclangir --offload-arch=gfx906 -x hip %s -### 2>&1 \
// RUN:   | FileCheck %s

// Exactly one cc1 invocation with the CIR offload flags.
// CHECK: "-cc1"
// CHECK: "-aux-triple" "amdgcn-amd-amdhsa"
// CHECK: "-fclangir"
// CHECK: "-clangir-offload"
// CHECK: "-aux-target-cpu" "gfx906"

// No device cc1: the device-side flag must not appear anywhere.
// CHECK-NOT: "-fcuda-is-device"

__global__ void kernel(int a) {}
int main() {
  kernel<<<1, 1>>>(42);
  return 0;
}
