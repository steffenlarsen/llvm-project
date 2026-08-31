// A member of a local class inside a function template forked per target is
// instantiated under that target. Its call was bound to that target's copy of
// a function template that the widened merge then folded into a shared copy,
// so the shared copy must be found instead. The shared copy's own unresolved
// call was bound under the host to a callee that stayed per target, so under
// the instantiating target that callee must be replaced by the target's own
// copy, as in rocThrust's cross_system_copy_n ("call to function ... that is
// neither visible in the template definition nor found by argument-dependent
// lookup").
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a -nogpulib -nogpuinc \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only -Xclang -verify %s
// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpulib -nogpuinc -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -Xclang -verify %s

// expected-no-diagnostics

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))

namespace ns {
// Stays per target: the host's copy differs from the devices'.
template <class P> __host__ __device__ int per_target(P &, int) {
#ifndef __HIP_DEVICE_COMPILE__
  return 2;
#else
  return 1;
#endif
}

// Its copies are identical, so the widened merge folds them into one.
template <class P> __host__ __device__ int shared(P &p) {
  return per_target(p, 0);
}

template <class P> __host__ __device__ void forked(P &p) {
  struct local {
    __host__ __device__ static void call(P &p) { (void)shared(p); }
  };
#ifndef __HIP_DEVICE_COMPILE__
  local::call(p);
#else
  local::call(p); (void)0;
#endif
}
} // namespace ns

void use() {
  int p;
  ns::forked(p);
}
