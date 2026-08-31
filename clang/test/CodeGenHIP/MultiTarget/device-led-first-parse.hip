// The first parse of device-only code at namespace scope is a device's, from
// the declaration's first token. Its attributes, template head and return type
// see the device's answers, not the host's. A device member function is part
// of its class's declaration, whose first parse is the host's, so a device
// that agrees with the host shares that parse and one that does not gets its
// own.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O1 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -O1 -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -O1 -target-cpu gfx942" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefixes=DEV,GFX90A %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefixes=DEV,GFX942 %s

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__gfx90a__)
constexpr int V = 1;
constexpr int Threads = 128;
#else
constexpr int V = 2;
constexpr int Threads = 256;
#endif

// Read where the template head is parsed.
template <int N = alignof(long double)> __device__ int by_default() {
  return N;
}

// Read in an attribute written before __global__.
__attribute__((amdgpu_flat_work_group_size(1, Threads))) __global__ void
bounded(int *o) {
  o[0] = by_default();
}

constexpr __device__ int pick() { return V; }
struct S {
  __device__ int inline_member() { return pick() * 10; }
  __device__ int out_of_line();
};
__device__ int S::out_of_line() { return pick() * 100; }
__global__ void members(int *o) {
  o[0] = S().inline_member();
  o[1] = S().out_of_line();
}

// DEV: define {{.*}}amdgpu_kernel void @_Z7boundedPi({{.*}} #[[BOUNDED:[0-9]+]]
// DEV: store i32 8,
// DEV-LABEL: define {{.*}}amdgpu_kernel void @_Z7membersPi(
// GFX90A: store i32 10,
// GFX90A: store i32 100,
// GFX942: store i32 20,
// GFX942: store i32 200,
// GFX90A: attributes #[[BOUNDED]] = {{.*}}"amdgpu-flat-work-group-size"="1,128"
// GFX942: attributes #[[BOUNDED]] = {{.*}}"amdgpu-flat-work-group-size"="1,256"
