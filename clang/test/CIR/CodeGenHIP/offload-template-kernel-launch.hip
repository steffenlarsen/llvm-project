// Tests that kernel launches via template parameters (indirect callees)
// fall back to the runtime call path instead of crashing.
//
// CK and similar libraries launch kernels through template parameter F:
//   template <typename F> void launch(F kernel, ...) {
//       kernel<<<grid, block>>>(args...);
//   }
//
// In this case expr->getCalleeDecl() returns a ParmVarDecl (the function
// pointer parameter), not a FunctionDecl. The offload path falls back to
// the base CIRGenCUDARuntime::emitCUDAKernelCallExpr which handles this.
//
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -clangir-offload \
// RUN:            -x hip -fhip-new-launch-api \
// RUN:            -aux-triple amdgcn-amd-amdhsa -aux-target-cpu gfx942 \
// RUN:            -I%S/Inputs -emit-cir %s -o - \
// RUN:   | FileCheck --check-prefix=HOST %s

#include "hip_runtime_minimal.h"

__global__ void my_kernel(int *p, int n) {}

template <typename F, typename... Args>
__host__ void launch(F kernel, dim3 grid, dim3 block, Args... args) {
    kernel<<<grid, block, 0, 0>>>(args...);
}

void host_fn() {
    int *p = nullptr;
    launch(my_kernel, dim3(1,1,1), dim3(256,1,1), p, 1024);
}

// The template-parameter kernel launch should compile without crashing.
// The host module should contain the launch infrastructure.
// HOST: cir.func {{.*}}@_Z7host_fnv
