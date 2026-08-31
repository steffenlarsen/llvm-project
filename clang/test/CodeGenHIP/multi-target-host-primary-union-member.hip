// A union declared and used entirely inside one arm of a widened #if/#else,
// nested inside a __device__ function's body, called from a __global__
// kernel. Exercises member lookup on a union field under the real driver's
// host-primary combined-frontend shape (host is the primary -cc1 triple,
// device archs are aux targets), which a manually-constructed
// -fcuda-is-device invocation does not exercise.
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -merge-equivalent-variants \
// RUN:   %s

#include <hip/hip_runtime.h>

struct MyHalf2Raw { unsigned short x, y; };

__device__ unsigned int unsafeAtomicAddTest(unsigned int *address) {
#if __has_builtin(__builtin_amdgcn_flat_atomic_fadd_v2f16)
  typedef _Float16 __attribute__((ext_vector_type(2))) vec_fp162;
  static_assert(sizeof(vec_fp162) == sizeof(MyHalf2Raw), "size mismatch");
  union {
    MyHalf2Raw h2r;
    vec_fp162 fp16;
  } u{};
  return 1;
#else
  static_assert(sizeof(MyHalf2Raw) == sizeof(unsigned int), "size mismatch");
  union u_hold {
    MyHalf2Raw h2r;
    unsigned int u32;
  };
  u_hold old_val, new_val;
  old_val.u32 = *address;
  new_val.h2r = old_val.h2r;
  *address = new_val.u32;
  return old_val.u32;
#endif
}

__global__ void kernel(unsigned int *p) {
  *p = unsafeAtomicAddTest(p);
}
