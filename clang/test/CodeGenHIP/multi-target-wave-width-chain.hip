// Device code instantiated where it is not known which devices compile it
// need not compile for all of them. Here only the wave64 device reaches
// impl<64>::run, whose wave32 body fails a static_assert. A device that does
// not compile it reports that only if it emits it.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -target-cpu gfx1030 -fcuda-is-device \
// RUN:   -O2 -x hip -emit-llvm -o %t.gfx1030.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc
// RUN: FileCheck --check-prefix=GFX1030 %s < %t.gfx1030.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=GFX90A %s

// Host-primary, the wave64 device first and second.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.h1.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1030 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.h1.gfx1030.bc
// RUN: llvm-dis %t.h1.gfx90a.bc -o - | FileCheck --check-prefix=GFX90A %s
// RUN: llvm-dis %t.h1.gfx1030.bc -o - | FileCheck --check-prefix=GFX1030 %s
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1030 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.h2.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.h2.gfx90a.bc
// RUN: llvm-dis %t.h2.gfx90a.bc -o - | FileCheck --check-prefix=GFX90A %s
// RUN: llvm-dis %t.h2.gfx1030.bc -o - | FileCheck --check-prefix=GFX1030 %s

// The wave32 device emitting it is an error, as in its own compilation.
// RUN: not %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.host.ll %s -DWAVE32_USES_IT \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.e.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1030 -fcuda-is-device -O2 -x hip -DWAVE32_USES_IT" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.e.gfx1030.bc \
// RUN: 2>&1 | FileCheck --check-prefix=ERR %s
// ERR: error: static assertion failed due to requirement '64 <= 32': wider than the wave
// ERR: 1 error generated when compiling for gfx1030.

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__gfx1030__)
#define WAVE 32
#else
#define WAVE 64
#endif
constexpr int wave() { return WAVE; }

template <int N> struct impl {
  __device__ int run(int x) {
#if WAVE == 32
    static_assert(N <= 32, "wider than the wave");
    return x + N;
#else
    return x * N;
#endif
  }
};
template <int N> struct inner {
  __device__ int step(int x) { return impl<N>().run(x); }
};
template <int N> struct outer {
  __device__ int step(int x) { return inner<N>().step(x); }
};
template <int W = wave()> struct dispatch {
  __device__ int go(int x) { return outer<W>().step(x); }
};

// GFX1030-LABEL: define {{.*}}@_Z1kPi(
// GFX1030: add nsw i32 %{{.*}}, 32
// GFX90A-LABEL: define {{.*}}@_Z1kPi(
// GFX90A: shl nsw i32 %{{.*}}, 6
__global__ void k(int *p) { p[0] = dispatch<>().go(p[1]); }

#ifdef WAVE32_USES_IT
__global__ void k64(int *p) { p[0] = impl<64>().run(p[1]); }
#endif
