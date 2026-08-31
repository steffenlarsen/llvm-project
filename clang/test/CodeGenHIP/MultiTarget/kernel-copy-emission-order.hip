// Each device emits a kernel template whose body differs per target where
// its own compile does, among the other functions. Making the other devices'
// specializations after every other instantiation emitted them last, and the
// AMDGPU backend's code for a function can depend on the functions compiled
// before it (llama.cpp flash_attn_tile spilled registers on RDNA).
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx1030 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1030" \
// RUN:   -multi-target-aux-output=2:%t.gfx1030.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1100" \
// RUN:   -multi-target-aux-output=3:%t.gfx1100.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=4:%t.gfx90a.bc
// RUN: llvm-dis -o - %t.gfx1030.bc | FileCheck %s
// RUN: llvm-dis -o - %t.gfx1100.bc | FileCheck %s
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck %s
// RUN: %clang_cc1 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu \
// RUN:   -target-cpu gfx90a -fcuda-is-device -x hip -emit-llvm -o - %s | FileCheck %s

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

template <int N> __global__ void wide(int *o) {
#if defined(__gfx90a__)
  o[0] = N * 64;
#else
  o[0] = N * 32;
#endif
}
template <class T> __global__ void plain(T *o) { o[0] = 1; }
template <int N> void launch_wide(int *o) { wide<N><<<1, 1>>>(o); }
void run(int *o, float *f) {
  launch_wide<1>(o);
  plain<<<1, 1>>>(f);
}

// CHECK: define {{.*}} @_Z4wideILi1EEvPi(
// CHECK: define {{.*}} @_Z5plainIfEvPT_(
