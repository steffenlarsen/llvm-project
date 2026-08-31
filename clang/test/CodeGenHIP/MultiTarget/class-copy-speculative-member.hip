// A class template copied per target gives each device its own copy's
// members. Each device instantiates the others' constructors too, in case
// they need them. One that does not compile for a device is an error only if
// that device emits it: gfx942's lanes<64> fails its static_assert, but only
// gfx90a's reducer<int, 64> uses it.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=GFX90A %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=GFX942 %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

struct dim3 {
  unsigned x, y, z;
  __host__ __device__ dim3(unsigned x = 1, unsigned y = 1, unsigned z = 1)
      : x(x), y(y), z(z) {}
};
typedef struct ihipStream_t *hipStream_t;
extern "C" int hipConfigureCall(dim3, dim3, unsigned long = 0,
                                hipStream_t = 0);

#if defined(__gfx90a__) || !defined(__HIP_DEVICE_COMPILE__)
#define WAVE 64
#else
#define WAVE 32
#endif

template <int W> struct lanes {
  __device__ lanes() { static_assert(W <= WAVE, "wider than the wave"); }
  int n = W;
};

template <class T, int W = WAVE> struct reducer {
  lanes<W> l;
  __device__ reducer() {}
  __device__ T get() { return l.n; }
};

template <int N> __global__ void kernel(int *o) { o[0] = reducer<int>().get() + N; }
void launch(int *o) { kernel<1><<<1, 1>>>(o); }

// GFX90A: define {{.*}} @_ZN7reducerIiLi64EEC2Ev(
// GFX90A: define {{.*}} @_ZN5lanesILi64EEC2Ev(
// GFX90A: store i32 64
// GFX942: define {{.*}} @_ZN7reducerIiLi32EEC2Ev(
// GFX942: define {{.*}} @_ZN5lanesILi32EEC2Ev(
// GFX942: store i32 32
