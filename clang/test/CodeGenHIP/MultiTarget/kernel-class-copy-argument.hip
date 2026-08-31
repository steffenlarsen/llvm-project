// A kernel specialized for a class every device copies for itself, launched
// by the host with the host's copy, is specialized for each device's own copy
// too. Devices whose copy differs from the host's lost the kernel.
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
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=GFX9 %s
// RUN: llvm-dis %t.gfx1030.bc -o - | FileCheck --check-prefix=RDNA %s
// RUN: llvm-dis %t.gfx1100.bc -o - | FileCheck --check-prefix=RDNA %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
#define __shared__ __attribute__((shared))

struct dim3 {
  unsigned x, y, z;
  __host__ __device__ dim3(unsigned x = 1, unsigned y = 1, unsigned z = 1)
      : x(x), y(y), z(z) {}
};
typedef struct ihipStream_t *hipStream_t;
extern "C" int hipConfigureCall(dim3, dim3, unsigned long = 0,
                                hipStream_t = 0);

template <class T> struct state {
#if defined(__GFX9__)
  long x;
#else
  int x;
#endif
  T y;
};

// GFX9-LABEL: define {{.*}}@_Z4initI5stateIiEEvT_Pi(
// GFX9: store i32 8,
// GFX9-LABEL: define {{.*}}@_Z4initI5stateIfEEvT_Pi(
// GFX9: store i32 8,
// RDNA-LABEL: define {{.*}}@_Z4initI5stateIiEEvT_Pi(
// RDNA: store i32 4,
// RDNA-LABEL: define {{.*}}@_Z4initI5stateIfEEvT_Pi(
// RDNA: store i32 4,
template <class S> __global__ void init(S s, int *p) { *p = sizeof(s.x); }
void run1(int *p) { state<int> s{}; init<<<1, 1>>>(s, p); }
template <class T> void run2(int *p) {
  state<T> s{};
  auto l = [&](auto ss) { init<<<1, 1>>>(ss, p); };
  l(s);
}
template void run2<float>(int *);
