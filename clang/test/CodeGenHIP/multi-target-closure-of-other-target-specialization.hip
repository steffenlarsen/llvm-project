// A specialization keyed by the canonical device stands for every device
// without one of its own. One whose template arguments name a closure of
// another target's specialization, here the lambda a keyed warp scan passes
// down, mangles the same as the device's own and must not be emitted for
// it. A device then defined the closure's call operator of neither and only
// declared it.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -std=c++20 -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip -std=c++20" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1030 -fcuda-is-device -x hip -std=c++20" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1100 -fcuda-is-device -x hip -std=c++20" \
// RUN:   -mllvm -multi-target-aux-output=4:%t.gfx1100.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=W64 %s \
// RUN:   --implicit-check-not="declare {{.*}}ENKUliE_clEi"
// RUN: llvm-dis %t.gfx1030.bc -o - | FileCheck --check-prefix=W32 %s \
// RUN:   --implicit-check-not="declare {{.*}}ENKUliE_clEi"
// RUN: llvm-dis %t.gfx1100.bc -o - | FileCheck --check-prefix=W32 %s \
// RUN:   --implicit-check-not="declare {{.*}}ENKUliE_clEi"

// W64-DAG: define {{.*}}@_ZZ8move_dppIN6TraitsILi128ELb1EE4ElemELi337EET_RKS3_ENKUliE_clEi(
// W64-DAG: define {{.*}}@_ZZ8move_dppIN6TraitsILi128ELb0EE4ElemELi337EET_RKS3_ENKUliE_clEi(
// W32-DAG: define {{.*}}@_ZZ8move_dppIN6TraitsILi128ELb1EE4ElemELi305EET_RKS3_ENKUliE_clEi(
// W32-DAG: define {{.*}}@_ZZ8move_dppIN6TraitsILi128ELb0EE4ElemELi305EET_RKS3_ENKUliE_clEi(

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

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
template <class T, class F>
__device__ __attribute__((noinline)) T shuffle_op(const T &in, F &&f) {
  T out = in;
  out.a = f(in.a);
  return out;
}
template <class T, int Ctrl> __device__ __attribute__((noinline)) T move_dpp(const T &in) {
  return shuffle_op(in, [=](int v) -> int { return v + Ctrl; });
}
template <class T, int WS> struct warp_scan {
  __device__ __attribute__((noinline)) T scan(const T &e) {
    return move_dpp<T, 273 + WS>(e);
  }
};
template <class T, int BS, target W = get_target()> struct block_scan {
  __device__ __attribute__((noinline)) T run(const T &e) {
    return warp_scan<T, W == target::size64 ? 64 : 32>().scan(e);
  }
};
template <int N, bool A> struct Traits {
  struct Elem { int a; int b; };
  using Scan = block_scan<Elem, N>;
};
template <class Tr> __global__ void kernel(int *p) {
  using E = typename Tr::Elem;
  E e{p[0], p[1]};
  p[2] = typename Tr::Scan().run(e).a;
}
void *launch(bool b) {
  void *k = nullptr;
  auto dispatch = [&]<bool A>() {
    using Tr = Traits<128, A>;
    k = (void *)kernel<Tr>;
  };
  if (b)
    dispatch.template operator()<true>();
  else
    dispatch.template operator()<false>();
  return k;
}
