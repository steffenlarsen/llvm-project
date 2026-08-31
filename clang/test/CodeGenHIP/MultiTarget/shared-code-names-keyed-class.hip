// Code analysed once for every target that names a specialization keyed by
// target names one target's. Where that specialization's definition differs
// between the targets, e.g. through a base class named with a default argument
// giving the wave size, the code is analysed per target. Every device used the
// canonical device's layout.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -std=c++17 -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -O2 -std=c++17" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1030 -O2 -std=c++17" \
// RUN:   -multi-target-aux-output=3:%t.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1100 -O2 -std=c++17" \
// RUN:   -multi-target-aux-output=4:%t.gfx1100.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=W64 %s
// RUN: llvm-dis %t.gfx1030.bc -o - | FileCheck --check-prefix=W32 %s
// RUN: llvm-dis %t.gfx1100.bc -o - | FileCheck --check-prefix=W32 %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
#define __shared__ __attribute__((shared))

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
template <int BS, target W = get_target()> struct scan {
  struct storage_type { int x[BS / (W == target::size64 ? 64 : 32)]; };
  __device__ int run() { return W == target::size64 ? 64 : 32; }
};
template <int BS> class BlockScan : private scan<BS> {
  using base = scan<BS>;
public:
  using TempStorage = typename base::storage_type;
  __device__ int go() { return base::run(); }
};

// W64-LABEL: define {{.*}}@_Z1kPi(
// W64: store i32 64,
// W64: store i32 64,
// W32-LABEL: define {{.*}}@_Z1kPi(
// W32: store i32 128,
// W32: store i32 32,
__global__ void k(int *p) {
  using BS = BlockScan<1024>;
  __shared__ typename BS::TempStorage t;
  p[0] = sizeof(t);
  p[1] = BS().go();
}

// W64-LABEL: define {{.*}}@_Z2k2Pi(
// W64: store i32 64,
// W32-LABEL: define {{.*}}@_Z2k2Pi(
// W32: store i32 128,
__device__ int size() { return sizeof(BlockScan<1024>::TempStorage); }
__global__ void k2(int *p) { p[0] = size(); }
