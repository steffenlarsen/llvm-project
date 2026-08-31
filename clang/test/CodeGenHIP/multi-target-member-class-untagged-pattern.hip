// A member class of a class template specialization, instantiated from a
// pattern no target forked. The instantiated member class is tagged with
// whichever target happened to be ambient when it was completed, but it is the
// only copy its enclosing specialization holds, so a reference to it must not
// be pinned to the primary target's copy. This follows rocPRIM's
// block_adjacent_difference and block_merge_impl ("no type named
// 'storage_type' in 'rocprim::detail::block_merge_impl<int, int, 256, 4>'").
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang -x hip --offload-arch=gfx90a -nogpulib -nogpuinc \
// RUN:   -fintegrated-hip-device-codegen -fsyntax-only -Xclang -verify %s
// RUN: %clang -x hip --offload-arch=gfx90a --offload-arch=gfx942 \
// RUN:   -nogpulib -nogpuinc -fintegrated-hip-device-codegen -fsyntax-only \
// RUN:   -Xclang -verify %s

// expected-no-diagnostics

#define __device__ __attribute__((device))
#define __global__ __attribute__((global))
#define __shared__ __attribute__((shared))

template <typename T> struct raw_storage {
  unsigned char storage[sizeof(T)];
};

template <typename T, unsigned N> class impl {
public:
  struct storage_type {
    T items[N];
  };
};

template <class T, unsigned N> class outer : private impl<T, N> {
  using base_type = impl<T, N>;
  struct storage_type_ {
    typename base_type::storage_type left;
    typename base_type::storage_type right;
  };

public:
  using storage_type = raw_storage<storage_type_>;
};

template <int B> __global__ void kernel(int *out) {
  __shared__ typename outer<int, B>::storage_type storage;
  out[0] = sizeof(storage);
}

void *launch() { return (void *)kernel<64>; }
