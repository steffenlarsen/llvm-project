// A class whose copies differ only in a default member initializer lays out
// alike on every target. Code naming it is still each target's: the implicit
// constructor initializes the member with the target's own value.
//
// REQUIRES: amdgpu-registered-target

// Host-primary.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.h.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.h.gfx942.bc
// RUN: llvm-dis %t.h.gfx90a.bc -o - | FileCheck --check-prefix=GFX90A %s
// RUN: llvm-dis %t.h.gfx942.bc -o - | FileCheck --check-prefix=GFX942 %s

// Device-primary.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx942 -fcuda-is-device \
// RUN:   -O2 -x hip -emit-llvm -o %t.d.gfx942.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.d.gfx90a.bc
// RUN: FileCheck --check-prefix=GFX942 %s < %t.d.gfx942.ll
// RUN: llvm-dis %t.d.gfx90a.bc -o - | FileCheck --check-prefix=GFX90A %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__gfx90a__)
#define ARCH 910
#elif defined(__gfx942__)
#define ARCH 942
#else
#define ARCH 0
#endif

struct Base { int x = ARCH; };
struct E { int y = ARCH + 1; int z = 2; };
__device__ int dev_use(E e) { return e.y; }

// GFX90A-LABEL: define {{.*}}@_Z1kPi(
// GFX90A-DAG: store i32 910,
// GFX90A-DAG: store i32 911,
// GFX942-LABEL: define {{.*}}@_Z1kPi(
// GFX942-DAG: store i32 942,
// GFX942-DAG: store i32 943,
__global__ void k(int *p) {
  Base b;
  p[0] = b.x;
  E e;
  p[1] = dev_use(e);
}
