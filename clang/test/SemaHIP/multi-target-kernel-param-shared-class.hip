// A class template whose targets differ only in a member function's body is
// one class template for every target. A kernel template whose parameter
// names it then has one specialization, so taking its address in host code
// agrees with the type it deduces.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -fsyntax-only -fcxx-exceptions -fexceptions -verify %s \
// RUN:   "-multi-target-aux-invocation=-std=c++17 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -fcxx-exceptions -fexceptions -x hip"

// expected-no-diagnostics

#define __global__ __attribute__((global))
#define __host__ __attribute__((host))
#define __device__ __attribute__((device))

#ifdef __HIP_DEVICE_COMPILE__
#define HOST_DEVICE_THROW()
#else
#define HOST_DEVICE_THROW() throw 1
#endif

#ifndef GEN_GUARD
#define GEN_GUARD
namespace ns {
template <unsigned R, unsigned B> class Gen {
  typedef unsigned long result_type;
  typedef result_type counter_type[2];
  __host__ __device__ void restart() { HOST_DEVICE_THROW(); }
  __host__ __device__ result_type operator()() {
    if (index_ >= c_resultsPerCounter_) {}
    return 0;
  }
  static const unsigned c_resultsPerCounter_ =
      static_cast<unsigned>(sizeof(counter_type) / sizeof(result_type));
  unsigned index_;
};
template <unsigned B = 64> class GenFast : public Gen<13, B> {};
typedef GenFast<> DefaultEngine;
} // namespace ns
#endif

using ns::Gen;
template <unsigned R> __global__ void setup(Gen<R, 0> *p, unsigned long k) {}
template <unsigned R> void run() { auto kernelPtr = setup<R>; (void)kernelPtr; }
void use() { run<13>(); }
