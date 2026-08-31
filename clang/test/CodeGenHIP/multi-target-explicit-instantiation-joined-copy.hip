// A function template re-parsed per target, whose copies for two targets are
// one, is explicitly instantiated for each target its copies stand for. The
// second of those targets lost the kernel.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -std=c++17 -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip -std=c++17" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1030 -fcuda-is-device -O2 -x hip -std=c++17" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1100 -fcuda-is-device -O2 -x hip -std=c++17" \
// RUN:   -mllvm -multi-target-aux-output=4:%t.gfx1100.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=GFX9 %s
// RUN: llvm-dis %t.gfx1030.bc -o - | FileCheck --check-prefix=RDNA %s
// RUN: llvm-dis %t.gfx1100.bc -o - | FileCheck --check-prefix=RDNA %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
#define __shared__ __attribute__((shared))

#if defined(__GFX9__)
typedef long sidet;
static constexpr int w = 64;
#else
typedef int sidet;
static constexpr int w = 32;
#endif
template <int N> struct arr { int x[N]; };

// GFX9-LABEL: define {{.*}}@_Z1kIiEvPT_(
// GFX9: store i32 8,
// GFX9: store i32 64,
// GFX9: store i32 256,
// RDNA-LABEL: define {{.*}}@_Z1kIiEvPT_(
// RDNA: store i32 4,
// RDNA: store i32 32,
// RDNA: store i32 128,
template <class U> __global__ void k(U *p) {
  p[0] = sizeof(sidet);
  p[1] = w;
  p[2] = sizeof(arr<w>);
}
template __global__ void k<int>(int *);
