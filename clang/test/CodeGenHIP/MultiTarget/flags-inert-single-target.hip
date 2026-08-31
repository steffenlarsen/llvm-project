// A device function is analysed for the devices only. Re-parsed for the host,
// it would reject the AMDGPU builtin's non-constant selector. The device's
// own copy of the function it calls still gives the device its answer.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1030 -O2" \
// RUN:   -multi-target-aux-output=2:%t.dev.bc
// RUN: llvm-dis %t.dev.bc -o - | FileCheck %s

// CHECK-LABEL: define {{.*}}@_Z6unpacki(
// CHECK: call i32 @llvm.amdgcn.perm(
// CHECK: lshr i32 %{{.*}}, 8

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__HIP_DEVICE_COMPILE__)
__device__ int shift() { return 8; }
#else
__device__ int shift() { return 0; }
#endif
__device__ int sep;

__device__ int unpack(int q) {
  unsigned sel = (q & 0x03) | ((q & 0x0C) << 6);
  return __builtin_amdgcn_perm(0x020100FF, 0x020100FF, sel) >> shift();
}
__global__ void k(int *p) { p[0] = unpack(p[1]); }
