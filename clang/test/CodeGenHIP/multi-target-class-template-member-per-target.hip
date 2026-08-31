// A class template whose constexpr members are defined differently per target
// is specialized per target from each target's copy of the template. Code
// naming a specialization keeps one type, so that objects pass between host
// and device code; a constant expression calling a member, directly or
// through a function template, evaluates the specialization of the target it
// is compiled for. Device code evaluated the host's, or gfx90a's for gfx942.
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

template <class T> struct cfg {
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
};

__host__ __device__ int by_value(cfg<int> c) { return c.side(); }
__host__ __device__ int constant() {
  constexpr int v = cfg<int>(1).side();
  return v;
}
template <int J> __device__ int in_template() {
  constexpr int v = cfg<int>(J).arch();
  return v;
}
__device__ int in_function() {
  constexpr int v = cfg<int>(2).arch();
  return v;
}
template <int J> constexpr __device__ int through() {
  return cfg<int>(J).arch();
}
__device__ int through_template() {
  constexpr int v = through<3>();
  return v;
}
__device__ int not_constant() { return through<4>(); }
template <int J> __device__ int by_reference(const cfg<int> &c) {
  return c.arch() + J;
}

__global__ void k(int *o, cfg<int> c) {
  o[0] = by_value(c);
  o[1] = constant();
  o[2] = in_template<7>();
  o[3] = in_function();
  o[4] = through_template();
  o[5] = not_constant();
  o[6] = by_reference<5>(cfg<int>(o[7]));
}
int h(int *o) {
  cfg<int> c(5);
  k<<<1, 1>>>(o, c);
  return by_value(c) * 1000 + constant();
}

// HOST-LABEL: define {{.*}} @_Z1hPi(
// HOST: ret i32 205201

// DEV-LABEL: define {{.*}} @_Z1kPi3cfgIiE(
// DEV: add nsw i32 %c.coerce, 100
// DEV: store i32 101,
// GFX90A: store i32 97,
// GFX942: store i32 949,
// GFX90A: store i32 92,
// GFX942: store i32 944,
// GFX90A: store i32 93,
// GFX942: store i32 945,
// GFX90A: store i32 94,
// GFX942: store i32 946,
// GFX90A: add nsw i32 %{{.*}}, 95
// GFX942: add nsw i32 %{{.*}}, 947
