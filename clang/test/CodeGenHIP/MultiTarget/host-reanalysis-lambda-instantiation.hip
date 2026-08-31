// A host function the devices analyse again has a lambda for each copy, whose
// closure types are mangled the same. A constexpr function template is
// instantiated for each while the copy is parsed, before the copy is claimed
// for its targets. Each instantiation is its copy's, so the host emits its own
// and the lambda it calls.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -x hip -emit-llvm -o - %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++20 -target-cpu gfx90a" \
// RUN:   | FileCheck %s

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))

template <class It, class F> constexpr It transform(It b, It e, F f) {
  for (; b != e; ++b)
    *b = f(*b);
  return b;
}

constexpr __host__ __device__ int width() {
#if defined(__HIP_DEVICE_COMPILE__)
  return 64;
#else
  return 32;
#endif
}

int host_fn(int *p) {
  constexpr int k = width();
  transform(p, p + k, [](int x) { return x + 1; });
  return k;
}

// CHECK-DAG: define {{.*}}@_Z9transformIPiZ7host_fnS0_EUliE_ET_S2_S2_T0_(
// CHECK-DAG: define {{.*}}@_ZZ7host_fnPiENKUliE_clEi(
// CHECK-NOT: declare {{.*}}@_ZZ7host_fnPiENKUliE_clEi(
