// Static data members of a class template re-parsed per target are
// instantiated as one copy per target, not as duplicate members.
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
typedef int sidet;
#else
typedef long sidet;
#endif

namespace detail { namespace inner {
static const int width = sizeof(sidet);
}}

template <int N> struct S {
  static const int one = N;
  static const int sz = sizeof(sidet) * 10;
  static const int nested = ::detail::inner::width * 100;
  static constexpr int odr = sizeof(sidet) * 7;
  __host__ __device__ int get() const {
    return sz + nested;
  }
  int pad;
};

// DEV: @_ZN1SILi3EE3odrE = {{.*}} i32 28,
// HOST: @_ZN1SILi3EE3odrE = {{.*}} i32 56,
// DEV-LABEL: define {{.*}}@_Z1kPi(
// DEV: store i32 444,
// DEV: store i32 440,
// HOST-LABEL: define {{.*}}@_Z8host_usePi(
// HOST: store i32 884,
// HOST: store i32 880,
__global__ void k(int *p) {
  S<3> s;
  p[0] = S<3>::sz + S<3>::nested + S<3>::one + 1;
  p[1] = s.get();
  p[2] = *(const volatile int *)&S<3>::odr;
}
void host_use(int *p) {
  S<3> s;
  p[0] = S<3>::sz + S<3>::nested + S<3>::one + 1;
  p[1] = s.get();
  p[2] = *(const volatile int *)&S<3>::odr;
}
