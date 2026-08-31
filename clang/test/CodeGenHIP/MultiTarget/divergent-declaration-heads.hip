// Declarations whose targets differ inside a braced initializer or in the
// alignment of a class are each target's own. The `}` closing either does
// not end the declaration, and a class whose copies differ only in alignment
// lays out differently, so code naming it is each target's too.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// Host-primary.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=2:%t.h.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1030 -O2" \
// RUN:   -multi-target-aux-output=3:%t.h.gfx1030.bc
// RUN: FileCheck --check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis %t.h.gfx90a.bc -o - | FileCheck --check-prefix=GFX90A %s
// RUN: llvm-dis %t.h.gfx1030.bc -o - | FileCheck --check-prefix=GFX1030 %s

// Device-primary.
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1030 -fcuda-is-device \
// RUN:   -O2 -x hip -emit-llvm -o %t.d.gfx1030.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=2:%t.d.gfx90a.bc
// RUN: FileCheck --check-prefix=GFX1030 %s < %t.d.gfx1030.ll
// RUN: llvm-dis %t.d.gfx90a.bc -o - | FileCheck --check-prefix=GFX90A %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__gfx1030__)
#define WS 32
#else
#define WS 64
#endif

__device__ const int tab[] = {
#if defined(__gfx1030__)
    1, 2, 3
#else
    4, 5
#endif
};

struct alignas(WS) A { int x; };
struct F { alignas(WS) char c; };
struct [[gnu::aligned(WS)]] G { int x; };
typedef struct alignas(WS) { int x; } H;

// GFX90A-LABEL: define {{.*}}@_Z1kPi(
// GFX90A: store i32 2,
// GFX90A: store i32 64,
// GFX90A: store i32 64,
// GFX90A: store i32 64,
// GFX90A: store i32 64,
// GFX1030-LABEL: define {{.*}}@_Z1kPi(
// GFX1030: store i32 3,
// GFX1030: store i32 32,
// GFX1030: store i32 32,
// GFX1030: store i32 32,
// GFX1030: store i32 32,
__global__ void k(int *p) {
  p[1] = sizeof(tab) / sizeof(int);
  p[2] = alignof(A);
  p[3] = sizeof(F);
  p[4] = alignof(G);
  p[5] = alignof(H);
}

// HOST-LABEL: define {{.*}}@_Z5sizesv(
// HOST: ret i32 2064
int sizes() { return sizeof(tab) / sizeof(int) * 1000 + alignof(H); }
