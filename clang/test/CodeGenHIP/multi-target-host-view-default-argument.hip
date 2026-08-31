// The host's view of device code names a class template specialization as
// the host's code would, with the host's default arguments. It used the
// canonical device's, a wave64 specialization the host never instantiates,
// whose host arm then failed a static_assert.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -std=c++17 -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip -std=c++17" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1030 -fcuda-is-device -O2 -x hip -std=c++17" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1100 -fcuda-is-device -O2 -x hip -std=c++17" \
// RUN:   -mllvm -multi-target-aux-output=4:%t.gfx1100.bc \
// RUN: -verify

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
#define __shared__ __attribute__((shared))

// expected-no-diagnostics
enum class target { size32, size64, dynamic };
constexpr target get_target() {
#if !defined(__HIP_DEVICE_COMPILE__)
  return target::dynamic;
#elif defined(__GFX9__)
  return target::size64;
#else
  return target::size32;
#endif
}
template <class T, unsigned W> struct wscan {
  template <class Op> __device__ T inclusive(T v, Op op) {
#if defined(__GFX9__)
    return op(v, (T)(W * 2));
#else
    static_assert(W <= 32, "no broadcast");
    return op(v, (T)W);
#endif
  }
};
struct plus {
  __device__ int operator()(int a, int b) const { return a + b; }
};
template <int BS, target W = get_target()> struct bscan {
  __device__ int run(int v) {
    return wscan<int, W == target::size64 ? 64 : 32>().inclusive(v, plus());
  }
};
__global__ void k(int *p) { p[0] = bscan<1024>().run(p[1]); }
