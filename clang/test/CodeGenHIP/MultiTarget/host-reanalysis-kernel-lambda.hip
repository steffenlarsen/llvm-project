// A host function the devices analyse again launches a kernel over a lambda of
// its own. The devices' copy of the function makes a kernel over its copy of
// the lambda, which mangles the same as the one the host launches. Only that
// one is emitted, its stub by the host and its body by the device.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc
// RUN: FileCheck --check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=DEV %s

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

constexpr __host__ __device__ int width() {
#if defined(__HIP_DEVICE_COMPILE__)
  return 64;
#else
  return 32;
#endif
}

template <class F> __global__ void apply(F f, int *p) { p[0] = f(p[1]); }

int launch(int *p) {
  constexpr int w = width();
  apply<<<1, 1>>>([] __host__ __device__(int x) { return x + 1; }, p);
  return w;
}

// HOST: define {{.*}}@_Z20__device_stub__applyIZ6launchPiEUliE_EvT_S0_(
// DEV: define {{.*}}amdgpu_kernel void @_Z5applyIZ6launchPiEUliE_EvT_S0_(
// DEV-NOT: define {{.*}}@_Z5applyIZ6launchPiEUliE_EvT_S0_(
