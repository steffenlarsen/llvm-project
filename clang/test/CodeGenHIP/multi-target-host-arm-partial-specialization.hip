// A kernel template whose host and device arms name different specializations
// of a class template (llama.cpp's conv3d.cu: tile<16, 16, float> on the host,
// tile<16, 16, float, DATA_LAYOUT_J_MAJOR> on the devices). The host's arm is
// the host's view of the code: a class template's partial specializations are
// matched as the host sees them there, not as a device does. Its explicit
// instantiation stands for each target's copy of the template, so every device
// emits the kernel.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
namespace m {
enum data_layout { I_MAJOR = 0, J_MAJOR = 10 };
template <int I_, int J_, typename T, data_layout ds_ = I_MAJOR> struct tile {};
template <int I_, int J_, typename T> struct tile<I_, J_, T, I_MAJOR> {
  static constexpr int I = I_;
#if defined(__HIP_DEVICE_COMPILE__)
  static constexpr int ne = I_ * J_ / 64;
#else
  static constexpr int ne = I_ * J_ / 32;
#endif
  T x[ne];
};
template <int I_, int J_, typename T> struct tile<I_, J_, T, J_MAJOR> {
  static constexpr int ne = tile<I_, J_, T, I_MAJOR>::ne;
  T x[ne];
};
} // namespace m
template <int K, bool use>
__global__ void k(float *out) {
  using namespace m;
#if defined(__HIP_DEVICE_COMPILE__)
  using tile_c = tile<16, 16, float, J_MAJOR>;
#else
  using tile_c = tile<16, 16, float>;
#endif
  tile_c c;
  if constexpr (use) {
    for (int l = 0; l < c.ne; ++l)
      out[l] = c.ne + K;
  }
}
template __global__ void k<3, true>(float *);

// CHECK-LABEL: define {{.*}} @_Z1kILi3ELb1EEvPf(
// CHECK: store float 7.0
