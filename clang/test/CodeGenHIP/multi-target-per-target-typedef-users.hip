// Code naming a typedef that differs per target, outside a template argument,
// is each target's own. Here a namespace's first declaration, and the member
// of a class template that only a wave64 specialization reaches.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1030 -fcuda-is-device \
// RUN:   -O2 -x hip -emit-llvm -o %t.d.gfx1030.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.d.gfx90a.bc
// RUN: FileCheck --check-prefix=GFX1030 %s < %t.d.gfx1030.ll
// RUN: llvm-dis %t.d.gfx90a.bc -o - | FileCheck --check-prefix=GFX90A %s

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.h.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1030 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.h.gfx1030.bc
// RUN: llvm-dis %t.h.gfx90a.bc -o - | FileCheck --check-prefix=GFX90A %s
// RUN: llvm-dis %t.h.gfx1030.bc -o - | FileCheck --check-prefix=GFX1030 %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__gfx1030__)
#define WAVE 32
#else
#define WAVE 64
#endif
constexpr int wave() { return WAVE; }
template <bool B, class T, class F> struct cond { using type = T; };
template <class T, class F> struct cond<false, T, F> { using type = F; };

namespace ns {
using mask_t = typename cond<wave() == 32, unsigned, unsigned long long>::type;
}
namespace ns {
__device__ inline mask_t ballot(int x) { return (mask_t)x; }
__device__ inline int width() { return (int)sizeof(ballot(1)); }
}

template <int N> struct ranker {
  template <class T> __device__ int rank(T x) {
    ns::mask_t m = static_cast<ns::mask_t>(x);
    return (int)sizeof(m) * 100 + N;
  }
};
template <int W = wave()> struct dispatch {
  __device__ int go(int x) { return ranker<W>().rank(x); }
};

// GFX1030-LABEL: define {{.*}}@_Z1kPi(
// GFX1030: store i32 4,
// GFX1030: store i32 432,
// GFX90A-LABEL: define {{.*}}@_Z1kPi(
// GFX90A: store i32 8,
// GFX90A: store i32 864,
__global__ void k(int *p) {
  p[0] = ns::width();
  p[1] = dispatch<>().go(p[2]);
}
