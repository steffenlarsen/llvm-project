// A class copied per target because a constexpr member is defined differently
// per target keeps one type wherever code names it, so that objects pass
// between host and device code; a constant expression calling the member
// evaluates the definition of the target it is compiled for. Device code
// evaluated the host's: llama.cpp's ggml_cuda_mmq_config::use_mma_data_layout()
// chose the dp4a kernels over MFMA.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc
// RUN: FileCheck --check-prefix=HOST %s < %t.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefixes=DEV,GFX90A %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefixes=DEV,GFX942 %s

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

struct cfg {
  int x;
  constexpr __host__ __device__ cfg(int x) : x(x) {}
  constexpr __host__ __device__ int side() const {
#if defined(__HIP_DEVICE_COMPILE__)
    return x + 100;
#else
    return x + 200;
#endif
  }
  constexpr __host__ __device__ int arch() const {
#if defined(__gfx942__)
    return x + 942;
#elif defined(__gfx90a__)
    return x + 90;
#else
    return x;
#endif
  }
  static constexpr __host__ __device__ bool device() {
#if defined(__HIP_DEVICE_COMPILE__)
    return true;
#else
    return false;
#endif
  }
};
struct empty {
  constexpr __device__ int side() const {
#if defined(__HIP_DEVICE_COMPILE__)
    return 3;
#else
    return 4;
#endif
  }
};

__host__ __device__ int by_value(cfg c) { return c.side(); }
__host__ __device__ int constant() {
  constexpr int v = cfg(1).side();
  return v;
}
template <int J> __device__ int in_template() {
  constexpr int v = cfg(J).arch();
  return v;
}
__device__ int no_constructor() { return empty().side(); }

__global__ void k(int *o, cfg c) {
  o[0] = by_value(c);
  o[1] = constant();
  o[2] = in_template<7>();
  o[3] = cfg::device();
  o[4] = no_constructor();
}
int h(int *o) {
  cfg c(5);
  k<<<1, 1>>>(o, c);
  return by_value(c) * 1000 + constant() + cfg::device();
}

// HOST-LABEL: define {{.*}} @_Z1hPi(
// HOST: ret i32 205201

// DEV-LABEL: define {{.*}} @_Z1kPi3cfg(
// DEV: add nsw i32 %c.coerce, 100
// DEV: store i32 101,
// GFX90A: store i32 97,
// GFX942: store i32 949,
// DEV: store i32 1,
// DEV: store i32 3,
