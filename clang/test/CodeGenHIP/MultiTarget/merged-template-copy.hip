// A function template re-parsed per target whose copy for one device merged
// into another's is that device's too. Its specialization is defined for the
// device whose copy merged away, which calls it through a shared caller.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942 -O2" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: FileCheck --check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=DEV %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefix=DEV %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

#if defined(__HIP_DEVICE_COMPILE__)
__host__ __device__ inline int alloc() { return 1; }
#else
__host__ __device__ inline int alloc() { return 2; }
#endif

#if defined(__HIP_DEVICE_COMPILE__)
typedef int sidet;
#else
typedef long sidet;
#endif

struct base {};
struct derived : base {};

namespace gen {
struct pad {};
template <typename T> __host__ __device__ int get_tmp(derived &, int n) {
  sidet s = n;
  return alloc() + (int)s + (int)sizeof(sidet) * 100;
}
} // namespace gen

template <typename T> __host__ __device__ int get_tmp(const base &b, int n) {
  using gen::get_tmp;
  derived d;
  return get_tmp<T>(d, n) * 10;
}

__global__ void k(int *p) { p[0] = get_tmp<int>(base(), 3); }
int host_use() { return get_tmp<int>(base(), 3); }

// DEV-LABEL: define {{.*}}@_Z1kPi(
// DEV: store i32 4040,
// DEV-NOT: declare {{.*}}get_tmp
// HOST-LABEL: define {{.*}}@_Z8host_usev(
// HOST: ret i32 8050
