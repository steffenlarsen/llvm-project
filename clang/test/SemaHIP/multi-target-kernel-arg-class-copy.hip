// A kernel whose template argument is a shared class specialized on a class
// with one copy per target, as its layout differs. Rebuilt around a device's
// copy, the shared class matches none of its partial or explicit
// specializations, so that device keeps the kernel the code named instead of
// failing to instantiate it.
//
// REQUIRES: amdgpu-registered-target, x86-registered-target

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -aux-triple amdgcn-amd-amdhsa \
// RUN:   -aux-target-cpu gfx90a -x hip -fsyntax-only -verify %s \
// RUN:   "-multi-target-aux-invocation=-std=c++17 -triple amdgcn-amd-amdhsa -aux-triple x86_64-unknown-linux-gnu -target-cpu gfx90a -fcuda-is-device -x hip"

// expected-no-diagnostics

#define __global__ __attribute__((global))

#ifdef __HIP_DEVICE_COMPILE__
#define IF_ON_HOST(X)
#else
#define IF_ON_HOST(X) X
#endif

#ifndef POLICY_GUARD
#define POLICY_GUARD
namespace ns {
template <class Traits> class Policy {
public:
  Policy() { IF_ON_HOST(int x = 0; (void)x;) }
#ifdef __HIP_DEVICE_COMPILE__
  int pad;
#endif
};
} // namespace ns
#endif

template <class D> __global__ void launch(const D *d) {}

template <class P> class Partial;
template <class T> class Partial<ns::Policy<T>> {
public:
  void execute() const { auto k = launch<Partial>; (void)k; }
};

template <class P> class Explicit;
template <> class Explicit<ns::Policy<int>> {
public:
  void execute() const { auto k = launch<Explicit>; (void)k; }
};

void use() {
  Partial<ns::Policy<int>>().execute();
  Explicit<ns::Policy<int>>().execute();
}
