// A call to a function template whose redeclarations include a target's own
// copy is redirected to the calling target's copy of that template. Another
// overload of the same name that has a copy for that target is not one: here
// the call must keep the shared m<T, P>(P &, unsigned long), not instantiate
// m<P, Size>(P &, Size), whose body only fails, with its arguments (rocThrust's
// thrust::malloc calling generic::malloc).
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx90a -O2" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -target-cpu gfx942 -O2" \
// RUN:   -multi-target-aux-output=3:%t.gfx942.bc
// RUN: FileCheck --check-prefix=HOST %s < %t.ll
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefix=DEV %s
// RUN: llvm-dis %t.gfx942.bc -o - | FileCheck --check-prefix=DEV %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

namespace g {
struct pol {};
#if !defined(__HIP_DEVICE_COMPILE__)
template <class T, class P> __host__ __device__ int m(P &p, unsigned long n);
#endif
template <class P, class Size> __host__ __device__ int m(P &, Size) {
#if defined(__HIP_DEVICE_COMPILE__)
  static_assert(sizeof(Size) == 0, "device: unimplemented");
#else
  static_assert(sizeof(Size) == 0, "host: unimplemented");
#endif
  return 0;
}
template <class T, class P> __host__ __device__ int m(P &p, unsigned long n) {
  return n * sizeof(T);
}
} // namespace g

template <class T> __host__ __device__ int use(g::pol &p) {
  int r = g::m<T>(p, 4ul);
  return r;
}

__global__ void k(int *o) {
  g::pol p;
  o[0] = use<int>(p);
}
int h() {
  g::pol p;
  return use<int>(p);
}

// HOST-LABEL: define {{.*}} @_Z1hv(
// HOST: ret i32 16
// DEV-LABEL: define {{.*}} @_Z1kPi(
// DEV: store i32 16,
