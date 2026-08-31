// A class template whose base is chosen through a member alias template of an
// explicit specialization, e.g. `select<A>::template type<K>`, depends on what
// that alias names. Here the alias names a class each device architecture has
// a specialization of, so the class template has one per target too. Host
// code naming it first does not make the devices' code use the host's, whose
// members only the host has.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -O2 -x hip -emit-llvm -o %t.ll %s \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -O2 -target-cpu gfx90a" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=%{hip_device} %{host_aux_triple} -std=c++17 -O2 -target-cpu gfx1100" \
// RUN:   -multi-target-aux-output=3:%t.gfx1100.bc
// RUN: llvm-dis %t.gfx90a.bc -o - | FileCheck --check-prefixes=DEV,W64 %s \
// RUN:   --implicit-check-not="declare {{.*}}@_Z"
// RUN: llvm-dis %t.gfx1100.bc -o - | FileCheck --check-prefixes=DEV,W32 %s \
// RUN:   --implicit-check-not="declare {{.*}}@_Z"

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

constexpr __host__ __device__ int wave() {
#if !defined(__HIP_DEVICE_COMPILE__)
  return 0;
#elif defined(__GFX9__)
  return 64;
#else
  return 32;
#endif
}

template <class K> struct merge_impl {
  template <class C> __device__ K sort(K k, C c) { return c(k) + wave(); }
};

template <int A> struct select_impl;
template <> struct select_impl<0> {
  template <class K> using type = merge_impl<K>;
};

template <class K, int A = 0>
struct block_sort : select_impl<A>::template type<K> {};

template <class K> __device__ K run(K k) {
  return block_sort<K>().sort(k, [](K x) { return x; });
}

template <class K> __global__ void kern(K *p) { p[0] = run(p[1]); }

void *host() {
  (void)sizeof(block_sort<int>);
  return (void *)kern<int>;
}

// DEV-LABEL: define {{.*}}amdgpu_kernel void @_Z4kernIiEvPT_
// W64: add nsw i32 %{{.*}}, 64
// W32: add nsw i32 %{{.*}}, 32
