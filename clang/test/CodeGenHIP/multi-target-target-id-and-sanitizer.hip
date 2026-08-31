// Each target's device code is packaged under the target ID the driver bound
// it to, and is instrumented only by the sanitizers its own invocation
// enables. The driver enables AddressSanitizer only for xnack+ targets.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -fgpu-rdc --offload-new-driver -fsanitize=address \
// RUN:   -emit-llvm -o %t.host.ll %s -fembed-offload-object=%t.package \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -fgpu-rdc --offload-new-driver -fsanitize=address -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   -mllvm -multi-target-aux-target-id=2:gfx90a:xnack+ \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -fgpu-rdc --offload-new-driver -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc \
// RUN:   -mllvm -multi-target-aux-target-id=3:gfx942 \
// RUN:   -mllvm -multi-target-package-offload=%t.package
// RUN: FileCheck -check-prefix=HOST %s < %t.host.ll
// RUN: llvm-offload-binary %t.package \
// RUN:   --image=file=%t.img90a.bc,triple=amdgcn-amd-amdhsa,arch=gfx90a:xnack+ \
// RUN:   --image=file=%t.img942.bc,triple=amdgcn-amd-amdhsa,arch=gfx942
// RUN: llvm-dis -o - %t.img90a.bc | FileCheck -check-prefix=ASAN %s
// RUN: llvm-dis -o - %t.img942.bc | FileCheck -check-prefix=NOASAN %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

// HOST: define {{.*}}@_Z5hostfPi({{.*}}) #[[HATTR:[0-9]+]]
// HOST: attributes #[[HATTR]] = {{.*}}sanitize_address

// ASAN: define {{.*}}@_Z1kPi({{.*}}) #[[KATTR:[0-9]+]]
// ASAN: attributes #[[KATTR]] = {{.*}}sanitize_address

// NOASAN: define {{.*}}@_Z1kPi(
// NOASAN-NOT: sanitize_address
// NOASAN-NOT: __asan

__global__ void k(int *p) { p[0] = 1; }
void hostf(int *p) { p[1] = 2; }
