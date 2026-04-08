// Verify that the GpuSpecializeScalarArgs pass handles float arguments
// where the host passes a double literal cast to float.  Previously, the
// pass stripped the cir.cast(double→float) and used the pre-cast double
// attribute as a float constant, creating an invalid cir.const with
// mismatched type/value (FloatAttr semantics mismatch).

// REQUIRES: amdgpu-registered-target

// RUN: %clang -fclangir --offload-arch=gfx90a -x hip -c %s \
// RUN:   -I%S/Inputs -o /dev/null

#include "hip_runtime_minimal.h"

__global__ void init_kernel(float *out, float val) {
  int idx = threadIdx.x + blockIdx.x * blockDim.x;
  out[idx] = val;
}

void run(float *d_out, int n) {
  init_kernel<<<dim3(n / 256), dim3(256)>>>(d_out, (float)0.1);
}
