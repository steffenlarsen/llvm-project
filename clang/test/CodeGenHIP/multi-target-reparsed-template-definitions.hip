// A function template re-parsed per target has a definition per target. Each
// target instantiates its own definition, also where the template is declared
// once before them, and also for a device whose copy merged into another's.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -O2 -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc
// RUN: FileCheck --check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=DEV %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefix=DEV %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__HIP_DEVICE_COMPILE__)
typedef int sidet;
#else
typedef long sidet;
#endif

struct derived {};

namespace fwd {
template <typename T> __host__ __device__ int get(derived &, int n);
} // namespace fwd

template <typename T> __host__ __device__ int via_using(int n) {
  using fwd::get;
  derived d;
  return get<T>(d, n) * 10;
}

namespace fwd {
struct pad {};
template <typename T> __host__ __device__ int get(derived &, int n) {
  sidet s = n;
  return (int)s + (int)sizeof(sidet) * 100;
}
} // namespace fwd

namespace plain {
struct pad {};
template <typename T> __host__ __device__ int get(T n) {
  sidet s = n;
  return (int)s + (int)sizeof(sidet) * 1000;
}
} // namespace plain

template <typename T> __host__ __device__ int via_plain(T n) {
  return plain::get(n);
}

struct base {};
struct derived2 : base {};
namespace same {
struct pad {};
template <typename T> __host__ __device__ int get(derived2 &, int n) {
  sidet s = n;
  return (int)s + (int)sizeof(sidet) * 10000;
}
} // namespace same
template <typename T> __host__ __device__ int get(const base &, int n) {
  using same::get;
  derived2 d;
  return get<T>(d, n) * 10;
}

// DEV-LABEL: define {{.*}}@_Z1kPi(
// DEV: store i32 4030,
// DEV: store i32 4003,
// DEV: store i32 400030,
// DEV-NOT: declare {{.*}}get
// HOST-LABEL: define {{.*}}@_Z8host_usePi(
// HOST: store i32 8030,
// HOST: store i32 8003,
// HOST: store i32 800030,
__global__ void k(int *p) {
  p[0] = via_using<int>(3);
  p[1] = via_plain(3);
  p[2] = get<int>(base(), 3);
}
void host_use(int *p) {
  p[0] = via_using<int>(3);
  p[1] = via_plain(3);
  p[2] = get<int>(base(), 3);
}
