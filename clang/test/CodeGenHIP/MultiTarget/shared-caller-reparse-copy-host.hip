// A template re-parsed per target, as it uses a constant that differs, called
// from shared code that gfx90a's copy of a kernel instantiates first. The
// shared code calls gfx90a's specialization, so the host compiles its own copy's
// under the same name, or the host object leaves it undefined.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -disable-llvm-passes -x hip -std=c++17 -emit-llvm -o - %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -O2 -disable-llvm-passes -std=c++17" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx1030 -O2 -disable-llvm-passes -std=c++17" \
// RUN:   -multi-target-aux-output=3:%t.gfx1030.bc \
// RUN: | FileCheck --check-prefix=HOST %s
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=W64 %s
// RUN: llvm-dis %t.gfx1030.bc -o - | FileCheck --check-prefix=W32 %s

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

#if defined(__GFX9__)
constexpr int W = 64;
#else
constexpr int W = 32;
#endif

struct yes {};

// HOST-NOT: declare {{.*}}@_ZN2ns4implIiEEiT_3yes(
// HOST-LABEL: define {{.*}}@_ZN2ns4implIiEEiT_3yes(
// HOST: add nsw i32 %{{.*}}, 32
// W64-LABEL: define {{.*}}@_ZN2ns4implIiEEiT_3yes(
// W64: add nsw i32 %{{.*}}, 64
// W32-LABEL: define {{.*}}@_ZN2ns4implIiEEiT_3yes(
// W32: add nsw i32 %{{.*}}, 32
namespace ns {
template <class T> __host__ __device__ int impl(T x, yes) { return x + W; }
} // namespace ns

template <class T> __host__ __device__ int wrap(T x) {
  return ns::impl(x, yes());
}

#if defined(__GFX9__)
template <class T> __global__ void k(T *o) { o[0] = wrap(o[1]) + 1; }
#else
template <class T> __global__ void k(T *o) { o[0] = wrap(o[1]); }
#endif

void launch(int *o) { k<<<1, 1>>>(o); }
int run(int *o) { return wrap(o[2]); }
