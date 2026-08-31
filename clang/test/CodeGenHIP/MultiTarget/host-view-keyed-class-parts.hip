// The host's view of device code that names a class specialization keyed by
// the host, through a static member's value, is the host's view in every
// function it instantiates. A member of a class instantiated with such a
// class was given parts for the devices, whose bodies named each device's
// members with the signature's host types.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -std=c++17 -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -O2 -std=c++17" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942 -O2 -std=c++17" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc \
// RUN: -verify
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=GFX90A %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=GFX942 %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

// expected-no-diagnostics

struct dim3 {
  unsigned x, y, z;
  __host__ __device__ dim3(unsigned x = 1, unsigned y = 1, unsigned z = 1)
      : x(x), y(y), z(z) {}
};
typedef struct ihipStream_t *hipStream_t;
extern "C" int hipConfigureCall(dim3, dim3, unsigned long = 0,
                                hipStream_t = 0);

enum class target { device, host };
constexpr target get_target() {
#if defined(__HIP_DEVICE_COMPILE__)
  return target::device;
#else
  return target::host;
#endif
}

constexpr unsigned items() {
#if defined(__gfx90a__)
  return 2;
#else
  return 3;
#endif
}

template <class T, unsigned N, target W = get_target()> struct Exchange {
  struct Storage {
    T v[N];
  };
};

// Specializations keyed by the target, through the member's value.
template <class T, unsigned N> struct Load {
  static constexpr target W = get_target();
  using Storage = typename Exchange<T, N>::Storage;
  __device__ void load(int *p, Storage &s) { *p = sizeof(s); }
  template <class D> __device__ void load(int *, D d) { T x = d; }
};

template <class L> struct Helper {
  __device__ void load(int *p, typename L::Storage &s) { L().load(p, s); }
};

template <class T> struct Impl {
  using L = Load<T, items()>;
  __device__ void run(int *p) {
    typename L::Storage s;
    Helper<L>().load(p, s);
  }
};

__global__ void kernel(int *p) { Impl<int>().run(p); }

void launch(int *p) { kernel<<<1, 1>>>(p); }

// GFX90A: define {{.*}} @_Z6kernelPi(
// GFX90A: store i32 8,
// GFX942: define {{.*}} @_Z6kernelPi(
// GFX942: store i32 12,
