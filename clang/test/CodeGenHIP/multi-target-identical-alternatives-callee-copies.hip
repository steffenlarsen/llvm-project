// What a function whose copies have the same tokens computes depends on the
// copies of what they call. rocPRIM's wavefront::max_size() returns
// min_size() on every device, and min_size() is 64 on CDNA and 32 on RDNA,
// so code that uses max_size() through a default template argument is parsed
// once for each, not once for all the devices.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1030 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx1100 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=4:%t.gfx1100.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=CDNA %s
// RUN: llvm-dis %t.gfx1030.bc -o - | FileCheck --check-prefix=RDNA %s
// RUN: llvm-dis %t.gfx1100.bc -o - | FileCheck --check-prefix=RDNA %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

constexpr __device__ unsigned min_size() {
#if defined(__gfx1030__) || defined(__gfx1100__)
  return 32u;
#else
  return 64u;
#endif
}
constexpr __device__ unsigned max_size() {
#if defined(__HIP_DEVICE_COMPILE__)
  return min_size();
#else
  return 64u;
#endif
}
template <class T, int W = max_size()> struct warp_reduce {
  static constexpr int width = W;
};
__global__ void k(int *o) { o[0] = warp_reduce<float>::width; }
// CDNA-LABEL: define {{.*}} @_Z1kPi(
// CDNA: store i32 64,
// RDNA-LABEL: define {{.*}} @_Z1kPi(
// RDNA: store i32 32,
