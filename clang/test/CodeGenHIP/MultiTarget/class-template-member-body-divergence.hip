// A class template whose targets differ only in what member function bodies
// compute, here through a static constant, has one specialization for every
// target, so an object built by host code is the type device code takes.
// Each target evaluates the constant with its own function.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -emit-llvm -o %t.host.ll -verify %s \
// RUN:   "-multi-target-aux-invocation=-std=c++17 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip" \
// RUN:   -multi-target-aux-output=2:%t.gfx90a.bc
// RUN: FileCheck -check-prefix=HOST %s < %t.host.ll
// RUN: llvm-dis -o - %t.gfx90a.bc | FileCheck -check-prefix=DEV %s

// expected-no-diagnostics

#define __host__ __attribute__((host))
#define __device__ __attribute__((device))
#define __global__ __attribute__((global))

namespace ns {
#ifdef __HIP_DEVICE_COMPILE__
constexpr unsigned width(unsigned x) { return x ? 32 - __builtin_clz(x) : 0; }
#else
constexpr unsigned width(unsigned x) {
  unsigned n = 0;
  for (; x; x >>= 1)
    ++n;
  return n;
}
#endif
} // namespace ns

template <class Device> class Bits {
  static constexpr unsigned block_size = sizeof(unsigned) * 8;
  static constexpr unsigned block_shift = ns::width(block_size) - 1;
  unsigned m_size = 0;

public:
  __host__ __device__ unsigned blocks() const { return m_size >> block_shift; }
};

template <class Device> struct Map {
  Bits<Device> m_bits;
  Map() = default;
  template <class D2> __host__ __device__ Map(const Map<D2> &src) : m_bits(src.m_bits) {}
};

struct Dev {};
__global__ void kernel(Map<Dev> m, unsigned *o) {
  Map<Dev> c(m);
  *o = c.m_bits.blocks();
}
void host_use(unsigned *o) {
  Map<Dev> m;
  Map<Dev> c(m);
  *o = c.m_bits.blocks();
}

// HOST-LABEL: define {{.*}} @_ZNK4BitsI3DevE6blocksEv(
// HOST: lshr i32 %{{.*}}, 5
// DEV-LABEL: define {{.*}} @_ZNK4BitsI3DevE6blocksEv(
// DEV: lshr i32 %{{.*}}, 5
