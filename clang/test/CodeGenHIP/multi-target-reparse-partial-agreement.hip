// A declaration that uses a divergent entity is re-parsed once per aux target.
// When only some of those copies are equivalent to the host's, none of them
// may be merged: merging the agreeing ones, or merging agreeing aux copies with
// each other, leaves a target that sees both the shared declaration and its own
// tagged copy, as in rocprim's block_radix_sort `sort_type` alias:
// "reference to 'sort_type' is ambiguous".
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a -nogpulib -nogpuinc \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only -Xclang -verify %s
// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpulib -nogpuinc -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -Xclang -verify %s

// expected-no-diagnostics

#define __device__ __attribute__((device))

#ifdef __HIP_DEVICE_COMPILE__
#define DEVICE_ONLY(x) x
#else
#define DEVICE_ONLY(x)
#endif

#ifdef __gfx942__
#define GFX942_ONLY(x) x
#else
#define GFX942_ONLY(x)
#endif

#ifndef HOST_DIFFERS
#define HOST_DIFFERS
template <class K> struct HostDiffers {
  DEVICE_ONLY(int extra;)
  struct storage_type { K k; };
};
#endif

#ifndef GFX942_DIFFERS
#define GFX942_DIFFERS
template <class K> struct Gfx942Differs {
  GFX942_ONLY(int extra;)
  struct storage_type { K k; };
};
#endif

struct Separator;

// Every device copy agrees, but none agrees with the host's.
template <class K> using host_differs_t = HostDiffers<K>;
// The host's and gfx90a's copies agree, but gfx942's does not.
template <class K> using gfx942_differs_t = Gfx942Differs<K>;

host_differs_t<int>::storage_type host_differs_storage;

#ifdef __gfx942__
__device__ gfx942_differs_t<int>::storage_type *gfx942_differs_storage;
#endif
