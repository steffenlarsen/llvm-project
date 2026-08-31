// A class template whose body diverges per target, so each target gets its own
// copy and its own specialization, used from a shared kernel. Each device
// target must define its own specialization's constructor, whether called
// directly or as the base of another class, its own specialization of a
// member function template, and the static constexpr data member a device
// compilation makes implicitly __constant__. This follows rocPRIM's
// block_radix_sort and block_load ("undefined hidden symbol:
// rocprim::block_radix_sort<...>::radix_bits_per_pass", "undefined hidden
// symbol: rocprim::block_load<...>::block_load()" and, from HeCBench
// blockScan, "...::block_load<...>::load<int const*>(...)").
//
// REQUIRES: amdgpu-registered-target

// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -emit-llvm -o %t.host.ll %s \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=2:%t.gfx90a.bc \
// RUN:   "-multi-target-aux-invocation=-triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx942 -fcuda-is-device -x hip" \
// RUN:   -mllvm -multi-target-aux-output=3:%t.gfx942.bc \
// RUN:   -mllvm -multi-target-codegen \
// RUN:   -mllvm -multi-target-record \
// RUN:   -mllvm -parse-all-target-alternatives \
// RUN:   -mllvm -widen-target-alternatives \
// RUN:   -mllvm -allow-target-variant-decls \
// RUN:   -mllvm -reparse-divergent-users \
// RUN:   -mllvm -merge-equivalent-variants
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=DEV %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=DEV %s
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=GFX90A %s
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=GFX942 %s
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=SCALE90A %s \
// RUN:   --implicit-check-not="add i32 %{{.*}}, 9942"
// RUN: llvm-dis -o - %t.gfx942.bc | FileCheck -check-prefix=SCALE942 %s \
// RUN:   --implicit-check-not="add i32 %{{.*}}, 9090"

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

__device__ const unsigned &pick(const unsigned &a) { return a; }

template <class T, unsigned N> class sorter {
public:
  static constexpr unsigned bits = N * 2;
  __device__ sorter() {
#if defined(__gfx90a__)
    width = 64;
#elif defined(__HIP_DEVICE_COMPILE__)
    width = 32;
#else
    width = 0;
#endif
  }
  template <class U> __device__ unsigned run(U u) {
    return pick(bits) + width + u;
  }
  template <class U> __device__ unsigned scale(U u) {
#if defined(__gfx90a__)
    return u + 9090;
#elif defined(__gfx942__)
    return u + 9942;
#else
    return u;
#endif
  }
  unsigned width;
};

template <class T> class loader : public sorter<T, 4> {
public:
  __device__ loader() {}
};

__global__ void kernel(unsigned *o) {
  o[0] = sorter<int, 8>().run(0u);
  o[1] = loader<int>().run(0u);
  o[2] = sorter<int, 8>().scale(0u);
}

// DEV-DAG: @_ZN6sorterIiLj8EE4bitsE = linkonce_odr {{.*}}constant i32 16
// DEV-DAG: @_ZN6sorterIiLj4EE4bitsE = linkonce_odr {{.*}}constant i32 8

// DEV-DAG: define {{.*}} @_ZN6sorterIiLj8EEC1Ev(
// DEV-DAG: define {{.*}} @_ZN6sorterIiLj4EEC2Ev(

// GFX90A-LABEL: define {{.*}} @_ZN6sorterIiLj8EEC2Ev(
// GFX90A: store i32 64
// GFX942-LABEL: define {{.*}} @_ZN6sorterIiLj8EEC2Ev(
// GFX942: store i32 32

// SCALE90A: define {{.*}} @_ZN6sorterIiLj8EE5scaleIjEEjT_(
// SCALE90A: add i32 %{{.*}}, 9090
// SCALE942: define {{.*}} @_ZN6sorterIiLj8EE5scaleIjEEjT_(
// SCALE942: add i32 %{{.*}}, 9942
