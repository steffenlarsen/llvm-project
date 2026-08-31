// Regression test for the "u_hold" union-member lookup bug (found verifying
// Increment 8's host-primary ambient fallback fix, real mtcg build of
// llama.cpp's mmq-instance-nvfp4.cu / ROCm's amd_hip_unsafe_atomics.h).
//
// A union declared and used entirely inside one arm of a widened #if/#else,
// nested inside a __device__ function's body, called from a __global__
// kernel. isVisibleForTarget's tier1-3 ambient fallback used to
// unconditionally force Current = 1 whenever the ambient was 0 (shared) --
// even for a FieldDecl already correctly tagged to match the enclosing
// widened alternative before that alternative's own owning FunctionDecl had
// finished parsing (tagging happens only after the whole function finishes).
// This produced "no member named 'u32'/'h2r' in union u_hold" under the
// real driver's actual host-primary combined-frontend shape (host is the
// primary -cc1 triple, device archs are aux targets) -- a shape the
// existing multi-target-partial-spec-unconditional-member.hip test does not
// exercise, since that test's RUN lines use a manually-constructed
// device-primary -cc1 invocation (-fcuda-is-device) where CUDAIsDevice
// already bypasses this whole fallback block.
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
